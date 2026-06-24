/**
* @file    history_controller.h
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   本地聊天与传输历史的 QML 应用层入口
*
* 聚合 ChatManager 和 TransferSessionManager 的查询/删除意图，
* 通过 Repository 端口访问 SQLite，按设备或游标分页加载历史
* 并向 QML 提供筛选、删除和保留期限设置入口。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.4.0] GY   2026-06-25
* * 新增本地历史视图控制器，接入聊天和传输历史的查询与删除
*/

#pragma once

#include <QObject>
#include <QVariantList>

#include "history_records.h"

class ChatManager;
class ConfigManager;
class DatabaseWorker;
class IMessageRepository;
class ITransferHistoryRepository;
class TransferSessionManager;

class HistoryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int retentionDays READ retentionDays NOTIFY retentionDaysChanged)

public:
    explicit HistoryController(ChatManager *chat, TransferSessionManager *transfer,
                               ConfigManager *config, DatabaseWorker *worker,
                               IMessageRepository *messages,
                               ITransferHistoryRepository *transfers,
                               QObject *parent = nullptr);

    // 获取当前传输历史列表
    QVariantList transfers() const;
    // 获取历史查询加载状态
    bool loading() const;
    // 获取历史保留天数
    int retentionDays() const;

    // 加载指定设备更早的一页聊天记录
    Q_INVOKABLE void loadMoreMessages(const QString &deviceId);
    // 按筛选条件查询传输历史
    Q_INVOKABLE void queryTransfers(const QVariantMap &filter = {});
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
    void messagesLoaded(const QString &deviceId, bool hasMore);
    void operationFailed(const QString &message);
    void retentionDaysChanged();

private:
    // 设置加载状态并通知 QML
    void setLoading(bool value);
    // 将传输历史记录转换为 QML 可绑定的字段集合
    static QVariantMap transferToVariant(const TransferRecord &record);

    ChatManager *_chat = nullptr;  // 聊天运行期模型和清理入口
    TransferSessionManager *_transfer = nullptr;  // 传输运行期模型和清理入口
    ConfigManager *_config = nullptr;  // 历史保留期限配置来源
    DatabaseWorker *_worker = nullptr;  // 数据库异步任务投递入口
    IMessageRepository *_messages = nullptr;  // 聊天消息持久化端口
    ITransferHistoryRepository *_transferHistory = nullptr;  // 传输历史持久化端口
    QVariantList _transfers;  // 当前筛选条件下的传输历史视图数据
    bool _loading = false;  // 是否正在执行异步历史查询
};
