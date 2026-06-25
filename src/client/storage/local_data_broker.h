/**
* @file    local_data_broker.h
* @version 6.6.2
* @date    2026-06-28
* @author  GridYard Team
* @brief   本地数据层代管者
*
* LocalDataBroker 负责初始化 SQLite 存储、Repository、数据库任务线程，
* 并向应用层提供本地历史持久化、查询、删除和启动恢复入口。
*
* Change Log:
* [v6.6.2] GY   2026-06-28
* * 移除 worker/repository getter，改为提供历史查询、删除和清理语义接口
* [v6.6.2] GY   2026-06-27
* * 新增本地数据层 Broker，收拢 AppController 中的数据管理层细节
*/

#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <memory>

#include "history_records.h"

class DatabaseWorker;
class IMessageRepository;
class ITransferHistoryRepository;
struct PeerInfo;
class QThread;
class SqliteDatabaseBroker;
class SqliteDeviceRepository;
class SqliteMessageRepository;
class SqliteTransferHistoryRepository;

class LocalDataBroker : public QObject {
private:
    Q_OBJECT

public:
    using ChatHistoriesCallback = std::function<void(const QHash<QString, QList<MessageRecord>> &)>;
    using TransferHistoriesCallback = std::function<void(const QList<TransferRecord> &)>;
    using MessagesCallback = std::function<void(const QList<MessageRecord> &, bool)>;
    using TransfersCallback = std::function<void(const QList<TransferRecord> &, bool)>;
    using OperationCallback = std::function<void(bool)>;

    explicit LocalDataBroker(QObject *parent = nullptr);
    virtual ~LocalDataBroker() override;

    // 初始化数据库和存储任务线程
    bool initialize(const QString &databasePath, QString *errorMessage);
    // 获取本地历史是否可用
    bool isAvailable() const;

    // 持久化发现设备快照
    void persistDiscoveredPeer(const PeerInfo &peer);
    // 持久化聊天消息及其设备活动时间
    void persistChatMessage(const MessageRecord &record, const QVariantMap &endpoint);
    // 持久化传输历史及其设备活动时间
    void persistTransferRecord(const TransferRecord &record, const QVariantMap &endpoint);
    // 批量删除传输历史
    void deleteTransfers(const QStringList &recordIds);
    // 异步加载最近聊天历史
    void loadRecentChatHistories(QObject *receiver, const ChatHistoriesCallback &callback);
    // 异步加载最近传输历史
    void loadRecentTransferHistories(QObject *receiver, const TransferHistoriesCallback &callback);
    // 异步加载指定会话的一页聊天历史
    void loadMessages(QObject *receiver, const MessageCursor &cursor, int limit,
                      const MessagesCallback &callback);
    // 异步按条件查询传输历史
    void queryTransfers(QObject *receiver, const TransferQuery &query, int limit,
                        const TransfersCallback &callback);
    // 异步删除单条聊天消息
    void deleteMessage(QObject *receiver, const QString &messageId,
                       const OperationCallback &callback);
    // 异步删除指定设备的聊天会话
    void deleteConversation(QObject *receiver, const QString &deviceId,
                            const OperationCallback &callback);
    // 异步删除单条传输历史
    void deleteTransfer(QObject *receiver, const QString &recordId,
                        const OperationCallback &callback);
    // 异步清空全部聊天记录
    void clearAllMessages(QObject *receiver, const OperationCallback &callback);
    // 异步清空全部传输历史
    void clearAllTransfers(QObject *receiver, const OperationCallback &callback);
    // 异步删除指定时间前的聊天和传输历史
    void deleteExpiredRecords(const QDateTime &before);
    // 开始排空存储队列并停止接受新任务
    void beginShutdown();

signals:
    void operationFailed();
    void drained();

private:
    std::unique_ptr<SqliteDatabaseBroker> _storage;  // SQLite 连接、事务与迁移入口
    std::unique_ptr<SqliteDeviceRepository> _deviceRepository;  // 设备目录持久化实现
    std::unique_ptr<SqliteMessageRepository> _messageRepository;  // 聊天消息持久化实现
    std::unique_ptr<SqliteTransferHistoryRepository> _transferRepository;  // 传输历史持久化实现
    QThread *_storageThread = nullptr;  // 存储任务专用线程
    DatabaseWorker *_storageWorker = nullptr;  // 串行执行存储任务的 Worker
};
