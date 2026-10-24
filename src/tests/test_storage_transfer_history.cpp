/**
* @file    test_storage_transfer_history.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   传输历史仓储测试
*
* 结束态记录的字段往返、session_id 幂等覆盖、筛选分页、删除与过期清理
* 语义直接验证纯 C++ 传输历史仓储（TransferRepositoryCore）。旧 Qt 版
* 仓储已随 gy_storage 退役。
*/

#include <QtTest/QtTest>

#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "application_paths.h"
#include "db_seed.h"

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/transfer_repository_core.h"

class TestStorageTransferHistory : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testCoreUpsertBySessionIdAndNullErrorColumns();
    void testCoreFiltersAndCursorPagination();
    void testCoreDeleteAndClear();
    void testCoreReopenDatabase();

private:
    std::unique_ptr<gy::storage::DatabaseCore> openDatabase(const QString &relativePath);

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

// session_id 幂等覆盖与完成态错误列置空契约，字段往返完整
void TestStorageTransferHistory::testCoreUpsertBySessionIdAndNullErrorColumns()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-tr-upsert.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::TransferRepositoryCore repository(database.get());

    core::PeerRecord peer;
    peer.deviceId = "core-tr-peer";
    peer.deviceName = "Transfer Peer";
    peer.firstSeenAtMs = 1770000000000;
    peer.lastSeenAtMs = 1770000000000;
    std::string error;
    QVERIFY(devices.upsertPeer(peer, &error));

    const int64_t started =
        QDateTime::fromString("2026-06-25T20:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    core::TransferRecord record;
    record.recordId = "core-tr-001";
    record.sessionId = "session-core-dup";
    record.peerDeviceId = peer.deviceId;
    record.peerName = "Transfer Peer";
    record.displayName = "archive.zip";
    record.isDirectory = true;
    record.fileCount = 2;
    record.totalBytes = 2048;
    record.status = "failed";
    record.startedAtMs = started;
    record.finishedAtMs = started + 5000;
    record.errorCode = 7;
    record.errorMessage = QString::fromUtf8("连接中断").toStdString();
    QVERIFY(repository.upsertFinishedTransfer(record, &error));

    // 首写字段完整往返
    core::TransferQuery allQuery;
    const std::vector<core::TransferRecord> seeded = repository.queryTransfers(allQuery, 10, &error);
    QCOMPARE(int(seeded.size()), 1);
    QCOMPARE(QString::fromStdString(seeded.front().sessionId), QStringLiteral("session-core-dup"));
    QCOMPARE(QString::fromStdString(seeded.front().peerDeviceId), QStringLiteral("core-tr-peer"));
    QCOMPARE(QString::fromStdString(seeded.front().peerName), QStringLiteral("Transfer Peer"));
    QCOMPARE(QString::fromStdString(seeded.front().displayName), QStringLiteral("archive.zip"));
    QVERIFY(seeded.front().isDirectory);
    QCOMPARE(seeded.front().fileCount, 2);
    QCOMPARE(seeded.front().totalBytes, int64_t{2048});
    QCOMPARE(seeded.front().errorCode, 7);

    // 同 session_id 重复写入：record_id 保留首写，错误信息覆盖为最新快照
    core::TransferRecord rewritten = record;
    rewritten.errorCode = 8;
    rewritten.errorMessage = QString::fromUtf8("磁盘空间不足").toStdString();
    QVERIFY(repository.upsertFinishedTransfer(rewritten, &error));

    core::TransferQuery query;
    query.peerDeviceId = peer.deviceId;
    const std::vector<core::TransferRecord> loaded = repository.queryTransfers(query, 10, &error);
    QCOMPARE(int(loaded.size()), 1);
    QCOMPARE(QString::fromStdString(loaded.front().recordId), QStringLiteral("core-tr-001"));
    QCOMPARE(loaded.front().errorCode, 8);
    QCOMPARE(QString::fromStdString(loaded.front().errorMessage),
             QString::fromUtf8("磁盘空间不足"));

    // 完成态覆盖：error_code/error_message 列清空，结束时间保留
    core::TransferRecord completed = record;
    completed.status = core::kTransferStatusCompleted;
    QVERIFY(repository.upsertFinishedTransfer(completed, &error));
    const std::vector<core::TransferRecord> finals = repository.queryTransfers(query, 10, &error);
    QCOMPARE(int(finals.size()), 1);
    QCOMPARE(QString::fromStdString(finals.front().status), QStringLiteral("completed"));
    QCOMPARE(finals.front().errorCode, 0);  // NULL 列读回 0
    QVERIFY(finals.front().errorMessage.empty());
    QVERIFY(finals.front().finishedAtMs.has_value());
}

// 筛选、倒序分页与同毫秒次键游标
void TestStorageTransferHistory::testCoreFiltersAndCursorPagination()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-tr-query.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::TransferRepositoryCore repository(database.get());
    std::string error;

    const auto seedPeer = [&](const char *id) {
        core::PeerRecord peer;
        peer.deviceId = id;
        peer.deviceName = id;
        peer.firstSeenAtMs = 1770000000000;
        peer.lastSeenAtMs = 1770000000000;
        return devices.upsertPeer(peer, &error);
    };
    QVERIFY(seedPeer("core-tr-c"));
    QVERIFY(seedPeer("core-tr-d"));

    const int64_t base =
        QDateTime::fromString("2026-06-25T17:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const auto seed = [&](const char *recordId, const char *sessionId, const char *peerId,
                          const char *status, int64_t started) {
        core::TransferRecord record;
        record.recordId = recordId;
        record.sessionId = sessionId;
        record.peerDeviceId = peerId;
        record.peerName = peerId;
        record.displayName = "data.bin";
        record.fileCount = 1;
        record.totalBytes = 100;
        record.status = status;
        record.startedAtMs = started;
        record.finishedAtMs = started + 10000;
        return repository.upsertFinishedTransfer(record, &error);
    };
    QVERIFY(seed("core-c1", "core-s-c1", "core-tr-c", "completed", base));
    QVERIFY(seed("core-c2", "core-s-c2", "core-tr-c", "failed", base + 60000));
    QVERIFY(seed("core-d1", "core-s-d1", "core-tr-d", "cancelled", base + 120000));

    // 设备筛选倒序
    core::TransferQuery peerQuery;
    peerQuery.peerDeviceId = "core-tr-c";
    std::vector<core::TransferRecord> records = repository.queryTransfers(peerQuery, 10, &error);
    QCOMPARE(int(records.size()), 2);
    QCOMPARE(QString::fromStdString(records.at(0).recordId), QStringLiteral("core-c2"));
    QCOMPARE(QString::fromStdString(records.at(1).recordId), QStringLiteral("core-c1"));

    // 状态筛选
    core::TransferQuery statusQuery;
    statusQuery.status = "cancelled";
    records = repository.queryTransfers(statusQuery, 10, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().recordId), QStringLiteral("core-d1"));

    // 时间游标翻页：末条之前的记录
    core::TransferQuery pageQuery;
    records = repository.queryTransfers(pageQuery, 2, &error);
    QCOMPARE(int(records.size()), 2);
    pageQuery.beforeStartedAtMs = records.back().startedAtMs;
    records = repository.queryTransfers(pageQuery, 2, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().recordId), QStringLiteral("core-c1"));

    // 同毫秒三条按 record_id 倒序，二元组游标翻页不重不漏
    QVERIFY(seedPeer("core-tr-f"));
    const int64_t sameMs =
        QDateTime::fromString("2026-06-25T19:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    QVERIFY(seed("core-f1", "core-s-f1", "core-tr-f", "completed", sameMs));
    QVERIFY(seed("core-f2", "core-s-f2", "core-tr-f", "completed", sameMs));
    QVERIFY(seed("core-f3", "core-s-f3", "core-tr-f", "completed", sameMs));

    core::TransferQuery query;
    query.peerDeviceId = "core-tr-f";
    const std::vector<core::TransferRecord> firstPage = repository.queryTransfers(query, 2, &error);
    QCOMPARE(int(firstPage.size()), 2);
    QCOMPARE(QString::fromStdString(firstPage.at(0).recordId), QStringLiteral("core-f3"));
    QCOMPARE(QString::fromStdString(firstPage.at(1).recordId), QStringLiteral("core-f2"));
    query.beforeStartedAtMs = firstPage.back().startedAtMs;
    query.beforeRecordId = firstPage.back().recordId;
    const std::vector<core::TransferRecord> secondPage =
        repository.queryTransfers(query, 2, &error);
    QCOMPARE(int(secondPage.size()), 1);
    QCOMPARE(QString::fromStdString(secondPage.front().recordId), QStringLiteral("core-f1"));
}

// 单条删除不触碰已落盘文件、过期清理边界与全表清空
void TestStorageTransferHistory::testCoreDeleteAndClear()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-tr-del.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::TransferRepositoryCore repository(database.get());

    // 已接收文件在历史删除后必须保留在磁盘上
    QFile sourceFile(_temporaryDir.path() + "/keep-source.txt");
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("keep me");
    sourceFile.close();

    core::PeerRecord peer;
    peer.deviceId = "core-tr-x";
    peer.deviceName = "core-tr-x";
    peer.firstSeenAtMs = 1770000000000;
    peer.lastSeenAtMs = 1770000000000;
    std::string error;
    QVERIFY(devices.upsertPeer(peer, &error));

    const int64_t cutoff =
        QDateTime::fromString("2026-06-25T21:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const auto seed = [&](const char *recordId, int64_t started) {
        core::TransferRecord record;
        record.recordId = recordId;
        record.sessionId = recordId;
        record.peerDeviceId = peer.deviceId;
        record.peerName = "core-tr-x";
        record.displayName = "x.bin";
        record.fileCount = 1;
        record.totalBytes = 10;
        record.status = core::kTransferStatusCompleted;
        record.startedAtMs = started;
        record.finishedAtMs = started + 1000;
        return repository.upsertFinishedTransfer(record, &error);
    };
    QVERIFY(seed("core-x1", cutoff - 1));
    QVERIFY(seed("core-x2", cutoff));
    QVERIFY(seed("core-x3", cutoff + 1));

    // 单条删除：行消失，磁盘文件保留
    QVERIFY(repository.deleteTransfer("core-x3", &error));
    QVERIFY(QFile::exists(sourceFile.fileName()));

    // 过期边界保留：严格早于截止时间的才被清理
    QVERIFY(repository.deleteExpiredTransfers(cutoff, &error));
    core::TransferQuery query;
    query.peerDeviceId = peer.deviceId;
    std::vector<core::TransferRecord> records = repository.queryTransfers(query, 10, &error);
    QCOMPARE(int(records.size()), 1);
    QCOMPARE(QString::fromStdString(records.front().recordId), QStringLiteral("core-x2"));
    QVERIFY(QFile::exists(sourceFile.fileName()));

    // 全表清空幂等
    QVERIFY(repository.clearAllTransfers(&error));
    QVERIFY(repository.clearAllTransfers(&error));
    QVERIFY(repository.allTransfers(&error).empty());
}

// 重启后传输历史可恢复
void TestStorageTransferHistory::testCoreReopenDatabase()
{
    namespace core = gy::storage;
    {
        auto database = openDatabase("reopen.sqlite");
        QVERIFY(database);
        QVERIFY(gy::test::seedDevice(*database, QStringLiteral("peer-G"),
                                     QStringLiteral("Device Golf"),
                                     QDateTime::fromString("2026-06-25T19:30:00.000Z",
                                                           Qt::ISODateWithMs)));
        core::TransferRepositoryCore repository(database.get());

        core::TransferRecord record;
        record.recordId = "transfer-g1";
        record.sessionId = "session-g1";
        record.peerDeviceId = "peer-G";
        record.peerName = "Device Golf";
        record.displayName = "photo.png";
        record.fileCount = 1;
        record.totalBytes = 65536;
        record.status = "rejected";
        record.startedAtMs =
            QDateTime::fromString("2026-06-25T20:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
        record.finishedAtMs = record.startedAtMs + 30000;
        record.errorCode = 11;
        record.errorMessage = QString::fromUtf8("对方拒绝").toStdString();
        std::string error;
        QVERIFY(repository.upsertFinishedTransfer(record, &error));
    }

    auto reopened = openDatabase("reopen.sqlite");
    QVERIFY(reopened);
    core::TransferRepositoryCore repository(reopened.get());
    std::string error;
    core::TransferQuery query;
    const std::vector<core::TransferRecord> loaded = repository.queryTransfers(query, 10, &error);
    QCOMPARE(int(loaded.size()), 1);
    QCOMPARE(QString::fromStdString(loaded.front().recordId), QStringLiteral("transfer-g1"));
    QCOMPARE(QString::fromStdString(loaded.front().status), QStringLiteral("rejected"));
    QCOMPARE(loaded.front().errorCode, 11);
}

// 工具方法：在临时目录中打开（或创建）核心数据库
std::unique_ptr<gy::storage::DatabaseCore> TestStorageTransferHistory::openDatabase(
    const QString &relativePath)
{
    return gy::test::openDatabase(_temporaryDir.path(), relativePath);
}

QTEST_MAIN(TestStorageTransferHistory)
#include "test_storage_transfer_history.moc"
