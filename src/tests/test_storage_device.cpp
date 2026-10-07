/**
* @file    test_storage_device.cpp
* @version 7.21.2
* @date 2026-10-06
* @author  GY
* @brief   SQLite 设备目录 Repository 测试
*
* 覆盖设备 upsert 幂等、聊天/传输活动时间更新、最近设备排序、
* 相同发现快照节流和数据库重新打开后的设备目录恢复。
*/

#include <QtTest/QtTest>

#include "application_paths.h"
#include "db_seed.h"
#include "protocol.h"
#include "sqlite_database_broker.h"
#include "sqlite_device_repository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

class TestStorageDevice : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testUpsertInsertThenUpdate();
    void testMarkChatActivity();
    void testMarkTransferActivity();
    void testRecentPeersOrderByActivity();
    void testDiscoveryThrottle();
    void testReopenDatabase();
    void testSetDevicePinnedAndHidden();
    void testSetDeviceAlias();
    void testDeleteDeviceWithHistoryCascade();
    void testV2MigratedToV3KeepsManagementColumns();
    void testSetDeviceFavoriteAndRecentOrder();

private:
    // 在当前测试数据库上构造一个已初始化的设备 Repository
    std::unique_ptr<SqliteDatabaseBroker> openDatabase(const QString &relativePath);
    // 构造一份带固定字段的设备记录
    PeerRecord makeRecord(const QString &deviceId, const QString &name, const QDateTime &seen);
    // 构造一条指定会话的入站聊天消息记录
    MessageRecord makeMessage(const QString &deviceId, const QString &messageId,
                              const QDateTime &sentAt);
    // 构造一条已完成的传输历史记录
    TransferRecord makeTransfer(const QString &deviceId, const QString &recordId,
                                const QDateTime &startedAt);
    // 直接读取 peer_devices 行数，用于幂等校验
    int peerRowCount(SqliteDatabaseBroker &database);

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

// 首次 upsert 写入新设备，同 device_id 的二次 upsert 更新快照而不产生重复行
void TestStorageDevice::testUpsertInsertThenUpdate()
{
    auto database = openDatabase("insert-update.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    const QDateTime firstSeen = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);

    const PeerRecord first = makeRecord("device-A", "Alpha", firstSeen);
    QString error;
    QVERIFY2(repository.upsertPeer(first, &error), qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);

    // 同 device_id 不同名称、IP，应触发更新而不是插入
    PeerRecord updated = first;
    updated.deviceName = "Alpha-Renamed";
    updated.lastIpAddress = "192.168.1.55";
    updated.lastSeenAt = firstSeen.addSecs(60);
    QVERIFY2(repository.upsertPeer(updated, &error), qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);

    // recentPeers 应反映最新名称和 IP
    const QList<PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-A"));
    QCOMPARE(records.first().deviceName, QStringLiteral("Alpha-Renamed"));
    QCOMPARE(records.first().lastIpAddress, QStringLiteral("192.168.1.55"));
}

// markChatActivity 只更新 last_chat_at，不改变其他字段
void TestStorageDevice::testMarkChatActivity()
{
    auto database = openDatabase("chat-activity.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    const QDateTime base = QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs);
    const PeerRecord seed = makeRecord("device-B", "Bravo", base);
    QString error;
    QVERIFY2(repository.upsertPeer(seed, &error), qPrintable(error));

    // 名称变更触发非节流路径，确保 seed 已写入
    PeerRecord renamed = seed;
    renamed.deviceName = "Bravo-2";
    QVERIFY2(repository.upsertPeer(renamed, &error), qPrintable(error));

    const QDateTime chatTime = base.addSecs(120);
    QVERIFY2(repository.markChatActivity("device-B", chatTime, &error), qPrintable(error));

    const QList<PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().lastChatAt.toUTC(), chatTime.toUTC());
    // 其他字段保持 upsert 后的值
    QCOMPARE(records.first().deviceName, QStringLiteral("Bravo-2"));
    QVERIFY(!records.first().lastTransferAt.isValid());
}

// markTransferActivity 只更新 last_transfer_at，不改变其他字段
void TestStorageDevice::testMarkTransferActivity()
{
    auto database = openDatabase("transfer-activity.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    const QDateTime base = QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs);
    const PeerRecord seed = makeRecord("device-C", "Charlie", base);
    QString error;
    QVERIFY2(repository.upsertPeer(seed, &error), qPrintable(error));
    QVERIFY2(repository.markChatActivity("device-C", base.addSecs(60), &error), qPrintable(error));

    const QDateTime transferTime = base.addSecs(300);
    QVERIFY2(repository.markTransferActivity("device-C", transferTime, &error), qPrintable(error));

    const QList<PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().lastTransferAt.toUTC(), transferTime.toUTC());
    // 聊天时间不受影响
    QCOMPARE(records.first().lastChatAt.toUTC(), base.addSecs(60).toUTC());
}

// recentPeers 按 COALESCE(last_chat_at, last_transfer_at, last_seen_at) DESC 排序
void TestStorageDevice::testRecentPeersOrderByActivity()
{
    auto database = openDatabase("recent-order.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    QString error;

    // 三个设备初始 last_seen 各不相同
    const QDateTime t1 = QDateTime::fromString("2026-06-25T09:00:00.000Z", Qt::ISODateWithMs);
    const QDateTime t2 = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);
    const QDateTime t3 = QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs);

    PeerRecord a = makeRecord("device-A", "Alpha", t1);
    PeerRecord b = makeRecord("device-B", "Bravo", t2);
    PeerRecord c = makeRecord("device-C", "Charlie", t3);
    QVERIFY2(repository.upsertPeer(a, &error), qPrintable(error));
    QVERIFY2(repository.upsertPeer(b, &error), qPrintable(error));
    QVERIFY2(repository.upsertPeer(c, &error), qPrintable(error));

    // 让 A 的聊天时间最新，应排到第一位
    const QDateTime aChat = QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs);
    QVERIFY2(repository.markChatActivity("device-A", aChat, &error), qPrintable(error));

    const QList<PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(records.size(), 3);
    QCOMPARE(records.at(0).deviceId, QStringLiteral("device-A"));
    // C 的 last_seen 比 B 新，排在 B 前面
    QCOMPARE(records.at(1).deviceId, QStringLiteral("device-C"));
    QCOMPARE(records.at(2).deviceId, QStringLiteral("device-B"));

    // limit 生效
    const QList<PeerRecord> topTwo = repository.recentPeers(2, &error);
    QCOMPARE(topTwo.size(), 2);
    QCOMPARE(topTwo.first().deviceId, QStringLiteral("device-A"));
}

// 同一无变化发现快照在节流间隔内不重复写入磁盘
void TestStorageDevice::testDiscoveryThrottle()
{
    auto database = openDatabase("throttle.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    const QDateTime firstSeen = QDateTime::fromString("2026-06-25T13:00:00.000Z", Qt::ISODateWithMs);
    const PeerRecord seed = makeRecord("device-D", "Delta", firstSeen);
    QString error;
    QVERIFY2(repository.upsertPeer(seed, &error), qPrintable(error));

    // 直接在外部把 last_chat_at 改成一个可识别的值，作为节流是否生效的探针
    const QDateTime probeTime = QDateTime::fromString("2026-06-25T13:30:00.000Z", Qt::ISODateWithMs);
    QVERIFY2(repository.markChatActivity("device-D", probeTime, &error), qPrintable(error));

    // 节流窗口内（< 30s）再次 upsert 完全相同的发现快照
    PeerRecord heartbeat = seed;
    heartbeat.lastSeenAt = firstSeen.addSecs(5);  // 远小于 30 秒
    QVERIFY2(repository.upsertPeer(heartbeat, &error), qPrintable(error));

    // 探针时间应保持不变，说明节流跳过了 upsert 的潜在覆盖
    const QList<PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().lastChatAt.toUTC(), probeTime.toUTC());

    // 超过节流窗口后再次 upsert，应执行写入并刷新 last_seen_at
    PeerRecord later = seed;
    later.lastSeenAt = firstSeen.addSecs(60);  // 超过 30 秒
    QVERIFY2(repository.upsertPeer(later, &error), qPrintable(error));
    const QList<PeerRecord> afterWindow = repository.recentPeers(5, &error);
    QCOMPARE(afterWindow.size(), 1);
    QCOMPARE(afterWindow.first().lastSeenAt.toUTC(), later.lastSeenAt.toUTC());
}

// 关闭并重新打开数据库后，设备目录仍可恢复
void TestStorageDevice::testReopenDatabase()
{
    const QString path = _temporaryDir.path() + "/reopen.sqlite";

    {
        SqliteDatabaseBroker database;
        QString error;
        QVERIFY2(database.initialize(path, &error), qPrintable(error));
        SqliteDeviceRepository repository(&database);
        const QDateTime seen = QDateTime::fromString("2026-06-25T14:00:00.000Z", Qt::ISODateWithMs);
        const PeerRecord record = makeRecord("device-E", "Echo", seen);
        QVERIFY2(repository.upsertPeer(record, &error), qPrintable(error));
        QVERIFY2(repository.markChatActivity("device-E", seen.addSecs(120), &error), qPrintable(error));
    }

    // 重新打开数据库，设备目录和活动时间应保留
    SqliteDatabaseBroker reopened;
    QString error;
    QVERIFY2(reopened.initialize(path, &error), qPrintable(error));
    SqliteDeviceRepository repository(&reopened);
    const QList<PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-E"));
    QCOMPARE(records.first().deviceName, QStringLiteral("Echo"));
    QVERIFY(records.first().lastChatAt.isValid());
}

// 置顶与隐藏状态落库、幂等、心跳不清状态，且 Step 变体可组合进单事务
void TestStorageDevice::testSetDevicePinnedAndHidden()
{
    auto database = openDatabase("pin-hide.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    const QDateTime base = QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs);
    QString error;
    QVERIFY2(repository.upsertPeer(makeRecord("device-pin", "Pinned", base), &error),
             qPrintable(error));

    // 置顶与隐藏各自落库，读链路带出状态
    QVERIFY2(repository.setDevicePinned("device-pin", true, &error), qPrintable(error));
    QVERIFY2(repository.setDeviceHidden("device-pin", true, &error), qPrintable(error));
    QList<PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QVERIFY(records.first().pinned);
    QVERIFY(records.first().hidden);

    // 重复设置幂等，不产生重复行也不报错
    QVERIFY2(repository.setDevicePinned("device-pin", true, &error), qPrintable(error));
    QVERIFY2(repository.setDeviceHidden("device-pin", true, &error), qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);
    records = repository.recentPeers(5, &error);
    QVERIFY(records.first().pinned);
    QVERIFY(records.first().hidden);

    // 设备行不存在的更新同样幂等返回成功
    QVERIFY2(repository.setDevicePinned("no-such-device", true, &error), qPrintable(error));
    QVERIFY2(repository.setDeviceHidden("no-such-device", true, &error), qPrintable(error));

    // 心跳 upsert 不清管理状态（ON CONFLICT 不触碰三列的既有契约）
    PeerRecord heartbeat = makeRecord("device-pin", "Pinned", base.addSecs(60));
    QVERIFY2(repository.upsertPeer(heartbeat, &error), qPrintable(error));
    records = repository.recentPeers(5, &error);
    QVERIFY(records.first().pinned);
    QVERIFY(records.first().hidden);

    // 取消置顶与恢复显示
    QVERIFY2(repository.setDevicePinned("device-pin", false, &error), qPrintable(error));
    QVERIFY2(repository.setDeviceHidden("device-pin", false, &error), qPrintable(error));
    records = repository.recentPeers(5, &error);
    QVERIFY(!records.first().pinned);
    QVERIFY(!records.first().hidden);

    // Step 变体可与其他步骤组合进同一事务执行
    QVERIFY2(database->runSteps(
                 {SqliteDeviceRepository::setDevicePinnedStep("device-pin", true),
                  SqliteDeviceRepository::setDeviceHiddenStep("device-pin", true)},
                 &error),
             qPrintable(error));
    records = repository.recentPeers(5, &error);
    QVERIFY(records.first().pinned);
    QVERIFY(records.first().hidden);
}

// 备注别名设置、覆盖与清除幂等，心跳 upsert 不触碰备注列
void TestStorageDevice::testSetDeviceAlias()
{
    auto database = openDatabase("alias.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository repository(database.get());
    const QDateTime base = QDateTime::fromString("2026-06-25T17:00:00.000Z", Qt::ISODateWithMs);
    QString error;
    QVERIFY2(repository.upsertPeer(makeRecord("device-alias", "Alias", base), &error),
             qPrintable(error));

    // 设置备注后读链路带出
    QVERIFY2(repository.setDeviceAlias("device-alias", QStringLiteral("老王"), &error),
             qPrintable(error));
    QList<PeerRecord> records = repository.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().alias, QStringLiteral("老王"));

    // 重复设置覆盖旧值且幂等，不产生重复行
    QVERIFY2(repository.setDeviceAlias("device-alias", QStringLiteral("小李"), &error),
             qPrintable(error));
    QVERIFY2(repository.setDeviceAlias("device-alias", QStringLiteral("小李"), &error),
             qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);
    records = repository.recentPeers(5, &error);
    QCOMPARE(records.first().alias, QStringLiteral("小李"));

    // 设备行不存在的更新同样幂等返回成功
    QVERIFY2(repository.setDeviceAlias("no-such-device", QStringLiteral("幽灵"), &error),
             qPrintable(error));

    // 心跳 upsert 变更设备名与 IP 后备注保持：ON CONFLICT 不触碰 alias 列
    PeerRecord heartbeat = makeRecord("device-alias", "Alias-Renamed", base.addSecs(60));
    heartbeat.lastIpAddress = QStringLiteral("192.168.1.99");
    QVERIFY2(repository.upsertPeer(heartbeat, &error), qPrintable(error));
    records = repository.recentPeers(5, &error);
    QCOMPARE(records.first().deviceName, QStringLiteral("Alias-Renamed"));
    QCOMPARE(records.first().lastIpAddress, QStringLiteral("192.168.1.99"));
    QCOMPARE(records.first().alias, QStringLiteral("小李"));

    // 清除备注（空串）后回落空值
    QVERIFY2(repository.setDeviceAlias("device-alias", QString(), &error), qPrintable(error));
    records = repository.recentPeers(5, &error);
    QVERIFY(records.first().alias.isEmpty());

    // Step 变体可组合进同一事务执行
    QVERIFY2(database->runSteps(
                 {SqliteDeviceRepository::setDeviceAliasStep("device-alias",
                                                             QStringLiteral("事务备注"))},
                 &error),
             qPrintable(error));
    records = repository.recentPeers(5, &error);
    QCOMPARE(records.first().alias, QStringLiteral("事务备注"));
}

// 删除设备级联清空聊天与传输历史且不触碰文件系统，再次发现按全新设备入目录
void TestStorageDevice::testDeleteDeviceWithHistoryCascade()
{
    auto database = openDatabase("delete-cascade.sqlite");
    QVERIFY(database);

    SqliteDeviceRepository deviceRepository(database.get());
    SqliteMessageRepository messageRepository(database.get());
    SqliteTransferHistoryRepository transferRepository(database.get());
    QString error;

    const QDateTime base = QDateTime::fromString("2026-06-25T16:00:00.000Z", Qt::ISODateWithMs);
    QVERIFY2(deviceRepository.upsertPeer(makeRecord("device-del", "DeleteMe", base), &error),
             qPrintable(error));
    QVERIFY2(deviceRepository.upsertPeer(makeRecord("device-keep", "KeepMe", base), &error),
             qPrintable(error));

    // 被删设备两条聊天消息（会话行由 saveMessageStep 幂等创建）与两条传输历史
    QVERIFY2(messageRepository.saveMessage(makeMessage("device-del", "msg-1", base), &error),
             qPrintable(error));
    QVERIFY2(messageRepository.saveMessage(makeMessage("device-del", "msg-2", base.addSecs(60)), &error),
             qPrintable(error));
    QVERIFY2(transferRepository.upsertFinishedTransfer(makeTransfer("device-del", "tr-1", base), &error),
             qPrintable(error));
    QVERIFY2(transferRepository.upsertFinishedTransfer(makeTransfer("device-del", "tr-2", base.addSecs(30)), &error),
             qPrintable(error));
    // 保留设备同样有数据，用于验证删除不误伤
    QVERIFY2(messageRepository.saveMessage(makeMessage("device-keep", "msg-k", base), &error),
             qPrintable(error));
    QVERIFY2(transferRepository.upsertFinishedTransfer(makeTransfer("device-keep", "tr-k", base), &error),
             qPrintable(error));

    // 删除路径只涉及数据库行，不触碰任何本地文件
    QVERIFY2(deviceRepository.deleteDeviceWithHistory("device-del", &error), qPrintable(error));

    // 级联完整：设备行删除、传输历史清空、聊天会话与消息随 CASCADE 清空，
    // 保留设备的数据一行不少
    QCOMPARE(peerRowCount(*database), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_messages")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_conversations")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("transfer_history")), 1);

    // 幂等：设备行已不存在时再次删除仍返回成功
    QVERIFY2(deviceRepository.deleteDeviceWithHistory("device-del", &error), qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);

    const QList<PeerRecord> records = deviceRepository.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-keep"));

    // 删除后同快照心跳不被节流缓存拦截，按全新设备重新入目录（first_seen 语义），
    // 且目录重建不会带回任何旧聊天或传输历史
    deviceRepository.noteDeviceDeleted("device-del");
    QVERIFY2(deviceRepository.upsertPeer(makeRecord("device-del", "DeleteMe", base), &error),
             qPrintable(error));
    QCOMPARE(peerRowCount(*database), 2);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_messages")), 1);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("transfer_history")), 1);
}

// v2 存量库升级到 v3：favorite 加列默认 0，alias/pinned/hidden 与快照字段全部保留
void TestStorageDevice::testV2MigratedToV3KeepsManagementColumns()
{
    const QString path = _databasePath + ".v2-upgrade.sqlite";

    // 手工造一个最小 v2 假库：schema_version 登记 2，peer_devices 含管理三列并带存量值
    {
        QSqlDatabase seed = QSqlDatabase::addDatabase("QSQLITE", "v2-seed");
        seed.setDatabaseName(path);
        QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
        QSqlQuery build(seed);
        QVERIFY2(build.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)"),
                 qPrintable(build.lastError().text()));
        QVERIFY2(build.exec("INSERT INTO schema_version VALUES(2, '2026-10-05T00:00:00.000Z')"),
                 qPrintable(build.lastError().text()));
        QVERIFY2(build.exec("CREATE TABLE peer_devices (device_id TEXT PRIMARY KEY NOT NULL, device_name TEXT NOT NULL, last_ip_address TEXT, last_tcp_port INTEGER, first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL, last_chat_at TEXT, last_transfer_at TEXT, alias TEXT, pinned INTEGER NOT NULL DEFAULT 0, hidden INTEGER NOT NULL DEFAULT 0)"),
                 qPrintable(build.lastError().text()));
        QVERIFY2(build.exec("INSERT INTO peer_devices VALUES('device-v2', '存量设备', '192.168.1.30', 35100, "
                            "'2026-06-25T08:00:00.000Z', '2026-06-25T09:00:00.000Z', NULL, NULL, "
                            "'老王的电脑', 1, 0)"),
                 qPrintable(build.lastError().text()));
        seed.close();
    }
    QSqlDatabase::removeDatabase("v2-seed");

    SqliteDatabaseBroker database;
    QString error;
    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 3);

    SqliteDeviceRepository repository(&database);
    const QList<PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(records.size(), 1);
    const PeerRecord &record = records.first();
    // 管理三列与快照字段在 v2 到 v3 迁移后必须原样保留，收藏默认未勾选
    QCOMPARE(record.deviceId, QStringLiteral("device-v2"));
    QCOMPARE(record.alias, QStringLiteral("老王的电脑"));
    QCOMPARE(record.pinned, true);
    QCOMPARE(record.hidden, false);
    QCOMPARE(record.favorite, false);
}

// 收藏读写与排序：favorite 优先于普通设备恢复，心跳 upsert 不清收藏，取消后回落
void TestStorageDevice::testSetDeviceFavoriteAndRecentOrder()
{
    auto database = openDatabase("favorite.sqlite");
    QVERIFY(database);
    SqliteDeviceRepository repository(database.get());

    const QDateTime base = QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs);
    QString error;
    // 普通设备比收藏设备更新，且心跳后的收藏设备仍须保持更旧：
    // 取消收藏后回落排序的判别才成立
    QVERIFY2(repository.upsertPeer(makeRecord("device-plain", "普通设备", base.addSecs(120)), &error),
             qPrintable(error));
    QVERIFY2(repository.upsertPeer(makeRecord("device-fav", "收藏设备", base), &error),
             qPrintable(error));

    QVERIFY2(repository.setDeviceFavorite("device-fav", true, &error), qPrintable(error));

    // 收藏设备反超排最前，favorite 标记随行返回
    QList<PeerRecord> records = repository.recentPeers(10, &error);
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-fav"));
    QCOMPARE(records.first().favorite, true);
    QCOMPARE(records.at(1).deviceId, QStringLiteral("device-plain"));
    QCOMPARE(records.at(1).favorite, false);

    // 心跳改名 upsert 不携带 favorite 列：收藏按 device_id 落键，远程改名不清收藏
    // （活跃时间保持在普通设备之前）
    PeerRecord heartbeat = makeRecord("device-fav", "收藏设备已改名", base.addSecs(60));
    QVERIFY2(repository.upsertPeer(heartbeat, &error), qPrintable(error));
    records = repository.recentPeers(10, &error);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-fav"));
    QCOMPARE(records.first().deviceName, QStringLiteral("收藏设备已改名"));
    QCOMPARE(records.first().favorite, true);

    // 取消收藏后回落到按活跃时间排序
    QVERIFY2(repository.setDeviceFavorite("device-fav", false, &error), qPrintable(error));
    records = repository.recentPeers(10, &error);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-plain"));
    QCOMPARE(records.first().favorite, false);
}

// 工具方法：在临时目录中创建并初始化数据库
std::unique_ptr<SqliteDatabaseBroker> TestStorageDevice::openDatabase(const QString &relativePath)
{
    return gy::test::openDatabase(_temporaryDir.path(), relativePath);
}

// 工具方法：构造一份固定的 PeerRecord
PeerRecord TestStorageDevice::makeRecord(const QString &deviceId, const QString &name,
                                         const QDateTime &seen)
{
    PeerRecord record;
    record.deviceId = deviceId;
    record.deviceName = name;
    record.lastIpAddress = "192.168.1.10";
    record.lastTcpPort = 35100;
    record.firstSeenAt = seen;
    record.lastSeenAt = seen;
    return record;
}

// 工具方法：构造一条指定会话的入站聊天消息记录
MessageRecord TestStorageDevice::makeMessage(const QString &deviceId, const QString &messageId,
                                             const QDateTime &sentAt)
{
    MessageRecord record;
    record.messageId = messageId;
    record.peerDeviceId = deviceId;
    record.direction = RecordDirection::Incoming;
    record.senderDeviceId = deviceId;
    record.senderName = "Sender";
    record.content = "级联删除测试消息";
    record.sentAt = sentAt;
    record.localStatus = 1;
    record.createdAt = sentAt;
    return record;
}

// 工具方法：构造一条已完成的传输历史记录
TransferRecord TestStorageDevice::makeTransfer(const QString &deviceId, const QString &recordId,
                                               const QDateTime &startedAt)
{
    TransferRecord record;
    record.recordId = recordId;
    record.sessionId = recordId + "-session";
    record.peerDeviceId = deviceId;
    record.peerName = "Peer";
    record.direction = RecordDirection::Incoming;
    record.displayName = "file.txt";
    record.fileCount = 1;
    record.totalBytes = 100;
    record.status = gy::protocol::kTransferStatusCompleted;
    record.startedAt = startedAt;
    record.finishedAt = startedAt.addSecs(10);
    return record;
}

// 工具方法：统计 peer_devices 表行数
int TestStorageDevice::peerRowCount(SqliteDatabaseBroker &database)
{
    return gy::test::tableRowCount(database, QStringLiteral("peer_devices"));
}

QTEST_MAIN(TestStorageDevice)
#include "test_storage_device.moc"
