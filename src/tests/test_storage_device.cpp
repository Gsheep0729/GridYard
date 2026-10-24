/**
* @file    test_storage_device.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   设备目录仓储与设备合并测试
*
* 仓储语义直接验证纯 C++ 设备仓储（DeviceRepositoryCore）：写入更新、
* 活动时间、排序、节流、管理标记、检索与级联删除；跨设备关联合并经
* LocalDataBroker 的对外入口验证。旧 Qt 版仓储已随 gy_storage 退役。
*/

#include <QtTest/QtTest>

#include <QDateTime>
#include <QSharedPointer>
#include <QTemporaryDir>

#include <memory>

#include "application_paths.h"
#include "db_seed.h"
#include "history_records.h"
#include "local_data_broker.h"

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/message_repository_core.h"
#include "core/storage/sqlite_conn.h"
#include "core/storage/transfer_repository_core.h"

class TestStorageDevice : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testCoreUpsertInsertThenUpdateKeepsManagement();
    void testCoreMarkActivityColumns();
    void testCoreRecentPeersOrderByActivity();
    void testCoreSettersIdempotentAndSteps();
    void testCoreDiscoveryThrottle();
    void testCoreReopenDatabase();
    void testCoreV2MigratedToV3KeepsManagementColumns();
    void testCoreSearchPeersEscapeAndHidden();
    void testCoreMergeManualPeerMarkers();
    void testCoreDeleteDeviceWithHistoryCascade();
    void testCoreComposeStepsInSingleTransaction();
    void testMergeDeviceRecordsWithHistory();
    void testMergeDeviceRecordsWithoutHistory();

private:
    // 在临时目录中打开（或创建）指定相对路径的核心数据库
    std::unique_ptr<gy::storage::DatabaseCore> openDatabase(const QString &relativePath);
    // 构造一份带固定字段的核心设备记录
    gy::storage::PeerRecord makeRecord(const QString &deviceId, const QString &name,
                                       int64_t seenMs);
    // 构造一条指定会话的入站核心消息记录
    gy::storage::MessageRecord makeMessage(const QString &deviceId, const QString &messageId,
                                           int64_t sentMs);
    // 构造一条已完成的核心传输历史记录
    gy::storage::TransferRecord makeTransfer(const QString &deviceId, const QString &recordId,
                                             int64_t startedMs);

    QTemporaryDir _temporaryDir;
    QString _databasePath;
};

// 准备测试目录和数据库路径覆盖
void TestStorageDevice::initTestCase()
{
    QVERIFY(_temporaryDir.isValid());
    ApplicationPaths::coverForTest(_temporaryDir.path());
    _databasePath = _temporaryDir.path() + "/device-test.sqlite";
}

// 清理测试目录覆盖
void TestStorageDevice::cleanupTestCase()
{
    ApplicationPaths::clearTestCover();
}

// 首次 upsert 写入新设备，同 device_id 的二次 upsert 更新快照不产生重复行，
// 且名称/IP 变化的心跳不清除管理三列
void TestStorageDevice::testCoreUpsertInsertThenUpdateKeepsManagement()
{
    namespace core = gy::storage;
    auto database = openDatabase("insert-update.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());

    const int64_t firstSeen =
        QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const core::PeerRecord first = makeRecord("device-A", "Alpha", firstSeen);
    std::string error;
    QVERIFY(repository.upsertPeer(first, &error));
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("peer_devices")), 1);

    // 同 device_id 不同名称、IP，应触发更新而不是插入
    core::PeerRecord updated = first;
    updated.deviceName = "Alpha-Renamed";
    updated.lastIpAddress = "192.168.1.55";
    updated.lastSeenAtMs = firstSeen + 60000;
    QVERIFY(repository.upsertPeer(updated, &error));
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("peer_devices")), 1);

    // recentPeers 应反映最新名称和 IP，管理列默认值完整带回
    const std::vector<core::PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-A"));
    QCOMPARE(QString::fromStdString(records.front().deviceName), QStringLiteral("Alpha-Renamed"));
    QCOMPARE(QString::fromStdString(records.front().lastIpAddress), QStringLiteral("192.168.1.55"));
    QVERIFY(records.front().alias.empty());
    QVERIFY(!records.front().pinned);
    QVERIFY(!records.front().hidden);

    // 设置管理标记后，心跳 upsert 不清管理状态（ON CONFLICT 不触碰三列的既有契约）
    QVERIFY(repository.setDeviceAlias("device-A", "旧友备注", &error));
    QVERIFY(repository.setDevicePinned("device-A", true, &error));
    QVERIFY(repository.setDeviceHidden("device-A", true, &error));
    core::PeerRecord heartbeat = updated;
    heartbeat.deviceName = "Alpha-Heartbeat";
    heartbeat.lastIpAddress = "192.168.1.66";
    heartbeat.lastSeenAtMs = firstSeen + 120000;
    QVERIFY(repository.upsertPeer(heartbeat, &error));

    const std::vector<core::PeerRecord> after = repository.recentPeers(10, &error);
    QCOMPARE(int(after.size()), 1);
    QCOMPARE(QString::fromStdString(after.front().deviceName), QStringLiteral("Alpha-Heartbeat"));
    QCOMPARE(QString::fromStdString(after.front().alias), QStringLiteral("旧友备注"));
    QVERIFY(after.front().pinned);
    QVERIFY(after.front().hidden);
}

// markChatActivity 只更新 last_chat_at，markTransferActivity 只更新 last_transfer_at
void TestStorageDevice::testCoreMarkActivityColumns()
{
    namespace core = gy::storage;
    auto database = openDatabase("activity-columns.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());

    const int64_t base =
        QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;
    // 名称与默认种子不同，绕开节流确保写入
    core::PeerRecord seed = makeRecord("device-B", "Bravo", base);
    seed.deviceName = "Bravo-Init";
    QVERIFY(repository.upsertPeer(seed, &error));

    QVERIFY(repository.markChatActivity("device-B", base + 120000, &error));
    std::vector<core::PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(records.front().lastChatAtMs.value_or(0), base + 120000);
    QVERIFY(!records.front().lastTransferAtMs.has_value());  // 传输列不受影响

    QVERIFY(repository.markTransferActivity("device-B", base + 300000, &error));
    records = repository.recentPeers(5, &error);
    QCOMPARE(records.front().lastTransferAtMs.value_or(0), base + 300000);
    QCOMPARE(records.front().lastChatAtMs.value_or(0), base + 120000);  // 聊天时间不受影响
}

// recentPeers 排序：收藏优先，其后按 COALESCE(last_chat_at, last_transfer_at,
// last_seen_at) 倒序，limit 生效；取消收藏后回落按活动排序
void TestStorageDevice::testCoreRecentPeersOrderByActivity()
{
    namespace core = gy::storage;
    auto database = openDatabase("order.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());
    std::string error;

    const int64_t t1 =
        QDateTime::fromString("2026-06-25T09:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    QVERIFY(repository.upsertPeer(makeRecord("device-A", "Alpha", t1), &error));
    QVERIFY(repository.upsertPeer(makeRecord("device-B", "Bravo", t1 + 3600000), &error));
    QVERIFY(repository.upsertPeer(makeRecord("device-C", "Charlie", t1 + 7200000), &error));

    // 让 A 的聊天时间最新，压过 C 的心跳；B 收藏后反超全场
    const int64_t aChat =
        QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    QVERIFY(repository.markChatActivity("device-A", aChat, &error));
    QVERIFY(repository.setDeviceFavorite("device-B", true, &error));

    std::vector<core::PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 3);
    QCOMPARE(QString::fromStdString(records.at(0).deviceId), QStringLiteral("device-B"));
    QVERIFY(records.at(0).favorite);
    QCOMPARE(QString::fromStdString(records.at(1).deviceId), QStringLiteral("device-A"));
    QCOMPARE(QString::fromStdString(records.at(2).deviceId), QStringLiteral("device-C"));

    // limit 生效
    const std::vector<core::PeerRecord> topTwo = repository.recentPeers(2, &error);
    QCOMPARE(int(topTwo.size()), 2);
    QCOMPARE(QString::fromStdString(topTwo.front().deviceId), QStringLiteral("device-B"));

    // 心跳改名 upsert 不携带 favorite 列：收藏按 device_id 落键，远程改名不清收藏
    core::PeerRecord heartbeat = makeRecord("device-B", "Bravo-Renamed", t1 + 60000);
    QVERIFY(repository.upsertPeer(heartbeat, &error));
    records = repository.recentPeers(10, &error);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-B"));
    QCOMPARE(QString::fromStdString(records.front().deviceName), QStringLiteral("Bravo-Renamed"));
    QVERIFY(records.front().favorite);

    // 取消收藏后回落到按活动时间排序
    QVERIFY(repository.setDeviceFavorite("device-B", false, &error));
    records = repository.recentPeers(10, &error);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-A"));
    QVERIFY(!records.front().favorite);
}

// 置顶/隐藏/备注设置幂等、设备行不存在同样返回成功，Step 变体可组合进单事务
void TestStorageDevice::testCoreSettersIdempotentAndSteps()
{
    namespace core = gy::storage;
    auto database = openDatabase("setters.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());

    const int64_t base =
        QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;
    QVERIFY(repository.upsertPeer(makeRecord("device-manage", "Managed", base), &error));

    // 设置备注、置顶与隐藏，读链路带出
    QVERIFY(repository.setDeviceAlias("device-manage", "老王", &error));
    QVERIFY(repository.setDevicePinned("device-manage", true, &error));
    QVERIFY(repository.setDeviceHidden("device-manage", true, &error));
    std::vector<core::PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(QString::fromStdString(records.front().alias), QStringLiteral("老王"));
    QVERIFY(records.front().pinned);
    QVERIFY(records.front().hidden);

    // 重复设置幂等且覆盖旧值，不产生重复行
    QVERIFY(repository.setDeviceAlias("device-manage", "小李", &error));
    QVERIFY(repository.setDeviceAlias("device-manage", "小李", &error));
    QVERIFY(repository.setDevicePinned("device-manage", true, &error));
    QVERIFY(repository.setDeviceHidden("device-manage", true, &error));
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("peer_devices")), 1);

    // 设备行不存在的更新同样幂等返回成功
    QVERIFY(repository.setDeviceAlias("no-such-device", "幽灵", &error));
    QVERIFY(repository.setDevicePinned("no-such-device", true, &error));
    QVERIFY(repository.setDeviceHidden("no-such-device", true, &error));

    // 取消置顶、恢复显示、清除备注（空串）后回落默认值
    QVERIFY(repository.setDevicePinned("device-manage", false, &error));
    QVERIFY(repository.setDeviceHidden("device-manage", false, &error));
    QVERIFY(repository.setDeviceAlias("device-manage", "", &error));
    records = repository.recentPeers(5, &error);
    QVERIFY(!records.front().pinned);
    QVERIFY(!records.front().hidden);
    QVERIFY(records.front().alias.empty());

    // Step 变体可与其他步骤组合进同一事务执行
    QVERIFY(database->runSteps(
        {core::DeviceRepositoryCore::setDevicePinnedStep("device-manage", true),
         core::DeviceRepositoryCore::setDeviceHiddenStep("device-manage", true),
         core::DeviceRepositoryCore::setDeviceAliasStep("device-manage", "事务备注")},
        &error));
    records = repository.recentPeers(5, &error);
    QVERIFY(records.front().pinned);
    QVERIFY(records.front().hidden);
    QCOMPARE(QString::fromStdString(records.front().alias), QStringLiteral("事务备注"));
}

// 同一无变化发现快照在节流间隔内不重复写入磁盘
void TestStorageDevice::testCoreDiscoveryThrottle()
{
    namespace core = gy::storage;
    auto database = openDatabase("throttle.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());

    const int64_t firstSeen =
        QDateTime::fromString("2026-06-25T13:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const core::PeerRecord seed = makeRecord("device-D", "Delta", firstSeen);
    std::string error;
    QVERIFY(repository.upsertPeer(seed, &error));

    // 直接在外部把 last_chat_at 改成一个可识别的值，作为节流是否生效的探针
    const int64_t probeTime = firstSeen + 1800000;
    QVERIFY(repository.markChatActivity("device-D", probeTime, &error));

    // 节流窗口内（< 30s）再次 upsert 完全相同的发现快照
    core::PeerRecord heartbeat = seed;
    heartbeat.lastSeenAtMs = firstSeen + 5000;  // 远小于 30 秒
    QVERIFY(repository.upsertPeer(heartbeat, &error));

    // 探针时间应保持不变，说明节流跳过了 upsert 的潜在覆盖
    std::vector<core::PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(records.front().lastChatAtMs.value_or(0), probeTime);

    // 超过节流窗口后再次 upsert，应执行写入并刷新 last_seen_at
    core::PeerRecord later = seed;
    later.lastSeenAtMs = firstSeen + 60000;  // 超过 30 秒
    QVERIFY(repository.upsertPeer(later, &error));
    records = repository.recentPeers(5, &error);
    QCOMPARE(records.front().lastSeenAtMs, later.lastSeenAtMs);
}

// 关闭并重新打开数据库后，设备目录与活动时间仍可恢复
void TestStorageDevice::testCoreReopenDatabase()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/reopen.sqlite";
    const int64_t seen =
        QDateTime::fromString("2026-06-25T14:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();

    {
        auto database = openDatabase("reopen.sqlite");
        QVERIFY(database);
        core::DeviceRepositoryCore repository(database.get());
        std::string error;
        QVERIFY(repository.upsertPeer(makeRecord("device-E", "Echo", seen), &error));
        QVERIFY(repository.markChatActivity("device-E", seen + 120000, &error));
    }

    // 重新打开数据库，设备目录和活动时间应保留
    auto reopened = openDatabase("reopen.sqlite");
    QVERIFY(reopened);
    core::DeviceRepositoryCore repository(reopened.get());
    std::string error;
    const std::vector<core::PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-E"));
    QCOMPARE(QString::fromStdString(records.front().deviceName), QStringLiteral("Echo"));
    QVERIFY(records.front().lastChatAtMs.has_value());
}

// v2 存量库升级到 v3：favorite 加列默认 0，alias/pinned/hidden 与快照字段全部保留
void TestStorageDevice::testCoreV2MigratedToV3KeepsManagementColumns()
{
    namespace core = gy::storage;
    const QString path = _databasePath + ".v2-upgrade.sqlite";

    // 用纯 C++ 连接手工造一个最小 v2 假库：schema_version 登记 2，
    // peer_devices 含管理三列并带存量值
    {
        auto opened = core::SqliteConn::open(path.toStdString());
        QVERIFY(opened.has_value());
        core::SqliteConn &conn = opened.value();
        QVERIFY(conn.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)").has_value());
        QVERIFY(conn.exec("INSERT INTO schema_version VALUES(2, '2026-10-05T00:00:00.000Z')").has_value());
        QVERIFY(conn.exec("CREATE TABLE peer_devices (device_id TEXT PRIMARY KEY NOT NULL, device_name TEXT NOT NULL, last_ip_address TEXT, last_tcp_port INTEGER, first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL, last_chat_at TEXT, last_transfer_at TEXT, alias TEXT, pinned INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)").has_value());
        QVERIFY(conn.exec("INSERT INTO peer_devices VALUES('device-v2', '存量设备', '192.168.1.30', 35100, "
                          "'2026-06-25T08:00:00.000Z', '2026-06-25T09:00:00.000Z', NULL, NULL, "
                          "'老王的电脑', 1, 0)").has_value());
    }

    core::DatabaseCore database;
    std::string error;
    QVERIFY(database.initialize(path.toStdString(), &error));
    QCOMPARE(database.schemaVersion(), 3);

    core::DeviceRepositoryCore repository(&database);
    const std::vector<core::PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 1);
    const core::PeerRecord &record = records.front();
    // 管理三列与快照字段在 v2 到 v3 迁移后必须原样保留，收藏默认未勾选
    QCOMPARE(QString::fromStdString(record.deviceId), QStringLiteral("device-v2"));
    QCOMPARE(QString::fromStdString(record.alias), QStringLiteral("老王的电脑"));
    QCOMPARE(record.pinned, true);
    QCOMPARE(record.hidden, false);
    QCOMPARE(record.favorite, false);
}

// searchPeers：LIKE 通配符按字面匹配、隐藏设备不返回
void TestStorageDevice::testCoreSearchPeersEscapeAndHidden()
{
    namespace core = gy::storage;
    auto database = openDatabase("search.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());

    const int64_t seen =
        QDateTime::fromString("2026-06-25T16:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;
    QVERIFY(repository.upsertPeer(makeRecord("core-plain", "普通设备", seen), &error));
    QVERIFY(repository.upsertPeer(makeRecord("core-wild", "名称带%百分号", seen), &error));
    QVERIFY(repository.upsertPeer(makeRecord("core-under", "名称带_下划线", seen), &error));
    core::PeerRecord hidden = makeRecord("core-hidden", "隐藏设备", seen);
    hidden.lastIpAddress = "192.168.1.24";  // 独立 IP，验证隐藏条目按地址同样不可见
    QVERIFY(repository.upsertPeer(hidden, &error));
    QVERIFY(repository.setDeviceHidden("core-hidden", true, &error));

    // 关键字 % 与 _ 只做字面包含，不做通配展开
    std::vector<core::PeerRecord> hits = repository.searchPeers("%", 10, &error);
    QCOMPARE(int(hits.size()), 1);
    QCOMPARE(QString::fromStdString(hits.front().deviceId), QStringLiteral("core-wild"));
    hits = repository.searchPeers("_", 10, &error);
    QCOMPARE(int(hits.size()), 1);
    QCOMPARE(QString::fromStdString(hits.front().deviceId), QStringLiteral("core-under"));

    // 常规关键字命中"名称带"前缀的两台，隐藏设备按名称与 IP 检索均不返回
    hits = repository.searchPeers("名称带", 10, &error);
    QCOMPARE(int(hits.size()), 2);
    hits = repository.searchPeers("隐藏", 10, &error);
    QCOMPARE(int(hits.size()), 0);
    hits = repository.searchPeers("192.168.1.24", 10, &error);
    QCOMPARE(int(hits.size()), 0);

    // 纯空白关键字等价于空结果
    QVERIFY(repository.searchPeers("   ", 10, &error).empty());
}

// 伪条目标记合并：备注取非空一方、置顶/隐藏/收藏取或，伪行删除后幂等
void TestStorageDevice::testCoreMergeManualPeerMarkers()
{
    namespace core = gy::storage;
    auto database = openDatabase("merge-manual.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore repository(database.get());

    const int64_t seen =
        QDateTime::fromString("2026-06-25T13:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;

    // 手动伪条目带备注与置顶标记，真身带隐藏标记
    core::PeerRecord pseudo = makeRecord("manual_192.168.1.77", "手动端点", seen);
    pseudo.lastIpAddress = "192.168.1.77";
    QVERIFY(repository.upsertPeer(pseudo, &error));
    QVERIFY(repository.setDeviceAlias("manual_192.168.1.77", "老王的电脑", &error));
    QVERIFY(repository.setDevicePinned("manual_192.168.1.77", true, &error));
    QVERIFY(repository.setDeviceFavorite("manual_192.168.1.77", true, &error));

    core::PeerRecord real = makeRecord("real-uuid-1", "真身设备", seen + 60000);
    real.lastIpAddress = "192.168.1.77";
    QVERIFY(repository.upsertPeer(real, &error));
    QVERIFY(repository.setDeviceHidden("real-uuid-1", true, &error));
    QVERIFY(repository.mergeManualPeerMarkers("real-uuid-1", "manual_192.168.1.77", &error));

    std::vector<core::PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 1);  // 伪行已删除，只剩真行
    const core::PeerRecord &merged = records.front();
    QCOMPARE(QString::fromStdString(merged.deviceId), QStringLiteral("real-uuid-1"));
    QCOMPARE(QString::fromStdString(merged.alias), QStringLiteral("老王的电脑"));
    QVERIFY(merged.pinned);
    QVERIFY(merged.favorite);
    QVERIFY(merged.hidden);

    // 真行已有备注时不被伪行值覆盖（备注取非空一方）
    core::PeerRecord pseudo2 = makeRecord("manual_192.168.1.78", "手动端点二", seen);
    pseudo2.lastIpAddress = "192.168.1.78";
    QVERIFY(repository.upsertPeer(pseudo2, &error));
    QVERIFY(repository.setDeviceAlias("manual_192.168.1.78", "伪身备注", &error));
    core::PeerRecord real2 = makeRecord("real-uuid-2", "真身设备二", seen + 90000);
    real2.lastIpAddress = "192.168.1.78";
    QVERIFY(repository.upsertPeer(real2, &error));
    QVERIFY(repository.setDeviceAlias("real-uuid-2", "真身备注", &error));
    QVERIFY(repository.mergeManualPeerMarkers("real-uuid-2", "manual_192.168.1.78", &error));
    records = repository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 2);
    for (const core::PeerRecord &record : records) {
        if (record.deviceId == "real-uuid-2") {
            QCOMPARE(QString::fromStdString(record.alias), QStringLiteral("真身备注"));
        }
    }

    // 无伪行时迁移为空操作，真行保持原样；伪行已删除后再次合并不报错
    QVERIFY(repository.mergeManualPeerMarkers("real-uuid-1", "manual_10.0.0.1", &error));
    QVERIFY(repository.mergeManualPeerMarkers("real-uuid-1", "manual_192.168.1.77", &error));
    QCOMPARE(int(repository.recentPeers(10, &error).size()), 2);
}

// 删除设备级联清空聊天与传输历史且不触碰文件系统，保留设备不误伤；
// 再次发现按全新设备入目录
void TestStorageDevice::testCoreDeleteDeviceWithHistoryCascade()
{
    namespace core = gy::storage;
    auto database = openDatabase("delete-cascade.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::MessageRepositoryCore messages(database.get());
    core::TransferRepositoryCore transfers(database.get());

    const int64_t base =
        QDateTime::fromString("2026-06-25T16:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;
    QVERIFY(devices.upsertPeer(makeRecord("device-del", "DeleteMe", base), &error));
    QVERIFY(devices.upsertPeer(makeRecord("device-keep", "KeepMe", base), &error));

    // 被删设备两条聊天消息与两条传输历史，保留设备各一条
    QVERIFY(messages.saveMessage(makeMessage("device-del", "msg-1", base), &error));
    QVERIFY(messages.saveMessage(makeMessage("device-del", "msg-2", base + 60000), &error));
    QVERIFY(transfers.upsertFinishedTransfer(makeTransfer("device-del", "tr-1", base), &error));
    QVERIFY(transfers.upsertFinishedTransfer(makeTransfer("device-del", "tr-2", base + 30000), &error));
    QVERIFY(messages.saveMessage(makeMessage("device-keep", "msg-k", base), &error));
    QVERIFY(transfers.upsertFinishedTransfer(makeTransfer("device-keep", "tr-k", base), &error));

    // 删除路径只涉及数据库行，不触碰任何本地文件
    QVERIFY(devices.deleteDeviceWithHistory("device-del", &error));

    // 级联完整：设备行删除、传输历史清空、聊天会话与消息随 CASCADE 清空，
    // 保留设备的数据一行不少
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("peer_devices")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_messages")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_conversations")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("transfer_history")), 1);

    // 幂等：设备行已不存在时再次删除仍返回成功
    QVERIFY(devices.deleteDeviceWithHistory("device-del", &error));
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("peer_devices")), 1);

    const std::vector<core::PeerRecord> records = devices.recentPeers(5, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-keep"));

    // 删除后同快照心跳不被节流缓存拦截，按全新设备重新入目录（first_seen 语义），
    // 且目录重建不会带回任何旧聊天或传输历史
    devices.noteDeviceDeleted("device-del");
    QVERIFY(devices.upsertPeer(makeRecord("device-del", "DeleteMe", base), &error));
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("peer_devices")), 2);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_messages")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("transfer_history")), 1);
}

// *Step 组合进单事务：全部成功才提交，任一步失败整体回滚
void TestStorageDevice::testCoreComposeStepsInSingleTransaction()
{
    namespace core = gy::storage;
    auto database = openDatabase("steps.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::MessageRepositoryCore messages(database.get());

    const int64_t at =
        QDateTime::fromString("2026-06-25T19:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const core::PeerRecord peer = makeRecord("device-steps", "Steps", at);
    const core::MessageRecord message = makeMessage("device-steps", "steps-msg", at);

    // 设备 upsert、消息写入、活动时间三步同事务，再补一个必失败步验证整体回滚
    const auto failStep = [](core::SqliteConn &, std::string *taskError) {
        if (taskError) {
            *taskError = "组合事务失败步";
        }
        return false;
    };

    std::string error;
    QVERIFY(!database->runSteps({core::DeviceRepositoryCore::upsertPeerStep(peer),
                                 core::MessageRepositoryCore::saveMessageStep(message),
                                 failStep},
                                &error));
    QVERIFY(devices.recentPeers(5, &error).empty());  // 第一步也已回滚

    QVERIFY(database->runSteps({core::DeviceRepositoryCore::upsertPeerStep(peer),
                                core::MessageRepositoryCore::saveMessageStep(message),
                                core::DeviceRepositoryCore::markChatActivityStep(peer.deviceId, at + 1000)},
                               &error));
    core::MessageCursor cursor;
    cursor.peerDeviceId = peer.deviceId;
    QCOMPARE(int(messages.loadMessages(cursor, 5, &error).size()), 1);
}

// 关联合并（含历史）：旧行的管理标记并入新行，聊天与传输记录整体改挂新 ID，
// 旧行删除；新行不存在时先按传入记录建行
void TestStorageDevice::testMergeDeviceRecordsWithHistory()
{
    namespace core = gy::storage;
    auto database = openDatabase("merge-device-history.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore deviceRepository(database.get());
    core::MessageRepositoryCore messageRepository(database.get());
    core::TransferRepositoryCore transferRepository(database.get());

    const int64_t base =
        QDateTime::fromString("2026-06-25T14:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;
    core::PeerRecord oldDevice = makeRecord("device-old", "旧设备", base);
    oldDevice.lastIpAddress = "192.168.1.20";
    QVERIFY(deviceRepository.upsertPeer(oldDevice, &error));
    QVERIFY(deviceRepository.setDeviceAlias("device-old", "旧名备注", &error));
    QVERIFY(deviceRepository.setDeviceFavorite("device-old", true, &error));
    QVERIFY(deviceRepository.setDevicePinned("device-old", true, &error));

    // 旧设备一条消息与一条传输记录
    QVERIFY(messageRepository.saveMessage(makeMessage("device-old", "old-msg-1", base + 60000), &error));
    QVERIFY(transferRepository.upsertFinishedTransfer(
        makeTransfer("device-old", "old-rec-1", base + 120000), &error));

    // 新设备已存在（真身重新出现），带自己的名称
    core::PeerRecord newDevice = makeRecord("device-new", "新设备", base + 300000);
    QVERIFY(deviceRepository.upsertPeer(newDevice, &error));

    // 合并入口是 LocalDataBroker 的对外契约，传入 Qt 领域记录
    PeerRecord newDeviceQt;
    newDeviceQt.deviceId = "device-new";
    newDeviceQt.deviceName = "新设备";
    newDeviceQt.lastIpAddress = "192.168.1.10";
    newDeviceQt.lastTcpPort = 35100;
    newDeviceQt.firstSeenAt = QDateTime::fromMSecsSinceEpoch(newDevice.firstSeenAtMs, QTimeZone::UTC);
    newDeviceQt.lastSeenAt = newDeviceQt.firstSeenAt;

    const auto done = QSharedPointer<bool>::create(false);
    auto merge = std::make_unique<LocalDataBroker>();
    QString qtError;
    QVERIFY(merge->initialize(_temporaryDir.path() + "/merge-device-history.sqlite", &qtError));
    merge->mergeDeviceRecords(merge.get(), newDeviceQt, "device-old", true,
                              [done](bool succeeded) { *done = succeeded; });
    QTRY_VERIFY(*done);
    merge->closeStorage();

    // 旧行已删除，新行带迁移来的标记
    const std::vector<core::PeerRecord> records = deviceRepository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-new"));
    QCOMPARE(QString::fromStdString(records.front().deviceName), QStringLiteral("新设备"));
    QCOMPARE(QString::fromStdString(records.front().alias), QStringLiteral("旧名备注"));
    QVERIFY(records.front().favorite);
    QVERIFY(records.front().pinned);

    // 聊天与传输已改挂新 ID
    core::MessageCursor cursor;
    cursor.peerDeviceId = "device-new";
    const std::vector<core::MessageRecord> moved =
        messageRepository.loadMessages(cursor, 10, &error);
    QCOMPARE(int(moved.size()), 1);
    QCOMPARE(QString::fromStdString(moved.front().messageId), QStringLiteral("old-msg-1"));
    core::TransferQuery query;
    query.peerDeviceId = "device-new";
    QCOMPARE(int(transferRepository.queryTransfers(query, 10, &error).size()), 1);
}

// 关联合并（不迁移历史）：管理标记并入、旧行删除、旧聊天与传输一并放弃
void TestStorageDevice::testMergeDeviceRecordsWithoutHistory()
{
    namespace core = gy::storage;
    auto database = openDatabase("merge-device-nohistory.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore deviceRepository(database.get());
    core::MessageRepositoryCore messageRepository(database.get());
    core::TransferRepositoryCore transferRepository(database.get());

    const int64_t base =
        QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    std::string error;
    QVERIFY(deviceRepository.upsertPeer(makeRecord("device-old2", "旧设备二", base), &error));
    QVERIFY(deviceRepository.setDeviceAlias("device-old2", "待并备注", &error));
    QVERIFY(messageRepository.saveMessage(makeMessage("device-old2", "old2-msg-1", base + 60000), &error));
    QVERIFY(transferRepository.upsertFinishedTransfer(
        makeTransfer("device-old2", "old2-rec-1", base + 120000), &error));

    // 新行不存在：按传入记录建行
    PeerRecord newDeviceQt;
    newDeviceQt.deviceId = "device-new2";
    newDeviceQt.deviceName = "新设备二";
    newDeviceQt.firstSeenAt =
        QDateTime::fromMSecsSinceEpoch(base + 300000, QTimeZone::UTC);
    newDeviceQt.lastSeenAt = newDeviceQt.firstSeenAt;

    const auto done = QSharedPointer<bool>::create(false);
    auto merge = std::make_unique<LocalDataBroker>();
    QString qtError;
    QVERIFY(merge->initialize(_temporaryDir.path() + "/merge-device-nohistory.sqlite", &qtError));
    merge->mergeDeviceRecords(merge.get(), newDeviceQt, "device-old2", false,
                              [done](bool succeeded) { *done = succeeded; });
    QTRY_VERIFY(*done);
    merge->closeStorage();

    const std::vector<core::PeerRecord> records = deviceRepository.recentPeers(10, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().deviceId), QStringLiteral("device-new2"));
    QCOMPARE(QString::fromStdString(records.front().alias), QStringLiteral("待并备注"));

    // 旧历史被放弃：聊天随旧行级联清理，传输显式删除
    core::MessageCursor cursor;
    cursor.peerDeviceId = "device-old2";
    QCOMPARE(int(messageRepository.loadMessages(cursor, 10, &error).size()), 0);
    cursor.peerDeviceId = "device-new2";
    QCOMPARE(int(messageRepository.loadMessages(cursor, 10, &error).size()), 0);
    core::TransferQuery query;
    query.peerDeviceId = "device-old2";
    QCOMPARE(int(transferRepository.queryTransfers(query, 10, &error).size()), 0);
    query.peerDeviceId = "device-new2";
    QCOMPARE(int(transferRepository.queryTransfers(query, 10, &error).size()), 0);
}

// 工具方法：在临时目录中打开（或创建）核心数据库
std::unique_ptr<gy::storage::DatabaseCore> TestStorageDevice::openDatabase(
    const QString &relativePath)
{
    return gy::test::openDatabase(_temporaryDir.path(), relativePath);
}

// 工具方法：构造一份固定的核心设备记录
gy::storage::PeerRecord TestStorageDevice::makeRecord(const QString &deviceId,
                                                      const QString &name, int64_t seenMs)
{
    gy::storage::PeerRecord record;
    record.deviceId = deviceId.toStdString();
    record.deviceName = name.toStdString();
    record.lastIpAddress = "192.168.1.10";
    record.lastTcpPort = 35100;
    record.firstSeenAtMs = seenMs;
    record.lastSeenAtMs = seenMs;
    return record;
}

// 工具方法：构造一条指定会话的入站核心消息记录
gy::storage::MessageRecord TestStorageDevice::makeMessage(const QString &deviceId,
                                                          const QString &messageId,
                                                          int64_t sentMs)
{
    gy::storage::MessageRecord record;
    record.messageId = messageId.toStdString();
    record.peerDeviceId = deviceId.toStdString();
    record.direction = gy::storage::RecordDirection::Incoming;
    record.senderDeviceId = deviceId.toStdString();
    record.senderName = "Sender";
    record.content = "级联删除测试消息";
    record.sentAtMs = sentMs;
    record.localStatus = 1;
    record.createdAtMs = sentMs;
    return record;
}

// 工具方法：构造一条已完成的核心传输历史记录
gy::storage::TransferRecord TestStorageDevice::makeTransfer(const QString &deviceId,
                                                            const QString &recordId,
                                                            int64_t startedMs)
{
    gy::storage::TransferRecord record;
    record.recordId = recordId.toStdString();
    record.sessionId = (recordId + "-session").toStdString();
    record.peerDeviceId = deviceId.toStdString();
    record.peerName = "Peer";
    record.direction = gy::storage::RecordDirection::Incoming;
    record.displayName = "file.txt";
    record.fileCount = 1;
    record.totalBytes = 100;
    record.status = gy::storage::kTransferStatusCompleted;
    record.startedAtMs = startedMs;
    record.finishedAtMs = startedMs + 10000;
    return record;
}

QTEST_MAIN(TestStorageDevice)
#include "test_storage_device.moc"
