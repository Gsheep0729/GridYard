/**
* @file    test_conversation_timeline_model.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   ConversationTimelineModel 统一时间线模型测试
*
* 测试用例：消息与会话按时间合并、其他设备会话被过滤、传输进度刷新
* 只触发对应行 dataChanged（不整体重置）、头像连续分组、会话删除同步、
* 设备切换重建。
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
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
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
* [v7.15.8] GY   2026-10-03
* * 真正启用 QAbstractItemModelTester 一致性检查
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
*/

#include <QtTest/QtTest>
#include <QAbstractItemModelTester>
#include <QModelIndex>
#include <QSignalSpy>

#include "chat_controller.h"
#include "chat_manager.h"
#include "chat_message_model.h"
#include "conversation_timeline_model.h"
#include "transfer_session_model.h"

namespace {

// 构造一条消息行的字段映射
QVariantMap messageRow(const QString &messageId, const QString &senderName,
                       const QString &sentAt, bool isOutgoing, int status = 1)
{
    return {
        {"messageId",  messageId},
        {"senderName", senderName},
        {"content",    "content of " + messageId},
        {"sentAt",     sentAt},
        {"isOutgoing", isOutgoing},
        {"status",     status},
    };
}

// 构造一条传输会话行的字段映射
QVariantMap sessionRow(const QString &sessionId, const QString &deviceId, const QString &createdAt)
{
    return {
        {"sessionId",         sessionId},
        {"type",              "send"},
        {"deviceId",          deviceId},
        {"fileName",          sessionId + ".zip"},
        {"status",            "transferring"},
        {"progress",          0.25},
        {"bytesTransferred",  250},
        {"totalBytes",        1000},
        {"createdAt",         createdAt},
        {"peerDeviceName",    "Peer-" + deviceId},
        {"isDirectory",       false},
        {"fileList",          QVariantList{}},
        {"canDeleteLocalFile", false},
        {"errorMsg",          ""},
    };
}

}

class TestConversationTimelineModel : public QObject {
    Q_OBJECT

private slots:
    void testMergeSortsAndFiltersDevice();
    void testTransferProgressUpdatesSingleRow();
    void testAvatarGrouping();
    void testSessionRemovalSyncs();
    void testHistoryPrependInsertsAtTop();
    void testDeviceSwitchRebuilds();

private:
    // 组装一个绑定好聊天控制器与会话模型的时间线模型
    std::unique_ptr<ConversationTimelineModel> makeTimeline(
        ChatController *controller, TransferSessionModel *sessions, const QString &deviceId);
};

// 组装一个绑定好聊天控制器与会话模型的时间线模型
std::unique_ptr<ConversationTimelineModel> TestConversationTimelineModel::makeTimeline(
    ChatController *controller, TransferSessionModel *sessions, const QString &deviceId)
{
    std::unique_ptr<ConversationTimelineModel> timeline{
        new ConversationTimelineModel{}};
    // 挂上模型一致性检查器，所有增删改和角色访问都得到免费校验
    new QAbstractItemModelTester(timeline.get(), this);
    timeline->setChatController(controller);
    timeline->setTransferSessions(sessions);
    timeline->setDeviceId(deviceId);
    return timeline;
}

// 消息与会话按时间升序合并，其他设备的会话被过滤
void TestConversationTimelineModel::testMergeSortsAndFiltersDevice()
{
    ChatManager manager;
    ChatController controller(&manager);
    TransferSessionModel sessions;

    auto *messages = qobject_cast<ChatMessageModel *>(
        controller.messageModelForDevice("dev1"));
    QVERIFY(messages);
    messages->appendMessage(messageRow("m2", "Alice", "2026-10-02T08:05:00.000Z", false));
    messages->appendMessage(messageRow("m1", "Alice", "2026-10-02T08:00:00.000Z", false));

    sessions.appendSession(sessionRow("s-other", "dev2", "2026-10-02T08:06:00.000Z"));
    sessions.appendSession(sessionRow("s1", "dev1", "2026-10-02T08:02:00.000Z"));

    auto timeline = makeTimeline(&controller, &sessions, "dev1");
    QCOMPARE(timeline->rowCount(), 3);

    // 期望顺序：m1(08:00)、s1(08:02)、m2(08:05)，dev2 的会话被过滤
    QCOMPARE(timeline->index(0, 0).data(ConversationTimelineModel::KindRole).toString(), "message");
    QCOMPARE(timeline->index(0, 0).data(ConversationTimelineModel::MessageIdRole).toString(), "m1");
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::KindRole).toString(), "transfer");
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::SessionIdRole).toString(), "s1");
    QCOMPARE(timeline->index(2, 0).data(ConversationTimelineModel::KindRole).toString(), "message");
    QCOMPARE(timeline->index(2, 0).data(ConversationTimelineModel::MessageIdRole).toString(), "m2");
}

// 传输进度刷新只触发对应行的 dataChanged，不发生模型重置
void TestConversationTimelineModel::testTransferProgressUpdatesSingleRow()
{
    ChatManager manager;
    ChatController controller(&manager);
    TransferSessionModel sessions;

    sessions.appendSession(sessionRow("s1", "dev1", "2026-10-02T08:00:00.000Z"));
    sessions.appendSession(sessionRow("s2", "dev1", "2026-10-02T08:01:00.000Z"));

    auto timeline = makeTimeline(&controller, &sessions, "dev1");
    QCOMPARE(timeline->rowCount(), 2);

    QSignalSpy dataChangedSpy(timeline.get(), &QAbstractItemModel::dataChanged);
    QSignalSpy resetSpy(timeline.get(), &QAbstractItemModel::modelReset);
    QSignalSpy rowsInsertedSpy(timeline.get(), &QAbstractItemModel::rowsInserted);
    QSignalSpy rowsRemovedSpy(timeline.get(), &QAbstractItemModel::rowsRemoved);

    QVERIFY(sessions.updateSession("s1", [](QVariantMap &session) {
        session.insert("progress", 0.5);
        session.insert("bytesTransferred", 500);
    }));

    QCOMPARE(dataChangedSpy.count(), 1);  // 只通知一条变更
    QCOMPARE(resetSpy.count(), 0);        // 不允许全量重置
    QCOMPARE(rowsInsertedSpy.count(), 0);
    QCOMPARE(rowsRemovedSpy.count(), 0);

    const QModelIndex changed = dataChangedSpy.at(0).at(0).toModelIndex();
    QCOMPARE(changed.row(), 0);  // s1 位于合并后的第 0 行
    QCOMPARE(timeline->index(0, 0).data(ConversationTimelineModel::ProgressRole).toDouble(), 0.5);
    QCOMPARE(timeline->rowCount(), 2);
}

// 连续同方向消息只在首条显示头像，传输行打断连续性
void TestConversationTimelineModel::testAvatarGrouping()
{
    ChatManager manager;
    ChatController controller(&manager);
    TransferSessionModel sessions;

    auto *messages = qobject_cast<ChatMessageModel *>(
        controller.messageModelForDevice("dev1"));
    QVERIFY(messages);
    messages->appendMessage(messageRow("m1", "Alice", "2026-10-02T08:00:00.000Z", false));
    messages->appendMessage(messageRow("m2", "Alice", "2026-10-02T08:01:00.000Z", false));
    messages->appendMessage(messageRow("m3", "Alice", "2026-10-02T08:02:00.000Z", true));

    auto timeline = makeTimeline(&controller, &sessions, "dev1");
    QCOMPARE(timeline->rowCount(), 3);

    QCOMPARE(timeline->index(0, 0).data(ConversationTimelineModel::ShowAvatarRole).toBool(), true);
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::ShowAvatarRole).toBool(), false);
    QCOMPARE(timeline->index(2, 0).data(ConversationTimelineModel::ShowAvatarRole).toBool(), true);

    // 在两条入站消息之间插入一个传输任务，后一条消息需要重新显示头像
    sessions.appendSession(sessionRow("s1", "dev1", "2026-10-02T08:00:30.000Z"));
    QCOMPARE(timeline->rowCount(), 4);
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::KindRole).toString(), "transfer");
    QCOMPARE(timeline->index(2, 0).data(ConversationTimelineModel::KindRole).toString(), "message");
    QCOMPARE(timeline->index(2, 0).data(ConversationTimelineModel::ShowAvatarRole).toBool(), true);
    QCOMPARE(timeline->index(3, 0).data(ConversationTimelineModel::ShowAvatarRole).toBool(), true);
}

// 会话删除后合并列表同步移除对应行
void TestConversationTimelineModel::testSessionRemovalSyncs()
{
    ChatManager manager;
    ChatController controller(&manager);
    TransferSessionModel sessions;

    auto *messages = qobject_cast<ChatMessageModel *>(
        controller.messageModelForDevice("dev1"));
    QVERIFY(messages);
    messages->appendMessage(messageRow("m1", "Alice", "2026-10-02T08:00:00.000Z", false));

    sessions.appendSession(sessionRow("s1", "dev1", "2026-10-02T08:01:00.000Z"));
    sessions.appendSession(sessionRow("s2", "dev1", "2026-10-02T08:02:00.000Z"));

    auto timeline = makeTimeline(&controller, &sessions, "dev1");
    QCOMPARE(timeline->rowCount(), 3);

    QVERIFY(sessions.removeSession("s1"));
    QCOMPARE(timeline->rowCount(), 2);
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::SessionIdRole).toString(), "s2");
}

// 历史消息前插后按时间排到队首，原消息行号平移不串行
void TestConversationTimelineModel::testHistoryPrependInsertsAtTop()
{
    ChatManager manager;
    ChatController controller(&manager);
    TransferSessionModel sessions;

    auto *messages = qobject_cast<ChatMessageModel *>(
        controller.messageModelForDevice("dev1"));
    QVERIFY(messages);
    messages->appendMessage(messageRow("m1", "Alice", "2026-10-02T08:05:00.000Z", false));

    auto timeline = makeTimeline(&controller, &sessions, "dev1");
    QCOMPARE(timeline->rowCount(), 1);

    messages->prependMessages({messageRow("m0", "Alice", "2026-10-02T07:55:00.000Z", false)});
    QCOMPARE(timeline->rowCount(), 2);
    QCOMPARE(timeline->index(0, 0).data(ConversationTimelineModel::MessageIdRole).toString(), "m0");
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::MessageIdRole).toString(), "m1");

    // 前插后再更新后一条消息状态，行号映射仍然正确
    QVERIFY(messages->updateMessageStatus("m1", 2));
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::StatusRole).toInt(), 2);
}

// 切换设备后时间线只包含新设备的消息与会话
void TestConversationTimelineModel::testDeviceSwitchRebuilds()
{
    ChatManager manager;
    ChatController controller(&manager);
    TransferSessionModel sessions;

    auto *dev1Messages = qobject_cast<ChatMessageModel *>(
        controller.messageModelForDevice("dev1"));
    auto *dev2Messages = qobject_cast<ChatMessageModel *>(
        controller.messageModelForDevice("dev2"));
    QVERIFY(dev1Messages);
    QVERIFY(dev2Messages);
    dev1Messages->appendMessage(messageRow("m-dev1", "Alice", "2026-10-02T08:00:00.000Z", false));
    dev2Messages->appendMessage(messageRow("m-dev2", "Bob", "2026-10-02T08:01:00.000Z", false));

    sessions.appendSession(sessionRow("s1", "dev1", "2026-10-02T08:02:00.000Z"));
    sessions.appendSession(sessionRow("s2", "dev2", "2026-10-02T08:03:00.000Z"));

    auto timeline = makeTimeline(&controller, &sessions, "dev1");
    QCOMPARE(timeline->rowCount(), 2);

    timeline->setDeviceId("dev2");
    QCOMPARE(timeline->rowCount(), 2);
    QCOMPARE(timeline->index(0, 0).data(ConversationTimelineModel::MessageIdRole).toString(), "m-dev2");
    QCOMPARE(timeline->index(1, 0).data(ConversationTimelineModel::SessionIdRole).toString(), "s2");
}

QTEST_MAIN(TestConversationTimelineModel)
#include "test_conversation_timeline_model.moc"
