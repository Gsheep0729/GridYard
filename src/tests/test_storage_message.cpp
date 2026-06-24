/**
* @file    test_storage_message.cpp
* @version 6.2.0
* @date    2026-06-25
* @author  GY
* @brief   SQLite 聊天消息 Repository 测试
*
* 覆盖单条往返、幂等性、游标分页、中文/多行/emoji 内容、
* deleteConversation CASCADE、deleteExpiredMessages 和重启恢复。
*
* Change Log:
* [v6.2.0] GY   2026-06-25
* * 新增聊天消息 SQLite Repository 测试
*/

#include <QtTest/QtTest>

#include "application_paths.h"
#include "history_records.h"
#include "sqlite_database_broker.h"
#include "sqlite_device_repository.h"
#include "sqlite_message_repository.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>

class TestStorageMessage : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSaveAndLoad();
    void testIdempotentSave();
    void testPagination();
    void testChineseAndEmoji();
    void testDeleteConversation();
    void testDeleteExpiredMessages();
    void testReopenDatabase();

private:
    std::unique_ptr<SqliteDatabaseBroker> openDatabase(const QString &relativePath);
    void seedDevice(SqliteDatabaseBroker &database, const QString &deviceId, const QString &name);
    MessageRecord makeRecord(const QString &deviceId, const QString &msgId,
                             RecordDirection direction, const QString &content,
                             const QDateTime &sentAt);
    int messageRowCount(SqliteDatabaseBroker &database);
    int conversationRowCount(SqliteDatabaseBroker &database);

    QTemporaryDir _temporaryDir;
    QString _databasePath;
};

void TestStorageMessage::initTestCase()
{
    QVERIFY(_temporaryDir.isValid());
    ApplicationPaths::coverForTest(_temporaryDir.path());
    _databasePath = _temporaryDir.path() + "/message-test.sqlite";
}

void TestStorageMessage::cleanupTestCase()
{
    ApplicationPaths::clearTestCover();
}

// 单条消息写入后读取，字段一致
void TestStorageMessage::testSaveAndLoad()
{
    auto database = openDatabase("save-load.sqlite");
    QVERIFY(database);
    SqliteMessageRepository repository(database.get());
    seedDevice(*database, "peer-A", "Device Alpha");

    const QDateTime sentAt = QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs);
    const MessageRecord record = makeRecord("peer-A", "msg-001", RecordDirection::Outgoing,
                                            "Hello world", sentAt);
    QString error;
    QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));

    MessageCursor cursor;
    cursor.peerDeviceId = "peer-A";
    const QList<MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QVERIFY2(!loaded.isEmpty(), qPrintable(error));
    const MessageRecord &loadedMsg = loaded.first();
    QCOMPARE(loadedMsg.messageId, QStringLiteral("msg-001"));
    QCOMPARE(loadedMsg.peerDeviceId, QStringLiteral("peer-A"));
    QCOMPARE(loadedMsg.direction, RecordDirection::Outgoing);
    QCOMPARE(loadedMsg.senderDeviceId, QStringLiteral("self"));
    QCOMPARE(loadedMsg.senderName, QStringLiteral("Self Device"));
    QCOMPARE(loadedMsg.content, QStringLiteral("Hello world"));
    QVERIFY(loadedMsg.sentAt.toUTC() == sentAt.toUTC());
}

// 同一 message_id 多次 save 只产生一行
void TestStorageMessage::testIdempotentSave()
{
    auto database = openDatabase("idempotent.sqlite");
    QVERIFY(database);
    SqliteMessageRepository repository(database.get());
    seedDevice(*database, "peer-B", "Device Bravo");

    const QDateTime sentAt = QDateTime::fromString("2026-06-25T11:00:00.000Z", Qt::ISODateWithMs);
    const MessageRecord record = makeRecord("peer-B", "msg-002", RecordDirection::Incoming,
                                            "Repeated", sentAt);

    QString error;
    QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));
    QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));
    QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));

    // 只能查到一条
    MessageCursor cursor;
    cursor.peerDeviceId = "peer-B";
    const QList<MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QCOMPARE(loaded.size(), 1);

    // 数据库实际也只有一行
    QVERIFY(database->isAvailable());
    QSqlDatabase conn = database->connectionForWorkerThread(&error);
    QVERIFY(conn.isValid());
    QSqlQuery query(conn);
    QVERIFY(query.exec("SELECT COUNT(*) FROM chat_messages WHERE message_id='msg-002'"));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

// 游标分页按时间倒序取消息
void TestStorageMessage::testPagination()
{
    auto database = openDatabase("pagination.sqlite");
    QVERIFY(database);
    SqliteMessageRepository repository(database.get());
    seedDevice(*database, "peer-C", "Device Charlie");

    QString error;
    const QDateTime base = QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs);

    // 写入 5 条消息，时间递增
    for (int i = 0; i < 5; ++i) {
        const QString msgId = QStringLiteral("page-msg-%1").arg(i);
        const QDateTime t = base.addSecs(i * 60);
        const MessageRecord record = makeRecord("peer-C", msgId, RecordDirection::Outgoing,
                                                QStringLiteral("Message %1").arg(i), t);
        QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));
    }

    // 首次加载应拿到最新 3 条（倒序）
    MessageCursor cursor;
    cursor.peerDeviceId = "peer-C";
    const QList<MessageRecord> firstPage = repository.loadMessages(cursor, 3, &error);
    QVERIFY2(firstPage.size() == 3, qPrintable(error));
    QCOMPARE(firstPage.at(0).messageId, QStringLiteral("page-msg-4"));
    QCOMPARE(firstPage.at(1).messageId, QStringLiteral("page-msg-3"));
    QCOMPARE(firstPage.at(2).messageId, QStringLiteral("page-msg-2"));

    // 使用最后一条消息的 sentAt 和 messageId 构建游标继续加载
    const MessageRecord &lastOnPage = firstPage.last();
    cursor.beforeSentAt = lastOnPage.sentAt;
    cursor.beforeMessageId = lastOnPage.messageId;
    const QList<MessageRecord> secondPage = repository.loadMessages(cursor, 3, &error);
    QVERIFY2(secondPage.size() == 2, qPrintable(error));
    QCOMPARE(secondPage.at(0).messageId, QStringLiteral("page-msg-1"));
    QCOMPARE(secondPage.at(1).messageId, QStringLiteral("page-msg-0"));
}

// 中文、多行和 emoji 内容正确保存与读取
void TestStorageMessage::testChineseAndEmoji()
{
    auto database = openDatabase("unicode.sqlite");
    QVERIFY(database);
    SqliteMessageRepository repository(database.get());
    seedDevice(*database, "peer-D", "Device Delta");

    const QStringList samples = {
        QString::fromUtf8("你好世界"),
        QString::fromUtf8("第一行\n第二行\n第三行"),
        QString::fromUtf8("表情😀👍🎉"),
        QString::fromUtf8("混合：Hello 你好 😄\nNew line"),
    };

    const QDateTime sentAt = QDateTime::fromString("2026-06-25T13:00:00.000Z", Qt::ISODateWithMs);
    QString error;
    for (int i = 0; i < samples.size(); ++i) {
        const QString msgId = QStringLiteral("unicode-%1").arg(i);
        const MessageRecord record = makeRecord("peer-D", msgId, RecordDirection::Outgoing,
                                                samples[i], sentAt.addSecs(i * 10));
        QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));
    }

    MessageCursor cursor;
    cursor.peerDeviceId = "peer-D";
    const QList<MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QVERIFY2(loaded.size() == samples.size(), qPrintable(error));
    for (int i = 0; i < samples.size(); ++i) {
        const QString msgId = QStringLiteral("unicode-%1").arg(i);
        const auto it = std::find_if(loaded.cbegin(), loaded.cend(),
                                     [&msgId](const MessageRecord &r) { return r.messageId == msgId; });
        QVERIFY2(it != loaded.cend(), qPrintable(QString("msgId %1 not found").arg(msgId)));
        // 内容必须完全一致
        QVERIFY2(it->content == samples[i],
                 qPrintable(QString("Content mismatch for %1:\nwant: %2\ngot: %3")
                            .arg(msgId).arg(samples[i]).arg(it->content)));
    }
}

// deleteConversation 同时清理消息和会话行
void TestStorageMessage::testDeleteConversation()
{
    auto database = openDatabase("delete-conv.sqlite");
    QVERIFY(database);
    SqliteMessageRepository repository(database.get());
    seedDevice(*database, "peer-E", "Device Echo");

    const QDateTime sentAt = QDateTime::fromString("2026-06-25T14:00:00.000Z", Qt::ISODateWithMs);
    QString error;
    for (int i = 0; i < 3; ++i) {
        const MessageRecord record = makeRecord("peer-E",
                                                QStringLiteral("del-msg-%1").arg(i),
                                                RecordDirection::Incoming,
                                                QStringLiteral("To be deleted %1").arg(i),
                                                sentAt.addSecs(i * 30));
        QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));
    }

    // 确认数据和会话行已写入
    QVERIFY2(messageRowCount(*database) == 3, "messages not saved");
    QVERIFY2(conversationRowCount(*database) == 1, "conversation not created");

    // 删除会话
    QVERIFY2(repository.deleteConversation("peer-E", &error), qPrintable(error));

    // 消息和会话行均应清空
    QVERIFY2(messageRowCount(*database) == 0, "messages not deleted");
    QVERIFY2(conversationRowCount(*database) == 0, "conversation not deleted");

    // 重新加载应为空
    MessageCursor cursor;
    cursor.peerDeviceId = "peer-E";
    const QList<MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QVERIFY2(loaded.isEmpty(), "should return empty after delete");
}

// deleteExpiredMessages 只删除早于指定时间的消息
void TestStorageMessage::testDeleteExpiredMessages()
{
    auto database = openDatabase("expire.sqlite");
    QVERIFY(database);
    SqliteMessageRepository repository(database.get());
    seedDevice(*database, "peer-F", "Device Foxtrot");

    const QDateTime base = QDateTime::fromString("2026-06-25T15:00:00.000Z", Qt::ISODateWithMs);
    const QStringList msgIds = {"expire-0", "expire-1", "expire-2", "expire-3"};
    QString error;

    // 前两条在截止时间之前，后两条在截止时间之后
    for (int i = 0; i < msgIds.size(); ++i) {
        const QDateTime t = base.addSecs(i * 60);
        const MessageRecord record = makeRecord("peer-F", msgIds[i], RecordDirection::Outgoing,
                                                QStringLiteral("Expire test %1").arg(i), t);
        QVERIFY2(repository.saveMessage(record, &error), qPrintable(error));
    }

    // 截止时间是 base + 90s（expire-0 和 expire-1 应被删除）
    const QDateTime cutoff = base.addSecs(90);
    QVERIFY2(repository.deleteExpiredMessages(cutoff, &error), qPrintable(error));

    // 剩下 2 条
    MessageCursor cursor;
    cursor.peerDeviceId = "peer-F";
    const QList<MessageRecord> remaining = repository.loadMessages(cursor, 50, &error);
    QVERIFY2(remaining.size() == 2, qPrintable(error));

    // 剩下的应该是 expire-2 和 expire-3
    QList<QString> remainingIds;
    for (const auto &r : remaining) {
        remainingIds.append(r.messageId);
    }
    QVERIFY2(remainingIds.contains("expire-2"), "expire-2 should remain");
    QVERIFY2(remainingIds.contains("expire-3"), "expire-3 should remain");
    QVERIFY2(!remainingIds.contains("expire-0"), "expire-0 should be deleted");
    QVERIFY2(!remainingIds.contains("expire-1"), "expire-1 should be deleted");
}

// 重启后消息历史可恢复
void TestStorageMessage::testReopenDatabase()
{
    const QString path = _temporaryDir.path() + "/reopen-msg.sqlite";

    {
        SqliteDatabaseBroker database;
        QString error;
        QVERIFY2(database.initialize(path, &error), qPrintable(error));

        SqliteDeviceRepository deviceRepository(&database);
        PeerRecord peer;
        peer.deviceId = "peer-G";
        peer.deviceName = "Device Golf";
        peer.firstSeenAt = QDateTime::fromString("2026-06-25T16:00:00.000Z", Qt::ISODateWithMs);
        peer.lastSeenAt = peer.firstSeenAt;
        QVERIFY2(deviceRepository.upsertPeer(peer, &error), qPrintable(error));

        SqliteMessageRepository messageRepository(&database);
        const QDateTime sentAt = QDateTime::fromString("2026-06-25T16:30:00.000Z", Qt::ISODateWithMs);
        const MessageRecord record = makeRecord("peer-G", "reopen-001", RecordDirection::Incoming,
                                               "Survives restart", sentAt);
        QVERIFY2(messageRepository.saveMessage(record, &error), qPrintable(error));
    }

    // 重新打开数据库
    SqliteDatabaseBroker reopened;
    QString error;
    QVERIFY2(reopened.initialize(path, &error), qPrintable(error));
    SqliteMessageRepository repository(&reopened);
    MessageCursor cursor;
    cursor.peerDeviceId = "peer-G";
    const QList<MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QVERIFY2(!loaded.isEmpty(), "messages should survive restart");
    QCOMPARE(loaded.first().messageId, QStringLiteral("reopen-001"));
    QCOMPARE(loaded.first().content, QStringLiteral("Survives restart"));
}

// 工具方法：在临时目录中创建并初始化数据库
std::unique_ptr<SqliteDatabaseBroker> TestStorageMessage::openDatabase(const QString &relativePath)
{
    auto database = std::make_unique<SqliteDatabaseBroker>();
    const QString path = _temporaryDir.path() + "/" + relativePath;
    QString error;
    if (!database->initialize(path, &error)) {
        qWarning() << "数据库初始化失败:" << error;
        return nullptr;
    }
    return database;
}

// 工具方法：向数据库写入一个设备记录（消息 Repository 依赖 peer_devices FK）
void TestStorageMessage::seedDevice(SqliteDatabaseBroker &database,
                                     const QString &deviceId,
                                     const QString &name)
{
    SqliteDeviceRepository deviceRepository(&database);
    PeerRecord peer;
    peer.deviceId = deviceId;
    peer.deviceName = name;
    peer.firstSeenAt = QDateTime::fromString("2026-06-25T09:00:00.000Z", Qt::ISODateWithMs);
    peer.lastSeenAt = peer.firstSeenAt;
    QString error;
    QVERIFY2(deviceRepository.upsertPeer(peer, &error), qPrintable(error));
}

// 工具方法：构造一条固定字段的消息记录
MessageRecord TestStorageMessage::makeRecord(const QString &deviceId, const QString &msgId,
                                              RecordDirection direction, const QString &content,
                                              const QDateTime &sentAt)
{
    MessageRecord record;
    record.messageId = msgId;
    record.peerDeviceId = deviceId;
    record.direction = direction;
    record.senderDeviceId = QStringLiteral("self");
    record.senderName = QStringLiteral("Self Device");
    record.content = content;
    record.sentAt = sentAt;
    record.localStatus = 1;
    record.createdAt = QDateTime::currentDateTimeUtc();
    return record;
}

// 工具方法：统计 chat_messages 行数
int TestStorageMessage::messageRowCount(SqliteDatabaseBroker &database)
{
    QString error;
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    if (!connection.isValid())
        return -1;
    QSqlQuery query(connection);
    if (!query.exec("SELECT COUNT(*) FROM chat_messages"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

// 工具方法：统计 chat_conversations 行数
int TestStorageMessage::conversationRowCount(SqliteDatabaseBroker &database)
{
    QString error;
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    if (!connection.isValid())
        return -1;
    QSqlQuery query(connection);
    if (!query.exec("SELECT COUNT(*) FROM chat_conversations"))
        return -1;
    if (!query.next())
        return -1;
    return query.value(0).toInt();
}

QTEST_MAIN(TestStorageMessage)
#include "test_storage_message.moc"
