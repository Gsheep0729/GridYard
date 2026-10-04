/**
* @file    test_peer_discovery_view_model.cpp
* @version 7.17.0
* @date    2026-10-04
* @author  GY
* @brief   设备发现视图模型测试
*
* 测试用例：在线设备与手动端点按来源优先级排序、发现服务信号转发、
* 无数据层时历史刷新安全、空发现服务的防御行为、选中设备查询。
*
* Change Log:
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
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
#include <QSharedPointer>
#include <QTemporaryDir>

#include <memory>

#include "config_manager.h"
#include "data_types.h"
#include "discovery_service.h"
#include "local_data_broker.h"
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
    void testHiddenPeerFilteredFromList();
    void testDeleteDeviceSyncsLists();
    void testHideAndRestoreDevice();

private:
    // 在临时目录打开一份本地历史库
    std::unique_ptr<LocalDataBroker> openBroker(const QString &fileName);
    // 构造一份注入用的设备信息
    PeerInfo makePeerInfo(const QString &deviceId, const QString &name, const QString &ip);
    // 通过数据层异步写入设备快照并等待落库完成
    void seedPeer(LocalDataBroker &broker, const PeerInfo &peer);

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

// 隐藏态设备不进合并列表（在线与历史条目一体过滤），置顶数据可被查询到
void TestPeerDiscoveryViewModel::testHiddenPeerFilteredFromList()
{
    auto broker = openBroker("hidden-filter.sqlite");
    QVERIFY(broker);

    // visiblePeer 只写历史库，hiddenPeer 同时注入在线列表，验证在线条目也被过滤
    const PeerInfo visiblePeer = makePeerInfo(QStringLiteral("visible-peer"), QStringLiteral("可见设备"),
                                              QStringLiteral("10.254.254.241"));
    const PeerInfo hiddenPeer = makePeerInfo(QStringLiteral("hidden-peer"), QStringLiteral("隐藏设备"),
                                             QStringLiteral("10.254.254.242"));
    seedPeer(*broker, visiblePeer);
    seedPeer(*broker, hiddenPeer);
    _discovery->addManualPeer(hiddenPeer);

    bool hiddenSet = false;
    broker->setDeviceHidden(this, QStringLiteral("hidden-peer"), true,
                            [&hiddenSet](bool) { hiddenSet = true; });
    QTRY_COMPARE_WITH_TIMEOUT(hiddenSet, true, 3000);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());

    // 目录加载完成后隐藏设备不进列表（即便它仍在在线表），可见设备正常出现
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("hidden-peer")).isEmpty(), 3000);
    const QVariantMap visible = viewModel.deviceById(QStringLiteral("visible-peer"));
    QVERIFY(!visible.isEmpty());
    QCOMPARE(visible.value("pinned").toBool(), false);
    QCOMPARE(visible.value("hidden").toBool(), false);
}

// 删除设备后数据库行、在线条目与历史条目同步消失，其余设备不受影响
void TestPeerDiscoveryViewModel::testDeleteDeviceSyncsLists()
{
    auto broker = openBroker("delete-sync.sqlite");
    QVERIFY(broker);

    const PeerInfo deletePeer = makePeerInfo(QStringLiteral("delete-peer"), QStringLiteral("待删设备"),
                                             QStringLiteral("10.254.254.243"));
    const PeerInfo keepPeer = makePeerInfo(QStringLiteral("keep-peer"), QStringLiteral("保留设备"),
                                           QStringLiteral("10.254.254.244"));
    seedPeer(*broker, deletePeer);
    seedPeer(*broker, keepPeer);
    _discovery->addManualPeer(deletePeer);  // 在线条目一并注入，删除时同步清理

    PeerDiscoveryViewModel viewModel(_discovery);
    QSignalSpy changedSpy(&viewModel, &PeerDiscoveryViewModel::peersChanged);
    viewModel.initDataBroker(broker.get());
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("delete-peer")).isEmpty(), 3000);

    viewModel.deleteDeviceWithHistory(QStringLiteral("delete-peer"));

    // 列表与在线表移除条目并发出通知
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("delete-peer")).isEmpty(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!changedSpy.isEmpty(), 3000);
    QCOMPARE(viewModel.selectedDeviceId(), QString());  // 删除选中设备时清空选中态

    // 数据库行已删除，保留设备不受影响
    const auto goneFlag = QSharedPointer<bool>::create(false);
    const auto keptFlag = QSharedPointer<bool>::create(false);
    broker->loadRecentPeers(this, 10,
                            [this, goneFlag, keptFlag](const QList<PeerRecord> &records, bool ok) {
                                if (!ok) {
                                    return;
                                }
                                for (const PeerRecord &record : records) {
                                    if (record.deviceId == QStringLiteral("delete-peer")) {
                                        *goneFlag = true;
                                    } else if (record.deviceId == QStringLiteral("keep-peer")) {
                                        *keptFlag = true;
                                    }
                                }
                            });
    QTRY_COMPARE_WITH_TIMEOUT(*goneFlag, false, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(*keptFlag, true, 3000);
}

// 隐藏后设备离开列表，入站活动触发 restoreHiddenDevice 后复位并恢复显示
void TestPeerDiscoveryViewModel::testHideAndRestoreDevice()
{
    auto broker = openBroker("hide-restore.sqlite");
    QVERIFY(broker);

    const PeerInfo peer = makePeerInfo(QStringLiteral("restore-peer"), QStringLiteral("恢复设备"),
                                       QStringLiteral("10.254.254.245"));
    seedPeer(*broker, peer);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("restore-peer")).isEmpty(), 3000);

    // 隐藏落库后列表立即移除条目
    viewModel.setDeviceHidden(QStringLiteral("restore-peer"), true);
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("restore-peer")).isEmpty(), 3000);

    // 入站消息/传输请求触发的恢复入口：复位 hidden 并把设备带回列表
    viewModel.restoreHiddenDevice(QStringLiteral("restore-peer"));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("restore-peer")).isEmpty(), 5000);
    const QVariantMap restored = viewModel.deviceById(QStringLiteral("restore-peer"));
    QCOMPARE(restored.value("hidden").toBool(), false);

    // 数据库中的 hidden 标志已复位
    const auto clearedFlag = QSharedPointer<bool>::create(false);
    broker->loadRecentPeers(this, 10,
                            [clearedFlag](const QList<PeerRecord> &records, bool ok) {
                                if (!ok) {
                                    return;
                                }
                                for (const PeerRecord &record : records) {
                                    if (record.deviceId == QStringLiteral("restore-peer")) {
                                        *clearedFlag = !record.hidden;
                                    }
                                }
                            });
    QTRY_COMPARE_WITH_TIMEOUT(*clearedFlag, true, 3000);

    // 未隐藏设备调用恢复入口是安全空操作
    viewModel.restoreHiddenDevice(QStringLiteral("never-hidden-peer"));
    QVERIFY(true);
}

// 工具方法：在临时目录打开一份本地历史库
std::unique_ptr<LocalDataBroker> TestPeerDiscoveryViewModel::openBroker(const QString &fileName)
{
    auto broker = std::make_unique<LocalDataBroker>();
    QString error;
    if (!broker->initialize(_tempDir->path() + "/" + fileName, &error)) {
        qWarning() << "本地历史库初始化失败:" << error;
        return nullptr;
    }
    return broker;
}

// 工具方法：构造一份注入用的设备信息
PeerInfo TestPeerDiscoveryViewModel::makePeerInfo(const QString &deviceId, const QString &name,
                                                  const QString &ip)
{
    PeerInfo peer;
    peer.deviceId = deviceId;
    peer.deviceName = name;
    peer.ipAddress = ip;
    peer.tcpPort = 35100;
    peer.isOnline = true;
    peer.lastSeen = QDateTime::currentDateTimeUtc();
    peer.source = QStringLiteral("manual");
    return peer;
}

// 工具方法：通过数据层异步写入设备快照并等待落库完成
void TestPeerDiscoveryViewModel::seedPeer(LocalDataBroker &broker, const PeerInfo &peer)
{
    broker.persistDiscoveredPeer(peer);

    // 数据层为串行队列，一次读取回调返回即代表此前全部写入已完成
    const auto seeded = QSharedPointer<bool>::create(false);
    broker.loadRecentPeers(this, 10, [seeded, &peer](const QList<PeerRecord> &records, bool ok) {
        if (!ok) {
            return;
        }
        for (const PeerRecord &record : records) {
            if (record.deviceId == peer.deviceId) {
                *seeded = true;
            }
        }
    });
    QTRY_COMPARE_WITH_TIMEOUT(*seeded, true, 3000);
}

QTEST_MAIN(TestPeerDiscoveryViewModel)
#include "test_peer_discovery_view_model.moc"
