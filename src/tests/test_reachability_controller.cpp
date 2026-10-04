/**
* @file    test_reachability_controller.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   网络可达性控制器测试
*
* 测试用例：本机地址收集、TCP 探测成功与失败、邀请文本生成与导入
* 校验、手动端点的 IP 校验与添加。
*
* Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 版本头对齐到 v7.19.0
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 补邀请导入探测成功入表与失败不注入用例，清理顺序改为先销毁发现服务再释放配置
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
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
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
*/

#include <QtTest/QtTest>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>

#include "config_manager.h"
#include "data_types.h"
#include "discovery_service.h"
#include "reachability_controller.h"

class TestReachabilityController : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testRefreshLocalAddresses();
    void testProbeEndpointOnline();
    void testProbeEndpointOffline();
    void testGenerateInviteWithoutConfig();
    void testGenerateInviteWithConfig();
    void testImportInviteRejectsGarbage();
    void testImportInviteAcceptsValidText();
    void testImportInviteInjectsDirectedPeer();
    void testImportInviteProbeFailureDoesNotInject();
    void testAddManualEndpointValidation();
    void testAddManualEndpointAddsPeer();

private:
    QTemporaryDir *_tempDir = nullptr;
    ConfigManager *_config = nullptr;
    DiscoveryService *_discovery = nullptr;
};

// 每个用例使用独立的临时配置，避免单例配置串扰
void TestReachabilityController::init()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "ReachTest");
    _config = new ConfigManager{};

    _discovery = new DiscoveryService(_config, this);
}

void TestReachabilityController::cleanup()
{
    // 先销毁发现服务再释放配置：其广播定时器仍持有 _config 裸指针，
    // 顺序颠倒会让存活到套件结束的实例在定时器回调里访问已释放对象
    delete _discovery;
    _discovery = nullptr;
    delete _config;
    _config = nullptr;
    delete _tempDir;
    _tempDir = nullptr;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 本机地址刷新后应收集到非回环 IPv4 地址并发出变化通知
void TestReachabilityController::testRefreshLocalAddresses()
{
    ReachabilityController controller;
    controller.setConfigManager(_config);

    QSignalSpy changedSpy(&controller, &ReachabilityController::localAddressesChanged);
    controller.refreshLocalAddresses();

    QVERIFY(!controller.localAddresses().isEmpty());
    QVERIFY(!changedSpy.isEmpty());
}

// 探测本机在线端口应返回连接成功
void TestReachabilityController::testProbeEndpointOnline()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost));

    ReachabilityController controller;
    controller.probeEndpoint(QStringLiteral("127.0.0.1"), listener.serverPort(), 2000);

    // 等待探测完成：isProbing 应回落到 false 且结果里 tcpConnected 为真
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isProbing(), 3000);
    const QVariantMap result = controller.lastProbeResult();
    QCOMPARE(result.value("targetIp").toString(), QStringLiteral("127.0.0.1"));
    QCOMPARE(result.value("targetPort").toInt(), listener.serverPort());
    QVERIFY(result.value("tcpConnected").toBool());
}

// 探测未监听端口应在超时后返回失败
void TestReachabilityController::testProbeEndpointOffline()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
        probe.close();
    }

    ReachabilityController controller;
    controller.probeEndpoint(QStringLiteral("127.0.0.1"), closedPort, 500);

    QTRY_VERIFY_WITH_TIMEOUT(!controller.isProbing(), 3000);
    QVERIFY(!controller.lastProbeResult().value("tcpConnected").toBool());
}

// 未注入配置管理器时生成邀请应报错且不产出文本
void TestReachabilityController::testGenerateInviteWithoutConfig()
{
    ReachabilityController controller;

    QSignalSpy errorSpy(&controller, &ReachabilityController::inviteErrorChanged);
    const QString inviteText = controller.generateInvite();

    QVERIFY(inviteText.isEmpty());
    QVERIFY(!controller.inviteError().isEmpty());
    QCOMPARE(errorSpy.count(), 1);
}

// 注入配置后生成的邀请文本可被解析回相同设备信息
void TestReachabilityController::testGenerateInviteWithConfig()
{
    ReachabilityController controller;
    controller.setConfigManager(_config);

    const QString inviteText = controller.generateInvite();
    QVERIFY2(inviteText.startsWith("gridyard://invite?"), "邀请文本应使用 gridyard://invite 协议");
    QVERIFY(inviteText.contains(_config->deviceId()));

    QCOMPARE(controller.inviteError(), QString());
}

// 畸形邀请文本应被拒绝并给出错误信息
void TestReachabilityController::testImportInviteRejectsGarbage()
{
    ReachabilityController controller;
    controller.setConfigManager(_config);

    QSignalSpy importedSpy(&controller, &ReachabilityController::inviteImported);
    controller.importInvite(QStringLiteral("这不是邀请文本"));

    QCOMPARE(importedSpy.count(), 1);
    QVERIFY(!importedSpy.at(0).at(0).toBool());
    QVERIFY(!controller.inviteError().isEmpty());
}

// 合法邀请文本导入后应立即回报成功并携带设备标识
void TestReachabilityController::testImportInviteAcceptsValidText()
{
    ReachabilityController controller;
    controller.setConfigManager(_config);

    const QString inviteText = controller.generateInvite();

    QSignalSpy importedSpy(&controller, &ReachabilityController::inviteImported);
    controller.importInvite(inviteText);

    QCOMPARE(importedSpy.count(), 1);
    QVERIFY(importedSpy.at(0).at(0).toBool());
    QCOMPARE(importedSpy.at(0).at(1).toString(), _config->deviceId());

    // 导入会触发对目标端点的探测，等待其收尾避免用例间残留探测任务
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isProbing(), 3000);
}

// 邀请导入的探测成功后，邀请携带的真实身份应注入发现列表
void TestReachabilityController::testImportInviteInjectsDirectedPeer()
{
    QTcpServer listener;
    QVERIFY(listener.listen(QHostAddress::LocalHost));

    // 构造指向本机真实监听端口的邀请文本，探测必然成功
    InviteCodec::Invite invite;
    invite.deviceId = QStringLiteral("invite-target-id");
    invite.deviceName = QStringLiteral("邀请目标");
    invite.ipAddress = QStringLiteral("127.0.0.1");
    invite.tcpPort = listener.serverPort();
    invite.discoveryPort = 45678;

    ReachabilityController controller;
    controller.setConfigManager(_config);
    controller.setDiscoveryService(_discovery);

    QSignalSpy updatedSpy(_discovery, &DiscoveryService::peerUpdated);
    controller.importInvite(InviteCodec::encode(invite));

    // 探测异步收尾后注入，等待 directed 条目可查询到端点
    QTRY_VERIFY_WITH_TIMEOUT(
        !_discovery->transferEndpoint(QStringLiteral("invite-target-id")).isEmpty(), 5000);

    bool found = false;
    const QVariantList peers = _discovery->peers();
    for (const QVariant &entry : peers) {
        const PeerInfo peer = entry.value<PeerInfo>();
        if (peer.deviceId != QStringLiteral("invite-target-id")) {
            continue;
        }
        found = true;
        QCOMPARE(peer.source, QStringLiteral("directed"));
        QVERIFY(peer.isOnline);
        QCOMPARE(peer.deviceName, QStringLiteral("邀请目标"));
        break;
    }
    QVERIFY2(found, "邀请导入探测成功后设备应注入发现列表");
    QVERIFY2(!updatedSpy.isEmpty(), "注入应发射 peerUpdated 供设备目录持久化");
}

// 邀请目标 TCP 不可达时不应注入设备条目
void TestReachabilityController::testImportInviteProbeFailureDoesNotInject()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
        probe.close();
    }

    InviteCodec::Invite invite;
    invite.deviceId = QStringLiteral("invite-offline-id");
    invite.deviceName = QStringLiteral("离线目标");
    invite.ipAddress = QStringLiteral("127.0.0.1");
    invite.tcpPort = closedPort;
    invite.discoveryPort = 45678;

    ReachabilityController controller;
    controller.setConfigManager(_config);
    controller.setDiscoveryService(_discovery);

    controller.importInvite(InviteCodec::encode(invite));

    // 连接拒绝会立即失败，等待探测收尾后确认未注入
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isProbing(), 5000);
    QVERIFY(_discovery->transferEndpoint(QStringLiteral("invite-offline-id")).isEmpty());
}

// 空 IP 与非法 IP 都应被拦截
void TestReachabilityController::testAddManualEndpointValidation()
{
    ReachabilityController controller;
    controller.setDiscoveryService(_discovery);

    QSignalSpy errorSpy(&controller, &ReachabilityController::inviteErrorChanged);

    controller.addManualEndpoint(QString(), 35100);
    QVERIFY(!controller.inviteError().isEmpty());

    controller.addManualEndpoint(QStringLiteral("999.999.1.1"), 35100);
    QVERIFY(!controller.inviteError().isEmpty());
    QVERIFY(errorSpy.count() >= 2);
}

// 合法端点加入后出现在发现列表，来源标记为 manual，并经 peerUpdated 入设备目录
void TestReachabilityController::testAddManualEndpointAddsPeer()
{
    ReachabilityController controller;
    controller.setConfigManager(_config);
    controller.setDiscoveryService(_discovery);

    QSignalSpy updatedSpy(_discovery, &DiscoveryService::peerUpdated);
    controller.addManualEndpoint(QStringLiteral("10.254.254.254"), 35100);
    QCOMPARE(controller.inviteError(), QString());

    // 手动端点是同步进入发现列表的
    bool found = false;
    const QVariantList peers = _discovery->peers();
    for (const QVariant &entry : peers) {
        const PeerInfo peer = entry.value<PeerInfo>();
        if (peer.source == QStringLiteral("manual")
                && peer.ipAddress == QStringLiteral("10.254.254.254")) {
            found = true;
            break;
        }
    }
    QVERIFY2(found, "手动端点应出现在发现列表并带 manual 来源");
    QVERIFY2(!updatedSpy.isEmpty(), "手动端点入列应发射 peerUpdated 供设备目录持久化");
}

QTEST_MAIN(TestReachabilityController)
#include "test_reachability_controller.moc"
