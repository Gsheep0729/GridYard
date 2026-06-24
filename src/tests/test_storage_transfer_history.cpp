/**
* @file    test_storage_transfer_history.cpp
* @version 6.3.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 传输历史 Proxy 测试
*
* Change Log:
* [v6.3.0] GY   2026-06-25
* * 新增传输历史 SQLite Proxy 测试
*/

#include <QtTest/QtTest>

#include "application_paths.h"
#include "history_records.h"
#include "sqlite_database_proxy.h"
#include "sqlite_device_proxy.h"
#include "sqlite_transfer_history_proxy.h"

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <memory>

class TestStorageTransferHistory : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSaveAndQuery();
    void testUpsertBySessionId();
    void testFiltersAndPagination();
    void testDeleteTransferKeepsSourceFile();
    void testDeleteExpiredTransfers();
    void testReopenDatabase();

private:
    std::unique_ptr<SqliteDatabaseProxy> openDatabase(const QString &relativePath);
    void seedDevice(SqliteDatabaseProxy &database, const QString &deviceId, const QString &name);
    TransferRecord makeRecord(const QString &sessionId, const QString &peerDeviceId,
                              const QString &status, const QDateTime &startedAt);
    int transferRowCount(SqliteDatabaseProxy &database);

    QTemporaryDir _temporaryDir;
};

void TestStorageTransferHistory::initTestCase()
{
    QVERIFY(_temporaryDir.isValid());
    ApplicationPaths::coverForTest(_temporaryDir.path());
}

void TestStorageTransferHistory::cleanupTestCase()
{
    ApplicationPaths::clearTestCover();
}

void TestStorageTransferHistory::testSaveAndQuery()
{
    auto database = openDatabase("save-query.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryProxy proxy(database.get());
    seedDevice(*database, "peer-A", "Device Alpha");

    QString error;
    const QDateTime startedAt = QDateTime::fromString("2026-06-25T16:00:00.000Z", Qt::ISODateWithMs);
    const TransferRecord record = makeRecord("session-001", "peer-A", "completed", startedAt);
    QVERIFY2(proxy.upsertFinishedTransfer(record, &error), qPrintable(error));

    TransferQuery query;
    const QList<TransferRecord> loaded = proxy.queryTransfers(query, 10, &error);
    QVERIFY2(loaded.size() == 1, qPrintable(error));
    QCOMPARE(loaded.first().sessionId, QStringLiteral("session-001"));
    QCOMPARE(loaded.first().peerDeviceId, QStringLiteral("peer-A"));
    QCOMPARE(loaded.first().peerName, QStringLiteral("Peer peer-A"));
    QCOMPARE(loaded.first().displayName, QStringLiteral("archive.zip"));
    QCOMPARE(loaded.first().status, QStringLiteral("completed"));
    QCOMPARE(loaded.first().fileCount, 1);
    QCOMPARE(loaded.first().totalBytes, qint64(4096));
    QCOMPARE(loaded.first().errorCode, 0);
    QVERIFY(loaded.first().errorMessage.isEmpty());
}

void TestStorageTransferHistory::testUpsertBySessionId()
{
    auto database = openDatabase("upsert.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryProxy proxy(database.get());
    seedDevice(*database, "peer-B", "Device Bravo");

    QString error;
    const QDateTime startedAt = QDateTime::fromString("2026-06-25T16:10:00.000Z", Qt::ISODateWithMs);
    TransferRecord first = makeRecord("session-dup", "peer-B", "failed", startedAt);
    first.errorCode = 7;
    first.errorMessage = QString::fromUtf8("连接断开");
    QVERIFY2(proxy.upsertFinishedTransfer(first, &error), qPrintable(error));

    TransferRecord second = first;
    second.recordId = "transfer-rewritten";
    second.errorCode = 8;
    second.errorMessage = QString::fromUtf8("磁盘空间不足");
    QVERIFY2(proxy.upsertFinishedTransfer(second, &error), qPrintable(error));

    TransferQuery query;
    query.peerDeviceId = "peer-B";
    const QList<TransferRecord> loaded = proxy.queryTransfers(query, 10, &error);
    QVERIFY2(loaded.size() == 1, qPrintable(error));
    QCOMPARE(loaded.first().sessionId, QStringLiteral("session-dup"));
    QCOMPARE(loaded.first().recordId, QStringLiteral("transfer-001"));
    QCOMPARE(loaded.first().errorCode, 8);
    QCOMPARE(loaded.first().errorMessage, QString::fromUtf8("磁盘空间不足"));
}

void TestStorageTransferHistory::testFiltersAndPagination()
{
    auto database = openDatabase("filters.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryProxy proxy(database.get());
    seedDevice(*database, "peer-C", "Device Charlie");
    seedDevice(*database, "peer-D", "Device Delta");

    QString error;
    const QDateTime base = QDateTime::fromString("2026-06-25T17:00:00.000Z", Qt::ISODateWithMs);
    TransferRecord first = makeRecord("session-c1", "peer-C", "completed", base);
    first.recordId = "transfer-c1";
    first.displayName = "folder-A";
    first.isDirectory = true;
    first.fileCount = 4;
    first.totalBytes = 1024;
    QVERIFY2(proxy.upsertFinishedTransfer(first, &error), qPrintable(error));

    TransferRecord second = makeRecord("session-c2", "peer-C", "failed", base.addSecs(60));
    second.recordId = "transfer-c2";
    second.errorCode = 9;
    second.errorMessage = QString::fromUtf8("网络错误");
    QVERIFY2(proxy.upsertFinishedTransfer(second, &error), qPrintable(error));

    TransferRecord third = makeRecord("session-d1", "peer-D", "cancelled", base.addSecs(120));
    third.recordId = "transfer-d1";
    third.errorCode = 10;
    third.errorMessage = QString::fromUtf8("已取消");
    QVERIFY2(proxy.upsertFinishedTransfer(third, &error), qPrintable(error));

    TransferQuery peerQuery;
    peerQuery.peerDeviceId = "peer-C";
    const QList<TransferRecord> peerRecords = proxy.queryTransfers(peerQuery, 10, &error);
    QVERIFY2(peerRecords.size() == 2, qPrintable(error));
    QCOMPARE(peerRecords.at(0).recordId, QStringLiteral("transfer-c2"));
    QCOMPARE(peerRecords.at(1).recordId, QStringLiteral("transfer-c1"));

    TransferQuery statusQuery;
    statusQuery.status = "cancelled";
    const QList<TransferRecord> statusRecords = proxy.queryTransfers(statusQuery, 10, &error);
    QVERIFY2(statusRecords.size() == 1, qPrintable(error));
    QCOMPARE(statusRecords.first().recordId, QStringLiteral("transfer-d1"));

    TransferQuery pageQuery;
    const QList<TransferRecord> firstPage = proxy.queryTransfers(pageQuery, 2, &error);
    QVERIFY2(firstPage.size() == 2, qPrintable(error));
    pageQuery.beforeStartedAt = firstPage.last().startedAt;
    const QList<TransferRecord> secondPage = proxy.queryTransfers(pageQuery, 2, &error);
    QVERIFY2(secondPage.size() == 1, qPrintable(error));
    QCOMPARE(secondPage.first().recordId, QStringLiteral("transfer-c1"));
}

void TestStorageTransferHistory::testDeleteTransferKeepsSourceFile()
{
    auto database = openDatabase("delete-one.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryProxy proxy(database.get());
    seedDevice(*database, "peer-E", "Device Echo");

    QFile sourceFile(_temporaryDir.path() + "/keep-source.txt");
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("keep me");
    sourceFile.close();

    QString error;
    TransferRecord record = makeRecord("session-e1", "peer-E", "completed",
                                       QDateTime::fromString("2026-06-25T18:00:00.000Z",
                                                             Qt::ISODateWithMs));
    record.recordId = "transfer-e1";
    QVERIFY2(proxy.upsertFinishedTransfer(record, &error), qPrintable(error));
    QVERIFY(QFile::exists(sourceFile.fileName()));

    QVERIFY2(proxy.deleteTransfer("transfer-e1", &error), qPrintable(error));
    QCOMPARE(transferRowCount(*database), 0);
    QVERIFY(QFile::exists(sourceFile.fileName()));
}

void TestStorageTransferHistory::testDeleteExpiredTransfers()
{
    auto database = openDatabase("delete-expired.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryProxy proxy(database.get());
    seedDevice(*database, "peer-F", "Device Foxtrot");

    QString error;
    const QDateTime oldTime = QDateTime::fromString("2026-06-25T19:00:00.000Z", Qt::ISODateWithMs);
    const QDateTime keepTime = QDateTime::fromString("2026-06-25T19:05:00.000Z", Qt::ISODateWithMs);
    TransferRecord oldRecord = makeRecord("session-f1", "peer-F", "completed", oldTime);
    oldRecord.recordId = "transfer-f1";
    TransferRecord keepRecord = makeRecord("session-f2", "peer-F", "completed", keepTime);
    keepRecord.recordId = "transfer-f2";
    QVERIFY2(proxy.upsertFinishedTransfer(oldRecord, &error), qPrintable(error));
    QVERIFY2(proxy.upsertFinishedTransfer(keepRecord, &error), qPrintable(error));

    QVERIFY2(proxy.deleteExpiredTransfers(keepTime, &error), qPrintable(error));

    TransferQuery query;
    const QList<TransferRecord> loaded = proxy.queryTransfers(query, 10, &error);
    QVERIFY2(loaded.size() == 1, qPrintable(error));
    QCOMPARE(loaded.first().recordId, QStringLiteral("transfer-f2"));
}

void TestStorageTransferHistory::testReopenDatabase()
{
    const QString path = _temporaryDir.path() + "/reopen.sqlite";

    {
        auto database = openDatabase("reopen.sqlite");
        QVERIFY(database);
        SqliteTransferHistoryProxy proxy(database.get());
        seedDevice(*database, "peer-G", "Device Golf");

        QString error;
        TransferRecord record = makeRecord("session-g1", "peer-G", "rejected",
                                           QDateTime::fromString("2026-06-25T20:00:00.000Z",
                                                                 Qt::ISODateWithMs));
        record.recordId = "transfer-g1";
        record.errorCode = 11;
        record.errorMessage = QString::fromUtf8("对方拒绝");
        QVERIFY2(proxy.upsertFinishedTransfer(record, &error), qPrintable(error));

        // WAL 模式下，关闭前做一次 checkpoint 确保数据落盘到主库
        QSqlDatabase conn = database->connectionForWorkerThread(&error);
        QSqlQuery q(conn);
        q.exec("PRAGMA wal_checkpoint(TRUNCATE)");
        conn.close();
    }

    // 清理当前线程上所有残留的 Qt 数据库连接句柄
    for (const QString &name : QSqlDatabase::connectionNames()) {
        QSqlDatabase::removeDatabase(name);
    }

    auto reopened = openDatabase("reopen.sqlite");
    QVERIFY(reopened);
    SqliteTransferHistoryProxy proxy(reopened.get());
    QString error;
    TransferQuery query;
    const QList<TransferRecord> loaded = proxy.queryTransfers(query, 10, &error);
    QVERIFY2(loaded.size() == 1, qPrintable(error));
    QCOMPARE(loaded.first().recordId, QStringLiteral("transfer-g1"));
    QCOMPARE(loaded.first().status, QStringLiteral("rejected"));
}

std::unique_ptr<SqliteDatabaseProxy> TestStorageTransferHistory::openDatabase(const QString &relativePath)
{
    auto database = std::make_unique<SqliteDatabaseProxy>();
    QString error;
    if (!database->initialize(_temporaryDir.path() + "/" + relativePath, &error)) {
        qWarning() << error;
        return {};
    }
    return database;
}

void TestStorageTransferHistory::seedDevice(SqliteDatabaseProxy &database, const QString &deviceId,
                                            const QString &name)
{
    SqliteDeviceProxy proxy(&database);
    PeerRecord peer;
    peer.deviceId = deviceId;
    peer.deviceName = name;
    peer.lastIpAddress = "192.168.1.10";
    peer.lastTcpPort = 35100;
    peer.firstSeenAt = QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs);
    peer.lastSeenAt = peer.firstSeenAt;
    QString error;
    QVERIFY2(proxy.upsertPeer(peer, &error), qPrintable(error));
}

TransferRecord TestStorageTransferHistory::makeRecord(const QString &sessionId,
                                                      const QString &peerDeviceId,
                                                      const QString &status,
                                                      const QDateTime &startedAt)
{
    TransferRecord record;
    record.recordId = "transfer-001";
    record.sessionId = sessionId;
    record.peerDeviceId = peerDeviceId;
    record.peerName = QStringLiteral("Peer %1").arg(peerDeviceId);
    record.direction = RecordDirection::Outgoing;
    record.displayName = "archive.zip";
    record.isDirectory = false;
    record.fileCount = 1;
    record.totalBytes = 4096;
    record.status = status;
    record.startedAt = startedAt;
    record.finishedAt = startedAt.addSecs(30);
    return record;
}

int TestStorageTransferHistory::transferRowCount(SqliteDatabaseProxy &database)
{
    QString error;
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    if (!connection.isValid())
        return -1;
    QSqlQuery query(connection);
    if (!query.exec("SELECT COUNT(*) FROM transfer_history"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

QTEST_MAIN(TestStorageTransferHistory)
#include "test_storage_transfer_history.moc"
