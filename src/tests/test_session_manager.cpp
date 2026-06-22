/**
* @file    test_session_manager.cpp
* @version 6.3.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   TransferSessionManager 会话管理测试
*
* 测试用例：会话创建 / 接受 / 拒绝 / 取消 / 信号通知 / 历史恢复
*
* Change Log:
* [v6.3.0] GY   2026-06-25
* * 新增已结束传输历史恢复测试
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "transfer_session_manager.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"

class TestSessionManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInit();
    void testCreateSendSession();
    void testSessionSignals();
    void testCancelSession();
    void testMultipleSessions();
    void testAcceptRejectRemoveSession();
    void testRestoreFinishedTransfers();

private:
    ConfigManager *_config = nullptr;
    DiscoveryService *_discovery = nullptr;
    P2pServer *_p2pServer = nullptr;
    TransferSessionManager *_manager = nullptr;
    QTemporaryDir *_tempDir = nullptr;
};

void TestSessionManager::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    QString configPath = _tempDir->path() + "/config.ini";
    qputenv("GRIDYARD_CONFIG", configPath.toUtf8());
    qputenv("GRIDYARD_NAME", "TestDevice");

    _config = ConfigManager::create(nullptr, nullptr);
    _discovery = new DiscoveryService(_config, this);
    _p2pServer = new P2pServer(_config, this);
    _manager = new TransferSessionManager(this);

    // 初始化
    _manager->init(_config, _discovery, _p2pServer);
}

void TestSessionManager::cleanupTestCase()
{
    delete _manager;
    delete _p2pServer;
    delete _discovery;
    delete _config;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

void TestSessionManager::testInit()
{
    // 验证初始化
    QVERIFY(_manager != nullptr);

    // 验证 sessions 属性
    QVariantList sessions = _manager->sessions();
    QCOMPARE(sessions.size(), 0);
}

void TestSessionManager::testCreateSendSession()
{
    QSignalSpy spy(_manager, &TransferSessionManager::sessionsChanged);
    QSignalSpy errorSpy(_manager, &TransferSessionManager::errorOccurred);

    // 创建发送会话（会失败，因为目标设备不存在）
    _manager->createSendSession("non_existent_device", "/tmp/test.txt");

    // 验证发射了错误信号
    QVERIFY(errorSpy.count() > 0);
    QVERIFY(!errorSpy.first().first().toString().isEmpty());
}

void TestSessionManager::testSessionSignals()
{
    // 测试 errorOccurred 信号
    QSignalSpy errorSpy(_manager, &TransferSessionManager::errorOccurred);

    // 测试 messageOccurred 信号
    QSignalSpy msgSpy(_manager, &TransferSessionManager::messageOccurred);

    // 测试 receiveRequestReceived 信号
    QSignalSpy recvSpy(_manager, &TransferSessionManager::receiveRequestReceived);

    // 触发 errorOccurred（通过创建无效会话）
    _manager->createSendSession("invalid_device", "/nonexistent/path");
    QVERIFY(errorSpy.count() > 0);
}

void TestSessionManager::testCancelSession()
{
    QSignalSpy sessionsSpy(_manager, &TransferSessionManager::sessionsChanged);

    // 取消不存在的会话（应安全处理，不崩溃）
    _manager->cancelSession("non_existent_session");

    // 验证 sessions 列表未变化
    QCOMPARE(_manager->sessions().size(), 0);
}

void TestSessionManager::testMultipleSessions()
{
    QSignalSpy errorSpy(_manager, &TransferSessionManager::errorOccurred);

    // 创建多个会话（都会失败，因为目标设备不存在）
    for (int i = 0; i < 5; ++i) {
        _manager->createSendSession(
            QString("device_%1").arg(i),
            QString("/tmp/test_%1.txt").arg(i)
        );
    }

    // 验证每个会话都触发了错误信号
    QCOMPARE(errorSpy.count(), 5);
}

void TestSessionManager::testAcceptRejectRemoveSession()
{
    QSignalSpy sessionsSpy(_manager, &TransferSessionManager::sessionsChanged);

    // 测试 acceptReceiveSession（会话不存在，应安全处理）
    _manager->acceptReceiveSession("non_existent_session");
    QCOMPARE(_manager->sessions().size(), 0);

    // 测试 rejectReceiveSession（会话不存在，应安全处理）
    _manager->rejectReceiveSession("non_existent_session");
    QCOMPARE(_manager->sessions().size(), 0);

    // 测试 removeSession（会话不存在，应安全处理）
    _manager->removeSession("non_existent_session");
    QCOMPARE(_manager->sessions().size(), 0);
}

void TestSessionManager::testRestoreFinishedTransfers()
{
    TransferRecord first;
    first.recordId = "record-1";
    first.sessionId = "restored-1";
    first.peerDeviceId = "peer-one";
    first.peerName = "Peer One";
    first.direction = RecordDirection::Incoming;
    first.displayName = "from-history.txt";
    first.fileCount = 1;
    first.totalBytes = 128;
    first.status = "completed";
    first.startedAt = QDateTime::fromString("2026-06-25T21:00:00.000Z", Qt::ISODateWithMs);

    TransferRecord second;
    second.recordId = "record-2";
    second.sessionId = "restored-2";
    second.peerDeviceId = "peer-two";
    second.peerName = "Peer Two";
    second.direction = RecordDirection::Outgoing;
    second.displayName = "failed.bin";
    second.fileCount = 1;
    second.totalBytes = 256;
    second.status = "failed";
    second.startedAt = QDateTime::fromString("2026-06-25T21:05:00.000Z", Qt::ISODateWithMs);
    second.errorCode = 12;
    second.errorMessage = QString::fromUtf8("网络错误");

    _manager->restoreFinishedTransfers({second, first});
    _manager->restoreFinishedTransfers({second});

    const QVariantList sessions = _manager->sessions();
    QCOMPARE(sessions.size(), 2);
    QCOMPARE(sessions.at(0).toMap().value("sessionId").toString(), QStringLiteral("restored-1"));
    QCOMPARE(sessions.at(1).toMap().value("sessionId").toString(), QStringLiteral("restored-2"));
    QCOMPARE(sessions.at(0).toMap().value("status").toString(), QStringLiteral("completed"));
    QCOMPARE(sessions.at(1).toMap().value("errorCode").toInt(), 12);
}

QTEST_MAIN(TestSessionManager)
#include "test_session_manager.moc"
