/**
* @file    test_session_manager.cpp
* @version 7.9.0
* @date    2026-06-25
* @author  GY
* @brief   TransferSessionManager 会话管理测试
*
* 测试用例：会话创建 / 接受 / 拒绝 / 取消 / 信号通知 / 历史恢复 /
* Relay 降级策略（从不中继、询问后中继、协调服务器离线时重试）
*
* Change Log:
* [v7.9.0] GY   2026-07-26
* * 新增 Relay 降级策略测试（awaiting_relay 状态机与降级入口校验）
* [v6.3.0] GY   2026-06-25
* * 新增已结束传输历史恢复测试
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "transfer_session_manager.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"
#include "rendezvous_client.h"
#include "rendezvous_server.h"

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
    void testRelayDegradationNever();
    void testRelayDegradationAsk();
    void testRetryViaRelayWithoutServer();

private:
    // 将测试设备注入发现服务，指向本机必然拒绝连接的端口
    QString addDeadTargetDevice();
    // 等待会话进入指定状态
    bool waitForStatus(const QString &sessionId, const QString &status, int timeoutMs = 5000);

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

// 注入指向本机拒绝连接端点的目标设备
QString TestSessionManager::addDeadTargetDevice()
{
    PeerInfo peer;
    peer.deviceId = QStringLiteral("dead-target");
    peer.deviceName = QStringLiteral("DeadTarget");
    peer.ipAddress = QStringLiteral("127.0.0.1");
    peer.tcpPort = 1;  // 本机保留端口，连接立即被拒绝
    peer.isOnline = true;
    peer.source = QStringLiteral("manual");
    _discovery->addManualPeer(peer);

    // 创建一个真实文件，保证会话能走到连接阶段
    const QString filePath = _tempDir->path() + "/relay-fallback.txt";
    QFile file{filePath};
    if (!file.exists() && !file.open(QIODevice::WriteOnly)) {
        qWarning() << "无法创建测试文件" << filePath;
        return {};
    }
    file.close();
    return filePath;
}

// 等待会话进入指定状态
bool TestSessionManager::waitForStatus(const QString &sessionId, const QString &status, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!timer.hasExpired(timeoutMs)) {
        const QVariantList sessions = _manager->sessions();
        for (const QVariant &entry : sessions) {
            const QVariantMap session = entry.toMap();
            if (session["sessionId"].toString() == sessionId
                && session["status"].toString() == status) {
                return true;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return false;
}

// 从不中继：直连失败后直接终结为 failed，不触发中继请求
void TestSessionManager::testRelayDegradationNever()
{
    // 连接真实协调服务器，使"策略放行"与"协调在线"两个条件同时成立，
    // 只有 NeverRelay 策略能阻止降级
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());
    RendezvousClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    _config->setRelayMode(RelayMode::NeverRelay);
    _manager->init(_config, _discovery, _p2pServer, &client);

    const QString filePath = addDeadTargetDevice();
    QVERIFY(!filePath.isEmpty());
    QSignalSpy relaySpy(_manager, &TransferSessionManager::relayModeRequested);
    _manager->createSendSession(QStringLiteral("dead-target"), filePath);

    const QString sessionId = _manager->sessions().last().toMap()["sessionId"].toString();
    QVERIFY(waitForStatus(sessionId, "failed"));
    QCOMPARE(relaySpy.count(), 0);

    client.disconnectFromServer();
    _config->setRelayMode(RelayMode::AskBeforeRelay);
}

// 询问后中继：直连失败进入 awaiting_relay 并发射 relayModeRequested，用户取消后收敛
void TestSessionManager::testRelayDegradationAsk()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());
    RendezvousClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    _config->setRelayMode(RelayMode::AskBeforeRelay);
    _manager->init(_config, _discovery, _p2pServer, &client);

    const QString filePath = addDeadTargetDevice();
    QVERIFY(!filePath.isEmpty());
    QSignalSpy relaySpy(_manager, &TransferSessionManager::relayModeRequested);
    _manager->createSendSession(QStringLiteral("dead-target"), filePath);

    const QString sessionId = _manager->sessions().last().toMap()["sessionId"].toString();
    QVERIFY(waitForStatus(sessionId, "awaiting_relay"));
    QCOMPARE(relaySpy.count(), 1);
    QCOMPARE(relaySpy.first().at(0).toString(), sessionId);
    QCOMPARE(relaySpy.first().at(1).toString(), QStringLiteral("dead-target"));

    // 用户放弃中继：会话以 cancelled 收敛，不再悬挂
    _manager->cancelSession(sessionId);
    QVERIFY(waitForStatus(sessionId, "cancelled"));

    client.disconnectFromServer();
}

// 协调服务器离线时确认中继：会话以 failed 收敛并给出错误提示
void TestSessionManager::testRetryViaRelayWithoutServer()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());
    RendezvousClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    _config->setRelayMode(RelayMode::AskBeforeRelay);
    _manager->init(_config, _discovery, _p2pServer, &client);

    const QString filePath = addDeadTargetDevice();
    QVERIFY(!filePath.isEmpty());
    _manager->createSendSession(QStringLiteral("dead-target"), filePath);
    const QString sessionId = _manager->sessions().last().toMap()["sessionId"].toString();
    QVERIFY(waitForStatus(sessionId, "awaiting_relay"));

    // 模拟用户确认时协调服务器已经掉线
    client.disconnectFromServer();
    QTRY_VERIFY_WITH_TIMEOUT(!client.isConnected(), 3000);

    QSignalSpy errorSpy(_manager, &TransferSessionManager::errorOccurred);
    _manager->retryViaRelay(sessionId);
    QVERIFY(waitForStatus(sessionId, "failed"));
    QVERIFY(errorSpy.count() >= 1);  // 失败原因已通过 errorOccurred 提示

    client.disconnectFromServer();
}

QTEST_MAIN(TestSessionManager)
#include "test_session_manager.moc"
