/**
 * @file    test_history_controller.cpp
 * @version 6.4.0
 * @date    2026-06-25
 * @author  GY
 * @brief   HistoryController 本地历史视图与保留清理测试
 *
 * Change Log:
 * [v6.4.0] GY 2026-06-25
 * * 新增本地历史控制器测试
 */

#include <QtTest/QtTest>

#include "application_paths.h"
#include "chat_manager.h"
#include "config_manager.h"
#include "database_worker.h"
#include "history_controller.h"
#include "history_records.h"
#include "history_repositories.h"
#include "sqlite_database_broker.h"
#include "sqlite_device_repository.h"
#include "sqlite_message_repository.h"
#include "sqlite_transfer_history_repository.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>

#include <memory>

class TestHistoryController : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testChatPaginationCursorAdvances();
    void testChatPaginationNoDuplicates();
    void testTransferFilterByDevice();
    void testTransferFilterByStatus();
    void testDeleteMessageOnlyAffectsTarget();
    void testDeleteConversationOnlyAffectsTarget();
    void testDeleteTransferOnlyAffectsTarget();
    void testClearAllMessagesPreservesDevices();
    void testClearAllTransfersPreservesDevices();
    void testRetentionDaysCleansExpired();
    void testConfigManagerRetentionDaysPersists();

private:
    std::unique_ptr<SqliteDatabaseBroker> openDatabase(const QString &relativePath);
    void seedDevice(SqliteDatabaseBroker &database, const QString &deviceId, const QString &name);
    void seedMessage(SqliteMessageRepository &repository, const QString &deviceId, const QString &msgId,
                     const QDateTime &sentAt, const QString &content = "test");
    void seedTransfer(SqliteTransferHistoryRepository &repository, const QString &sessionId,
                      const QString &peerDeviceId, const QString &status, const QDateTime &startedAt);
    int messageRowCount(SqliteDatabaseBroker &database);
    int transferRowCount(SqliteDatabaseBroker &database);
    int deviceRowCount(SqliteDatabaseBroker &database);

    QTemporaryDir _temporaryDir;
};

void TestHistoryController::initTestCase()
{
    QVERIFY(_temporaryDir.isValid());
    ApplicationPaths::coverForTest(_temporaryDir.path());
}

void TestHistoryController::cleanupTestCase()
{
    ApplicationPaths::clearTestCover();
}

void TestHistoryController::testChatPaginationCursorAdvances()
{
    auto database = openDatabase("cursor-advance.sqlite");
    QVERIFY(database);
    SqliteMessageRepository messageRepository(database.get());
    seedDevice(*database, "peer-A", "Device Alpha");

    const QDateTime base = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);
    for (int i = 0; i < 10; ++i) {
        seedMessage(messageRepository, "peer-A",
                    QStringLiteral("cursor-%1").arg(i),
                    base.addSecs(i * 60),
                    QStringLiteral("Message %1").arg(i));
    }

    ChatManager chat;
    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteTransferHistoryRepository transferRepository(database.get());
    HistoryController controller(&chat, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    QSignalSpy spy1(&controller, &HistoryController::messagesLoaded);
    controller.loadMoreMessages("peer-A");
    QVERIFY(spy1.wait(3000));
    QCOMPARE(spy1.first().at(0).toString(), QStringLiteral("peer-A"));

    QVariantList messages = chat.messagesForDevice("peer-A");
    QCOMPARE(messages.size(), 10);

    QSignalSpy spy2(&controller, &HistoryController::messagesLoaded);
    controller.loadMoreMessages("peer-A");
    QVERIFY(spy2.wait(3000));
    QCOMPARE(spy2.first().at(1).toBool(), false);

    QCOMPARE(chat.messagesForDevice("peer-A").size(), 10);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testChatPaginationNoDuplicates()
{
    auto database = openDatabase("no-dup.sqlite");
    QVERIFY(database);
    SqliteMessageRepository messageRepository(database.get());
    seedDevice(*database, "peer-B", "Device Bravo");

    const QDateTime base = QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs);
    seedMessage(messageRepository, "peer-B", "dup-001", base, "first");
    seedMessage(messageRepository, "peer-B", "dup-002", base.addSecs(60), "second");

    ChatManager chat;
    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteTransferHistoryRepository transferRepository(database.get());
    HistoryController controller(&chat, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    QSignalSpy spy(&controller, &HistoryController::messagesLoaded);
    controller.loadMoreMessages("peer-B");
    QVERIFY(spy.wait(3000));

    QCOMPARE(chat.messagesForDevice("peer-B").size(), 2);

    QSignalSpy spy2(&controller, &HistoryController::messagesLoaded);
    controller.loadMoreMessages("peer-B");
    QVERIFY(spy2.wait(3000));
    QCOMPARE(chat.messagesForDevice("peer-B").size(), 2);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testTransferFilterByDevice()
{
    auto database = openDatabase("filter-device.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryRepository transferRepository(database.get());
    seedDevice(*database, "peer-C", "Device Charlie");
    seedDevice(*database, "peer-D", "Device Delta");

    const QDateTime base = QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs);
    seedTransfer(transferRepository, "s-c1", "peer-C", "completed", base);
    seedTransfer(transferRepository, "s-d1", "peer-D", "completed", base.addSecs(60));

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteMessageRepository messageRepository(database.get());
    HistoryController controller(nullptr, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    QSignalSpy spy(&controller, &HistoryController::transfersChanged);
    QVariantMap filter;
    filter["peerDeviceId"] = "peer-C";
    controller.queryTransfers(filter);
    QVERIFY(spy.wait(3000));

    QVariantList transfers = controller.transfers();
    QCOMPARE(transfers.size(), 1);
    QCOMPARE(transfers.first().toMap().value("peerDeviceId").toString(),
             QStringLiteral("peer-C"));

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testTransferFilterByStatus()
{
    auto database = openDatabase("filter-status.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryRepository transferRepository(database.get());
    seedDevice(*database, "peer-E", "Device Echo");

    const QDateTime base = QDateTime::fromString("2026-06-25T13:00:00.000Z", Qt::ISODateWithMs);
    seedTransfer(transferRepository, "s-e1", "peer-E", "completed", base);
    seedTransfer(transferRepository, "s-e2", "peer-E", "failed", base.addSecs(60));
    seedTransfer(transferRepository, "s-e3", "peer-E", "cancelled", base.addSecs(120));

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteMessageRepository messageRepository(database.get());
    HistoryController controller(nullptr, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    QSignalSpy spy(&controller, &HistoryController::transfersChanged);
    QVariantMap filter;
    filter["status"] = "failed";
    controller.queryTransfers(filter);
    QVERIFY(spy.wait(3000));

    QVariantList transfers = controller.transfers();
    QCOMPARE(transfers.size(), 1);
    QCOMPARE(transfers.first().toMap().value("status").toString(),
             QStringLiteral("failed"));

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testDeleteMessageOnlyAffectsTarget()
{
    auto database = openDatabase("del-msg.sqlite");
    QVERIFY(database);
    SqliteMessageRepository messageRepository(database.get());
    seedDevice(*database, "peer-F", "Device Foxtrot");

    const QDateTime base = QDateTime::fromString("2026-06-25T14:00:00.000Z", Qt::ISODateWithMs);
    seedMessage(messageRepository, "peer-F", "del-001", base, "keep");
    seedMessage(messageRepository, "peer-F", "del-002", base.addSecs(60), "remove");

    ChatManager chat;
    MessageRecord m1;
    m1.messageId = "del-001";
    m1.peerDeviceId = "peer-F";
    m1.senderDeviceId = "self";
    m1.senderName = "Self";
    m1.content = "keep";
    m1.sentAt = base;
    m1.direction = RecordDirection::Outgoing;
    m1.localStatus = 1;
    m1.createdAt = base;
    MessageRecord m2 = m1;
    m2.messageId = "del-002";
    m2.content = "remove";
    m2.sentAt = base.addSecs(60);
    chat.prependHistoryMessages("peer-F", {m1, m2});

    QCOMPARE(chat.messagesForDevice("peer-F").size(), 2);

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteTransferHistoryRepository transferRepository(database.get());
    HistoryController controller(&chat, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    controller.deleteMessage("peer-F", "del-002");
    QTest::qWait(500);

    QCOMPARE(chat.messagesForDevice("peer-F").size(), 1);
    QCOMPARE(messageRowCount(*database), 1);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testDeleteConversationOnlyAffectsTarget()
{
    auto database = openDatabase("del-conv.sqlite");
    QVERIFY(database);
    SqliteMessageRepository messageRepository(database.get());
    seedDevice(*database, "peer-G", "Device Golf");
    seedDevice(*database, "peer-H", "Device Hotel");

    const QDateTime base = QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs);
    seedMessage(messageRepository, "peer-G", "gc-001", base, "to-delete");
    seedMessage(messageRepository, "peer-H", "hc-001", base.addSecs(60), "to-keep");

    ChatManager chat;
    MessageRecord mg;
    mg.messageId = "gc-001";
    mg.peerDeviceId = "peer-G";
    mg.senderDeviceId = "self";
    mg.senderName = "Self";
    mg.content = "to-delete";
    mg.sentAt = base;
    mg.direction = RecordDirection::Outgoing;
    mg.localStatus = 1;
    mg.createdAt = base;
    chat.prependHistoryMessages("peer-G", {mg});
    MessageRecord mh;
    mh.messageId = "hc-001";
    mh.peerDeviceId = "peer-H";
    mh.senderDeviceId = "self";
    mh.senderName = "Self";
    mh.content = "to-keep";
    mh.sentAt = base.addSecs(60);
    mh.direction = RecordDirection::Outgoing;
    mh.localStatus = 1;
    mh.createdAt = base.addSecs(60);
    chat.prependHistoryMessages("peer-H", {mh});

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteTransferHistoryRepository transferRepository(database.get());
    HistoryController controller(&chat, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    controller.deleteConversation("peer-G");
    QTest::qWait(500);

    QVERIFY(chat.messagesForDevice("peer-G").isEmpty());
    QCOMPARE(chat.messagesForDevice("peer-H").size(), 1);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testDeleteTransferOnlyAffectsTarget()
{
    auto database = openDatabase("del-transfer.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryRepository transferRepository(database.get());
    seedDevice(*database, "peer-I", "Device India");

    const QDateTime base = QDateTime::fromString("2026-06-25T16:00:00.000Z", Qt::ISODateWithMs);
    seedTransfer(transferRepository, "s-i1", "peer-I", "completed", base);
    seedTransfer(transferRepository, "s-i2", "peer-I", "failed", base.addSecs(60));

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteMessageRepository messageRepository(database.get());
    HistoryController controller(nullptr, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    QSignalSpy spy(&controller, &HistoryController::transfersChanged);
    controller.queryTransfers();
    QVERIFY(spy.wait(3000));
    QCOMPARE(controller.transfers().size(), 2);

    controller.deleteTransfer("record-s-i1");
    QTest::qWait(500);

    QCOMPARE(controller.transfers().size(), 1);
    QCOMPARE(transferRowCount(*database), 1);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testClearAllMessagesPreservesDevices()
{
    auto database = openDatabase("clear-msg.sqlite");
    QVERIFY(database);
    SqliteMessageRepository messageRepository(database.get());
    seedDevice(*database, "peer-J", "Device Juliet");

    const QDateTime base = QDateTime::fromString("2026-06-25T17:00:00.000Z", Qt::ISODateWithMs);
    seedMessage(messageRepository, "peer-J", "cj-001", base);

    ChatManager chat;
    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteTransferHistoryRepository transferRepository(database.get());
    HistoryController controller(&chat, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    controller.clearAllMessages();
    QTest::qWait(500);

    QCOMPARE(messageRowCount(*database), 0);
    QCOMPARE(deviceRowCount(*database), 1);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testClearAllTransfersPreservesDevices()
{
    auto database = openDatabase("clear-transfer.sqlite");
    QVERIFY(database);
    SqliteTransferHistoryRepository transferRepository(database.get());
    seedDevice(*database, "peer-K", "Device Kilo");

    const QDateTime base = QDateTime::fromString("2026-06-25T18:00:00.000Z", Qt::ISODateWithMs);
    seedTransfer(transferRepository, "s-k1", "peer-K", "completed", base);

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    SqliteMessageRepository messageRepository(database.get());
    HistoryController controller(nullptr, nullptr, nullptr, &worker,
                                 &messageRepository, &transferRepository);

    controller.clearAllTransfers();
    QTest::qWait(500);

    QCOMPARE(transferRowCount(*database), 0);
    QCOMPARE(deviceRowCount(*database), 1);

    workerThread.quit();
    workerThread.wait();
}

void TestHistoryController::testRetentionDaysCleansExpired()
{
    auto database = openDatabase("retention.sqlite");
    QVERIFY(database);
    SqliteMessageRepository messageRepository(database.get());
    SqliteTransferHistoryRepository transferRepository(database.get());
    seedDevice(*database, "peer-L", "Device Lima");

    const QDateTime old = QDateTime::currentDateTimeUtc().addDays(-30);
    const QDateTime recent = QDateTime::currentDateTimeUtc().addDays(-3);
    seedMessage(messageRepository, "peer-L", "old-msg", old, "old");
    seedMessage(messageRepository, "peer-L", "recent-msg", recent, "recent");
    seedTransfer(transferRepository, "s-old", "peer-L", "completed", old);
    seedTransfer(transferRepository, "s-recent", "peer-L", "completed", recent);

    QTemporaryDir configDir;
    qputenv("GRIDYARD_CONFIG", (configDir.path() + "/retention.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "RetentionTest");
    ConfigManager *config = ConfigManager::create(nullptr, nullptr);
    config->setRetentionDays(7);

    DatabaseWorker worker(database.get());
    QThread workerThread;
    worker.moveToThread(&workerThread);
    workerThread.start();

    ChatManager chat;
    HistoryController controller(&chat, nullptr, config, &worker,
                                 &messageRepository, &transferRepository);

    controller.cleanupExpiredRecords();
    QTest::qWait(500);

    QCOMPARE(messageRowCount(*database), 1);
    QCOMPARE(transferRowCount(*database), 1);
    QCOMPARE(deviceRowCount(*database), 1);

    workerThread.quit();
    workerThread.wait();
    delete config;
    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

void TestHistoryController::testConfigManagerRetentionDaysPersists()
{
    QTemporaryDir temp;
    qputenv("GRIDYARD_CONFIG", (temp.path() + "/persist.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "PersistTest");

    {
        ConfigManager *config = ConfigManager::create(nullptr, nullptr);
        config->setRetentionDays(30);
        QCOMPARE(config->retentionDays(), 30);
        delete config;
    }

    {
        ConfigManager *config = ConfigManager::create(nullptr, nullptr);
        QCOMPARE(config->retentionDays(), 30);
        delete config;
    }

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

std::unique_ptr<SqliteDatabaseBroker> TestHistoryController::openDatabase(
    const QString &relativePath)
{
    auto database = std::make_unique<SqliteDatabaseBroker>();
    QString error;
    if (!database->initialize(_temporaryDir.path() + "/" + relativePath, &error)) {
        qWarning() << error;
        return nullptr;
    }
    return database;
}

void TestHistoryController::seedDevice(SqliteDatabaseBroker &database,
                                       const QString &deviceId, const QString &name)
{
    SqliteDeviceRepository repository(&database);
    PeerRecord peer;
    peer.deviceId = deviceId;
    peer.deviceName = name;
    peer.lastIpAddress = "192.168.1.10";
    peer.lastTcpPort = 35100;
    peer.firstSeenAt = QDateTime::fromString("2026-06-25T09:00:00.000Z", Qt::ISODateWithMs);
    peer.lastSeenAt = peer.firstSeenAt;
    QString error;
    repository.upsertPeer(peer, &error);
}

void TestHistoryController::seedMessage(SqliteMessageRepository &repository, const QString &deviceId,
                                        const QString &msgId, const QDateTime &sentAt,
                                        const QString &content)
{
    MessageRecord record;
    record.messageId = msgId;
    record.peerDeviceId = deviceId;
    record.direction = RecordDirection::Outgoing;
    record.senderDeviceId = "self";
    record.senderName = "Self Device";
    record.content = content;
    record.sentAt = sentAt;
    record.localStatus = 1;
    record.createdAt = sentAt;
    QString error;
    repository.saveMessage(record, &error);
}

void TestHistoryController::seedTransfer(SqliteTransferHistoryRepository &repository,
                                         const QString &sessionId,
                                         const QString &peerDeviceId,
                                         const QString &status,
                                         const QDateTime &startedAt)
{
    TransferRecord record;
    record.recordId = QStringLiteral("record-%1").arg(sessionId);
    record.sessionId = sessionId;
    record.peerDeviceId = peerDeviceId;
    record.peerName = QStringLiteral("Peer %1").arg(peerDeviceId);
    record.direction = RecordDirection::Outgoing;
    record.displayName = "test.zip";
    record.isDirectory = false;
    record.fileCount = 1;
    record.totalBytes = 1024;
    record.status = status;
    record.startedAt = startedAt;
    record.finishedAt = startedAt.addSecs(30);
    record.errorCode = 0;
    QString error;
    repository.upsertFinishedTransfer(record, &error);
}

int TestHistoryController::messageRowCount(SqliteDatabaseBroker &database)
{
    QString error;
    QSqlDatabase conn = database.connectionForWorkerThread(&error);
    if (!conn.isValid())
        return -1;
    QSqlQuery query(conn);
    if (!query.exec("SELECT COUNT(*) FROM chat_messages"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

int TestHistoryController::transferRowCount(SqliteDatabaseBroker &database)
{
    QString error;
    QSqlDatabase conn = database.connectionForWorkerThread(&error);
    if (!conn.isValid())
        return -1;
    QSqlQuery query(conn);
    if (!query.exec("SELECT COUNT(*) FROM transfer_history"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

int TestHistoryController::deviceRowCount(SqliteDatabaseBroker &database)
{
    QString error;
    QSqlDatabase conn = database.connectionForWorkerThread(&error);
    if (!conn.isValid())
        return -1;
    QSqlQuery query(conn);
    if (!query.exec("SELECT COUNT(*) FROM peer_devices"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

QTEST_MAIN(TestHistoryController)
#include "test_history_controller.moc"
