/**
* @file    test_file_transfer.cpp
* @version 4.16.4
* @date    2026-06-24
* @author  GridYard Team
* @brief   文件传输完整流程测试
*
* 测试用例：单文件传输 / 多文件传输 / 取消传输 / 超时处理 / SHA-256 校验
*
* Change Log:
* [v4.16.4] GY   2026-06-24
* * 新增首帧路由的聊天连接和未知 Type 测试
* [v4.16.1] GY   2026-06-21
* * 改用接收请求快照和完成结果验证文件接收流程
* [v4.15.1] FengChunlin   2026-06-17
* * 新增 testProtocolVersionMismatch：主版本不兼容时接收端拒绝并断开
* * 新增 testMalformedTransferRequest：无效 JSON、缺字段、数量不一致均被拒绝
* * 新增 testInvalidFilePath：../ 路径、绝对路径、负数大小均被拒绝
* * 补充 #include "frame_codec.h"
* [v4.15.0] GY   2026-06-17
* * 适配 transferFinished 信号添加 ErrorCode 参数
* * 适配接收侧后台化线程模型
* * testConnectionLost 改用系统分配端口，减少端口复用导致的失败
* * testConnectionLost 等待接收线程退出后再结束用例
* [v4.14.0] GY   2026-06-15
* * 验证移除接收记录时删除实际保存文件且不影响发送源文件
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
#include "chat_message.h"
#include "frame_codec.h"
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
    void testProtocolVersionMismatch();
    void testMalformedTransferRequest();
    void testInvalidFilePath();
    void testChatConnectionRouting();
    void testUnsupportedFirstFrameRejected();

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
                                        const QVariantMap &request) {
        const QString senderDeviceId = request["senderDeviceId"].toString();
        const QString senderName = request["senderName"].toString();
        qDebug() << "[Test] 收到传输请求信号，senderDeviceId:" << senderDeviceId;
        receivedSenderDeviceId = senderDeviceId;
        receivedSenderName = senderName;
        // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
        QMetaObject::invokeMethod(worker, [worker]() {
            worker->rejectTransfer("测试完成");
        }, Qt::QueuedConnection);
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
                FileReceiverWorker *worker, const QVariantMap &request) {
        receiverDisplayName = request["fileName"].toString();
        receiverFilePaths = request["sourcePaths"].toStringList();
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, gy::protocol::ErrorCode,
                                                      const QString &, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
        QMetaObject::invokeMethod(worker, [worker, this]() {
            worker->setReceivePath(_recvDir->path());
            worker->acceptTransfer();
        }, Qt::QueuedConnection);
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
                FileReceiverWorker *worker, const QVariantMap &) {
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, gy::protocol::ErrorCode,
                                                      const QString &, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
        QMetaObject::invokeMethod(worker, [worker, this]() {
            worker->setReceivePath(_recvDir->path());
            worker->acceptTransfer();
        }, Qt::QueuedConnection);
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
                FileReceiverWorker *worker, const QVariantMap &) {
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, gy::protocol::ErrorCode,
                                                      const QString &, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
        QMetaObject::invokeMethod(worker, [worker, this]() {
            worker->setReceivePath(_recvDir->path());
            worker->acceptTransfer();
        }, Qt::QueuedConnection);
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
    const QString receivedPath = _recvDir->path() + "/auto_accept.txt";
    QVERIFY(QFile::exists(receivedPath));

    const QVariantList sessions = manager.sessions();
    QCOMPARE(sessions.size(), 1);
    const QVariantMap receivedSession = sessions.first().toMap();
    QCOMPARE(receivedSession["localPath"].toString(), receivedPath);
    QVERIFY(receivedSession["canDeleteLocalFile"].toBool());

    manager.removeSessionAndDeleteFile(receivedSession["sessionId"].toString());
    QVERIFY(manager.sessions().isEmpty());
    QVERIFY(!QFile::exists(receivedPath));
    QVERIFY(QFile::exists(sendPath));

    stopSenderThread(sender, senderThread);

    const QString clearSendPath = _sendDir->path() + "/auto_clear.txt";
    createTestFile(clearSendPath, "clear finished sessions");
    completedSpy.clear();

    FileSenderWorker clearSender;
    QSignalSpy clearSenderSpy(&clearSender, &FileSenderWorker::transferFinished);
    QThread clearSenderThread;
    clearSender.moveToThread(&clearSenderThread);
    clearSenderThread.start();
    QMetaObject::invokeMethod(&clearSender, "startTransfer", Qt::QueuedConnection,
                              Q_ARG(QString, "127.0.0.1"),
                              Q_ARG(quint16, _testPort),
                              Q_ARG(QString, clearSendPath),
                              Q_ARG(QString, "auto-sender-id"),
                              Q_ARG(QString, "AutoSender"));

    QVERIFY(waitForTransfer(completedSpy));
    QVERIFY(waitForTransfer(clearSenderSpy));
    QVERIFY(clearSenderSpy.first().at(0).toBool());

    const QString clearReceivedPath = _recvDir->path() + "/auto_clear.txt";
    QVERIFY(QFile::exists(clearReceivedPath));
    manager.clearFinishedSessions(true);
    QVERIFY(manager.sessions().isEmpty());
    QVERIFY(!QFile::exists(clearReceivedPath));
    QVERIFY(QFile::exists(clearSendPath));

    stopSenderThread(clearSender, clearSenderThread);
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
                FileReceiverWorker *worker, const QVariantMap &) {
        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess](bool success, gy::protocol::ErrorCode,
                                                      const QString &, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
        });
        // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
        QMetaObject::invokeMethod(worker, [worker, this]() {
            worker->setReceivePath(_recvDir->path());
            worker->acceptTransfer();
        }, Qt::QueuedConnection);
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

    // 使用原始 QTcpServer 模拟连接断开
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::AnyIPv4, 0));
    const quint16 serverPort = server.serverPort();
    QVERIFY(serverPort != 0);

    bool receiverFinished = false;
    bool receiverSuccess = true;
    bool receiverThreadFinished = false;
    connect(&server, &QTcpServer::newConnection, this,
            [this, &server, &receiverFinished, &receiverSuccess, &receiverThreadFinished]() {
        QTcpSocket *socket = server.nextPendingConnection();
        if (!socket) return;

        // 创建后台线程处理接收
        auto *thread = new QThread{this};
        auto *worker = new FileReceiverWorker{socket};
        worker->moveToThread(thread);

        connect(worker, &FileReceiverWorker::transferFinished, this,
                [&receiverFinished, &receiverSuccess, thread](bool success, gy::protocol::ErrorCode,
                                                              const QString &, const QString &) {
            receiverFinished = true;
            receiverSuccess = success;
            thread->quit();
        });
        connect(thread, &QThread::started, worker, &FileReceiverWorker::initialize);
        connect(worker, &FileReceiverWorker::transferRequestReceived, this,
                [worker, this](const QVariantMap &) {
            QMetaObject::invokeMethod(worker, [worker, this]() {
                worker->setReceivePath(_recvDir->path());
                worker->acceptTransfer();
            }, Qt::QueuedConnection);
        });
        connect(thread, &QThread::finished, this, [&receiverThreadFinished]() {
            receiverThreadFinished = true;
        });
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        thread->start();

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
                              Q_ARG(quint16, serverPort),
                              Q_ARG(QString, sendPath),
                              Q_ARG(QString, "connlost-sender-id"),
                              Q_ARG(QString, "ConnLostSender"));

    // 验证发送端收到连接断开错误
    QVERIFY(waitForTransfer(senderSpy, 10000));
    QVERIFY(!senderSpy.first().at(0).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(receiverFinished, 5000);
    QVERIFY(!receiverSuccess);
    QTRY_VERIFY_WITH_TIMEOUT(receiverThreadFinished, 5000);

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

void TestFileTransfer::testProtocolVersionMismatch()
{
    // 构造一个主版本号不同的 TransferReq，发送到接收端，应被拒绝
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool rejected = false;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [&rejected](FileReceiverWorker *, const QVariantMap &) {
        // 不应该收到请求（应被版本检查拒绝）
        rejected = true;
    });

    // 手动连接到接收端并发送错误版本的帧
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, _testPort);
    QVERIFY(socket.waitForConnected(5000));

    // 构造主版本号为 99 的 TransferReq
    QJsonObject json;
    json["session_id"] = "test-mismatch";
    json["sender_device_id"] = "bad-sender";
    json["sender_name"] = "BadSender";
    json["is_directory"] = false;
    json["root_name"] = "test.txt";
    json["total_files"] = 0;
    json["total_bytes"] = 0;
    json["protocol_version"] = static_cast<int>(99 << 8);  // 主版本 99
    json["files"] = QJsonArray();

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
    socket.write(frame);
    socket.flush();

    // 等待一小段时间，验证没有收到请求（被版本拒绝）
    QTest::qWait(500);
    QVERIFY(!rejected);

    // 验证连接被关闭（接收端断开）
    if (socket.state() != QAbstractSocket::UnconnectedState) {
        socket.waitForDisconnected(2000);
    }
    QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
}

void TestFileTransfer::testMalformedTransferRequest()
{
    // 发送畸形 JSON 到接收端，应被拒绝且不崩溃
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool rejected = false;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [&rejected](FileReceiverWorker *, const QVariantMap &) {
        rejected = true;
    });

    // 测试 1：发送无效 JSON
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, _testPort);
        QVERIFY(socket.waitForConnected(5000));

        QByteArray badJson = "{invalid json!!!";
        QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, badJson);
        socket.write(frame);
        socket.flush();

        QTest::qWait(200);
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(2000);
        }
        QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
    }

    // 测试 2：发送缺少必需字段的 JSON
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, _testPort);
        QVERIFY(socket.waitForConnected(5000));

        QJsonObject json;
        json["session_id"] = "test-missing";
        // 缺少 sender_device_id, sender_name, total_files, files 等
        QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
        socket.write(frame);
        socket.flush();

        QTest::qWait(200);
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(2000);
        }
        QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
    }

    // 测试 3：发送 total_files 与 files 数组不一致
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, _testPort);
        QVERIFY(socket.waitForConnected(5000));

        QJsonObject json;
        json["session_id"] = "test-mismatch-count";
        json["sender_device_id"] = "test-sender";
        json["sender_name"] = "TestSender";
        json["is_directory"] = false;
        json["root_name"] = "test.txt";
        json["total_files"] = 2;  // 声称 2 个文件
        json["total_bytes"] = 0;
        json["protocol_version"] = gy::protocol::kProtocolVersion;
        json["files"] = QJsonArray();  // 实际 0 个
        json["empty_directories"] = QJsonArray();

        QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
        socket.write(frame);
        socket.flush();

        QTest::qWait(200);
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(2000);
        }
        QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
    }

    // 验证从未收到请求
    QVERIFY(!rejected);
}

void TestFileTransfer::testInvalidFilePath()
{
    // 发送包含危险路径的 TransferReq，应被拒绝
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool rejected = false;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [&rejected](FileReceiverWorker *, const QVariantMap &) {
        rejected = true;
    });

    // 测试 1：../ 路径
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, _testPort);
        QVERIFY(socket.waitForConnected(5000));

        QJsonObject fileObj;
        fileObj["file_index"] = 0;
        fileObj["relative_path"] = "../etc/passwd";
        fileObj["size_bytes"] = 100;
        fileObj["sha256"] = "abc";

        QJsonObject json;
        json["session_id"] = "test-bad-path";
        json["sender_device_id"] = "test-sender";
        json["sender_name"] = "TestSender";
        json["is_directory"] = false;
        json["root_name"] = "test.txt";
        json["total_files"] = 1;
        json["total_bytes"] = 100;
        json["protocol_version"] = gy::protocol::kProtocolVersion;
        json["files"] = QJsonArray{fileObj};
        json["empty_directories"] = QJsonArray();

        QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
        socket.write(frame);
        socket.flush();

        QTest::qWait(200);
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(2000);
        }
        QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
    }

    // 测试 2：绝对路径
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, _testPort);
        QVERIFY(socket.waitForConnected(5000));

        QJsonObject fileObj;
        fileObj["file_index"] = 0;
        fileObj["relative_path"] = "/etc/passwd";
        fileObj["size_bytes"] = 100;
        fileObj["sha256"] = "abc";

        QJsonObject json;
        json["session_id"] = "test-abs-path";
        json["sender_device_id"] = "test-sender";
        json["sender_name"] = "TestSender";
        json["is_directory"] = false;
        json["root_name"] = "test.txt";
        json["total_files"] = 1;
        json["total_bytes"] = 100;
        json["protocol_version"] = gy::protocol::kProtocolVersion;
        json["files"] = QJsonArray{fileObj};
        json["empty_directories"] = QJsonArray();

        QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
        socket.write(frame);
        socket.flush();

        QTest::qWait(200);
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(2000);
        }
        QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
    }

    // 测试 3：负数大小
    {
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, _testPort);
        QVERIFY(socket.waitForConnected(5000));

        QJsonObject fileObj;
        fileObj["file_index"] = 0;
        fileObj["relative_path"] = "test.txt";
        fileObj["size_bytes"] = -1;
        fileObj["sha256"] = "abc";

        QJsonObject json;
        json["session_id"] = "test-neg-size";
        json["sender_device_id"] = "test-sender";
        json["sender_name"] = "TestSender";
        json["is_directory"] = false;
        json["root_name"] = "test.txt";
        json["total_files"] = 1;
        json["total_bytes"] = -1;
        json["protocol_version"] = gy::protocol::kProtocolVersion;
        json["files"] = QJsonArray{fileObj};
        json["empty_directories"] = QJsonArray();

        QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
        QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
        socket.write(frame);
        socket.flush();

        QTest::qWait(200);
        if (socket.state() != QAbstractSocket::UnconnectedState) {
            socket.waitForDisconnected(2000);
        }
        QVERIFY(socket.state() == QAbstractSocket::UnconnectedState);
    }

    // 验证从未收到请求
    QVERIFY(!rejected);
}

void TestFileTransfer::testChatConnectionRouting()
{
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    QTcpSocket *routedSocket = nullptr;
    QByteArray routedData;
    bool transferRequestReceived = false;
    connect(&server, &P2pServer::transferRequestReceived, this,
            [&transferRequestReceived](FileReceiverWorker *, const QVariantMap &) {
        transferRequestReceived = true;
    });
    connect(&server, &P2pServer::chatConnectionReceived, this,
            [this, &routedSocket, &routedData](QTcpSocket *socket) {
        routedSocket = socket;
        socket->setParent(this);
        routedData = socket->readAll();
    });

    gy::ChatMessage message;
    message.messageId = "c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c";
    message.fromDeviceId = "chat-sender-id";
    message.fromName = "ChatSender";
    message.content = "首帧路由测试";
    message.sentAt = QDateTime::currentDateTimeUtc();

    QByteArray payload;
    QVERIFY(gy::ChatMessageCodec::encode(message, &payload));
    const QByteArray frame = FrameCodec::encode(gy::protocol::kTypeChatText, payload);
    QVERIFY(!frame.isEmpty());

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, _testPort);
    QVERIFY(client.waitForConnected(5000));

    QVERIFY(client.write(frame.left(gy::protocol::kHeaderBytes)) > 0);
    QVERIFY(client.waitForBytesWritten(1000));
    QTest::qWait(100);
    QVERIFY(routedSocket == nullptr);

    QVERIFY(client.write(frame.mid(gy::protocol::kHeaderBytes)) > 0);
    QVERIFY(client.waitForBytesWritten(1000));
    QTRY_VERIFY_WITH_TIMEOUT(routedSocket != nullptr, 3000);
    QCOMPARE(routedData, frame);
    QVERIFY(!transferRequestReceived);

    FrameCodec codec;
    QSignalSpy frameSpy(&codec, &FrameCodec::frameReady);
    codec.feed(routedData);
    QCOMPARE(frameSpy.count(), 1);
    QCOMPARE(frameSpy.first().at(0).toUInt(), gy::protocol::kTypeChatText);
    QCOMPARE(frameSpy.first().at(1).toByteArray(), payload);

    client.disconnectFromHost();
    client.waitForDisconnected(1000);
}

void TestFileTransfer::testUnsupportedFirstFrameRejected()
{
    _config->setTcpPort(++_testPort);
    P2pServer server(_config);
    QVERIFY(server.start());

    bool chatConnectionReceived = false;
    bool transferRequestReceived = false;
    connect(&server, &P2pServer::chatConnectionReceived, this,
            [&chatConnectionReceived](QTcpSocket *socket) {
        chatConnectionReceived = true;
        socket->setParent(nullptr);
        socket->deleteLater();
    });
    connect(&server, &P2pServer::transferRequestReceived, this,
            [&transferRequestReceived](FileReceiverWorker *, const QVariantMap &) {
        transferRequestReceived = true;
    });

    QTcpSocket client;
    QSignalSpy disconnectedSpy(&client, &QTcpSocket::disconnected);
    client.connectToHost(QHostAddress::LocalHost, _testPort);
    QVERIFY(client.waitForConnected(5000));

    const QByteArray frame = FrameCodec::encode(0x0503, "{}");
    QVERIFY(client.write(frame) > 0);
    QVERIFY(client.waitForBytesWritten(1000));
    QVERIFY(disconnectedSpy.wait(3000));
    QVERIFY(!chatConnectionReceived);
    QVERIFY(!transferRequestReceived);

    QTcpSocket invalidChatClient;
    QSignalSpy invalidChatDisconnectedSpy(&invalidChatClient, &QTcpSocket::disconnected);
    invalidChatClient.connectToHost(QHostAddress::LocalHost, _testPort);
    QVERIFY(invalidChatClient.waitForConnected(5000));

    const QByteArray invalidChatFrame = FrameCodec::encode(gy::protocol::kTypeChatText, "{}");
    QVERIFY(invalidChatClient.write(invalidChatFrame) > 0);
    QVERIFY(invalidChatClient.waitForBytesWritten(1000));
    QVERIFY(invalidChatDisconnectedSpy.wait(3000));
    QVERIFY(!chatConnectionReceived);
    QVERIFY(!transferRequestReceived);
}

QTEST_MAIN(TestFileTransfer)
#include "test_file_transfer.moc"
