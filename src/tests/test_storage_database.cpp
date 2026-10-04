/**
* @file    test_storage_database.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   SQLite 初始化、迁移与异常恢复测试
*
* 使用临时数据库验证首次建库、重复初始化、缺失驱动、锁竞争和损坏库重建。
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
* * 新增 v1 存量库升级 v2 与 upsert 保持管理三列两用例
* * 版本守卫用例改为造 v3 假库，既有版本断言对齐 v2
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 损坏库用例补充重建标志、备份路径与属性透出断言
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
* [v7.15.6] GY 2026-10-03
* * 补充库版本高于支持上限时拒绝打开的守卫用例
* [v6.6.0] GY 2026-06-25
* * 补充 Stage 6 阶段 G 的数据库异常验收场景
* [v6.0.0] GY 2026-06-25
* * 新增 SQLite migration 基础测试
*/

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "application_paths.h"
#include "local_data_broker.h"
#include "sqlite_database_broker.h"
#include "sqlite_device_repository.h"

class TestStorageDatabase : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInitializeAndSchema();
    void testRepeatedInitializePreservesData();
    void testV1MigratedToV2KeepsRows();
    void testUpsertPeerKeepsManagementColumns();
    void testMissingDriverDegrades();
    void testCorruptDatabaseBackedUpAndRebuilt();
    void testLockedDatabaseWriteFailsButBrokerStaysAvailable();
    void testNewerSchemaVersionRejected();

private:
    bool tableExists(QSqlDatabase &connection, const QString &tableName);

    QTemporaryDir _temporaryDir;
};

void TestStorageDatabase::initTestCase()
{
    QVERIFY(_temporaryDir.isValid());
    ApplicationPaths::coverForTest(_temporaryDir.path());
}

void TestStorageDatabase::cleanupTestCase()
{
    ApplicationPaths::clearTestCover();
}

void TestStorageDatabase::testInitializeAndSchema()
{
    SqliteDatabaseBroker database;
    QString error;
    const QString path = ApplicationPaths::databaseDir() + "/gridyard-history.sqlite";

    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 2);
    QVERIFY(QFileInfo::exists(path));

    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QVERIFY(tableExists(connection, "peer_devices"));
    QVERIFY(tableExists(connection, "chat_conversations"));
    QVERIFY(tableExists(connection, "chat_messages"));
    QVERIFY(tableExists(connection, "transfer_history"));
}

void TestStorageDatabase::testRepeatedInitializePreservesData()
{
    QString error;
    const QString path = _temporaryDir.path() + "/repeat.sqlite";

    {
        SqliteDatabaseBroker database;
        QVERIFY2(database.initialize(path, &error), qPrintable(error));
        QSqlDatabase connection = database.connectionForWorkerThread(&error);
        QVERIFY2(connection.isValid(), qPrintable(error));

        QSqlQuery insert(connection);
        insert.prepare("INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                       "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?)");
        insert.addBindValue("device-repeat");
        insert.addBindValue("重复初始化设备");
        insert.addBindValue("127.0.0.1");
        insert.addBindValue(35100);
        insert.addBindValue("2026-06-25T00:00:00.000Z");
        insert.addBindValue("2026-06-25T00:00:00.000Z");
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
        QSqlQuery checkpoint(connection);
        QVERIFY2(checkpoint.exec("PRAGMA wal_checkpoint(TRUNCATE)"),
                 qPrintable(checkpoint.lastError().text()));
    }

    SqliteDatabaseBroker database;
    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 2);

    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QSqlQuery count(connection);
    QVERIFY(count.exec("SELECT COUNT(*) FROM peer_devices WHERE device_id='device-repeat'"));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

// v1 存量库升级到 v2：三列存在、默认值正确、存量行数据完整
void TestStorageDatabase::testV1MigratedToV2KeepsRows()
{
    const QString path = _temporaryDir.path() + "/v1-upgrade.sqlite";

    // 手工造一个最小 v1 假库：schema_version 登记 1，peer_devices 为旧列布局并带存量行
    {
        QSqlDatabase seed = QSqlDatabase::addDatabase("QSQLITE", "v1-seed");
        seed.setDatabaseName(path);
        QVERIFY2(seed.open(), qPrintable(seed.lastError().text()));
        QSqlQuery build(seed);
        QVERIFY2(build.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)"),
                 qPrintable(build.lastError().text()));
        QVERIFY2(build.exec("INSERT INTO schema_version VALUES(1, '2026-06-25T00:00:00.000Z')"),
                 qPrintable(build.lastError().text()));
        QVERIFY2(build.exec("CREATE TABLE peer_devices (device_id TEXT PRIMARY KEY NOT NULL, device_name TEXT NOT NULL, last_ip_address TEXT, last_tcp_port INTEGER, first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL, last_chat_at TEXT, last_transfer_at TEXT)"),
                 qPrintable(build.lastError().text()));
        QVERIFY2(build.exec("INSERT INTO peer_devices VALUES('device-v1', '存量设备', '192.168.1.20', 35100, "
                            "'2026-06-25T08:00:00.000Z', '2026-06-25T09:00:00.000Z', "
                            "'2026-06-25T08:30:00.000Z', NULL)"),
                 qPrintable(build.lastError().text()));
        seed.close();
    }
    QSqlDatabase::removeDatabase("v1-seed");

    SqliteDatabaseBroker database;
    QString error;
    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 2);

    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));

    QSqlQuery check(connection);
    QVERIFY2(check.exec("SELECT alias, pinned, hidden, device_name, last_ip_address, last_tcp_port, "
                        "first_seen_at, last_seen_at, last_chat_at, last_transfer_at "
                        "FROM peer_devices WHERE device_id='device-v1'"),
             qPrintable(check.lastError().text()));
    QVERIFY(check.next());
    QVERIFY(check.isNull(0));  // alias 加列后存量行取 NULL
    QCOMPARE(check.value(1).toInt(), 0);  // pinned 默认 0
    QCOMPARE(check.value(2).toInt(), 0);  // hidden 默认 0
    // 存量行原字段在迁移后必须原样保留
    QCOMPARE(check.value(3).toString(), QStringLiteral("存量设备"));
    QCOMPARE(check.value(4).toString(), QStringLiteral("192.168.1.20"));
    QCOMPARE(check.value(5).toInt(), 35100);
    QCOMPARE(check.value(6).toString(), QStringLiteral("2026-06-25T08:00:00.000Z"));
    QCOMPARE(check.value(7).toString(), QStringLiteral("2026-06-25T09:00:00.000Z"));
    QCOMPARE(check.value(8).toString(), QStringLiteral("2026-06-25T08:30:00.000Z"));
    QVERIFY(check.isNull(9));

    // 读链路带出新列：恢复记录的三列与 SQL 直查一致
    SqliteDeviceRepository repository(&database);
    const QList<PeerRecord> peers = repository.recentPeers(10, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(peers.size(), 1);
    QCOMPARE(peers.first().deviceId, QStringLiteral("device-v1"));
    QCOMPARE(peers.first().deviceName, QStringLiteral("存量设备"));
    QVERIFY(peers.first().alias.isEmpty());
    QVERIFY(!peers.first().pinned);
    QVERIFY(!peers.first().hidden);
}

// 心跳/发现更新不得清除用户管理状态：upsert 后三列保持原值
void TestStorageDatabase::testUpsertPeerKeepsManagementColumns()
{
    SqliteDatabaseBroker database;
    QString error;
    const QString path = _temporaryDir.path() + "/upsert-keep.sqlite";
    QVERIFY2(database.initialize(path, &error), qPrintable(error));

    SqliteDeviceRepository repository(&database);
    PeerRecord peer;
    peer.deviceId = "device-manage";
    peer.deviceName = "原始名";
    peer.lastIpAddress = "192.168.1.10";
    peer.lastTcpPort = 35100;
    peer.firstSeenAt = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);
    peer.lastSeenAt = peer.firstSeenAt;
    QVERIFY2(repository.upsertPeer(peer, &error), qPrintable(error));

    // 直接落库模拟用户管理状态（管理接口属于 PhaseN2-B），非默认值便于观察被回退
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QSqlQuery mark(connection);
    QVERIFY2(mark.exec("UPDATE peer_devices SET alias='旧友备注', pinned=1, hidden=1 "
                       "WHERE device_id='device-manage'"),
             qPrintable(mark.lastError().text()));

    // 心跳更新设备名与 IP，名称/IP 变化绕开发现节流，必然写盘
    PeerRecord heartbeat = peer;
    heartbeat.deviceName = "改名后";
    heartbeat.lastIpAddress = "192.168.1.11";
    heartbeat.lastSeenAt = peer.lastSeenAt.addSecs(60);
    QVERIFY2(repository.upsertPeer(heartbeat, &error), qPrintable(error));

    QSqlQuery check(connection);
    QVERIFY2(check.exec("SELECT device_name, last_ip_address, alias, pinned, hidden "
                        "FROM peer_devices WHERE device_id='device-manage'"),
             qPrintable(check.lastError().text()));
    QVERIFY(check.next());
    QCOMPARE(check.value(0).toString(), QStringLiteral("改名后"));
    QCOMPARE(check.value(1).toString(), QStringLiteral("192.168.1.11"));
    QCOMPARE(check.value(2).toString(), QStringLiteral("旧友备注"));
    QCOMPARE(check.value(3).toInt(), 1);
    QCOMPARE(check.value(4).toInt(), 1);

    // 读链路同样还原管理三列，证明 recentPeers 映射没有丢失
    const QList<PeerRecord> peers = repository.recentPeers(10, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(peers.size(), 1);
    QCOMPARE(peers.first().deviceName, QStringLiteral("改名后"));
    QCOMPARE(peers.first().alias, QStringLiteral("旧友备注"));
    QVERIFY(peers.first().pinned);
    QVERIFY(peers.first().hidden);
}

void TestStorageDatabase::testMissingDriverDegrades()
{
    SqliteDatabaseBroker database([] { return QStringList{}; });
    QString error;

    QVERIFY(!database.initialize(_temporaryDir.path() + "/missing-driver.sqlite", &error));
    QVERIFY(!database.isAvailable());
    QVERIFY(error.contains("QSQLITE"));
}

void TestStorageDatabase::testCorruptDatabaseBackedUpAndRebuilt()
{
    const QString path = _temporaryDir.path() + "/corrupt.sqlite";
    QFile corruptFile(path);
    QVERIFY(corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corruptFile.write("this is not a sqlite database") > 0);
    corruptFile.close();

    SqliteDatabaseBroker database;
    QString error;
    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QVERIFY(database.isAvailable());
    QCOMPARE(database.schemaVersion(), 2);

    // 重建发生后必须报告标志与备份路径，供上层界面提示历史被清零重置
    QVERIFY(database.lastInitializeRebuilt());
    const QString backupPath = database.rebuiltBackupPath();
    QVERIFY(!backupPath.isEmpty());
    QVERIFY(QFileInfo(backupPath).size() > 0);

    const QFileInfo fileInfo(path);
    const QStringList backups = QDir(fileInfo.absolutePath())
        .entryList({fileInfo.fileName() + ".corrupt-*"}, QDir::Files);
    QCOMPARE(backups.size(), 1);
    // 上报的备份路径必须指向实际生成的备份文件
    QCOMPARE(backupPath, fileInfo.absolutePath() + "/" + backups.first());

    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QVERIFY(tableExists(connection, "chat_messages"));

    // 正常建库不置位重建标志，备份路径保持为空
    LocalDataBroker freshBroker;
    const QString freshPath = _temporaryDir.path() + "/fresh-rebuilt.sqlite";
    QVERIFY2(freshBroker.initialize(freshPath, &error), qPrintable(error));
    QVERIFY(!freshBroker.historyDatabaseRebuilt());
    QVERIFY(freshBroker.rebuiltBackupPath().isEmpty());
    freshBroker.closeStorage();

    // 通知属性经 LocalDataBroker 向上透出，断言置位且备份文件真实存在
    const QString rebuiltPath = _temporaryDir.path() + "/corrupt-rebuilt.sqlite";
    QFile corruptRebuiltFile(rebuiltPath);
    QVERIFY(corruptRebuiltFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corruptRebuiltFile.write("this is not a sqlite database") > 0);
    corruptRebuiltFile.close();

    LocalDataBroker rebuiltBroker;
    QVERIFY2(rebuiltBroker.initialize(rebuiltPath, &error), qPrintable(error));
    QVERIFY(rebuiltBroker.isAvailable());
    QVERIFY(rebuiltBroker.historyDatabaseRebuilt());
    QVERIFY(!rebuiltBroker.rebuiltBackupPath().isEmpty());
    QVERIFY(QFileInfo::exists(rebuiltBroker.rebuiltBackupPath()));
}

void TestStorageDatabase::testLockedDatabaseWriteFailsButBrokerStaysAvailable()
{
    SqliteDatabaseBroker database;
    QString error;
    const QString path = _temporaryDir.path() + "/locked.sqlite";
    QVERIFY2(database.initialize(path, &error), qPrintable(error));

    // 先写入一条记录作为主键冲突的种子
    const bool seedOk = database.runInTransaction(
        [](QSqlDatabase &connection, QString *taskError) {
            QSqlQuery query(connection);
            query.prepare("INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                          "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?)");
            query.addBindValue("device-dup");
            query.addBindValue("DupSeed");
            query.addBindValue("127.0.0.1");
            query.addBindValue(35100);
            query.addBindValue("2026-06-25T00:00:00.000Z");
            query.addBindValue("2026-06-25T00:00:00.000Z");
            if (query.exec()) return true;
            if (taskError) *taskError = query.lastError().text();
            return false;
        },
        &error);
    QVERIFY2(seedOk, qPrintable(error));

    // 插入相同主键应触发约束违反，runInTransaction 返回 false
    error.clear();
    const bool succeeded = database.runInTransaction(
        [](QSqlDatabase &connection, QString *taskError) {
            QSqlQuery query(connection);
            query.prepare("INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                          "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?)");
            query.addBindValue("device-dup");
            query.addBindValue("DupAgain");
            query.addBindValue("127.0.0.1");
            query.addBindValue(35100);
            query.addBindValue("2026-06-25T00:00:00.000Z");
            query.addBindValue("2026-06-25T00:00:00.000Z");
            if (query.exec()) return true;
            if (taskError) *taskError = query.lastError().text();
            return false;
        },
        &error);

    QVERIFY(!succeeded);
    QVERIFY(!error.isEmpty());
    QVERIFY(database.isAvailable());

    // 后续合法查询仍能执行
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QSqlQuery count(connection);
    QVERIFY(count.exec("SELECT COUNT(*) FROM peer_devices WHERE device_id='device-dup'"));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

// 库版本高于当前支持上限时拒绝打开，降级路径按契约回调失败
void TestStorageDatabase::testNewerSchemaVersionRejected()
{
    QString error;
    const QString path = _temporaryDir.path() + "/future.sqlite";

    {
        SqliteDatabaseBroker database;
        QVERIFY2(database.initialize(path, &error), qPrintable(error));

        QSqlDatabase connection = database.connectionForWorkerThread(&error);
        QVERIFY2(connection.isValid(), qPrintable(error));
        // 手工把最高 Schema 版本抬到 3（恰高于当前支持上限 2），模拟由更新版本程序创建的库
        QSqlQuery upgrade(connection);
        QVERIFY2(upgrade.exec("UPDATE schema_version SET version = 3 "
                              "WHERE version = (SELECT MAX(version) FROM schema_version)"),
                 qPrintable(upgrade.lastError().text()));
        QVERIFY2(upgrade.exec("PRAGMA wal_checkpoint(TRUNCATE)"),
                 qPrintable(upgrade.lastError().text()));
    }

    SqliteDatabaseBroker database;
    QVERIFY(!database.initialize(path, &error));
    QVERIFY(!database.isAvailable());
    QVERIFY(error.contains("更新版本"));

    // 降级路径符合存储契约：查询回调恰好一次且以失败结束，历史功能不可用但不挂起
    LocalDataBroker broker;
    QVERIFY(!broker.initialize(path, &error));
    QVERIFY(!broker.isAvailable());

    int chatCalls = 0;
    bool chatSucceeded = true;
    broker.loadRecentChatHistories(&broker,
        [&](const QHash<QString, QList<MessageRecord>> &, bool succeeded) {
            ++chatCalls;
            chatSucceeded = succeeded;
        });

    int transferCalls = 0;
    bool transferSucceeded = true;
    broker.loadRecentTransferHistories(&broker,
        [&](const QList<TransferRecord> &, bool succeeded) {
            ++transferCalls;
            transferSucceeded = succeeded;
        });

    // 回调经 QueuedConnection 回投，轮询事件循环等待到达
    QTRY_COMPARE(chatCalls, 1);
    QTRY_COMPARE(transferCalls, 1);
    QCOMPARE(chatSucceeded, false);
    QCOMPARE(transferSucceeded, false);
}

bool TestStorageDatabase::tableExists(QSqlDatabase &connection, const QString &tableName)
{
    QSqlQuery query(connection);
    query.prepare("SELECT name FROM sqlite_master WHERE type='table' AND name=?");
    query.addBindValue(tableName);
    if (!query.exec()) {
        return false;
    }
    return query.next();
}

QTEST_MAIN(TestStorageDatabase)
#include "test_storage_database.moc"
