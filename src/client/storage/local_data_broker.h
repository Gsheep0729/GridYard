/**
* @file    local_data_broker.h
* @version 6.6.2
* @date    2026-06-27
* @author  GridYard Team
* @brief   本地数据层代管者
*
* LocalDataBroker 负责初始化 SQLite 存储、Repository、数据库任务线程，
* 并向应用层提供本地历史持久化和启动恢复入口。
*
* Change Log:
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

    explicit LocalDataBroker(QObject *parent = nullptr);
    virtual ~LocalDataBroker() override;

    // 初始化数据库和存储任务线程
    bool initialize(const QString &databasePath, QString *errorMessage);
    // 获取本地历史是否可用
    bool isAvailable() const;
    // 获取数据库异步任务投递入口
    DatabaseWorker *worker() const;
    // 获取聊天消息持久化端口
    IMessageRepository *messageRepository() const;
    // 获取传输历史持久化端口
    ITransferHistoryRepository *transferHistoryRepository() const;

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
