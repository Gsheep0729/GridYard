/**
* @file    test_storage_database.cpp
* @version 6.0.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 初始化与迁移测试
*
* 使用临时数据库验证首次建库、重复初始化和应用数据路径覆盖。
*
* Change Log:
* [v6.0.0] GY 2026-06-25
* * 新增 SQLite migration 基础测试
*/
#include <QtTest/QtTest>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include "application_paths.h"
#include "sqlite_database_proxy.h"
class TestStorageDatabase : public QObject { Q_OBJECT
private slots: void initTestCase(); void cleanupTestCase(); void testInitializeAndSchema(); void testRepeatedInitialize();
private: QTemporaryDir _temporaryDir; };
void TestStorageDatabase::initTestCase() { QVERIFY(_temporaryDir.isValid()); ApplicationPaths::coverForTest(_temporaryDir.path()); }
void TestStorageDatabase::cleanupTestCase() { ApplicationPaths::clearTestCover(); }
void TestStorageDatabase::testInitializeAndSchema() { SqliteDatabaseProxy database; QString error; const QString path = ApplicationPaths::databaseDir() + "/gridyard-history.sqlite"; QVERIFY2(database.initialize(path, &error), qPrintable(error)); QCOMPARE(database.schemaVersion(), 1); QVERIFY(QFileInfo::exists(path)); QSqlDatabase connection = database.connectionForWorkerThread(&error); QVERIFY2(connection.isValid(), qPrintable(error)); QSqlQuery query(connection); QVERIFY(query.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='transfer_history'")); QVERIFY(query.next()); }
void TestStorageDatabase::testRepeatedInitialize() { SqliteDatabaseProxy database; QString error; const QString path = _temporaryDir.path() + "/repeat.sqlite"; QVERIFY2(database.initialize(path, &error), qPrintable(error)); QCOMPARE(database.schemaVersion(), 1); QVERIFY2(database.initialize(path, &error), qPrintable(error)); QCOMPARE(database.schemaVersion(), 1); }
QTEST_MAIN(TestStorageDatabase)
#include "test_storage_database.moc"
