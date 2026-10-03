/**
* @file    test_discovery.cpp
* @version 7.15.19
* @date    2026-10-04
* @author  GY
* @brief   DiscoveryService 设备发现测试
*
* 测试用例：UDP 广播收发 / 节点发现 / 节点过期 / refresh()
*
* Change Log:
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
* [v7.14.2] GY   2026-10-03
* * 三个占位用例改为真实断言：过期、刷新、多节点发现
* [v4.16.1] GY   2026-06-21
* * 改为验证不存在设备不会返回发送端点快照
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>

#include "discovery_service.h"
#include "config_manager.h"

class TestDiscovery : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testNodeDiscovery();
    void testNodeExpiry();
    void testRefresh();
    void testMultipleNodes();
    void testAddDirectedPeerInjectsEntry();
    void testAddDirectedPeerRespectsBroadcastGuard();
    void testPeerInfo();

private:
    void waitForSignal(QSignalSpy &spy, int timeout = 2000);

    ConfigManager *_config1 = nullptr;
    ConfigManager *_config2 = nullptr;
    DiscoveryService *_discovery1 = nullptr;
    DiscoveryService *_discovery2 = nullptr;
    QTemporaryDir *_tempDir = nullptr;
};

void TestDiscovery::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    // 创建两个独立的配置（直接创建实例，不使用单例）
    QString config1Path = _tempDir->path() + "/config1.ini";
    QString config2Path = _tempDir->path() + "/config2.ini";

    qputenv("GRIDYARD_CONFIG", config1Path.toUtf8());
    qputenv("GRIDYARD_NAME", "Device1");
    _config1 = new ConfigManager{};

    qputenv("GRIDYARD_CONFIG", config2Path.toUtf8());
    qputenv("GRIDYARD_NAME", "Device2");
    _config2 = new ConfigManager{};

    // 创建发现服务
    _discovery1 = new DiscoveryService(_config1, this);
    _discovery2 = new DiscoveryService(_config2, this);

    // 等待服务初始化
    QTest::qWait(500);
}

void TestDiscovery::cleanupTestCase()
{
    delete _discovery1;
    delete _discovery2;
    delete _config1;
    delete _config2;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

void TestDiscovery::waitForSignal(QSignalSpy &spy, int timeout)
{
    if (spy.isEmpty()) {
        spy.wait(timeout);
    }
}

void TestDiscovery::testNodeDiscovery()
{
    // 监听节点发现信号
    QSignalSpy spy1(_discovery1, &DiscoveryService::nodeDiscovered);
    QSignalSpy spy2(_discovery2, &DiscoveryService::nodeDiscovered);

    // 触发刷新
    _discovery1->refresh();
    _discovery2->refresh();

    // 等待节点发现
    waitForSignal(spy1, 3000);
    waitForSignal(spy2, 3000);

    // 验证至少有一个节点被发现（UDP 广播在同一机器上应该可靠）
    QVERIFY2(spy1.count() > 0 || spy2.count() > 0,
             "至少一个实例应该发现另一个实例");

    // 验证 peers 列表不为空
    QVariantList peers1 = _discovery1->peers();
    QVariantList peers2 = _discovery2->peers();
    QVERIFY2(!peers1.isEmpty() || !peers2.isEmpty(),
             "至少一个实例的 peers 列表应该非空");
}

void TestDiscovery::testNodeExpiry()
{
    // 建立局部第四实例作为"将下线"的对端，销毁即停止广播，不影响共享实例
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config4.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "Device4");
    ConfigManager *config4 = ConfigManager::create(nullptr, nullptr);
    auto *discovery4 = new DiscoveryService(config4);

    _discovery1->refresh();
    discovery4->refresh();
    const QString device4Id = config4->deviceId();
    QTRY_VERIFY_WITH_TIMEOUT(_discovery1->transferEndpoint(device4Id).isEmpty() == false, 5000);

    // 注入 2 秒超时（默认 15 秒），prune 定时器每 3 秒清理一次：
    // 对端销毁停止广播后，节点应在数秒内被标记离线并发出过期信号
    QSignalSpy spy1(_discovery1, &DiscoveryService::nodeExpired);
    _discovery1->setNodeTimeoutSec(2);
    delete discovery4;
    delete config4;
    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");

    QTRY_VERIFY_WITH_TIMEOUT(spy1.count() >= 1, 10000);
    QVERIFY(spy1.first().at(0).toString() == device4Id);
    QCOMPARE(_discovery1->transferEndpoint(device4Id).isEmpty(), true);
}

void TestDiscovery::testRefresh()
{
    // refresh() 触发广播后，同机对端应进入本端节点表
    _discovery2->refresh();
    _discovery1->refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!_discovery1->peers().isEmpty(), 5000);
    QCOMPARE(_discovery1->transferEndpoint(_config2->deviceId()).isEmpty(), false);
}

void TestDiscovery::testMultipleNodes()
{
    // 创建第三个节点
    QString config3Path = _tempDir->path() + "/config3.ini";
    qputenv("GRIDYARD_CONFIG", config3Path.toUtf8());
    qputenv("GRIDYARD_NAME", "Device3");
    ConfigManager *config3 = ConfigManager::create(nullptr, nullptr);
    DiscoveryService discovery3(config3);

    QSignalSpy discovered1(_discovery1, &DiscoveryService::nodeDiscovered);

    // 触发刷新
    _discovery1->refresh();
    _discovery2->refresh();
    discovery3.refresh();

    // 验证 device2 与 device3 都进入 discovery1 的节点表
    const QString deviceId2 = _config2->deviceId();
    const QString deviceId3 = config3->deviceId();
    QTRY_VERIFY_WITH_TIMEOUT(_discovery1->transferEndpoint(deviceId2).isEmpty() == false, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(_discovery1->transferEndpoint(deviceId3).isEmpty() == false, 5000);

    delete config3;
}

// 邀请导入注入的 directed 条目应带真实身份进入节点表并走 peerUpdated 持久化链路
void TestDiscovery::testAddDirectedPeerInjectsEntry()
{
    PeerInfo invited;
    invited.deviceId = QStringLiteral("directed-peer-1");
    invited.deviceName = QStringLiteral("邀请设备");
    invited.ipAddress = QStringLiteral("10.253.253.253");
    invited.tcpPort = 35200;

    QSignalSpy updatedSpy(_discovery1, &DiscoveryService::peerUpdated);
    _discovery1->addDirectedPeer(invited);

    QVERIFY2(!updatedSpy.isEmpty(), "directed 注入应发射 peerUpdated 供设备目录持久化");

    bool found = false;
    const QVariantList peers = _discovery1->peers();
    for (const QVariant &entry : peers) {
        const PeerInfo peer = entry.value<PeerInfo>();
        if (peer.deviceId != invited.deviceId) {
            continue;
        }
        found = true;
        QCOMPARE(peer.source, QStringLiteral("directed"));
        QVERIFY(peer.isOnline);
        QCOMPARE(peer.ipAddress, QStringLiteral("10.253.253.253"));
        break;
    }
    QVERIFY2(found, "directed 注入的设备应出现在节点表");
    QVERIFY(!_discovery1->transferEndpoint(invited.deviceId).isEmpty());
}

// 已有在线 broadcast 条目时，directed 注入不得覆盖广播来源
void TestDiscovery::testAddDirectedPeerRespectsBroadcastGuard()
{
    PeerInfo broadcast;
    broadcast.deviceId = QStringLiteral("directed-peer-2");
    broadcast.deviceName = QStringLiteral("广播名");
    broadcast.ipAddress = QStringLiteral("192.168.50.100");
    broadcast.tcpPort = 35201;
    broadcast.isOnline = true;
    broadcast.lastSeen = QDateTime::currentDateTimeUtc();
    broadcast.source = QStringLiteral("broadcast");
    _discovery1->addManualPeer(broadcast);  // 借道手动入口注入 broadcast 来源条目

    PeerInfo invited = broadcast;
    invited.deviceName = QStringLiteral("邀请名");
    invited.ipAddress = QStringLiteral("10.253.253.100");
    invited.source = QStringLiteral("directed");
    _discovery1->addDirectedPeer(invited);

    bool found = false;
    const QVariantList peers = _discovery1->peers();
    for (const QVariant &entry : peers) {
        const PeerInfo peer = entry.value<PeerInfo>();
        if (peer.deviceId != broadcast.deviceId) {
            continue;
        }
        found = true;
        QCOMPARE(peer.source, QStringLiteral("broadcast"));
        QCOMPARE(peer.deviceName, QStringLiteral("广播名"));
        QCOMPARE(peer.ipAddress, QStringLiteral("192.168.50.100"));
        break;
    }
    QVERIFY2(found, "broadcast 条目应保留");
}

void TestDiscovery::testPeerInfo()
{
    // 不存在或离线设备不应提供发送端点
    QVERIFY(_discovery1->transferEndpoint("non_existent_device").isEmpty());
}

QTEST_MAIN(TestDiscovery)
#include "test_discovery.moc"
