/**
* @file    test_storage_database.cpp
* @version 6.6.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 初始化、迁移与异常恢复测试
*
* 使用临时数据库验证首次建库、重复初始化、缺失驱动、锁竞争和损坏库重建。
*
* Change Log:
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
#include "sqlite_database_proxy.h"

class TestStorageDatabase : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInitializeAndSchema();
    void testRepeatedInitializePreservesData();
    void testMissingDriverDegrades();
    void testCorruptDatabaseBackedUpAndRebuilt();
    void testLockedDatabaseWriteFailsButProxyStaysAvailable();

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
    SqliteDatabaseProxy database;
    QString error;
    const QString path = ApplicationPaths::databaseDir() + "/gridyard-history.sqlite";

    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 1);
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
        SqliteDatabaseProxy database;
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

    SqliteDatabaseProxy database;
    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QCOMPARE(database.schemaVersion(), 1);

    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QSqlQuery count(connection);
    QVERIFY(count.exec("SELECT COUNT(*) FROM peer_devices WHERE device_id='device-repeat'"));
    QVERIFY(count.next());
    QCOMPARE(count.value(0).toInt(), 1);
}

void TestStorageDatabase::testMissingDriverDegrades()
{
    SqliteDatabaseProxy database([] { return QStringList{}; });
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

    SqliteDatabaseProxy database;
    QString error;
    QVERIFY2(database.initialize(path, &error), qPrintable(error));
    QVERIFY(database.isAvailable());
    QCOMPARE(database.schemaVersion(), 1);

    const QFileInfo fileInfo(path);
    const QStringList backups = QDir(fileInfo.absolutePath())
        .entryList({fileInfo.fileName() + ".corrupt-*"}, QDir::Files);
    QCOMPARE(backups.size(), 1);
    QVERIFY(QFileInfo(fileInfo.absolutePath() + "/" + backups.first()).size() > 0);

    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    QVERIFY2(connection.isValid(), qPrintable(error));
    QVERIFY(tableExists(connection, "chat_messages"));
}

void TestStorageDatabase::testLockedDatabaseWriteFailsButProxyStaysAvailable()
{
    SqliteDatabaseProxy database;
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
