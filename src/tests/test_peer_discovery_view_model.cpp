/**
* @file    test_peer_discovery_view_model.cpp
* @version 7.15.19
* @date    2026-10-04
* @author  GY
* @brief   设备发现视图模型测试
*
* 测试用例：在线设备与手动端点按来源优先级排序、发现服务信号转发、
* 无数据层时历史刷新安全、空发现服务的防御行为、选中设备查询。
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
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "config_manager.h"
#include "data_types.h"
#include "discovery_service.h"
#include "peer_discovery_view_model.h"

class TestPeerDiscoveryViewModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testPeersSortedBySourcePriority();
    void testDiscoverySignalsForwarded();
    void testRefreshHistoryWithoutBroker();
    void testNullDiscoveryDefensive();
    void testSelectedDeviceAndLookup();

private:
    ConfigManager *_config = nullptr;
    DiscoveryService *_discovery = nullptr;
    QTemporaryDir *_tempDir = nullptr;
};

void TestPeerDiscoveryViewModel::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "ViewModelTest");
    _config = new ConfigManager{};

    _discovery = new DiscoveryService(_config, this);
    QTest::qWait(300);
}

void TestPeerDiscoveryViewModel::cleanupTestCase()
{
    delete _config;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 广播来源设备排在手动端点之前（broadcast > manual）
void TestPeerDiscoveryViewModel::testPeersSortedBySourcePriority()
{
    PeerDiscoveryViewModel viewModel(_discovery);

    // 直接注入不同来源的两台设备做排序对照，不依赖真实局域网环境
    PeerInfo manualPeer;
    manualPeer.deviceId = QStringLiteral("manual-test");
    manualPeer.deviceName = QStringLiteral("手动端点");
    manualPeer.ipAddress = QStringLiteral("10.254.254.254");
    manualPeer.tcpPort = 35100;
    manualPeer.isOnline = true;
    manualPeer.lastSeen = QDateTime::currentDateTimeUtc();
    manualPeer.source = QStringLiteral("manual");
    _discovery->addManualPeer(manualPeer);

    PeerInfo broadcastPeer = manualPeer;
    broadcastPeer.deviceId = QStringLiteral("broadcast-test");
    broadcastPeer.deviceName = QStringLiteral("广播设备");
    broadcastPeer.ipAddress = QStringLiteral("192.168.50.50");
    broadcastPeer.source = QStringLiteral("broadcast");
    _discovery->addManualPeer(broadcastPeer);  // addManualPeer 保留传入的 source 字段

    // 局域网上可能存在其他真实设备，只断言两条测试条目的相对顺序
    const QVariantList peers = viewModel.peers();
    int broadcastIndex = -1;
    int manualIndex = -1;
    for (int i = 0; i < peers.size(); ++i) {
        const QString deviceId = peers.at(i).value<PeerInfo>().deviceId;
        if (deviceId == QStringLiteral("broadcast-test")) {
            broadcastIndex = i;
        } else if (deviceId == QStringLiteral("manual-test")) {
            manualIndex = i;
        }
    }

    QVERIFY2(broadcastIndex >= 0, "视图模型应包含注入的 broadcast 设备");
    QVERIFY2(manualIndex >= 0, "视图模型应包含注入的 manual 设备");
    QVERIFY2(broadcastIndex < manualIndex, "broadcast 来源应排在 manual 之前");
}

// 发现服务的 peersChanged 应转发给视图模型
void TestPeerDiscoveryViewModel::testDiscoverySignalsForwarded()
{
    PeerDiscoveryViewModel viewModel(_discovery);

    QSignalSpy vmSpy(&viewModel, &PeerDiscoveryViewModel::peersChanged);
    QSignalSpy discoverySpy(_discovery, &DiscoveryService::peersChanged);

    // addManualPeer 会同步发出 peersChanged，确定性验证转发链路
    PeerInfo manualPeer;
    manualPeer.deviceId = QStringLiteral("forward-test");
    manualPeer.deviceName = QStringLiteral("转发测试");
    manualPeer.ipAddress = QStringLiteral("10.254.254.250");
    manualPeer.isOnline = true;
    manualPeer.lastSeen = QDateTime::currentDateTimeUtc();
    manualPeer.source = QStringLiteral("manual");
    _discovery->addManualPeer(manualPeer);

    QVERIFY(!discoverySpy.isEmpty());
    QVERIFY(!vmSpy.isEmpty());
}

// 未注入数据层时刷新历史应为安全空操作
void TestPeerDiscoveryViewModel::testRefreshHistoryWithoutBroker()
{
    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.refreshHistory();  // 不应崩溃
    viewModel.refresh();
    QVERIFY(true);
}

// 空发现服务下视图模型应返回空列表且可安全刷新
void TestPeerDiscoveryViewModel::testNullDiscoveryDefensive()
{
    PeerDiscoveryViewModel viewModel(nullptr);
    QVERIFY(viewModel.peers().isEmpty());
    viewModel.refresh();
    viewModel.refreshHistory();
    QVERIFY(viewModel.peers().isEmpty());
}

// 选中设备 ID 收编：写入发通知，deviceById 返回展示字段，未命中返回空表
void TestPeerDiscoveryViewModel::testSelectedDeviceAndLookup()
{
    PeerDiscoveryViewModel viewModel(_discovery);

    // 设备注入前查询为空
    QVERIFY(viewModel.deviceById(QStringLiteral("select-test")).isEmpty());

    PeerInfo peer;
    peer.deviceId = QStringLiteral("select-test");
    peer.deviceName = QStringLiteral("选中测试");
    peer.ipAddress = QStringLiteral("10.254.254.249");
    peer.tcpPort = 35100;
    peer.isOnline = true;
    peer.lastSeen = QDateTime::currentDateTimeUtc();
    peer.source = QStringLiteral("manual");
    _discovery->addManualPeer(peer);

    // 未选中时 selectedDeviceId 为空，写入选中 ID 后发通知
    QSignalSpy spy(&viewModel, &PeerDiscoveryViewModel::selectedDeviceIdChanged);
    QVERIFY(viewModel.selectedDeviceId().isEmpty());

    viewModel.setSelectedDeviceId(QStringLiteral("select-test"));
    QCOMPARE(viewModel.selectedDeviceId(), QStringLiteral("select-test"));
    QVERIFY(!spy.isEmpty());

    // 重复写入同一 ID 不重复通知
    const int notified = spy.count();
    viewModel.setSelectedDeviceId(QStringLiteral("select-test"));
    QCOMPARE(spy.count(), notified);

    // 查询返回展示字段；未知 ID 返回空表
    const QVariantMap info = viewModel.deviceById(QStringLiteral("select-test"));
    QCOMPARE(info.value("deviceName").toString(), QStringLiteral("选中测试"));
    QCOMPARE(info.value("ipAddress").toString(), QStringLiteral("10.254.254.249"));
    QCOMPARE(info.value("isOnline").toBool(), true);
    QVERIFY(viewModel.deviceById(QStringLiteral("no-such-device")).isEmpty());

    viewModel.setSelectedDeviceId(QString());
    QCOMPARE(viewModel.selectedDeviceId(), QString());
}

QTEST_MAIN(TestPeerDiscoveryViewModel)
#include "test_peer_discovery_view_model.moc"
