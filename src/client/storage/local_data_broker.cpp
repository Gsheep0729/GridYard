/**
* @file    local_data_broker.cpp
* @version 6.6.2
* @date    2026-06-28
* @author  GridYard Team
* @brief   本地数据层代管者实现
*
* 集中管理 SQLite 初始化、Repository 实现、数据库任务线程和历史数据编排，
* 避免 AppController 直接持有数据管理层细节。
*
* Change Log:
* [v6.6.2] GY   2026-06-28
* * 收拢 HistoryController 所需的历史查询、删除、清空和过期清理操作
* [v6.6.2] GY   2026-06-27
* * 新增本地数据层 Broker，收拢历史持久化和恢复任务
*/

#include "local_data_broker.h"

#include "data_types.h"
#include "database_worker.h"
#include "sqlite_database_broker.h"
#include "sqlite_device_repository.h"
#include "sqlite_message_repository.h"
#include "sqlite_transfer_history_repository.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThread>

// 构造函数
LocalDataBroker::LocalDataBroker(QObject *parent)
    : QObject{parent}
    , _storage{std::make_unique<SqliteDatabaseBroker>()}
    , _deviceRepository{std::make_unique<SqliteDeviceRepository>(_storage.get())}
    , _messageRepository{std::make_unique<SqliteMessageRepository>(_storage.get())}
    , _transferRepository{std::make_unique<SqliteTransferHistoryRepository>(_storage.get())}
    , _storageThread{new QThread{this}}
    , _storageWorker{new DatabaseWorker{_storage.get()}}
{
}

// 析构函数
LocalDataBroker::~LocalDataBroker()
{
    if (_storageThread && _storageThread->isRunning()) {
        // 等待已入队任务结束，防止 Worker 继续访问即将销毁的 Repository。
        _storageThread->quit();
        _storageThread->wait();
    }
}

// 初始化数据库和存储任务线程
bool LocalDataBroker::initialize(const QString &databasePath, QString *errorMessage)
{
    const bool initialized = _storage->initialize(databasePath, errorMessage);

    // Worker 没有 parent，才能移动到存储线程并由 finished 安全回收。
    _storageWorker->moveToThread(_storageThread);
    connect(_storageThread, &QThread::finished, _storageWorker, &QObject::deleteLater);
    connect(_storageThread, &QThread::finished, _storageThread, &QObject::deleteLater);
    connect(_storageWorker, &DatabaseWorker::taskFinished,
            this, [this](bool succeeded, const QString &) {
                if (!succeeded) {
                    emit operationFailed();
                }
            });
    connect(_storageWorker, &DatabaseWorker::drained,
            this, &LocalDataBroker::drained,
            Qt::QueuedConnection);

    _storageThread->start();
    return initialized;
}

// 获取本地历史是否可用
bool LocalDataBroker::isAvailable() const
{
    return _storage->isAvailable();
}

// 持久化发现设备快照
void LocalDataBroker::persistDiscoveredPeer(const PeerInfo &peer)
{
    PeerRecord record;
    record.deviceId = peer.deviceId;
    record.deviceName = peer.deviceName;
    record.lastIpAddress = peer.ipAddress;
    record.lastTcpPort = peer.tcpPort;
    // 发现服务不区分首次和后续心跳，firstSeenAt 和 lastSeenAt 暂用同一时间戳
    record.firstSeenAt = peer.lastSeen;
    record.lastSeenAt = peer.lastSeen;

    _storageWorker->submitSave(
        [this, record](SqliteDatabaseBroker &, QString *errorMessage) {
            // 设备目录存在节流，短时间内重复心跳不会实际写磁盘
            return _deviceRepository->upsertPeer(record, errorMessage);
        });
}

// 持久化聊天消息及其设备活动时间
void LocalDataBroker::persistChatMessage(const MessageRecord &record, const QVariantMap &endpoint)
{
    PeerRecord peer;
    peer.deviceId = record.peerDeviceId;
    // endpoint 来自发现服务快照，设备名优先用快照值，回退到消息发送方名称
    peer.deviceName = endpoint.value("deviceName", record.senderName).toString();
    peer.lastIpAddress = endpoint.value("ipAddress").toString();
    peer.lastTcpPort = static_cast<quint16>(endpoint.value("tcpPort").toUInt());
    peer.firstSeenAt = QDateTime::currentDateTimeUtc();
    peer.lastSeenAt = peer.firstSeenAt;

    _storageWorker->submitSave(
        [this, peer, record](SqliteDatabaseBroker &, QString *errorMessage) {
            // 同一任务内按顺序执行：先确保设备目录存在，再更新活动时间，最后写消息
            // 三步在同一事务中，避免部分写入导致外键不一致
            return _deviceRepository->upsertPeer(peer, errorMessage)
                   && _deviceRepository->markChatActivity(peer.deviceId, record.sentAt, errorMessage)
                   && _messageRepository->saveMessage(record, errorMessage);
        });
}

// 持久化传输历史及其设备活动时间
void LocalDataBroker::persistTransferRecord(const TransferRecord &record,
                                            const QVariantMap &endpoint)
{
    // 取传输完成时间作为活动时间；尚未完成时退而使用开始时间
    const QDateTime activityAt = record.finishedAt.isValid()
                                    ? record.finishedAt
                                    : record.startedAt;

    PeerRecord peer;
    peer.deviceId = record.peerDeviceId;
    peer.deviceName = endpoint.value("deviceName", record.peerName).toString();
    peer.lastIpAddress = endpoint.value("ipAddress").toString();
    peer.lastTcpPort = static_cast<quint16>(endpoint.value("tcpPort").toUInt());
    // 传输记录可独立触发设备目录写入，活动时间与传输结束时刻对齐
    peer.firstSeenAt = activityAt;
    peer.lastSeenAt = activityAt;

    _storageWorker->submitSave(
        [this, peer, record, activityAt](SqliteDatabaseBroker &, QString *errorMessage) {
            // 与聊天消息持久化相同的三步事务：设备目录 → 活动时间 → 历史记录
            return _deviceRepository->upsertPeer(peer, errorMessage)
                   && _deviceRepository->markTransferActivity(peer.deviceId, activityAt,
                                                              errorMessage)
                   && _transferRepository->upsertFinishedTransfer(record, errorMessage);
        });
}

// 批量删除传输历史
void LocalDataBroker::deleteTransfers(const QStringList &recordIds)
{
    if (recordIds.isEmpty()) {
        return;
    }

    _storageWorker->submitDelete(
        [this, recordIds](SqliteDatabaseBroker &, QString *errorMessage) {
            // 批量删除保持在同一存储任务中，避免界面侧频繁触发数据库队列。
            for (const QString &recordId : recordIds) {
                if (!_transferRepository->deleteTransfer(recordId, errorMessage)) {
                    return false;
                }
            }
            return true;
        });
}

// 异步加载最近聊天历史
void LocalDataBroker::loadRecentChatHistories(QObject *receiver,
                                              const ChatHistoriesCallback &callback)
{
    if (!_storage->isAvailable() || !_messageRepository || !_deviceRepository || !receiver) {
        return;
    }

    _storageWorker->submitLoad(
        [this, receiver, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 启动只恢复最近设备，避免历史量随使用时长线性拖慢首屏。
            const QList<PeerRecord> recentDevices = _deviceRepository->recentPeers(10, errorMessage);
            if (recentDevices.isEmpty() && errorMessage->isEmpty()) {
                return true;
            }

            QHash<QString, QList<MessageRecord>> histories;
            for (const PeerRecord &peer : recentDevices) {
                MessageCursor cursor;
                cursor.peerDeviceId = peer.deviceId;
                // 每个会话限制一页，向上翻页由后续历史界面负责。
                const QList<MessageRecord> messages = _messageRepository->loadMessages(
                    cursor, 50, errorMessage);
                if (!errorMessage->isEmpty()) {
                    return false;
                }
                if (!messages.isEmpty()) {
                    histories.insert(peer.deviceId, messages);
                }
            }

            QMetaObject::invokeMethod(receiver, [callback, histories] {
                callback(histories);
            }, Qt::QueuedConnection);
            return true;
        });
}

// 异步加载最近传输历史
void LocalDataBroker::loadRecentTransferHistories(QObject *receiver,
                                                  const TransferHistoriesCallback &callback)
{
    if (!_storage->isAvailable() || !_transferRepository || !receiver) {
        return;
    }

    _storageWorker->submitLoad(
        [this, receiver, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            TransferQuery query;
            const QList<TransferRecord> records = _transferRepository->queryTransfers(
                query, 100, errorMessage);
            if (!errorMessage->isEmpty()) {
                return false;
            }

            QMetaObject::invokeMethod(receiver, [callback, records] {
                callback(records);
            }, Qt::QueuedConnection);
            return true;
        });
}

// 异步加载指定会话的一页聊天历史
void LocalDataBroker::loadMessages(QObject *receiver, const MessageCursor &cursor, int limit,
                                   const MessagesCallback &callback)
{
    if (!_storage->isAvailable() || !_messageRepository || !receiver) {
        return;
    }

    _storageWorker->submitLoad(
        [this, receiver, cursor, limit, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 游标分页按时间倒序取消息，cursor.beforeSentAt 为空时取首页
            const QList<MessageRecord> records = _messageRepository->loadMessages(
                cursor, limit, errorMessage);
            const bool succeeded = errorMessage->isEmpty();
            // 回投到 receiver 所在线程（通常是主线程），避免跨线程操作模型
            QMetaObject::invokeMethod(receiver, [callback, records, succeeded] {
                callback(records, succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步按条件查询传输历史
void LocalDataBroker::queryTransfers(QObject *receiver, const TransferQuery &query, int limit,
                                     const TransfersCallback &callback)
{
    if (!_storage->isAvailable() || !_transferRepository || !receiver) {
        return;
    }

    _storageWorker->submitLoad(
        [this, receiver, query, limit, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 筛选条件由调用方组合，空条件等价于全量查询
            const QList<TransferRecord> records = _transferRepository->queryTransfers(
                query, limit, errorMessage);
            const bool succeeded = errorMessage->isEmpty();
            QMetaObject::invokeMethod(receiver, [callback, records, succeeded] {
                callback(records, succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步删除单条聊天消息
void LocalDataBroker::deleteMessage(QObject *receiver, const QString &messageId,
                                    const OperationCallback &callback)
{
    if (!_storage->isAvailable() || !_messageRepository || !receiver) {
        return;
    }

    _storageWorker->submitDelete(
        [this, receiver, messageId, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // messageId 是网络层 UUID，不存在时视为已成功删除
            const bool succeeded = _messageRepository->deleteMessage(messageId, errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步删除指定设备的聊天会话（外键 CASCADE 同时清理消息）
void LocalDataBroker::deleteConversation(QObject *receiver, const QString &deviceId,
                                         const OperationCallback &callback)
{
    if (!_storage->isAvailable() || !_messageRepository || !receiver) {
        return;
    }

    _storageWorker->submitDelete(
        [this, receiver, deviceId, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 删除父会话行时 SQLite CASCADE 自动清理关联消息，无需手动删消息
            const bool succeeded = _messageRepository->deleteConversation(deviceId, errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步删除单条传输历史（不触碰本地文件）
void LocalDataBroker::deleteTransfer(QObject *receiver, const QString &recordId,
                                     const OperationCallback &callback)
{
    if (!_storage->isAvailable() || !_transferRepository || !receiver) {
        return;
    }

    _storageWorker->submitDelete(
        [this, receiver, recordId, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 只删除数据库记录，已接收的本地文件由调用方决定是否清理
            const bool succeeded = _transferRepository->deleteTransfer(recordId, errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步清空全部聊天记录（保留设备目录）
void LocalDataBroker::clearAllMessages(QObject *receiver, const OperationCallback &callback)
{
    if (!_storage->isAvailable() || !_messageRepository || !receiver) {
        return;
    }

    _storageWorker->submitDelete(
        [this, receiver, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // CASCADE 清理会话和消息行，设备目录表不受影响
            const bool succeeded = _messageRepository->clearAllMessages(errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步清空全部传输历史（保留设备目录）
void LocalDataBroker::clearAllTransfers(QObject *receiver, const OperationCallback &callback)
{
    if (!_storage->isAvailable() || !_transferRepository || !receiver) {
        return;
    }

    _storageWorker->submitDelete(
        [this, receiver, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const bool succeeded = _transferRepository->clearAllTransfers(errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
            return succeeded;
        });
}

// 异步删除指定时间前的聊天和传输历史（由保留期限定时器触发）
void LocalDataBroker::deleteExpiredRecords(const QDateTime &before)
{
    if (!_storage->isAvailable() || !_messageRepository || !_transferRepository) {
        return;
    }

    _storageWorker->submitDelete(
        [this, before](SqliteDatabaseBroker &, QString *errorMessage) {
            // 两张表独立清理，任一失败则整体报告失败
            return _messageRepository->deleteExpiredMessages(before, errorMessage)
                   && _transferRepository->deleteExpiredTransfers(before, errorMessage);
        });
}

// 开始排空存储队列并停止接受新任务
void LocalDataBroker::beginShutdown()
{
    // 存储线程未启动或 Worker 已销毁时直接通知排空完成
    if (!_storageThread || !_storageThread->isRunning() || !_storageWorker) {
        emit drained();
        return;
    }

    // beginShutdown 通过 QueuedConnection 投递到 Worker 线程，
    // 执行到它时说明之前提交的所有任务均已处理完成
    QMetaObject::invokeMethod(_storageWorker, &DatabaseWorker::beginShutdown,
                              Qt::QueuedConnection);
}
