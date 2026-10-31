/**
* @file    history_controller.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   本地聊天与传输历史的 QML 应用层入口
*
* 聚合 ChatManager 和 TransferSessionManager 的查询/删除意图，
* 通过 LocalDataBroker 访问本地历史数据，按设备或游标分页加载历史
* 并向 QML 提供筛选、删除和保留期限设置入口。
*/

#pragma once

#include <QObject>
#include <QVariantList>

#include "history_records.h"

class ChatManager;
class ConfigManager;
class LocalDataBroker;
class TransferSessionManager;

class HistoryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // 传输历史是否还有更早一页（最近一页取满即认为可能还有）
    Q_PROPERTY(bool hasMoreTransfers READ hasMoreTransfers NOTIFY hasMoreTransfersChanged)
    Q_PROPERTY(int retentionDays READ retentionDays NOTIFY retentionDaysChanged)

public:
    explicit HistoryController(ChatManager *chat, TransferSessionManager *transfer,
                               ConfigManager *config, LocalDataBroker *dataBroker,
                               QObject *parent = nullptr);

    // 获取当前传输历史列表
    QVariantList transfers() const;
    // 获取历史查询加载状态
    bool loading() const;
    // 获取传输历史是否还有更早一页
    bool hasMoreTransfers() const;
    // 获取历史保留天数
    int retentionDays() const;

    // 加载指定设备更早的一页聊天记录
    Q_INVOKABLE void loadMoreMessages(const QString &deviceId);
    // 按筛选条件查询传输历史第一页
    Q_INVOKABLE void queryTransfers(const QVariantMap &filter = {});
    // 翻页加载更早的传输历史，新记录追加到当前列表尾部
    Q_INVOKABLE void loadMoreTransfers(const QVariantMap &filter = {});
    // 删除单条聊天记录
    Q_INVOKABLE void deleteMessage(const QString &deviceId, const QString &messageId);
    // 删除指定设备的整段聊天会话
    Q_INVOKABLE void deleteConversation(const QString &deviceId);
    // 删除单条传输历史
    Q_INVOKABLE void deleteTransfer(const QString &recordId);
    // 清空全部聊天记录
    Q_INVOKABLE void clearAllMessages();
    // 清空全部传输历史
    Q_INVOKABLE void clearAllTransfers();
    // 设置历史保留天数
    Q_INVOKABLE void setRetentionDays(int days);
    // 按当前保留期限清理过期历史
    void cleanupExpiredRecords();

signals:
    void transfersChanged();
    void loadingChanged();
    void hasMoreTransfersChanged();
    void messagesLoaded(const QString &deviceId, bool hasMore);
    void operationFailed(const QString &message);
    void retentionDaysChanged();

private:
    // 聊天历史每页条数（翻页响应速度和内存占用可控）
    static constexpr int kChatPageSize = 50;
    // 传输历史每页条数（单页回投体量与翻页节奏的平衡值）
    static constexpr int kTransferPageSize = 200;

    // 设置加载状态并通知 QML
    void setLoading(bool value);
    // 设置传输历史是否还有下一页并通知 QML
    void setHasMoreTransfers(bool value);
    // 将传输历史记录转换为 QML 可绑定的字段集合
    static QVariantMap transferToVariant(const TransferRecord &record);

    ChatManager *_chat = nullptr;  // 聊天运行期模型和清理入口
    TransferSessionManager *_transfer = nullptr;  // 传输运行期模型和清理入口
    ConfigManager *_config = nullptr;  // 历史保留期限配置来源
    LocalDataBroker *_dataBroker = nullptr;  // 本地历史数据访问代管者
    QVariantList _transfers;  // 当前筛选条件下的传输历史视图数据
    bool _hasMoreTransfers = false;  // 最近一页是否取满（满页才可能有更早记录）
    bool _loading = false;  // 是否正在执行异步历史查询
};
