/**
* @file    test_session_manager.cpp
* @version 7.17.0
* @date    2026-10-04
* @author  GY
* @brief   TransferSessionManager 会话管理测试
*
* 测试用例：会话创建 / 接受 / 拒绝 / 取消 / 信号通知 / 历史恢复 /
* Relay 降级策略（从不中继、询问后中继、自动中继端到端、协调服务器离线时重试）/
* waiting_confirm 会话过期信号 / 并发请求的等待确认快照队列化 / 活动会话计数增减
*
* Change Log:
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 新增活动会话计数用例：会话创建时加一、终态迁移时减一且各发一次 NOTIFY
* [v7.15.17] GY   2026-10-04
* * 新增并发接收请求的等待确认快照队列化用例：拒绝与后端终态后快照正确收敛
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.8] GY   2026-10-03
* * 适配 RendezvousServer 构造函数删除 host 死参数
* [v7.15.7] GY   2026-10-03
* * 会话状态轮询等待改用 tests/test_utils 的 waitFor
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.15.1] GY   2026-10-03
* * 新增 sessionStale 信号用例：后端终结恰好一次、正常完成与用户拒绝不发射
* [v7.15.0] GY   2026-10-03
* * relayModeRequested 断言随信号更名调整，AutoRelay 档升级为经真实中继的端到端用例
* [v7.14.2] GY   2026-10-03
* * 新增重复取消守卫、完成后迟到操作守卫与 AutoRelay 档用例
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
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include "transfer_session_manager.h"
#include "transfer_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "file_sender_worker.h"
#include "p2p_server.h"
#include "relay_server.h"
#include "transfer_session_model.h"
#include "rendezvous_client.h"
#include "rendezvous_server.h"

#include "test_utils/wait.h"

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
    void testRelayDegradationAuto();
    void testRetryViaRelayWithoutServer();
    void testRepeatedCancelOnLiveSession();
    void testGuardsAfterCompletion();
    void testSessionStaleSignal();
    void testWaitingConfirmSnapshotQueueing();
    void testActiveSessionCountLifecycle();
    void testIncomingRequestEntrySignal();

private:
    // 将测试设备注入发现服务，指向本机必然拒绝连接的端口
    QString addDeadTargetDevice(const QString &deviceId = QStringLiteral("dead-target"));
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
QString TestSessionManager::addDeadTargetDevice(const QString &deviceId)
{
    PeerInfo peer;
    peer.deviceId = deviceId;
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
    return gy::test::waitFor([this, &sessionId, &status]() {
        const QVariantList sessions = _manager->sessions();
        for (const QVariant &entry : sessions) {
            const QVariantMap session = entry.toMap();
            if (session["sessionId"].toString() == sessionId
                && session["status"].toString() == status) {
                return true;
            }
        }
        return false;
    }, timeoutMs);
}

// 从不中继：直连失败后直接终结为 failed，不触发中继请求
void TestSessionManager::testRelayDegradationNever()
{
    // 连接真实协调服务器，使"策略放行"与"协调在线"两个条件同时成立，
    // 只有 NeverRelay 策略能阻止降级
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    RendezvousClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    _config->setRelayMode(RelayMode::NeverRelay);
    _manager->init(_config, _discovery, _p2pServer, &client);

    const QString filePath = addDeadTargetDevice();
    QVERIFY(!filePath.isEmpty());
    QSignalSpy relaySpy(_manager, &TransferSessionManager::relayConfirmRequested);
    _manager->createSendSession(QStringLiteral("dead-target"), filePath);

    const QString sessionId = _manager->sessions().last().toMap()["sessionId"].toString();
    QVERIFY(waitForStatus(sessionId, "failed"));
    QCOMPARE(relaySpy.count(), 0);

    client.disconnectFromServer();
    _config->setRelayMode(RelayMode::AskBeforeRelay);
}

// 询问后中继：直连失败进入 awaiting_relay 并发射 relayConfirmRequested，用户取消后收敛
void TestSessionManager::testRelayDegradationAsk()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    RendezvousClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);

    _config->setRelayMode(RelayMode::AskBeforeRelay);
    _manager->init(_config, _discovery, _p2pServer, &client);

    const QString filePath = addDeadTargetDevice();
    QVERIFY(!filePath.isEmpty());
    QSignalSpy relaySpy(_manager, &TransferSessionManager::relayConfirmRequested);
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
    RendezvousServer server{0, QString()};
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

// 自动中继：直连失败后 C++ 内部自动重试中继，经真实协调节点 + 中继完成端到端发送，
// 全程无需 QML 参与，也不发射确认请求
void TestSessionManager::testRelayDegradationAuto()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    const quint16 port = server.serverPort();

    // 协调节点模式同端口内置中继（与 test_relay_chain 的装配一致）
    RelayServer relay{port, QString(), &server};
    QObject::connect(&server, &RendezvousServer::relayPipeRequested,
                     &relay, &RelayServer::adoptConnection);

    // 本 fixture 的 P2pServer 兼任接收端入口：中继 socket 交给首帧路由
    _config->setTcpPort(0);
    QVERIFY(_p2pServer->start());
    _config->setReceivePath(_tempDir->path() + "/auto-recv");
    _config->setAutoAcceptFiles(true);
    _config->setRendezvousHost(QStringLiteral("127.0.0.1"));
    _config->setRendezvousPort(static_cast<int>(port));

    // 接收端注册到协调节点，中继邀请到达即把会话并入中继
    RendezvousClient receiverClient;
    receiverClient.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(port));
    QTRY_VERIFY_WITH_TIMEOUT(receiverClient.isConnected(), 3000);
    receiverClient.registerDevice(QStringLiteral("default"), QStringLiteral("device-b"),
                                  QStringLiteral("B"), {}, 0, 0);
    QObject::connect(&receiverClient, &RendezvousClient::relayInvitesReceived,
                     this, [this, port](const QList<QVariantMap> &invites) {
        for (const QVariantMap &invite : invites) {
            _p2pServer->joinRelaySession(QStringLiteral("127.0.0.1"), port,
                                         invite.value(QStringLiteral("relayId")).toString());
        }
    });

    // 发送端使用同一协调节点（Manager 的中继信令来源）
    RendezvousClient senderClient;
    senderClient.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(port));
    QTRY_VERIFY_WITH_TIMEOUT(senderClient.isConnected(), 3000);
    senderClient.registerDevice(QStringLiteral("default"), QStringLiteral("device-a"),
                                QStringLiteral("A"), {}, 0, 0);

    _config->setRelayMode(RelayMode::AutoRelay);
    _manager->init(_config, _discovery, _p2pServer, &senderClient);

    // 目标设备在协调节点在线，但直连端点是必拒端口
    const QString filePath = addDeadTargetDevice(QStringLiteral("device-b"));
    QVERIFY(!filePath.isEmpty());

    QSignalSpy confirmSpy(_manager, &TransferSessionManager::relayConfirmRequested);
    QSignalSpy messageSpy(_manager, &TransferSessionManager::messageOccurred);
    _manager->createSendSession(QStringLiteral("device-b"), filePath);

    const QString sessionId = _manager->sessions().last().toMap()["sessionId"].toString();

    // 自动重试后中继通道建立，relayId 落到会话行（awaiting_relay 是瞬态，不作观察断言）
    QTRY_VERIFY_WITH_TIMEOUT(
        !_manager->sessionModel()->sessionById(sessionId).value("relayId").toString().isEmpty(),
        5000);

    // 接收端重注册触发立即轮询（正常场景由 5 秒心跳覆盖），传输随后自动完成
    receiverClient.registerDevice(QStringLiteral("default"), QStringLiteral("device-b"),
                                  QStringLiteral("B"), {}, 0, 0);
    QVERIFY(waitForStatus(sessionId, "completed"));
    QCOMPARE(confirmSpy.count(), 0);  // 自动档不发确认请求

    bool autoToastSeen = false;
    for (const QVariant &entry : messageSpy) {
        if (entry.toList().first().toString() == QStringLiteral("直连失败，已自动切换中继传输")) {
            autoToastSeen = true;
        }
    }
    QVERIFY(autoToastSeen);

    // 还原公共配置，避免泄漏到后续用例
    _config->setAutoAcceptFiles(false);
    _p2pServer->stop();
    senderClient.disconnectFromServer();
    receiverClient.disconnectFromServer();
    _config->setRelayMode(RelayMode::AskBeforeRelay);
}

// 活动会话重复取消：首次取消收敛为 cancelled，重复取消被终态守卫忽略
void TestSessionManager::testRepeatedCancelOnLiveSession()
{
    RendezvousServer server{0, QString()};
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

    _manager->cancelSession(sessionId);
    QVERIFY(waitForStatus(sessionId, "cancelled"));

    // 重复取消不应崩溃，也不应改变终态
    _manager->cancelSession(sessionId);
    _manager->cancelSession(sessionId);
    QVERIFY(waitForStatus(sessionId, "cancelled"));
    QCOMPARE(_manager->sessionModel()->sessionById(sessionId).value("status").toString(), QStringLiteral("cancelled"));

    client.disconnectFromServer();
}

// 会话完成后的迟到操作：再取消、再接受均被守卫忽略，状态保持 completed
void TestSessionManager::testGuardsAfterCompletion()
{
    _config->setReceivePath(_tempDir->path() + "/guard-recv");
    _config->setTcpPort(0);
    QVERIFY(_p2pServer->start());
    const quint16 port = _p2pServer->serverPort();
    QVERIFY(port > 0);

    const QString sourcePath = _tempDir->path() + "/guard-source.txt";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("guard-payload")), qint64(13));
    source.close();

    // 真实发送端向本 fixture 的接收服务器传输
    FileSenderWorker sender;
    QThread senderThread;
    sender.moveToThread(&senderThread);
    QObject::connect(&senderThread, &QThread::started, &sender, [&sender, port, &sourcePath]() {
        sender.startTransfer(QStringLiteral("127.0.0.1"), port, sourcePath,
                             QStringLiteral("guard-sender"), QStringLiteral("GuardSender"));
    });

    QSignalSpy requestSpy(_manager, &TransferSessionManager::receiveRequestReceived);
    QSignalSpy sendSpy(&sender, &FileSenderWorker::transferFinished);
    senderThread.start();

    QVERIFY2(requestSpy.wait(5000), "接收请求应在 5 秒内到达");
    const QString sessionId = requestSpy.first().at(0).toString();
    QVERIFY(waitForStatus(sessionId, "waiting_confirm"));

    _manager->acceptReceiveSession(sessionId);
    QVERIFY(waitForStatus(sessionId, "completed"));
    // 发送端可能在 completed 可见前就已发出完成信号，只等新信号的 wait 会超时
    QTRY_COMPARE_WITH_TIMEOUT(sendSpy.count(), 1, 5000);
    QVERIFY(sendSpy.first().at(0).toBool());

    // 完成后的迟到操作：状态保持 completed，不崩溃
    _manager->cancelSession(sessionId);
    QCOMPARE(_manager->sessionModel()->sessionById(sessionId).value("status").toString(), QStringLiteral("completed"));
    _manager->acceptReceiveSession(sessionId);
    QCOMPARE(_manager->sessionModel()->sessionById(sessionId).value("status").toString(), QStringLiteral("completed"));

    senderThread.quit();
    QVERIFY(senderThread.wait(3000));
}

// 等待确认的接收会话被后端终结（对方在确认前取消/断连）时 sessionStale 恰好发射一次；
// 用户主动拒绝会先改状态再终结，不算过期；正常完成同样不发射
void TestSessionManager::testSessionStaleSignal()
{
    _config->setReceivePath(_tempDir->path() + "/stale-recv");
    _config->setTcpPort(0);
    _p2pServer->stop();  // 上一用例可能仍占用监听，先停再取新端口
    QVERIFY(_p2pServer->start());
    const quint16 port = _p2pServer->serverPort();

    const QString sourcePath = _tempDir->path() + "/stale-source.txt";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("stale-payload")), qint64(13));
    source.close();

    FileSenderWorker sender;
    QThread senderThread;
    sender.moveToThread(&senderThread);
    QObject::connect(&senderThread, &QThread::started, &sender, [&sender, port, &sourcePath]() {
        sender.startTransfer(QStringLiteral("127.0.0.1"), port, sourcePath,
                             QStringLiteral("stale-sender"), QStringLiteral("StaleSender"));
    });

    QSignalSpy requestSpy(_manager, &TransferSessionManager::receiveRequestReceived);
    QSignalSpy staleSpy(_manager, &TransferSessionManager::sessionStale);
    senderThread.start();

    QVERIFY2(requestSpy.wait(5000), "接收请求应在 5 秒内到达");
    const QString sessionId = requestSpy.first().at(0).toString();
    QVERIFY(waitForStatus(sessionId, "waiting_confirm"));
    QCOMPARE(staleSpy.count(), 0);

    // 发送端在确认前取消（与 Manager 同款双步取消）：接收会话仍处 waiting_confirm 时被后端终结
    sender.requestCancel();
    QMetaObject::invokeMethod(&sender, "cancel");
    QVERIFY2(staleSpy.wait(5000), "会话过期信号应在 5 秒内到达");
    QCOMPARE(staleSpy.count(), 1);
    QCOMPARE(staleSpy.first().first().toString(), sessionId);
    QVERIFY(waitForStatus(sessionId, "cancelled"));

    senderThread.quit();
    QVERIFY(senderThread.wait(3000));

    // 正常完成接收：sessionStale 不发射
    const QString okPath = _tempDir->path() + "/stale-ok.txt";
    QFile okSource{okPath};
    QVERIFY(okSource.open(QIODevice::WriteOnly));
    QCOMPARE(okSource.write(QByteArray("ok-payload")), qint64(10));
    okSource.close();

    FileSenderWorker okSender;
    QThread okThread;
    okSender.moveToThread(&okThread);
    QObject::connect(&okThread, &QThread::started, &okSender, [&okSender, port, &okPath]() {
        okSender.startTransfer(QStringLiteral("127.0.0.1"), port, okPath,
                               QStringLiteral("ok-sender"), QStringLiteral("OkSender"));
    });
    okThread.start();

    QVERIFY2(requestSpy.wait(5000), "第二次接收请求应在 5 秒内到达");
    const QString okSessionId = requestSpy.last().at(0).toString();
    QVERIFY(waitForStatus(okSessionId, "waiting_confirm"));
    _manager->acceptReceiveSession(okSessionId);
    QVERIFY(waitForStatus(okSessionId, "completed"));
    QCOMPARE(staleSpy.count(), 1);

    okThread.quit();
    QVERIFY(okThread.wait(3000));

    // 用户主动拒绝：状态先改为 rejected 再终结，不算过期
    const QString rejectPath = _tempDir->path() + "/stale-reject.txt";
    QFile rejectSource{rejectPath};
    QVERIFY(rejectSource.open(QIODevice::WriteOnly));
    QCOMPARE(rejectSource.write(QByteArray("reject-payload")), qint64(14));
    rejectSource.close();

    FileSenderWorker rejectSender;
    QThread rejectThread;
    rejectSender.moveToThread(&rejectThread);
    QObject::connect(&rejectThread, &QThread::started, &rejectSender, [&rejectSender, port, &rejectPath]() {
        rejectSender.startTransfer(QStringLiteral("127.0.0.1"), port, rejectPath,
                                   QStringLiteral("reject-sender"), QStringLiteral("RejectSender"));
    });
    rejectThread.start();

    QVERIFY2(requestSpy.wait(5000), "第三次接收请求应在 5 秒内到达");
    const QString rejectSessionId = requestSpy.last().at(0).toString();
    QVERIFY(waitForStatus(rejectSessionId, "waiting_confirm"));
    _manager->rejectReceiveSession(rejectSessionId);
    QVERIFY(waitForStatus(rejectSessionId, "rejected"));
    QCOMPARE(staleSpy.count(), 1);

    rejectThread.quit();
    QVERIFY(rejectThread.wait(3000));
    _p2pServer->stop();
}

// 并发接收请求的等待确认快照（Controller 门面，QML 弹窗串行队列化的数据来源）：
// 两请求并发后快照按到达序含两个；拒绝第一个后只剩第二个；
// 仍有请求在后端超时终态（用例以发送方取消驱动同一 finalize 收敛路径，30 秒真实超时不宜等）
// 后快照正确收敛且不影响其余等待中的会话
void TestSessionManager::testWaitingConfirmSnapshotQueueing()
{
    _config->setReceivePath(_tempDir->path() + "/queue-recv");
    _config->setTcpPort(0);
    _p2pServer->stop();  // 上一用例可能仍占用监听，先停再取新端口
    QVERIFY(_p2pServer->start());
    const quint16 port = _p2pServer->serverPort();

    // 经 QML 同款门面查询，直接覆盖 Controller API
    TransferController controller{_manager};

    // 三个真实发送端并发建连，产生三个接收请求（辅助 lambda 返回非 void，宏断言放调用处）
    auto makeSource = [this](const QString &name, const QByteArray &payload) -> QString {
        const QString path = _tempDir->path() + "/" + name;
        QFile source{path};
        if (!source.open(QIODevice::WriteOnly)
            || source.write(payload) != payload.size()) {
            return {};
        }
        source.close();
        return path;
    };
    const QString pathA = makeSource("queue-a.txt", "payload-a");
    const QString pathB = makeSource("queue-b.txt", "payload-b");
    const QString pathC = makeSource("queue-c.txt", "payload-c");
    QVERIFY2(!pathA.isEmpty() && !pathB.isEmpty() && !pathC.isEmpty(), "测试源文件应创建成功");

    FileSenderWorker senderA;
    QThread threadA;
    senderA.moveToThread(&threadA);
    QObject::connect(&threadA, &QThread::started, &senderA, [&senderA, port, &pathA]() {
        senderA.startTransfer(QStringLiteral("127.0.0.1"), port, pathA,
                              QStringLiteral("queue-sender-a"), QStringLiteral("QueueA"));
    });
    FileSenderWorker senderB;
    QThread threadB;
    senderB.moveToThread(&threadB);
    QObject::connect(&threadB, &QThread::started, &senderB, [&senderB, port, &pathB]() {
        senderB.startTransfer(QStringLiteral("127.0.0.1"), port, pathB,
                              QStringLiteral("queue-sender-b"), QStringLiteral("QueueB"));
    });
    FileSenderWorker senderC;
    QThread threadC;
    senderC.moveToThread(&threadC);
    QObject::connect(&threadC, &QThread::started, &senderC, [&senderC, port, &pathC]() {
        senderC.startTransfer(QStringLiteral("127.0.0.1"), port, pathC,
                              QStringLiteral("queue-sender-c"), QStringLiteral("QueueC"));
    });

    QSignalSpy requestSpy(_manager, &TransferSessionManager::receiveRequestReceived);
    threadA.start();
    threadB.start();

    // 两请求并发：快照按到达序包含两个等待确认的接收会话
    QVERIFY2(gy::test::waitFor([&requestSpy]() { return requestSpy.count() >= 2; }, 5000),
             "两个接收请求应在 5 秒内到达");
    const QString firstId = requestSpy.first().at(0).toString();
    const QString secondId = requestSpy.at(1).at(0).toString();
    QVERIFY(gy::test::waitFor([&controller]() {
        return controller.waitingConfirmReceiveSessions().size() == 2;
    }, 5000));

    QVariantList snapshot = controller.waitingConfirmReceiveSessions();
    QCOMPARE(snapshot.at(0).toMap().value("sessionId").toString(), firstId);
    QCOMPARE(snapshot.at(1).toMap().value("sessionId").toString(), secondId);
    QCOMPARE(snapshot.at(0).toMap().value("status").toString(), QStringLiteral("waiting_confirm"));

    // 拒绝第一个：快照只剩第二个
    _manager->rejectReceiveSession(firstId);
    QVERIFY(waitForStatus(firstId, "rejected"));
    QVERIFY(gy::test::waitFor([&controller, &secondId]() {
        const QVariantList waiting = controller.waitingConfirmReceiveSessions();
        return waiting.size() == 1
               && waiting.at(0).toMap().value("sessionId").toString() == secondId;
    }, 5000));

    // 第三个请求加入队列：快照按到达序追加在第二个之后
    threadC.start();
    QVERIFY2(gy::test::waitFor([&requestSpy]() { return requestSpy.count() >= 3; }, 5000),
             "第三个接收请求应在 5 秒内到达");
    const QString thirdId = requestSpy.at(2).at(0).toString();
    QVERIFY(gy::test::waitFor([&controller]() {
        return controller.waitingConfirmReceiveSessions().size() == 2;
    }, 5000));
    snapshot = controller.waitingConfirmReceiveSessions();
    QCOMPARE(snapshot.at(0).toMap().value("sessionId").toString(), secondId);
    QCOMPARE(snapshot.at(1).toMap().value("sessionId").toString(), thirdId);

    // 第三个会话被后端终态（超时同路径）：快照收敛回第二个，其余等待会话不受影响
    senderC.requestCancel();
    QMetaObject::invokeMethod(&senderC, "cancel");
    QVERIFY(waitForStatus(thirdId, "cancelled"));
    QVERIFY(gy::test::waitFor([&controller, &secondId]() {
        const QVariantList waiting = controller.waitingConfirmReceiveSessions();
        return waiting.size() == 1
               && waiting.at(0).toMap().value("sessionId").toString() == secondId;
    }, 5000));

    // 收尾：终结仍等待的第二个会话，让接收 worker 全部退出
    _manager->rejectReceiveSession(secondId);
    QVERIFY(waitForStatus(secondId, "rejected"));
    QVERIFY(gy::test::waitFor([&controller]() {
        return controller.waitingConfirmReceiveSessions().isEmpty();
    }, 5000));

    threadA.quit();
    QVERIFY(threadA.wait(3000));
    threadB.quit();
    QVERIFY(threadB.wait(3000));
    threadC.quit();
    QVERIFY(threadC.wait(3000));
    _p2pServer->stop();
}

// 活动会话计数（退出前警示的数据来源）：会话创建进入未终态时加一，
// 迁移到完成/失败/拒绝/取消时减一，每次增减恰好发一次 NOTIFY
void TestSessionManager::testActiveSessionCountLifecycle()
{
    TransferController controller{_manager};
    QSignalSpy countSpy(_manager, &TransferSessionManager::activeSessionCountChanged);

    // 此前用例的会话均已收敛到终态，基线应为 0
    QCOMPARE(_manager->activeSessionCount(), 0);
    QCOMPARE(controller.activeSessionCount(), 0);

    // 发送会话：connecting 即算活动；NeverRelay 下直连失败直接终结
    _config->setRelayMode(RelayMode::NeverRelay);
    const QString filePath = addDeadTargetDevice();
    QVERIFY(!filePath.isEmpty());
    _manager->createSendSession(QStringLiteral("dead-target"), filePath);
    const QString sendId = _manager->sessions().last().toMap()["sessionId"].toString();
    QCOMPARE(controller.activeSessionCount(), 1);
    QVERIFY(waitForStatus(sendId, "failed"));
    QCOMPARE(controller.activeSessionCount(), 0);

    // 接收会话：waiting_confirm 算活动，用户拒绝后收敛
    _config->setReceivePath(_tempDir->path() + "/count-recv");
    _config->setTcpPort(0);
    QVERIFY(_p2pServer->start());
    const quint16 port = _p2pServer->serverPort();

    const QString sourcePath = _tempDir->path() + "/count-source.txt";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("count-payload")), qint64(13));
    source.close();

    FileSenderWorker sender;
    QThread senderThread;
    sender.moveToThread(&senderThread);
    QObject::connect(&senderThread, &QThread::started, &sender, [&sender, port, &sourcePath]() {
        sender.startTransfer(QStringLiteral("127.0.0.1"), port, sourcePath,
                             QStringLiteral("count-sender"), QStringLiteral("CountSender"));
    });
    QSignalSpy requestSpy(_manager, &TransferSessionManager::receiveRequestReceived);
    senderThread.start();

    QVERIFY2(requestSpy.wait(5000), "接收请求应在 5 秒内到达");
    const QString recvId = requestSpy.first().at(0).toString();
    QVERIFY(waitForStatus(recvId, "waiting_confirm"));
    QCOMPARE(controller.activeSessionCount(), 1);

    _manager->rejectReceiveSession(recvId);
    QVERIFY(waitForStatus(recvId, "rejected"));
    QCOMPARE(controller.activeSessionCount(), 0);

    senderThread.quit();
    QVERIFY(senderThread.wait(3000));
    _p2pServer->stop();

    // 加一与减一各两次，共四次 NOTIFY；拒绝后 worker 的迟到终结不再触发
    QCOMPARE(countSpy.count(), 4);
    QCOMPARE(_manager->activeSessionCount(), 0);

    _config->setRelayMode(RelayMode::AskBeforeRelay);
}

// 入站请求的处理入口信号在弹窗确认与自动接受两条路径上都发射（隐藏设备自动恢复显示的触发点）
void TestSessionManager::testIncomingRequestEntrySignal()
{
    _config->setReceivePath(_tempDir->path() + "/entry-recv");
    _config->setTcpPort(0);
    QVERIFY(_p2pServer->start());
    const quint16 port = _p2pServer->serverPort();

    const QString sourcePath = _tempDir->path() + "/entry-source.txt";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("entry-payload")), qint64(13));
    source.close();

    QSignalSpy entrySpy(_manager, &TransferSessionManager::incomingTransferRequested);

    // 路径一：需要弹窗确认的正常请求
    FileSenderWorker senderA;
    QThread threadA;
    senderA.moveToThread(&threadA);
    QObject::connect(&threadA, &QThread::started, &senderA, [&senderA, port, &sourcePath]() {
        senderA.startTransfer(QStringLiteral("127.0.0.1"), port, sourcePath,
                              QStringLiteral("entry-sender-a"), QStringLiteral("EntrySenderA"));
    });
    threadA.start();
    QVERIFY2(gy::test::waitFor([&entrySpy]() { return entrySpy.count() >= 1; }, 5000),
             "入口信号应在 5 秒内到达");
    QCOMPARE(entrySpy.first().at(0).toString(), QStringLiteral("entry-sender-a"));
    const QString firstId = _manager->sessions().last().toMap()["sessionId"].toString();
    QVERIFY(waitForStatus(firstId, "waiting_confirm"));
    _manager->rejectReceiveSession(firstId);
    QVERIFY(waitForStatus(firstId, "rejected"));

    // 路径二：自动接受时不弹 receiveRequestReceived，但入口信号仍必须发射
    const bool autoAcceptSaved = _config->autoAcceptFiles();
    _config->setAutoAcceptFiles(true);
    const int confirmRequestsBefore = entrySpy.count();

    FileSenderWorker senderB;
    QThread threadB;
    senderB.moveToThread(&threadB);
    QObject::connect(&threadB, &QThread::started, &senderB, [&senderB, port, &sourcePath]() {
        senderB.startTransfer(QStringLiteral("127.0.0.1"), port, sourcePath,
                              QStringLiteral("entry-sender-b"), QStringLiteral("EntrySenderB"));
    });
    QSignalSpy confirmSpy(_manager, &TransferSessionManager::receiveRequestReceived);
    threadB.start();
    QVERIFY2(gy::test::waitFor([this]() { return _manager->sessions().size() >= 2; }, 5000),
             "自动接受的会话应在 5 秒内创建");
    QCOMPARE(confirmSpy.count(), 0);  // 自动接受路径不弹窗
    QVERIFY2(gy::test::waitFor([&entrySpy, confirmRequestsBefore]() {
                 return entrySpy.count() >= confirmRequestsBefore + 1;
             }, 5000), "自动接受路径的入口信号应在 5 秒内到达");
    QCOMPARE(entrySpy.last().at(0).toString(), QStringLiteral("entry-sender-b"));
    QVERIFY(waitForStatus(_manager->sessions().last().toMap()["sessionId"].toString(), "transferring"));

    senderB.requestCancel();
    senderA.requestCancel();
    threadA.quit();
    threadB.quit();
    QVERIFY(threadA.wait(3000));
    QVERIFY(threadB.wait(3000));
    _p2pServer->stop();
    _config->setAutoAcceptFiles(autoAcceptSaved);
    _config->setRelayMode(RelayMode::AskBeforeRelay);
}

QTEST_MAIN(TestSessionManager)
#include "test_session_manager.moc"
