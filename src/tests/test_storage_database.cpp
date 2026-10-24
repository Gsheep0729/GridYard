/**
* @file    test_storage_database.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   SQLite 初始化、迁移与异常恢复测试
*
* 建库、迁移、损坏重建与回滚语义直接验证纯 C++ 存储核心（DatabaseCore）；
* 损坏重建透出标志与未来版本降级回调两条产品契约经 LocalDataBroker 验证。
*/

#include <QtTest/QtTest>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "application_paths.h"
#include "local_data_broker.h"

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/iso_time.h"
#include "core/storage/sqlite_conn.h"
#include "core/storage/sqlite_stmt.h"

class TestStorageDatabase : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testRepeatedInitializePreservesData();
    void testCorruptRebuildFlagsSurfaceThroughBroker();
    void testNewerSchemaVersionRejected();
    void testCoreIsoTimeMatchesQtFormat();
    void testCoreInitializeAndSchema();
    void testCoreV1MigratedToV3KeepsRows();
    void testCoreCorruptDatabaseBackedUpAndRebuilt();
    void testCoreNewerSchemaVersionRejected();
    void testCoreRunStepsRollbackOnFailure();

private:
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

// 重复初始化不重建 Schema，已写入的数据原样保留
void TestStorageDatabase::testRepeatedInitializePreservesData()
{
    namespace core = gy::storage;
    const QString path = _temporaryDir.path() + "/repeat.sqlite";
    std::string error;

    {
        core::DatabaseCore database;
        QVERIFY(database.initialize(path.toStdString(), &error));
        core::SqliteStmt insert{
            database.connection(),
            "INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
            "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?)"};
        insert.bind(1, "device-repeat");
        insert.bind(2, "重复初始化设备");
        insert.bind(3, "127.0.0.1");
        insert.bind(4, int64_t{35100});
        insert.bind(5, "2026-06-25T00:00:00.000Z");
        insert.bind(6, "2026-06-25T00:00:00.000Z");
        QVERIFY(insert.step().has_value());
    }

    core::DatabaseCore database;
    QVERIFY(database.initialize(path.toStdString(), &error));
    QCOMPARE(database.schemaVersion(), 3);

    core::SqliteStmt count{
        database.connection(),
        "SELECT COUNT(*) FROM peer_devices WHERE device_id='device-repeat'"};
    QVERIFY(count.step().has_value());
    QCOMPARE(static_cast<int>(count.columnInt64(0)), 1);
}

// 损坏库重建的标志与备份路径必须经 LocalDataBroker 向上层透出，正常建库不置位
void TestStorageDatabase::testCorruptRebuildFlagsSurfaceThroughBroker()
{
    QString error;
    const auto writeCorruptFile = [this](const QString &path) {
        QFile corruptFile(path);
        QVERIFY(corruptFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(corruptFile.write("this is not a sqlite database") > 0);
        corruptFile.close();
    };

    // 正常建库不置位重建标志，备份路径保持为空
    LocalDataBroker freshBroker;
    QVERIFY2(freshBroker.initialize(_temporaryDir.path() + "/fresh-rebuilt.sqlite", &error),
             qPrintable(error));
    QVERIFY(!freshBroker.historyDatabaseRebuilt());
    QVERIFY(freshBroker.rebuiltBackupPath().isEmpty());
    freshBroker.closeStorage();

    // 通知属性经 LocalDataBroker 向上透出，断言置位且备份文件真实存在
    const QString rebuiltPath = _temporaryDir.path() + "/corrupt-rebuilt.sqlite";
    writeCorruptFile(rebuiltPath);
    LocalDataBroker rebuiltBroker;
    QVERIFY2(rebuiltBroker.initialize(rebuiltPath, &error), qPrintable(error));
    QVERIFY(rebuiltBroker.isAvailable());
    QVERIFY(rebuiltBroker.historyDatabaseRebuilt());
    QVERIFY(!rebuiltBroker.rebuiltBackupPath().isEmpty());
    QVERIFY(QFileInfo::exists(rebuiltBroker.rebuiltBackupPath()));
    rebuiltBroker.closeStorage();
}

// 库版本高于当前支持上限时拒绝打开，降级路径按契约回调失败
void TestStorageDatabase::testNewerSchemaVersionRejected()
{
    namespace core = gy::storage;
    QString error;
    const QString path = _temporaryDir.path() + "/future.sqlite";

    {
        core::DatabaseCore database;
        std::string coreError;
        QVERIFY(database.initialize(path.toStdString(), &coreError));
        // 手工把最高 Schema 版本抬到 4（恰高于当前支持上限 3），模拟由更新版本程序创建的库
        QVERIFY(database.connection()
                    .exec("UPDATE schema_version SET version = 4 "
                          "WHERE version = (SELECT MAX(version) FROM schema_version)")
                    .has_value());
    }

    core::DatabaseCore reopened;
    std::string coreError;
    QVERIFY(!reopened.initialize(path.toStdString(), &coreError));
    QVERIFY(!reopened.isAvailable());
    QVERIFY(QString::fromStdString(coreError).contains("更新版本"));

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

QTEST_MAIN(TestStorageDatabase)
#include "test_storage_database.moc"
