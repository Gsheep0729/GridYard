/**
* @file    test_storage_message.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   聊天消息仓储测试
*
* 写入读取幂等、游标分页、UTF-8 内容、会话级联删除与过期清理语义
* 直接验证纯 C++ 消息仓储（MessageRepositoryCore）。旧 Qt 版仓储已随
* gy_storage 退役。
*/

#include <QtTest/QtTest>

#include <QDateTime>
#include <QTemporaryDir>

#include <memory>

#include "application_paths.h"
#include "db_seed.h"

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/message_repository_core.h"

class TestStorageMessage : public QObject {
private:
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testCoreSaveLoadAndIdempotency();
    void testCorePagination();
    void testCoreChineseAndEmoji();
    void testCoreDeleteConversationAndExpired();
    void testCoreReopenDatabase();

private:
    std::unique_ptr<gy::storage::DatabaseCore> openDatabase(const QString &relativePath);

    QTemporaryDir _temporaryDir;
};

void TestStorageMessage::initTestCase()
{
    QVERIFY(_temporaryDir.isValid());
    ApplicationPaths::coverForTest(_temporaryDir.path());
}

void TestStorageMessage::cleanupTestCase()
{
    ApplicationPaths::clearTestCover();
}

// 写入读取幂等：字段往返一致、重复保存单行
void TestStorageMessage::testCoreSaveLoadAndIdempotency()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-msg-basic.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::MessageRepositoryCore repository(database.get());

    core::PeerRecord peer;
    peer.deviceId = "core-peer-A";
    peer.deviceName = "Device Alpha";
    peer.lastIpAddress = "192.168.1.60";
    peer.lastTcpPort = 35100;
    peer.firstSeenAtMs = 1770000000000;
    peer.lastSeenAtMs = 1770000000000;
    std::string error;
    QVERIFY(devices.upsertPeer(peer, &error));

    core::MessageRecord record;
    record.messageId = "core-msg-001";
    record.peerDeviceId = "core-peer-A";
    record.direction = core::RecordDirection::Outgoing;
    record.senderDeviceId = "self";
    record.senderName = "Self Device";
    record.content = "Hello world";
    record.sentAtMs =
        QDateTime::fromString("2026-06-25T10:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    record.localStatus = 3;
    record.createdAtMs = record.sentAtMs + 500;
    QVERIFY(repository.saveMessage(record, &error));
    QVERIFY(repository.saveMessage(record, &error));  // 幂等：重复帧单行

    core::MessageCursor cursor;
    cursor.peerDeviceId = "core-peer-A";
    const std::vector<core::MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QCOMPARE(int(loaded.size()), 1);
    QCOMPARE(QString::fromStdString(loaded.front().messageId), QStringLiteral("core-msg-001"));
    QCOMPARE(loaded.front().direction, core::RecordDirection::Outgoing);
    QCOMPARE(QString::fromStdString(loaded.front().senderName), QStringLiteral("Self Device"));
    QCOMPARE(QString::fromStdString(loaded.front().content), QStringLiteral("Hello world"));
    QCOMPARE(loaded.front().sentAtMs, record.sentAtMs);
    QCOMPARE(loaded.front().createdAtMs, record.createdAtMs);
    QCOMPARE(loaded.front().localStatus, 3);

    // 数据库实际也只有一行
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_messages")), 1);
}

// 分页：首屏最新页与同毫秒次键游标不重不漏
void TestStorageMessage::testCorePagination()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-msg-page.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::MessageRepositoryCore repository(database.get());

    core::PeerRecord peer;
    peer.deviceId = "core-peer-P";
    peer.deviceName = "Pager";
    peer.firstSeenAtMs = 1770000000000;
    peer.lastSeenAtMs = 1770000000000;
    std::string error;
    QVERIFY(devices.upsertPeer(peer, &error));

    // 同一毫秒三条 + 更早一条：翻页稳定性由 (sent_at, message_id) 次键保证
    const int64_t sameMs =
        QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const char *ids[] = {"page-1", "page-2", "page-3"};
    for (const char *id : ids) {
        core::MessageRecord record;
        record.messageId = id;
        record.peerDeviceId = peer.deviceId;
        record.senderDeviceId = peer.deviceId;
        record.senderName = "Pager";
        record.content = "同毫秒消息";
        record.sentAtMs = sameMs;
        record.localStatus = 0;
        record.createdAtMs = sameMs;
        QVERIFY(repository.saveMessage(record, &error));
    }
    core::MessageRecord older;
    older.messageId = "page-0";
    older.peerDeviceId = peer.deviceId;
    older.senderDeviceId = peer.deviceId;
    older.senderName = "Pager";
    older.content = "更早消息";
    older.sentAtMs = sameMs - 1000;
    older.localStatus = 0;
    older.createdAtMs = sameMs - 1000;
    QVERIFY(repository.saveMessage(older, &error));

    core::MessageCursor cursor;
    cursor.peerDeviceId = peer.deviceId;
    const std::vector<core::MessageRecord> firstPage = repository.loadMessages(cursor, 2, &error);
    QCOMPARE(int(firstPage.size()), 2);
    QCOMPARE(QString::fromStdString(firstPage.at(0).messageId), QStringLiteral("page-3"));
    QCOMPARE(QString::fromStdString(firstPage.at(1).messageId), QStringLiteral("page-2"));

    // 时间 + message_id 二元组游标：翻出剩余两条且不重复
    cursor.beforeSentAtMs = firstPage.back().sentAtMs;
    cursor.beforeMessageId = firstPage.back().messageId;
    const std::vector<core::MessageRecord> secondPage = repository.loadMessages(cursor, 2, &error);
    QCOMPARE(int(secondPage.size()), 2);
    QCOMPARE(QString::fromStdString(secondPage.at(0).messageId), QStringLiteral("page-1"));
    QCOMPARE(QString::fromStdString(secondPage.at(1).messageId), QStringLiteral("page-0"));

    // 空会话与非正页大小不发起查询
    core::MessageCursor invalid;
    invalid.peerDeviceId = "no-such-peer";
    QVERIFY(repository.loadMessages(invalid, 10, &error).empty());
    invalid.peerDeviceId = peer.deviceId;
    QVERIFY(repository.loadMessages(invalid, 0, &error).empty());
}

// 中文、多行与表情内容按 UTF-8 往返无损
void TestStorageMessage::testCoreChineseAndEmoji()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-msg-utf8.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::MessageRepositoryCore repository(database.get());

    core::PeerRecord peer;
    peer.deviceId = "core-peer-U";
    peer.deviceName = "中文名设备";
    peer.firstSeenAtMs = 1770000000000;
    peer.lastSeenAtMs = 1770000000000;
    std::string error;
    QVERIFY(devices.upsertPeer(peer, &error));

    const QStringList samples = {
        QString::fromUtf8("中文消息 with emoji 🎉🚀 and symbols <>\"'&"),
        QString::fromUtf8("第一行\n第二行\n第三行"),
        QString::fromUtf8("表情😀👍🎉"),
    };
    for (int i = 0; i < samples.size(); ++i) {
        core::MessageRecord record;
        record.messageId = QStringLiteral("core-msg-utf8-%1").arg(i).toStdString();
        record.peerDeviceId = peer.deviceId;
        record.senderDeviceId = peer.deviceId;
        record.senderName = "中文名设备";
        record.content = samples[i].toStdString();
        record.sentAtMs = 1770000100000 + i * 1000;
        record.localStatus = 0;
        record.createdAtMs = record.sentAtMs;
        QVERIFY(repository.saveMessage(record, &error));
    }

    core::MessageCursor cursor;
    cursor.peerDeviceId = peer.deviceId;
    const std::vector<core::MessageRecord> loaded = repository.loadMessages(cursor, 10, &error);
    QCOMPARE(int(loaded.size()), samples.size());
    for (const auto &message : loaded) {
        const int index = QString::fromStdString(message.messageId).right(1).toInt();
        QVERIFY2(index >= 0 && index < samples.size(), "意外的消息标识");
        QCOMPARE(QString::fromStdString(message.content), samples[index]);
    }
}

// 会话级联删除与过期清理的严格小于边界
void TestStorageMessage::testCoreDeleteConversationAndExpired()
{
    namespace core = gy::storage;
    auto database = openDatabase("core-msg-del.sqlite");
    QVERIFY(database);
    core::DeviceRepositoryCore devices(database.get());
    core::MessageRepositoryCore repository(database.get());

    core::PeerRecord peer;
    peer.deviceId = "core-peer-D";
    peer.deviceName = "Deleter";
    peer.firstSeenAtMs = 1770000000000;
    peer.lastSeenAtMs = 1770000000000;
    std::string error;
    QVERIFY(devices.upsertPeer(peer, &error));

    const int64_t cutoff =
        QDateTime::fromString("2026-06-25T12:00:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();
    const auto seed = [&](const char *id, int64_t sentAt) {
        core::MessageRecord record;
        record.messageId = id;
        record.peerDeviceId = peer.deviceId;
        record.senderDeviceId = peer.deviceId;
        record.senderName = "Deleter";
        record.content = "删除用消息";
        record.sentAtMs = sentAt;
        record.localStatus = 0;
        record.createdAtMs = sentAt;
        return repository.saveMessage(record, &error);
    };
    QVERIFY(seed("del-old", cutoff - 1));
    QVERIFY(seed("del-edge", cutoff));
    QVERIFY(seed("del-new", cutoff + 1));

    // 边界记录保留：严格早于截止时间的才被清理
    QVERIFY(repository.deleteExpiredMessages(cutoff, &error));
    core::MessageCursor cursor;
    cursor.peerDeviceId = peer.deviceId;
    std::vector<core::MessageRecord> remaining = repository.loadMessages(cursor, 10, &error);
    QCOMPARE(int(remaining.size()), 2);

    // 会话删除同时清理消息与会话行，重新加载为空
    QVERIFY(repository.deleteConversation(peer.deviceId, &error));
    remaining = repository.loadMessages(cursor, 10, &error);
    QCOMPARE(int(remaining.size()), 0);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_messages")), 0);
    QCOMPARE(gy::test::tableRowCount(*database, QStringLiteral("chat_conversations")), 0);

    // 清空接口在空库上幂等成功
    QVERIFY(repository.clearAllMessages(&error));
}

// 重启后消息历史可恢复
void TestStorageMessage::testCoreReopenDatabase()
{
    namespace core = gy::storage;
    const int64_t sentAt =
        QDateTime::fromString("2026-06-25T16:30:00.000Z", Qt::ISODateWithMs).toMSecsSinceEpoch();

    {
        auto database = openDatabase("reopen-msg.sqlite");
        QVERIFY(database);
        QVERIFY(gy::test::seedDevice(*database, QStringLiteral("peer-G"),
                                     QStringLiteral("Device Golf"),
                                     QDateTime::fromString("2026-06-25T16:00:00.000Z",
                                                           Qt::ISODateWithMs)));
        core::MessageRepositoryCore repository(database.get());

        core::MessageRecord record;
        record.messageId = "reopen-001";
        record.peerDeviceId = "peer-G";
        record.direction = core::RecordDirection::Incoming;
        record.senderDeviceId = "peer-G";
        record.senderName = "Device Golf";
        record.content = "Survives restart";
        record.sentAtMs = sentAt;
        record.localStatus = 1;
        record.createdAtMs = sentAt;
        std::string error;
        QVERIFY(repository.saveMessage(record, &error));
    }

    // 重新打开数据库，消息历史应完整恢复
    auto reopened = openDatabase("reopen-msg.sqlite");
    QVERIFY(reopened);
    core::MessageRepositoryCore repository(reopened.get());
    core::MessageCursor cursor;
    cursor.peerDeviceId = "peer-G";
    std::string error;
    const std::vector<core::MessageRecord> loaded = repository.loadMessages(cursor, 50, &error);
    QCOMPARE(int(loaded.size()), 1);
    QCOMPARE(QString::fromStdString(loaded.front().messageId), QStringLiteral("reopen-001"));
    QCOMPARE(QString::fromStdString(loaded.front().content), QStringLiteral("Survives restart"));
}

// 工具方法：在临时目录中打开（或创建）核心数据库
std::unique_ptr<gy::storage::DatabaseCore> TestStorageMessage::openDatabase(
    const QString &relativePath)
{
    return gy::test::openDatabase(_temporaryDir.path(), relativePath);
}

QTEST_MAIN(TestStorageMessage)
#include "test_storage_message.moc"
