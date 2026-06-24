/**
* @file    local_data_broker.cpp
* @version 6.6.2
* @date    2026-06-27
* @author  GridYard Team
* @brief   本地数据层代管者实现
*
* 集中管理 SQLite 初始化、Repository 实现、数据库任务线程和持久化编排，
* 避免 AppController 直接持有数据管理层细节。
*
* Change Log:
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

// 获取数据库异步任务投递入口
DatabaseWorker *LocalDataBroker::worker() const
{
    return _storageWorker;
}

// 获取聊天消息持久化端口
IMessageRepository *LocalDataBroker::messageRepository() const
{
    return _messageRepository.get();
}

// 获取传输历史持久化端口
ITransferHistoryRepository *LocalDataBroker::transferHistoryRepository() const
{
    return _transferRepository.get();
}

// 持久化发现设备快照
void LocalDataBroker::persistDiscoveredPeer(const PeerInfo &peer)
{
    PeerRecord record;
    record.deviceId = peer.deviceId;
    record.deviceName = peer.deviceName;
    record.lastIpAddress = peer.ipAddress;
    record.lastTcpPort = peer.tcpPort;
    record.firstSeenAt = peer.lastSeen;
    record.lastSeenAt = peer.lastSeen;

    _storageWorker->submitSave(
        [this, record](SqliteDatabaseBroker &, QString *errorMessage) {
            return _deviceRepository->upsertPeer(record, errorMessage);
        });
}

// 持久化聊天消息及其设备活动时间
void LocalDataBroker::persistChatMessage(const MessageRecord &record, const QVariantMap &endpoint)
{
    PeerRecord peer;
    peer.deviceId = record.peerDeviceId;
    peer.deviceName = endpoint.value("deviceName", record.senderName).toString();
    peer.lastIpAddress = endpoint.value("ipAddress").toString();
    peer.lastTcpPort = static_cast<quint16>(endpoint.value("tcpPort").toUInt());
    peer.firstSeenAt = QDateTime::currentDateTimeUtc();
    peer.lastSeenAt = peer.firstSeenAt;

    _storageWorker->submitSave(
        [this, peer, record](SqliteDatabaseBroker &, QString *errorMessage) {
            return _deviceRepository->upsertPeer(peer, errorMessage)
                   && _deviceRepository->markChatActivity(peer.deviceId, record.sentAt, errorMessage)
                   && _messageRepository->saveMessage(record, errorMessage);
        });
}

// 持久化传输历史及其设备活动时间
void LocalDataBroker::persistTransferRecord(const TransferRecord &record,
                                            const QVariantMap &endpoint)
{
    const QDateTime activityAt = record.finishedAt.isValid()
                                    ? record.finishedAt
                                    : record.startedAt;

    PeerRecord peer;
    peer.deviceId = record.peerDeviceId;
    peer.deviceName = endpoint.value("deviceName", record.peerName).toString();
    peer.lastIpAddress = endpoint.value("ipAddress").toString();
    peer.lastTcpPort = static_cast<quint16>(endpoint.value("tcpPort").toUInt());
    peer.firstSeenAt = activityAt;
    peer.lastSeenAt = activityAt;

    _storageWorker->submitSave(
        [this, peer, record, activityAt](SqliteDatabaseBroker &, QString *errorMessage) {
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

// 开始排空存储队列并停止接受新任务
void LocalDataBroker::beginShutdown()
{
    if (!_storageThread || !_storageThread->isRunning() || !_storageWorker) {
        emit drained();
        return;
    }

    QMetaObject::invokeMethod(_storageWorker, &DatabaseWorker::beginShutdown,
                              Qt::QueuedConnection);
}
