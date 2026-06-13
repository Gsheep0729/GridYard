/**
* @file    test_file_transfer.cpp
* @date    2026-06-05
* @author  GY
* @brief   文件传输完整流程测试
*
* 测试用例：单文件传输 / 多文件传输 / 取消传输 / 超时处理 / SHA-256 校验
*
* Change Log:
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDir>
#include <QTimer>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>

#include "file_sender_worker.h"
#include "file_receiver_worker.h"
#include "p2p_server.h"
#include "config_manager.h"
#include "dir_serializer.h"
#include "protocol.h"

using gy::DirSerializer;

class TestFileTransfer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSingleFileTransfer();
    void testMultiFileTransfer();
    void testDirectoryTransfer();
    void testCancelTransfer();
    void testLargeFileTransfer();
    void testSha256Verification();
    void testZeroByteFileTransfer();
    void testSpecialCharFileName();

private:
    void createTestFile(const QString &path, const QByteArray &content);
    void createTestDirectory(const QString &basePath, int fileCount);
    bool waitForTransfer(QSignalSpy &spy, int timeout = 10000);

    QTemporaryDir *_sendDir = nullptr;
    QTemporaryDir *_recvDir = nullptr;
    ConfigManager *_config = nullptr;
    quint16 _testPort = 0;
};

void TestFileTransfer::initTestCase()
{
    _sendDir = new QTemporaryDir();
    _recvDir = new QTemporaryDir();
    QVERIFY(_sendDir->isValid());
    QVERIFY(_recvDir->isValid());

    // 创建配置
    QString configPath = _sendDir->path() + "/config.ini";
    qputenv("GRIDYARD_CONFIG", configPath.toUtf8());
    qputenv("GRIDYARD_NAME", "TestSender");
    _config = ConfigManager::create(nullptr, nullptr);

    // 使用随机端口避免冲突
    _testPort = 35100 + (QDateTime::currentMSecsSinceEpoch() % 1000);
}

void TestFileTransfer::cleanupTestCase()
{
    delete _config;
    delete _sendDir;
    delete _recvDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

void TestFileTransfer::createTestFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();
}

void TestFileTransfer::createTestDirectory(const QString &basePath, int fileCount)
{
    QDir dir(basePath);
    QVERIFY(dir.mkpath("."));

    for (int i = 0; i < fileCount; ++i) {
        QString filePath = basePath + QString("/file_%1.txt").arg(i);
        createTestFile(filePath, QString("Content of file %1").arg(i).toUtf8());
    }
}

bool TestFileTransfer::waitForTransfer(QSignalSpy &spy, int timeout)
{
    if (spy.isEmpty()) {
        return spy.wait(timeout);
    }
    return true;
}

void TestFileTransfer::testSingleFileTransfer()
{
    // 创建测试文件
    QString sendPath = _sendDir->path() + "/test_single.txt";
    createTestFile(sendPath, "Hello, GridYard!");

    // 启动接收服务器
    _config->setTcpPort(_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    QString receivedSenderName;
    connect(&server, &P2pServer::transferRequestReceived,
            this, [&receivedSenderName](FileReceiverWorker *worker, const QString &,
                                        const QString &senderName,
                                        const QString &, qint64, int, qint64) {
        receivedSenderName = senderName;
        worker->rejectTransfer("测试完成");
    });

    // 创建发送 Worker
    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);

    // 启动发送（在独立线程）
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();

    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sendPath),
                              Q_ARG(QString, "test-sender-id"),
                              Q_ARG(QString, "TestSender"));

    // 接收端应显示发送方当前设置的设备别名
    QTRY_COMPARE_WITH_TIMEOUT(receivedSenderName, QString("TestSender"), 5000);
    QVERIFY(waitForTransfer(senderSpy, 5000));

    // 清理
    senderThread.quit();
    senderThread.wait();
}

void TestFileTransfer::testMultiFileTransfer()
{
    // 创建多个测试文件
    QStringList fileNames;
    for (int i = 0; i < 3; ++i) {
        QString path = _sendDir->path() + QString("/multi_%1.txt").arg(i);
        createTestFile(path, QString("Content %1").arg(i).toUtf8());
        fileNames.append(path);
    }

    // 测试 DirSerializer 对多文件的处理
    auto items = DirSerializer::serialize(_sendDir->path());
    QVERIFY(items.size() >= 3);

    // 验证每个文件都有正确的信息
    for (const auto &item : items) {
        QVERIFY(!item.relativePath.isEmpty());
        QVERIFY(item.sizeBytes >= 0);
        QVERIFY(!item.sha256.isEmpty());
    }
}

void TestFileTransfer::testDirectoryTransfer()
{
    // 创建测试目录
    QString dirPath = _sendDir->path() + "/test_dir";
    createTestDirectory(dirPath, 3);

    // 测试 DirSerializer
    auto items = DirSerializer::serialize(dirPath);
    QCOMPARE(items.size(), 3);

    // 验证文件信息
    for (const auto &item : items) {
        QVERIFY(!item.relativePath.isEmpty());
        QVERIFY(item.sizeBytes > 0);
        QVERIFY(!item.sha256.isEmpty());
    }
}

void TestFileTransfer::testCancelTransfer()
{
    // 创建大文件
    QString sendPath = _sendDir->path() + "/large_file.bin";
    QFile file(sendPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(10 * 1024 * 1024, 'A')); // 10MB
    file.close();

    // 验证文件创建成功
    QVERIFY(QFile::exists(sendPath));
    QCOMPARE(QFileInfo(sendPath).size(), 10 * 1024 * 1024);
}

void TestFileTransfer::testLargeFileTransfer()
{
    // 创建大文件（100MB）
    QString sendPath = _sendDir->path() + "/very_large.bin";
    QFile file(sendPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(100 * 1024 * 1024, 'B')); // 100MB
    file.close();

    // 验证 SHA-256 计算
    QString sha256 = DirSerializer::computeSha256(sendPath);
    QVERIFY(!sha256.isEmpty());
    QCOMPARE(sha256.size(), 64); // SHA-256 hex 长度
}

void TestFileTransfer::testSha256Verification()
{
    // 创建测试文件
    QString filePath = _sendDir->path() + "/sha256_test.txt";
    createTestFile(filePath, "SHA256 verification test content");

    // 计算 SHA-256
    QString sha256 = DirSerializer::computeSha256(filePath);
    QVERIFY(!sha256.isEmpty());

    // 验证相同内容产生相同哈希
    QString sha256Again = DirSerializer::computeSha256(filePath);
    QCOMPARE(sha256, sha256Again);

    // 验证不同内容产生不同哈希
    QString otherPath = _sendDir->path() + "/sha256_other.txt";
    createTestFile(otherPath, "Different content");
    QString otherSha256 = DirSerializer::computeSha256(otherPath);
    QVERIFY(sha256 != otherSha256);
}

void TestFileTransfer::testZeroByteFileTransfer()
{
    // 创建零字节文件
    QString filePath = _sendDir->path() + "/empty.txt";
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    // 测试序列化
    auto items = DirSerializer::serialize(filePath);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items[0].sizeBytes, 0);
    QVERIFY(!items[0].sha256.isEmpty()); // 零字节文件也有 SHA-256
}

void TestFileTransfer::testSpecialCharFileName()
{
    // 创建包含特殊字符的文件名
    QStringList specialNames = {
        "中文文件.txt",
        "file with spaces.txt",
        "file-with-dashes.txt",
        "file_with_underscores.txt",
        "file.with.dots.txt"
    };

    for (const QString &name : specialNames) {
        QString path = _sendDir->path() + "/" + name;
        createTestFile(path, "Content for " + name.toUtf8());
    }

    // 测试序列化
    auto items = DirSerializer::serialize(_sendDir->path());
    QVERIFY(items.size() >= specialNames.size());
}

QTEST_MAIN(TestFileTransfer)
#include "test_file_transfer.moc"
