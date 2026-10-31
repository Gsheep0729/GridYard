/**
* @file    local_data_broker.h
* @version 7.24.0
* @date 2026-10-08
* @author  GridYard Team
* @brief   本地数据层代管者
*
* LocalDataBroker 负责初始化 SQLite 存储、Repository、数据库任务线程，
* 并向应用层提供本地历史持久化、查询、删除和启动恢复入口。
* 所有带回调的接口承诺任何路径下恰好回调一次，存储不可用时回调失败。
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
struct PeerInfo;
class QThread;
class SqliteDatabaseBroker;
class SqliteDeviceRepository;
class SqliteMessageRepository;
class SqliteTransferHistoryRepository;

class LocalDataBroker : public QObject {
private:
    Q_OBJECT
    Q_PROPERTY(bool historyDatabaseRebuilt READ historyDatabaseRebuilt NOTIFY historyDatabaseRebuiltChanged)
    Q_PROPERTY(QString rebuiltBackupPath READ rebuiltBackupPath NOTIFY rebuiltBackupPathChanged)

public:
    // 回调末位参数为成功标志，false 表示本次查询或操作失败
    using ChatHistoriesCallback = std::function<void(const QHash<QString, QList<MessageRecord>> &, bool)>;
    using TransferHistoriesCallback = std::function<void(const QList<TransferRecord> &, bool)>;
    using PeersCallback = std::function<void(const QList<PeerRecord> &, bool)>;
    using MessagesCallback = std::function<void(const QList<MessageRecord> &, bool)>;
    using TransfersCallback = std::function<void(const QList<TransferRecord> &, bool)>;
    using OperationCallback = std::function<void(bool)>;
    // 备份导出数据回调：devices 必带，messages/transfers 按勾选层返回
    using BackupDataCallback = std::function<void(const QList<PeerRecord> &,
                                                  const QList<MessageRecord> &,
                                                  const QList<TransferRecord> &, bool)>;
    // 备份导入结果回调：stats 携带各层新增/合并条数，false 表示事务已整体回滚
    using BackupImportCallback = std::function<void(const QVariantMap &, bool)>;
    // 备份导入预分析回调：本机各表条目数与导入数据的冲突数
    using BackupAnalyzeCallback = std::function<void(const QVariantMap &, bool)>;
    // 设备历史计数回调：该设备名下的聊天与传输条数
    using DeviceCountsCallback = std::function<void(int, int, bool)>;

    explicit LocalDataBroker(QObject *parent = nullptr);
    virtual ~LocalDataBroker() override;

    // 初始化数据库和存储任务线程
    bool initialize(const QString &databasePath, QString *errorMessage);
    // 获取本地历史是否可用
    bool isAvailable() const;
    // 获取本次启动是否因库损坏重建了本地历史库
    bool historyDatabaseRebuilt() const;
    // 获取重建前损坏库的备份路径（未重建时为空）
    QString rebuiltBackupPath() const;

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
    // 异步加载最近设备目录
    void loadRecentPeers(QObject *receiver, int limit, const PeersCallback &callback);
    // 异步按关键字检索设备目录
    void searchPeers(QObject *receiver, const QString &keyword, int limit,
                     const PeersCallback &callback);
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
    // 异步设置设备置顶状态（幂等，设备行不存在时同样回调成功）
    void setDevicePinned(QObject *receiver, const QString &deviceId, bool pinned,
                         const OperationCallback &callback);
    // 异步设置设备收藏状态（幂等，设备行不存在时同样回调成功）
    void setDeviceFavorite(QObject *receiver, const QString &deviceId, bool favorite,
                           const OperationCallback &callback);
    // 异步设置设备隐藏状态（幂等，设备行不存在时同样回调成功）
    void setDeviceHidden(QObject *receiver, const QString &deviceId, bool hidden,
                         const OperationCallback &callback);
    // 异步设置设备本地备注别名（空串表示清除；幂等，设备行不存在时同样回调成功）
    void setDeviceAlias(QObject *receiver, const QString &deviceId, const QString &alias,
                        const OperationCallback &callback);
    // 异步删除设备及其聊天与传输历史；不删除已接收的本地文件
    void deleteDeviceWithHistory(QObject *receiver, const QString &deviceId,
                                 const OperationCallback &callback);
    // 异步清空全部聊天记录
    void clearAllMessages(QObject *receiver, const OperationCallback &callback);
    // 异步清空全部传输历史
    void clearAllTransfers(QObject *receiver, const OperationCallback &callback);
    // 异步加载备份导出所需的全量数据（设备目录必带，聊天与传输按层加载）
    void loadBackupData(QObject *receiver, bool includeChat, bool includeTransfers,
                        const BackupDataCallback &callback);
    // 异步预分析备份数据：统计本机各表条目数与按 deviceId/message_id/session_id
    // 计算的冲突数，供导入预览弹窗展示
    void analyzeBackupRecords(QObject *receiver, const QList<PeerRecord> &devices,
                              const QList<MessageRecord> &messages,
                              const QList<TransferRecord> &transfers,
                              const BackupAnalyzeCallback &callback);
    // 单事务导入备份数据：设备按字段级合并，聊天按 message_id、传输按
    // session_id 幂等跳过，任何一步失败整体回滚
    void importBackupRecords(QObject *receiver, const QList<PeerRecord> &devices,
                             const QList<MessageRecord> &messages,
                             const QList<TransferRecord> &transfers,
                             const BackupImportCallback &callback);
    // 异步统计设备名下的聊天与传输条数（关联向导预览用）
    void countDeviceRecords(QObject *receiver, const QString &deviceId,
                            const DeviceCountsCallback &callback);
    // 单事务合并设备：把旧行的管理标记并入新行（可选连带聊天与传输历史），
    // 随后删除旧行，任何一步失败整体回滚
    void mergeDeviceRecords(QObject *receiver, const PeerRecord &newDevice,
                            const QString &oldDeviceId, bool includeHistory,
                            const OperationCallback &callback);
    // 异步删除指定时间前的聊天和传输历史
    void deleteExpiredRecords(const QDateTime &before);
    // 开始排空存储队列并停止接受新任务
    void beginShutdown();
    // 关闭存储线程并释放数据库连接
    void closeStorage();

signals:
    void operationFailed();
    void drained();
    void historyDatabaseRebuiltChanged();
    void rebuiltBackupPathChanged();

private:
    std::unique_ptr<SqliteDatabaseBroker> _storage;  // SQLite 连接、事务与迁移入口
    std::unique_ptr<SqliteDeviceRepository> _deviceRepository;  // 设备目录持久化实现
    std::unique_ptr<SqliteMessageRepository> _messageRepository;  // 聊天消息持久化实现
    std::unique_ptr<SqliteTransferHistoryRepository> _transferRepository;  // 传输历史持久化实现
    QThread *_storageThread = nullptr;  // 存储任务专用线程
    DatabaseWorker *_storageWorker = nullptr;  // 串行执行存储任务的 Worker
    bool _historyDatabaseRebuilt = false;  // 启动时是否因库损坏重建了本地历史库
    QString _rebuiltBackupPath;  // 重建前损坏库的备份文件路径
};
