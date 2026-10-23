/**
* @file    test_storage_database.cpp
* @version 7.26.1
* @date 2026-10-24
* @author  GY
* @brief   SQLite 初始化、迁移与异常恢复测试
*
* 使用临时数据库验证首次建库、重复初始化、缺失驱动、锁竞争和损坏库重建。
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

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/iso_time.h"
#include "core/storage/sqlite_stmt.h"

#include <QDateTime>

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
    void testCoreIsoTimeMatchesQtFormat();
    void testCoreInitializeAndSchema();
    void testCoreV1MigratedToV3KeepsRows();
    void testCoreCorruptDatabaseBackedUpAndRebuilt();
    void testCoreNewerSchemaVersionRejected();
    void testCoreRunStepsRollbackOnFailure();

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
    QCOMPARE(database.schemaVersion(), 3);
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
    QCOMPARE(database.schemaVersion(), 3);

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
    QCOMPARE(database.schemaVersion(), 3);

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
    QCOMPARE(database.schemaVersion(), 3);

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
        // 手工把最高 Schema 版本抬到 4（恰高于当前支持上限 3），模拟由更新版本程序创建的库
        QSqlQuery upgrade(connection);
        QVERIFY2(upgrade.exec("UPDATE schema_version SET version = 4 "
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

// 纯 C++ 时间格式化与 Qt QDateTime 的 ISO 输出逐字节一致（新旧行为兼容的前提）
void TestStorageDatabase::testCoreIsoTimeMatchesQtFormat()
{
    namespace core = gy::storage;
    const QList<qint64> samples = {
        0,
        -1,                                   // 1970 前一毫秒，向下取整
        1770000001234,                        // 常规时刻
        QDateTime::fromString("2024-02-29T23:59:59.999Z", Qt::ISODateWithMs).toMSecsSinceEpoch(),  // 闰日末毫秒
        QDateTime::fromString("2026-12-31T23:59:59.999Z", Qt::ISODateWithMs).toMSecsSinceEpoch(),
    };
    for (const qint64 millis : samples) {
        const QDateTime qtTime = QDateTime::fromMSecsSinceEpoch(millis, QTimeZone::UTC);
        const QString expected = qtTime.toString(Qt::ISODateWithMs);
        QCOMPARE(QString::fromStdString(core::toIso8601Ms(millis)), expected);
        QCOMPARE(core::parseIso8601Ms(expected.toStdString()).value_or(-1), millis);
    }
    // 非零偏移时区后缀拒绝解析，防止把本地时间误当 UTC
    QVERIFY(!core::parseIso8601Ms("2026-06-25T10:00:00.000+08:00").has_value());
}

// 纯 C++ DatabaseCore 建库、迁移与连接参数
void TestStorageDatabase::testCoreInitializeAndSchema()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/core-init.sqlite";
    core::DatabaseCore database;
    std::string error;
    QVERIFY(database.initialize(path.toStdString(), &error));
    QVERIFY(error.empty());
    QVERIFY(database.isAvailable());
    QCOMPARE(database.schemaVersion(), 3);
    QVERIFY(QFileInfo::exists(path));

    // WAL 生效：journal_mode 返回 wal
    core::SqliteStmt mode{database.connection(), "PRAGMA journal_mode"};
    QVERIFY(mode.step().has_value());
    QCOMPARE(QString::fromStdString(mode.columnText(0)), QStringLiteral("wal"));

    // 四张业务表全部建成
    const QStringList tables = {"peer_devices", "chat_conversations", "chat_messages",
                                "transfer_history"};
    for (const QString &table : tables) {
        core::SqliteStmt check{database.connection(),
                               "SELECT name FROM sqlite_master WHERE type='table' AND name=?"};
        check.bind(1, table.toStdString());
        const auto row = check.step();
        QVERIFY(row.has_value());
        QVERIFY(row.value());
    }
}

// 纯 C++ 迁移器把 v1 存量库补齐到 v3 且保留存量行
void TestStorageDatabase::testCoreV1MigratedToV3KeepsRows()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/core-v1-upgrade.sqlite";

    // 用纯 C++ 连接手工造最小 v1 假库（schema_version 登记 1，peer_devices 旧列布局）
    {
        auto opened = core::SqliteConn::open(path.toStdString());
        QVERIFY(opened.has_value());
        core::SqliteConn &conn = opened.value();
        QVERIFY(conn.exec("CREATE TABLE schema_version (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)").has_value());
        QVERIFY(conn.exec("INSERT INTO schema_version VALUES(1, '2026-06-25T00:00:00.000Z')").has_value());
        QVERIFY(conn.exec("CREATE TABLE peer_devices (device_id TEXT PRIMARY KEY NOT NULL, device_name TEXT NOT NULL, last_ip_address TEXT, last_tcp_port INTEGER, first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL, last_chat_at TEXT, last_transfer_at TEXT)").has_value());
        QVERIFY(conn.exec("INSERT INTO peer_devices VALUES('device-v1', '存量设备', '192.168.1.20', 35100, "
                          "'2026-06-25T08:00:00.000Z', '2026-06-25T09:00:00.000Z', "
                          "'2026-06-25T08:30:00.000Z', NULL)").has_value());
    }

    core::DatabaseCore database;
    std::string error;
    QVERIFY(database.initialize(path.toStdString(), &error));
    QCOMPARE(database.schemaVersion(), 3);

    // 读链路带出新列与原字段：v1 列布局 + v2/v3 补列默认值正确
    core::DeviceRepositoryCore repository(&database);
    const std::vector<core::PeerRecord> peers = repository.recentPeers(10, &error);
    QCOMPARE(int(peers.size()), 1);
    QCOMPARE(QString::fromStdString(peers.front().deviceId), QStringLiteral("device-v1"));
    QCOMPARE(QString::fromStdString(peers.front().deviceName), QStringLiteral("存量设备"));
    QCOMPARE(QString::fromStdString(peers.front().lastIpAddress), QStringLiteral("192.168.1.20"));
    QVERIFY(peers.front().alias.empty());
    QVERIFY(!peers.front().pinned);
    QVERIFY(!peers.front().hidden);
    QVERIFY(!peers.front().favorite);
    QCOMPARE(peers.front().lastTcpPort, quint16(35100));
    QVERIFY(peers.front().lastChatAtMs.has_value());
    QVERIFY(!peers.front().lastTransferAtMs.has_value());
}

// 纯 C++ 初始化遇损坏库同样备份重建并透出标志
void TestStorageDatabase::testCoreCorruptDatabaseBackedUpAndRebuilt()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/core-corrupt.sqlite";
    QFile corruptFile(path);
    QVERIFY(corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(corruptFile.write("this is not a sqlite database") > 0);
    corruptFile.close();

    core::DatabaseCore database;
    std::string error;
    QVERIFY(database.initialize(path.toStdString(), &error));
    QVERIFY(database.isAvailable());
    QCOMPARE(database.schemaVersion(), 3);

    // 重建标志与备份路径必须同时上报，且备份文件真实存在
    QVERIFY(database.lastInitializeRebuilt());
    const QString backupPath = QString::fromStdString(database.rebuiltBackupPath());
    QVERIFY(!backupPath.isEmpty());
    QVERIFY(QFileInfo(backupPath).size() > 0);
    QVERIFY(backupPath.startsWith(path + ".corrupt-"));

    // 正常建库不置位重建标志
    core::DatabaseCore fresh;
    QVERIFY(fresh.initialize((_temporaryDir.path() + "/core-fresh.sqlite").toStdString(), &error));
    QVERIFY(!fresh.lastInitializeRebuilt());
    QVERIFY(fresh.rebuiltBackupPath().empty());
}

// 纯 C++ 初始化拒绝打开高于支持上限的未来库
void TestStorageDatabase::testCoreNewerSchemaVersionRejected()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/core-future.sqlite";

    {
        core::DatabaseCore database;
        std::string error;
        QVERIFY(database.initialize(path.toStdString(), &error));
        // 手工把最高版本抬到 4，模拟由更新版本程序创建的库
        QVERIFY(database.connection()
                    .exec("UPDATE schema_version SET version = 4 "
                          "WHERE version = (SELECT MAX(version) FROM schema_version)")
                    .has_value());
    }

    core::DatabaseCore database;
    std::string error;
    QVERIFY(!database.initialize(path.toStdString(), &error));
    QVERIFY(!database.isAvailable());
    QVERIFY(QString::fromStdString(error).contains("更新版本"));
}

// runSteps 任一步失败整体回滚：前后数据保持一致
void TestStorageDatabase::testCoreRunStepsRollbackOnFailure()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/core-rollback.sqlite";
    core::DatabaseCore database;
    std::string error;
    QVERIFY(database.initialize(path.toStdString(), &error));

    // 第一步正常写入，第二步制造主键冲突
    const auto insertDevice = [](const char *name) {
        return [name](core::SqliteConn &conn, std::string *taskError) {
            core::SqliteStmt query{
                conn, "INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                      "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?)"};
            query.bind(1, name);
            query.bind(2, "Rollback");
            query.bind(3, "127.0.0.1");
            query.bind(4, int64_t{35100});
            query.bind(5, "2026-06-25T00:00:00.000Z");
            query.bind(6, "2026-06-25T00:00:00.000Z");
            if (auto row = query.step(); row.has_value()) {
                return true;
            } else if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        };
    };
    QVERIFY(database.runSteps({insertDevice("device-rb"), insertDevice("device-rb")}, &error) == false);
    QVERIFY(!error.empty());

    // 冲突事务整体回滚：第一行不能残留
    core::SqliteStmt count{database.connection(),
                           "SELECT COUNT(*) FROM peer_devices WHERE device_id='device-rb'"};
    QVERIFY(count.step().has_value());
    QCOMPARE(int(count.columnInt64(0)), 0);
    QVERIFY(database.isAvailable());  // 失败后连接仍可用
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
