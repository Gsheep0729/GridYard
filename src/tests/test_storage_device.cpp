/**
* @file    test_storage_device.cpp
* @version 6.1.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 设备目录 Proxy 测试
*
* 覆盖设备 upsert 幂等、聊天/传输活动时间更新、最近设备排序、
* 相同发现快照节流和数据库重新打开后的设备目录恢复。
*
* Change Log:
* [v6.1.0] GY   2026-06-25
* * 新增设备目录 SQLite Proxy 测试
*/

#include <QtTest/QtTest>

#include "application_paths.h"
#include "sqlite_database_proxy.h"
#include "sqlite_device_proxy.h"

#include <QSqlDatabase>
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

private:
    // 在当前测试数据库上构造一个已初始化的设备 Proxy
    std::unique_ptr<SqliteDatabaseProxy> openDatabase(const QString &relativePath);
    // 构造一份带固定字段的设备记录
    PeerRecord makeRecord(const QString &deviceId, const QString &name, const QDateTime &seen);
    // 直接读取 peer_devices 行数，用于幂等校验
    int peerRowCount(SqliteDatabaseProxy &database);

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

    SqliteDeviceProxy proxy(database.get());
    const QDateTime firstSeen = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);

    const PeerRecord first = makeRecord("device-A", "Alpha", firstSeen);
    QString error;
    QVERIFY2(proxy.upsertPeer(first, &error), qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);

    // 同 device_id 不同名称、IP，应触发更新而不是插入
    PeerRecord updated = first;
    updated.deviceName = "Alpha-Renamed";
    updated.lastIpAddress = "192.168.1.55";
    updated.lastSeenAt = firstSeen.addSecs(60);
    QVERIFY2(proxy.upsertPeer(updated, &error), qPrintable(error));
    QCOMPARE(peerRowCount(*database), 1);

    // recentPeers 应反映最新名称和 IP
    const QList<PeerRecord> records = proxy.recentPeers(10, &error);
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

    SqliteDeviceProxy proxy(database.get());
    const QDateTime base = QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs);
    const PeerRecord seed = makeRecord("device-B", "Bravo", base);
    QString error;
    QVERIFY2(proxy.upsertPeer(seed, &error), qPrintable(error));

    // 名称变更触发非节流路径，确保 seed 已写入
    PeerRecord renamed = seed;
    renamed.deviceName = "Bravo-2";
    QVERIFY2(proxy.upsertPeer(renamed, &error), qPrintable(error));

    const QDateTime chatTime = base.addSecs(120);
    QVERIFY2(proxy.markChatActivity("device-B", chatTime, &error), qPrintable(error));

    const QList<PeerRecord> records = proxy.recentPeers(5, &error);
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

    SqliteDeviceProxy proxy(database.get());
    const QDateTime base = QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs);
    const PeerRecord seed = makeRecord("device-C", "Charlie", base);
    QString error;
    QVERIFY2(proxy.upsertPeer(seed, &error), qPrintable(error));
    QVERIFY2(proxy.markChatActivity("device-C", base.addSecs(60), &error), qPrintable(error));

    const QDateTime transferTime = base.addSecs(300);
    QVERIFY2(proxy.markTransferActivity("device-C", transferTime, &error), qPrintable(error));

    const QList<PeerRecord> records = proxy.recentPeers(5, &error);
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

    SqliteDeviceProxy proxy(database.get());
    QString error;

    // 三个设备初始 last_seen 各不相同
    const QDateTime t1 = QDateTime::fromString("2026-06-25T09:00:00.000Z", Qt::ISODateWithMs);
    const QDateTime t2 = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);
    const QDateTime t3 = QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs);

    PeerRecord a = makeRecord("device-A", "Alpha", t1);
    PeerRecord b = makeRecord("device-B", "Bravo", t2);
    PeerRecord c = makeRecord("device-C", "Charlie", t3);
    QVERIFY2(proxy.upsertPeer(a, &error), qPrintable(error));
    QVERIFY2(proxy.upsertPeer(b, &error), qPrintable(error));
    QVERIFY2(proxy.upsertPeer(c, &error), qPrintable(error));

    // 让 A 的聊天时间最新，应排到第一位
    const QDateTime aChat = QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs);
    QVERIFY2(proxy.markChatActivity("device-A", aChat, &error), qPrintable(error));

    const QList<PeerRecord> records = proxy.recentPeers(10, &error);
    QCOMPARE(records.size(), 3);
    QCOMPARE(records.at(0).deviceId, QStringLiteral("device-A"));
    // C 的 last_seen 比 B 新，排在 B 前面
    QCOMPARE(records.at(1).deviceId, QStringLiteral("device-C"));
    QCOMPARE(records.at(2).deviceId, QStringLiteral("device-B"));

    // limit 生效
    const QList<PeerRecord> topTwo = proxy.recentPeers(2, &error);
    QCOMPARE(topTwo.size(), 2);
    QCOMPARE(topTwo.first().deviceId, QStringLiteral("device-A"));
}

// 同一无变化发现快照在节流间隔内不重复写入磁盘
void TestStorageDevice::testDiscoveryThrottle()
{
    auto database = openDatabase("throttle.sqlite");
    QVERIFY(database);

    SqliteDeviceProxy proxy(database.get());
    const QDateTime firstSeen = QDateTime::fromString("2026-06-25T13:00:00.000Z", Qt::ISODateWithMs);
    const PeerRecord seed = makeRecord("device-D", "Delta", firstSeen);
    QString error;
    QVERIFY2(proxy.upsertPeer(seed, &error), qPrintable(error));

    // 直接在外部把 last_chat_at 改成一个可识别的值，作为节流是否生效的探针
    const QDateTime probeTime = QDateTime::fromString("2026-06-25T13:30:00.000Z", Qt::ISODateWithMs);
    QVERIFY2(proxy.markChatActivity("device-D", probeTime, &error), qPrintable(error));

    // 节流窗口内（< 30s）再次 upsert 完全相同的发现快照
    PeerRecord heartbeat = seed;
    heartbeat.lastSeenAt = firstSeen.addSecs(5);  // 远小于 30 秒
    QVERIFY2(proxy.upsertPeer(heartbeat, &error), qPrintable(error));

    // 探针时间应保持不变，说明节流跳过了 upsert 的潜在覆盖
    const QList<PeerRecord> records = proxy.recentPeers(5, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().lastChatAt.toUTC(), probeTime.toUTC());

    // 超过节流窗口后再次 upsert，应执行写入并刷新 last_seen_at
    PeerRecord later = seed;
    later.lastSeenAt = firstSeen.addSecs(60);  // 超过 30 秒
    QVERIFY2(proxy.upsertPeer(later, &error), qPrintable(error));
    const QList<PeerRecord> afterWindow = proxy.recentPeers(5, &error);
    QCOMPARE(afterWindow.size(), 1);
    QCOMPARE(afterWindow.first().lastSeenAt.toUTC(), later.lastSeenAt.toUTC());
}

// 关闭并重新打开数据库后，设备目录仍可恢复
void TestStorageDevice::testReopenDatabase()
{
    const QString path = _temporaryDir.path() + "/reopen.sqlite";

    {
        SqliteDatabaseProxy database;
        QString error;
        QVERIFY2(database.initialize(path, &error), qPrintable(error));
        SqliteDeviceProxy proxy(&database);
        const QDateTime seen = QDateTime::fromString("2026-06-25T14:00:00.000Z", Qt::ISODateWithMs);
        const PeerRecord record = makeRecord("device-E", "Echo", seen);
        QVERIFY2(proxy.upsertPeer(record, &error), qPrintable(error));
        QVERIFY2(proxy.markChatActivity("device-E", seen.addSecs(120), &error), qPrintable(error));
    }

    // 重新打开数据库，设备目录和活动时间应保留
    SqliteDatabaseProxy reopened;
    QString error;
    QVERIFY2(reopened.initialize(path, &error), qPrintable(error));
    SqliteDeviceProxy proxy(&reopened);
    const QList<PeerRecord> records = proxy.recentPeers(10, &error);
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().deviceId, QStringLiteral("device-E"));
    QCOMPARE(records.first().deviceName, QStringLiteral("Echo"));
    QVERIFY(records.first().lastChatAt.isValid());
}

// 工具方法：在临时目录中创建并初始化数据库
std::unique_ptr<SqliteDatabaseProxy> TestStorageDevice::openDatabase(const QString &relativePath)
{
    auto database = std::make_unique<SqliteDatabaseProxy>();
    const QString path = _temporaryDir.path() + "/" + relativePath;
    QString error;
    if (!database->initialize(path, &error)) {
        qWarning() << "数据库初始化失败:" << error;
        return nullptr;
    }
    return database;
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

// 工具方法：统计 peer_devices 表行数
int TestStorageDevice::peerRowCount(SqliteDatabaseProxy &database)
{
    QString error;
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    if (!connection.isValid())
        return -1;
    QSqlQuery query(connection);
    if (!query.exec("SELECT COUNT(*) FROM peer_devices"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

QTEST_MAIN(TestStorageDevice)
#include "test_storage_device.moc"
