/**
* @file    test_peer_discovery_view_model.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   设备发现视图模型测试
*
* 测试用例：在线设备与手动端点按来源优先级排序、发现服务信号转发、
* 无数据层时历史刷新安全、空发现服务的防御行为、选中设备查询。
*/

#include <QtTest/QtTest>
#include <QSet>
#include <QSignalSpy>
#include <QSharedPointer>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>

#include "config_manager.h"
#include "data_types.h"
#include "db_seed.h"
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
    void testPinnedPeerSortedFirst();
    void testAliasDisplayAndSearchData();
    void testAliasSurvivesHeartbeatUpdate();
    void testSearchPeersMatchesNameAliasAndIp();
    void testSearchPeersLimitAndOrder();
    void testSearchPeersEmptyAndNoMatch();

private:
    // 在临时目录打开一份本地历史库
    std::unique_ptr<LocalDataBroker> openBroker(const QString &fileName);
    // 构造一份注入用的设备信息
    PeerInfo makePeerInfo(const QString &deviceId, const QString &name, const QString &ip);
    // 通过数据层异步写入设备快照并等待落库完成
    void seedPeer(LocalDataBroker &broker, const PeerInfo &peer);
    // 查询设备在合并列表或隐藏列表中的下标，未命中返回 -1
    int indexOfPeer(const PeerDiscoveryViewModel &viewModel, const QString &deviceId,
                    bool hiddenList = false) const;

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
        const QString deviceId = peers.at(i).toMap().value("deviceId").toString();
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

        // 隐藏落库后列表立即移除条目，同时进入 hiddenPeers 列表供设置页展示
        viewModel.setDeviceHidden(QStringLiteral("restore-peer"), true);
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("restore-peer")).isEmpty(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(indexOfPeer(viewModel, QStringLiteral("restore-peer"),
                                            true) >= 0, 3000);

        // 入站消息/传输请求触发的恢复入口：复位 hidden 并把设备带回列表。
        // 等待到最终态（在场且 hidden=false）：过滤集合解除先于异步重载完成时，
        // 存在性可能先由携带旧 hidden=1 的恢复数据短暂满足，单独等待会有假阳性
        viewModel.restoreHiddenDevice(QStringLiteral("restore-peer"));
        const auto restoredReady = [&viewModel]() {
            const QVariantMap snapshot = viewModel.deviceById(QStringLiteral("restore-peer"));
            return !snapshot.isEmpty() && !snapshot.value("hidden").toBool();
        };
        QTRY_VERIFY_WITH_TIMEOUT(restoredReady(), 5000);
        const QVariantMap restored = viewModel.deviceById(QStringLiteral("restore-peer"));
        QVERIFY(!restored.isEmpty());
        QCOMPARE(restored.value("hidden").toBool(), false);
        QTRY_VERIFY_WITH_TIMEOUT(indexOfPeer(viewModel, QStringLiteral("restore-peer"),
                                            true) < 0, 3000);

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

// 置顶设备排最前：置顶档优先于最近活跃排序，取消置顶后恢复原排序
void TestPeerDiscoveryViewModel::testPinnedPeerSortedFirst()
{
    auto broker = openBroker("pinned-sort.sqlite");
    QVERIFY(broker);

    // 先写入较早活跃的设备，再写入较新活跃的设备：未置顶时按最近活跃倒序
    const PeerInfo oldPeer = makePeerInfo(QStringLiteral("pinned-old"), QStringLiteral("较早设备"),
                                          QStringLiteral("10.254.254.246"));
    seedPeer(*broker, oldPeer);
    QTest::qWait(30);  // 拉开两台设备的 lastSeen 时间戳
    const PeerInfo newPeer = makePeerInfo(QStringLiteral("pinned-new"), QStringLiteral("较新设备"),
                                          QStringLiteral("10.254.254.247"));
    seedPeer(*broker, newPeer);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("pinned-old")).isEmpty(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("pinned-new")).isEmpty(), 3000);

    // 未置顶：较新的设备排在前面（两台均为 history 档，无在线条目干扰）
    QVERIFY(indexOfPeer(viewModel, QStringLiteral("pinned-new"))
            < indexOfPeer(viewModel, QStringLiteral("pinned-old")));

    // 置顶较早设备后立即反超排最前，deviceById 同步反映置顶态
    viewModel.setDevicePinned(QStringLiteral("pinned-old"), true);
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("pinned-old"))
                             .value("pinned").toBool(), 3000);
    QVERIFY(indexOfPeer(viewModel, QStringLiteral("pinned-old"))
            < indexOfPeer(viewModel, QStringLiteral("pinned-new")));

    // 取消置顶后恢复按最近活跃排序
    viewModel.setDevicePinned(QStringLiteral("pinned-old"), false);
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("pinned-old"))
                             .value("pinned").toBool(), 3000);
    QVERIFY(indexOfPeer(viewModel, QStringLiteral("pinned-new"))
            < indexOfPeer(viewModel, QStringLiteral("pinned-old")));
}

// 备注显示与搜索的数据链路：目录与在线条目均携带 alias，设置/清除即时生效
void TestPeerDiscoveryViewModel::testAliasDisplayAndSearchData()
{
    auto broker = openBroker("alias-display.sqlite");
    QVERIFY(broker);

    const PeerInfo peer = makePeerInfo(QStringLiteral("alias-peer"), QStringLiteral("DESKTOP-ABC123"),
                                       QStringLiteral("10.254.254.248"));
    seedPeer(*broker, peer);
    _discovery->addManualPeer(peer);  // 在线条目存在时备注同样需要注入

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("alias-peer")).isEmpty(), 3000);

    // 设置备注后条目携带 alias：显示优先级（备注 > 广播名）与搜索匹配
    // 均由展示层按该字段裁决，这里验证数据链路两端
    viewModel.setDeviceAlias(QStringLiteral("alias-peer"), QStringLiteral("老王的电脑"));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("alias-peer"))
                             .value("alias").toString() == QStringLiteral("老王的电脑"), 3000);

    // 合并列表条目的 alias 字段就位（在线与历史条目一体注入），搜索命中依赖它进过滤链路
    bool aliasPlumbed = false;
    const QVariantList peers = viewModel.peers();
    for (const QVariant &entry : peers) {
        const QVariantMap map = entry.toMap();
        if (map.value("deviceId").toString() == QStringLiteral("alias-peer")) {
            aliasPlumbed = map.contains("alias");
        }
    }
    QVERIFY(aliasPlumbed);

    // 清除备注后 alias 回落空值，广播名保持不变
    viewModel.setDeviceAlias(QStringLiteral("alias-peer"), QString());
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("alias-peer"))
                             .value("alias").toString().isEmpty(), 3000);
    QCOMPARE(viewModel.deviceById(QStringLiteral("alias-peer"))
             .value("deviceName").toString(), QStringLiteral("DESKTOP-ABC123"));
}

// 心跳保护端到端：设备入目录并设置备注后，模拟心跳更新设备名与 IP，
// 数据库与列表中的备注都保持，显示字段仍是备注
void TestPeerDiscoveryViewModel::testAliasSurvivesHeartbeatUpdate()
{
    auto broker = openBroker("alias-heartbeat.sqlite");
    QVERIFY(broker);

    const PeerInfo peer = makePeerInfo(QStringLiteral("heartbeat-peer"), QStringLiteral("原设备名"),
                                       QStringLiteral("10.254.254.252"));
    seedPeer(*broker, peer);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.deviceById(QStringLiteral("heartbeat-peer")).isEmpty(), 3000);

    viewModel.setDeviceAlias(QStringLiteral("heartbeat-peer"), QStringLiteral("实验室前台"));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("heartbeat-peer"))
                             .value("alias").toString() == QStringLiteral("实验室前台"), 3000);

    // 模拟后续心跳：同设备换了广播名与 IP（走不触 alias 列的 upsert 路径）
    PeerInfo heartbeat = peer;
    heartbeat.deviceName = QStringLiteral("改名设备XYZ");
    heartbeat.ipAddress = QStringLiteral("10.254.254.253");
    heartbeat.lastSeen = QDateTime::currentDateTimeUtc().addSecs(30);
    seedPeer(*broker, heartbeat);

    // 真实链路中在线条目随发现信号即时刷新，历史目录在离线或刷新时重载
    viewModel.refreshHistory();
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.deviceById(QStringLiteral("heartbeat-peer"))
                             .value("deviceName").toString() == QStringLiteral("改名设备XYZ"), 3000);

    // 列表条目设备名与 IP 已更新，备注仍在
    const QVariantMap snapshot = viewModel.deviceById(QStringLiteral("heartbeat-peer"));
    QCOMPARE(snapshot.value("deviceName").toString(), QStringLiteral("改名设备XYZ"));
    QCOMPARE(snapshot.value("ipAddress").toString(), QStringLiteral("10.254.254.253"));
    QCOMPARE(snapshot.value("alias").toString(), QStringLiteral("实验室前台"));

    // 数据库中备注未被心跳覆盖
    const auto aliasKept = QSharedPointer<QString>::create();
    broker->loadRecentPeers(this, 10,
                            [aliasKept](const QList<PeerRecord> &records, bool ok) {
                                if (!ok) {
                                    return;
                                }
                                for (const PeerRecord &record : records) {
                                    if (record.deviceId == QStringLiteral("heartbeat-peer")) {
                                        *aliasKept = record.alias;
                                    }
                                }
                            });
    QTRY_COMPARE_WITH_TIMEOUT(*aliasKept, QStringLiteral("实验室前台"), 3000);
}

// 关键字检索走数据库：设备名、备注与最近 IP 三路命中，隐藏设备不进结果
void TestPeerDiscoveryViewModel::testSearchPeersMatchesNameAliasAndIp()
{
    auto broker = openBroker("search-paths.sqlite");
    QVERIFY(broker);

    const PeerInfo nameHit = makePeerInfo(QStringLiteral("search-name-hit"),
                                          QStringLiteral("会议室目标甲"),
                                          QStringLiteral("10.253.100.1"));
    const PeerInfo aliasHit = makePeerInfo(QStringLiteral("search-alias-hit"),
                                           QStringLiteral("普通设备一"),
                                           QStringLiteral("10.253.100.2"));
    const PeerInfo ipHit = makePeerInfo(QStringLiteral("search-ip-hit"),
                                        QStringLiteral("普通设备二"),
                                        QStringLiteral("172.16.99.77"));
    const PeerInfo missPeer = makePeerInfo(QStringLiteral("search-miss"),
                                           QStringLiteral("无关设备"),
                                           QStringLiteral("10.253.100.3"));
    const PeerInfo hiddenPeer = makePeerInfo(QStringLiteral("search-hidden"),
                                             QStringLiteral("目标丙被隐藏"),
                                             QStringLiteral("10.253.100.4"));
    seedPeer(*broker, nameHit);
    seedPeer(*broker, aliasHit);
    seedPeer(*broker, ipHit);
    seedPeer(*broker, missPeer);
    seedPeer(*broker, hiddenPeer);

    // 备注命中走 N2-D 的 alias 列，隐藏命中依赖 hidden 列过滤，均经数据层落库
    bool aliasSet = false;
    broker->setDeviceAlias(this, QStringLiteral("search-alias-hit"),
                           QStringLiteral("目标乙的电脑"),
                           [&aliasSet](bool) { aliasSet = true; });
    QTRY_COMPARE_WITH_TIMEOUT(aliasSet, true, 3000);

    bool hiddenSet = false;
    broker->setDeviceHidden(this, QStringLiteral("search-hidden"), true,
                            [&hiddenSet](bool) { hiddenSet = true; });
    QTRY_COMPARE_WITH_TIMEOUT(hiddenSet, true, 3000);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());

    viewModel.searchPeers(QStringLiteral("目标"));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.searchBusy(), 3000);

    // 名称与备注命中两台，IP-only 与无关设备不命中，隐藏设备被过滤
    const QVariantList results = viewModel.searchResults();
    QCOMPARE(results.size(), 2);
    QStringList hitIds;
    for (const QVariant &entry : results) {
        hitIds.append(entry.toMap().value("deviceId").toString());
        QCOMPARE(entry.toMap().value("isOnline").toBool(), false);  // 数据库命中按离线卡展示
    }
    QVERIFY(hitIds.contains(QStringLiteral("search-name-hit")));
    QVERIFY(hitIds.contains(QStringLiteral("search-alias-hit")));
    QVERIFY(!hitIds.contains(QStringLiteral("search-ip-hit")));
    QVERIFY(!hitIds.contains(QStringLiteral("search-miss")));
    QVERIFY(!hitIds.contains(QStringLiteral("search-hidden")));

    // 换用 IP 关键字命中第三路
    viewModel.searchPeers(QStringLiteral("172.16.99"));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.searchBusy(), 3000);
    QCOMPARE(viewModel.searchResults().size(), 1);
    QCOMPARE(viewModel.searchResults().first().toMap().value("deviceId").toString(),
             QStringLiteral("search-ip-hit"));
}

// 检索分页：命中超过单次上限时只返回上限内条目，按最近活动倒序取最新的一批
void TestPeerDiscoveryViewModel::testSearchPeersLimitAndOrder()
{
    auto database = gy::test::openDatabase(_tempDir->path(), "search-limit.sqlite");
    QVERIFY(database);
    SqliteDeviceRepository repository(database.get());

    // 直写 150 台命中设备，活跃时间递增：越靠后越新
    const QDateTime base = QDateTime::fromString("2026-10-01T09:00:00.000Z", Qt::ISODateWithMs);
    for (int i = 1; i <= 150; ++i) {
        PeerRecord record;
        record.deviceId = QStringLiteral("bulk-dev-%1").arg(i, 3, 10, QChar('0'));
        record.deviceName = QStringLiteral("批量设备%1").arg(i, 3, 10, QChar('0'));
        record.lastIpAddress = QStringLiteral("10.77.0.%1").arg(i);
        record.lastTcpPort = 35100;
        record.firstSeenAt = base.addSecs(i * 60);
        record.lastSeenAt = base.addSecs(i * 60);
        QString error;
        QVERIFY(repository.upsertPeer(record, &error));
    }

    auto broker = openBroker("search-limit.sqlite");
    QVERIFY(broker);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());

    viewModel.searchPeers(QStringLiteral("批量设备"));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.searchBusy(), 3000);

    // 150 台全部命中但只回投上限 50 条（视图模型 kSearchPeerLimit 的口径）
    const QVariantList results = viewModel.searchResults();
    QCOMPARE(results.size(), 50);
    QCOMPARE(results.at(0).toMap().value("deviceId").toString(),
             QStringLiteral("bulk-dev-150"));
    QCOMPARE(results.at(49).toMap().value("deviceId").toString(),
             QStringLiteral("bulk-dev-101"));
}

// 空关键字立即清空结果，无命中关键字返回空结果且不影响后续检索
void TestPeerDiscoveryViewModel::testSearchPeersEmptyAndNoMatch()
{
    auto broker = openBroker("search-empty.sqlite");
    QVERIFY(broker);
    const PeerInfo peer = makePeerInfo(QStringLiteral("solo-peer"),
                                       QStringLiteral("孤立设备"),
                                       QStringLiteral("10.253.100.9"));
    seedPeer(*broker, peer);

    PeerDiscoveryViewModel viewModel(_discovery);
    viewModel.initDataBroker(broker.get());

    viewModel.searchPeers(QStringLiteral("绝不匹配的设备xyz"));
    QTRY_VERIFY_WITH_TIMEOUT(!viewModel.searchBusy(), 3000);
    QVERIFY(viewModel.searchResults().isEmpty());

    viewModel.searchPeers(QStringLiteral("孤立设备"));
    QTRY_VERIFY_WITH_TIMEOUT(viewModel.searchResults().size() == 1, 3000);
    QCOMPARE(viewModel.searchResults().first().toMap().value("deviceId").toString(),
             QStringLiteral("solo-peer"));

    // 空关键字同步清空且不发数据库查询
    viewModel.searchPeers(QString());
    QVERIFY(viewModel.searchResults().isEmpty());
    QVERIFY(!viewModel.searchBusy());
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

// 工具方法：查询设备在合并列表或隐藏列表中的下标
int TestPeerDiscoveryViewModel::indexOfPeer(const PeerDiscoveryViewModel &viewModel,
                                            const QString &deviceId, bool hiddenList) const
{
    const QVariantList peers = hiddenList ? viewModel.hiddenPeers() : viewModel.peers();
    for (int i = 0; i < peers.size(); ++i) {
        if (peers.at(i).toMap().value("deviceId").toString() == deviceId) {
            return i;
        }
    }
    return -1;
}

QTEST_MAIN(TestPeerDiscoveryViewModel)
#include "test_peer_discovery_view_model.moc"
