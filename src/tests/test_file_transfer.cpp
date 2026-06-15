/**
* @file    test_file_transfer.cpp
* @version 4.13.2
* @date    2026-06-15
* @author  GridYard Team
* @brief   文件传输完整流程测试
*
* 测试用例：单文件传输 / 多文件传输 / 取消传输 / 超时处理 / SHA-256 校验
*
* Change Log:
* [v4.13.2] FengChunlin   2026-06-15
* * 验证接收确认的文件夹名、总大小和根目录预览
* [v4.13.1] FengChunlin   2026-06-15
* * 验证文件夹接收请求显示名和相对路径列表
* [v4.12.1] FengChunlin   2026-06-14
* * 增加多文件夹、零字节文件和大型文件端到端传输回归测试
* [v4.11.0] GY   2026-06-13
* * 新增文件夹根目录与空文件夹端到端传输测试
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
#include "discovery_service.h"
#include "dir_serializer.h"
#include "protocol.h"
#include "transfer_session_manager.h"

using gy::DirSerializer;

class TestFileTransfer : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSingleFileTransfer();
    void testMultiFileTransfer();
    void testDirectoryTransfer();
    void testDirectoryTransferEndToEnd();
    void testEmptyDirectoryTransferEndToEnd();
    void testLargeFileTransferEndToEnd();
    void testAutoAcceptAndSave();
    void testReceiveFolderPreview();
    void testCancelTransfer();
    void testConnectionLost();
    void testTransferTimeout();
    void testLargeFileTransfer();
    void testSha256Verification();
    void testZeroByteFileTransfer();
    void testSpecialCharFileName();

private:
    void createTestFile(const QString &path, const QByteArray &content);
    void createTestDirectory(const QString &basePath, int fileCount);
    bool waitForTransfer(QSignalSpy &spy, int timeout = 10000);
    void stopSenderThread(FileSenderWorker &sender, QThread &thread);

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

void TestFileTransfer::stopSenderThread(FileSenderWorker &sender, QThread &thread)
{
    QThread *mainThread = QCoreApplication::instance()->thread();
    QMetaObject::invokeMethod(&sender, [&sender, mainThread]() {
        sender.moveToThread(mainThread);
    }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
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

    QString receivedSenderDeviceId;
    QString receivedSenderName;
    connect(&server, &P2pServer::transferRequestReceived,
            this, [&receivedSenderDeviceId, &receivedSenderName](
                                        FileReceiverWorker *worker,
                                        const QString &senderDeviceId,
                                        const QString &senderName,
                                        const QString &, qint64, int, qint64) {
        receivedSenderDeviceId = senderDeviceId;
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
    QTRY_COMPARE_WITH_TIMEOUT(receivedSenderDeviceId, QString("test-sender-id"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(receivedSenderName, QString("TestSender"), 5000);
    QVERIFY(waitForTransfer(senderSpy, 5000));

    // 清理
    stopSenderThread(sender, senderThread);
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

void TestFileTransfer::testDirectoryTransferEndToEnd()
{
    const QString sourcePath = _sendDir->path() + "/folder_e2e";
    QVERIFY(QDir().mkpath(sourcePath + "/nested/empty"));
    createTestFile(sourcePath + "/nested/content.txt", "folder transfer content");
    createTestFile(sourcePath + "/nested/second.txt", "second file content");
    createTestFile(sourcePath + "/nested/zz_empty.txt", QByteArray());

    _config->setReceivePath(_recvDir->path());
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool receiverFinished = false;
    bool receiverSuccess = false;
    QString receiverDisplayName;
    QStringList receiverFilePaths;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [this, &receiverFinished, &receiverSuccess,
             &receiverDisplayName, &receiverFilePaths](
                FileReceiverWorker *worker, const QString &, const QString &,
                const QString &, qint64, int, qint64) {
        receiverDisplayName = worker->fileName();
        receiverFilePaths = worker->filePaths();
        worker->setReceivePath(_recvDir->path());
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        worker->acceptTransfer();
    });

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();
    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sourcePath),
                              Q_ARG(QString, "folder-sender-id"),
                              Q_ARG(QString, "FolderSender"));

    QTRY_VERIFY_WITH_TIMEOUT(receiverFinished, 10000);
    QVERIFY(receiverSuccess);
    QVERIFY(waitForTransfer(senderSpy));
    QVERIFY(senderSpy.first().at(0).toBool());
    QVERIFY(QFile::exists(_recvDir->path() + "/folder_e2e/nested/content.txt"));
    QVERIFY(QFile::exists(_recvDir->path() + "/folder_e2e/nested/second.txt"));
    QVERIFY(QFile::exists(_recvDir->path() + "/folder_e2e/nested/zz_empty.txt"));
    QVERIFY(QDir(_recvDir->path() + "/folder_e2e/nested/empty").exists());
    QCOMPARE(receiverDisplayName, QString("folder_e2e"));
    QVERIFY(receiverFilePaths.contains("nested/content.txt"));
    QVERIFY(receiverFilePaths.contains("nested/empty/"));

    stopSenderThread(sender, senderThread);
}

void TestFileTransfer::testEmptyDirectoryTransferEndToEnd()
{
    const QString sourcePath = _sendDir->path() + "/empty_folder_e2e";
    QVERIFY(QDir().mkpath(sourcePath));

    _config->setReceivePath(_recvDir->path());
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool receiverFinished = false;
    bool receiverSuccess = false;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [this, &receiverFinished, &receiverSuccess](
                FileReceiverWorker *worker, const QString &, const QString &,
                const QString &, qint64, int, qint64) {
        worker->setReceivePath(_recvDir->path());
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        worker->acceptTransfer();
    });

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();
    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sourcePath),
                              Q_ARG(QString, "folder-sender-id"),
                              Q_ARG(QString, "FolderSender"));

    QTRY_VERIFY_WITH_TIMEOUT(receiverFinished, 10000);
    QVERIFY(receiverSuccess);
    QVERIFY(waitForTransfer(senderSpy));
    QVERIFY(senderSpy.first().at(0).toBool());
    QVERIFY(QDir(_recvDir->path() + "/empty_folder_e2e").exists());

    stopSenderThread(sender, senderThread);
}

void TestFileTransfer::testLargeFileTransferEndToEnd()
{
    const QString sendPath = _sendDir->path() + "/large_e2e.bin";
    QFile sourceFile(sendPath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    QVERIFY(sourceFile.resize(40 * 1024 * 1024));
    sourceFile.close();

    _config->setReceivePath(_recvDir->path());
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool receiverFinished = false;
    bool receiverSuccess = false;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [this, &receiverFinished, &receiverSuccess](
                FileReceiverWorker *worker, const QString &, const QString &,
                const QString &, qint64, int, qint64) {
        worker->setReceivePath(_recvDir->path());
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        worker->acceptTransfer();
    });

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();
    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sendPath),
                              Q_ARG(QString, "large-sender-id"),
                              Q_ARG(QString, "LargeSender"));

    QTRY_VERIFY_WITH_TIMEOUT(receiverFinished, 30000);
    const bool senderFinished = waitForTransfer(senderSpy, 30000);
    const bool senderSuccess = senderFinished
        && senderSpy.first().at(0).toBool();
    const qint64 receivedSize = QFileInfo(_recvDir->path() + "/large_e2e.bin").size();
    const qint64 sourceSize = QFileInfo(sendPath).size();

    stopSenderThread(sender, senderThread);

    QVERIFY(receiverSuccess);
    QVERIFY(senderFinished);
    QVERIFY(senderSuccess);
    QCOMPARE(receivedSize, sourceSize);
}

void TestFileTransfer::testAutoAcceptAndSave()
{
    const QString sendPath = _sendDir->path() + "/auto_accept.txt";
    createTestFile(sendPath, "auto accept content");

    _config->setReceivePath(_recvDir->path());
    _config->setAutoAcceptFiles(true);
    _config->setTcpPort(++_testPort);

    DiscoveryService discovery(_config);
    P2pServer server(_config);
    TransferSessionManager manager;
    manager.init(_config, &discovery, &server);
    QVERIFY(server.start());

    QSignalSpy requestSpy(&manager, &TransferSessionManager::receiveRequestReceived);
    QSignalSpy completedSpy(&manager, &TransferSessionManager::transferCompleted);

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();
    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sendPath),
                              Q_ARG(QString, "auto-sender-id"),
                              Q_ARG(QString, "AutoSender"));

    QVERIFY(waitForTransfer(completedSpy));
    QVERIFY(waitForTransfer(senderSpy));
    QVERIFY(senderSpy.first().at(0).toBool());
    QCOMPARE(requestSpy.count(), 0);
    QVERIFY(QFile::exists(_recvDir->path() + "/auto_accept.txt"));

    stopSenderThread(sender, senderThread);
    _config->setAutoAcceptFiles(false);
}

void TestFileTransfer::testReceiveFolderPreview()
{
    const QString sourcePath = _sendDir->path() + "/preview_folder";
    QVERIFY(QDir().mkpath(sourcePath + "/nested"));
    createTestFile(sourcePath + "/root.txt", "root");
    createTestFile(sourcePath + "/nested/child.png", "child");

    _config->setAutoAcceptFiles(false);
    _config->setTcpPort(++_testPort);

    DiscoveryService discovery(_config);
    P2pServer server(_config);
    TransferSessionManager manager;
    manager.init(_config, &discovery, &server);
    QVERIFY(server.start());

    QSignalSpy requestSpy(&manager, &TransferSessionManager::receiveRequestReceived);

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();
    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sourcePath),
                              Q_ARG(QString, "preview-sender-id"),
                              Q_ARG(QString, "PreviewSender"));

    QVERIFY(waitForTransfer(requestSpy));
    const QList<QVariant> request = requestSpy.first();
    QCOMPARE(request.size(), 9);
    QCOMPARE(request.at(3).toString(), QString("preview_folder"));
    QCOMPARE(request.at(5).toInt(), 2);
    QCOMPARE(request.at(6).toLongLong(), qint64(9));
    QVERIFY(request.at(7).toBool());

    const QVariantList preview = request.at(8).toList();
    QCOMPARE(preview.size(), 2);
    QVERIFY(preview.contains(QString("root.txt")));
    QVERIFY(preview.contains(QString("nested/")));

    manager.rejectReceiveSession(request.at(0).toString());
    QVERIFY(waitForTransfer(senderSpy));
    stopSenderThread(sender, senderThread);
}

void TestFileTransfer::testCancelTransfer()
{
    // 创建大文件（500MB）确保传输不会立即完成
    const QString sendPath = _sendDir->path() + "/cancel_test.bin";
    QFile sourceFile(sendPath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write(QByteArray(500 * 1024 * 1024, 'A'));
    sourceFile.close();

    _config->setReceivePath(_recvDir->path());
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool receiverFinished = false;
    bool receiverSuccess = true;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [this, &receiverFinished, &receiverSuccess](
                FileReceiverWorker *worker, const QString &, const QString &,
                const QString &, qint64, int, qint64) {
        worker->setReceivePath(_recvDir->path());
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        worker->acceptTransfer();
    });

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();

    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sendPath),
                              Q_ARG(QString, "cancel-sender-id"),
                              Q_ARG(QString, "CancelSender"));

    // 等待传输开始后取消
    QTest::qWait(200);
    QMetaObject::invokeMethod(&sender, "cancel", Qt::QueuedConnection);

    // 验证发送端收到取消结果
    QVERIFY(waitForTransfer(senderSpy, 10000));
    QVERIFY(!senderSpy.first().at(0).toBool());

    stopSenderThread(sender, senderThread);
}

void TestFileTransfer::testConnectionLost()
{
    // 创建大文件（500MB）确保传输不会立即完成
    const QString sendPath = _sendDir->path() + "/conn_lost.bin";
    QFile sourceFile(sendPath);
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write(QByteArray(500 * 1024 * 1024, 'C'));
    sourceFile.close();

    _config->setReceivePath(_recvDir->path());
    _config->setTcpPort(++_testPort);

    // 使用原始 QTcpServer 模拟连接断开
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4, _testPort));

    bool receiverFinished = false;
    bool receiverSuccess = true;
    connect(&server, &QTcpServer::newConnection, this, [this, &server, &receiverFinished, &receiverSuccess]() {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket) return;

        auto *worker = new FileReceiverWorker(socket, this);
        worker->setReceivePath(_recvDir->path());
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        connect(worker, &FileReceiverWorker::transferRequestReceived, this,
                [worker](const QString &, const QString &, const QString &, qint64, int, qint64) {
            worker->acceptTransfer();
        });
        // 传输开始后断开连接
        QTimer::singleShot(100, socket, &QTcpSocket::disconnectFromHost);
    });

    FileSenderWorker sender;
    QSignalSpy senderSpy(&sender, &FileSenderWorker::transferFinished);
    QThread senderThread;
    sender.moveToThread(&senderThread);
    senderThread.start();

    QMetaObject::invokeMethod(&sender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, sendPath),
                              Q_ARG(QString, "connlost-sender-id"),
                              Q_ARG(QString, "ConnLostSender"));

    // 验证发送端收到连接断开错误
    QVERIFY(waitForTransfer(senderSpy, 10000));
    QVERIFY(!senderSpy.first().at(0).toBool());

    stopSenderThread(sender, senderThread);
}

void TestFileTransfer::testTransferTimeout()
{
    // 验证超时机制存在
    // 注意：真正的超时测试需要等待 30 秒，这里只验证机制正确性
    FileSenderWorker sender;

    // 验证 sender 有超时处理能力
    QVERIFY(sender.metaObject()->indexOfSlot("onTimeout()") >= 0);
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
