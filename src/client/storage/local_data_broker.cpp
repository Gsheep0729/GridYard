/**
* @file    local_data_broker.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   本地数据层代管者实现
*
* 集中管理 SQLite 初始化、Repository 实现、数据库任务线程和历史数据编排，
* 避免 AppController 直接持有数据管理层细节。内部经纯 C++ 存储核心
*（DatabaseCore 与三个 *_core 仓储）落库，Qt 领域记录在任务边界做机械
* 互转；直接 SQL 的组合事务沿用与旧版逐字相同的 SQL 文本。
*/

#include "local_data_broker.h"

#include "data_types.h"
#include "database_worker.h"
#include "post_to_ui_thread.h"

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/iso_time.h"
#include "core/storage/message_repository_core.h"
#include "core/storage/sqlite_stmt.h"
#include "core/storage/transfer_repository_core.h"

#include <QDateTime>
#include <QMetaObject>
#include <QThread>
#include <QTimeZone>

#include <unordered_set>
#include <vector>

namespace {

// 有效时间转毫秒时间戳，无效时间返回空（对应 SQL NULL 列）
std::optional<int64_t> toOptionalMillis(const QDateTime &time)
{
    return time.isValid() ? std::optional<int64_t>{time.toMSecsSinceEpoch()} : std::nullopt;
}

// 毫秒时间戳还原为 UTC 时间的 QDateTime（与旧版 ISO 解析的时区口径一致）
QDateTime fromOptionalMillis(const std::optional<int64_t> &millis)
{
    return millis ? QDateTime::fromMSecsSinceEpoch(*millis, QTimeZone::UTC) : QDateTime();
}

// Qt 收发方向枚举转核心枚举（数值与数据库列取值绑定）
gy::storage::RecordDirection toCoreDirection(RecordDirection direction)
{
    return static_cast<gy::storage::RecordDirection>(static_cast<int>(direction));
}

// 核心收发方向枚举转 Qt 枚举
RecordDirection fromCoreDirection(gy::storage::RecordDirection direction)
{
    return static_cast<RecordDirection>(static_cast<int>(direction));
}

// 设备目录记录转核心记录（QString 转 UTF-8 std::string，QDateTime 转毫秒）
gy::storage::PeerRecord toCore(const PeerRecord &record)
{
    gy::storage::PeerRecord core;
    core.deviceId = record.deviceId.toStdString();
    core.deviceName = record.deviceName.toStdString();
    core.lastIpAddress = record.lastIpAddress.toStdString();
    core.lastTcpPort = record.lastTcpPort;
    core.firstSeenAtMs = record.firstSeenAt.toMSecsSinceEpoch();
    core.lastSeenAtMs = record.lastSeenAt.toMSecsSinceEpoch();
    core.lastChatAtMs = toOptionalMillis(record.lastChatAt);
    core.lastTransferAtMs = toOptionalMillis(record.lastTransferAt);
    core.alias = record.alias.toStdString();
    core.pinned = record.pinned;
    core.hidden = record.hidden;
    core.favorite = record.favorite;
    return core;
}

// 核心设备目录记录转 Qt 记录
PeerRecord fromCore(const gy::storage::PeerRecord &record)
{
    PeerRecord qt;
    qt.deviceId = QString::fromStdString(record.deviceId);
    qt.deviceName = QString::fromStdString(record.deviceName);
    qt.lastIpAddress = QString::fromStdString(record.lastIpAddress);
    qt.lastTcpPort = record.lastTcpPort;
    qt.firstSeenAt = QDateTime::fromMSecsSinceEpoch(record.firstSeenAtMs, QTimeZone::UTC);
    qt.lastSeenAt = QDateTime::fromMSecsSinceEpoch(record.lastSeenAtMs, QTimeZone::UTC);
    qt.lastChatAt = fromOptionalMillis(record.lastChatAtMs);
    qt.lastTransferAt = fromOptionalMillis(record.lastTransferAtMs);
    qt.alias = QString::fromStdString(record.alias);
    qt.pinned = record.pinned;
    qt.hidden = record.hidden;
    qt.favorite = record.favorite;
    return qt;
}

// 聊天消息记录转核心记录
gy::storage::MessageRecord toCore(const MessageRecord &record)
{
    gy::storage::MessageRecord core;
    core.messageId = record.messageId.toStdString();
    core.peerDeviceId = record.peerDeviceId.toStdString();
    core.direction = toCoreDirection(record.direction);
    core.senderDeviceId = record.senderDeviceId.toStdString();
    core.senderName = record.senderName.toStdString();
    core.content = record.content.toStdString();
    core.sentAtMs = record.sentAt.toMSecsSinceEpoch();
    core.localStatus = record.localStatus;
    core.createdAtMs = record.createdAt.toMSecsSinceEpoch();
    return core;
}

// 核心聊天消息记录转 Qt 记录
MessageRecord fromCore(const gy::storage::MessageRecord &record)
{
    MessageRecord qt;
    qt.messageId = QString::fromStdString(record.messageId);
    qt.peerDeviceId = QString::fromStdString(record.peerDeviceId);
    qt.direction = fromCoreDirection(record.direction);
    qt.senderDeviceId = QString::fromStdString(record.senderDeviceId);
    qt.senderName = QString::fromStdString(record.senderName);
    qt.content = QString::fromStdString(record.content);
    qt.sentAt = QDateTime::fromMSecsSinceEpoch(record.sentAtMs, QTimeZone::UTC);
    qt.localStatus = record.localStatus;
    qt.createdAt = QDateTime::fromMSecsSinceEpoch(record.createdAtMs, QTimeZone::UTC);
    return qt;
}

// 传输历史记录转核心记录（未结束时间以空值表达）
gy::storage::TransferRecord toCore(const TransferRecord &record)
{
    gy::storage::TransferRecord core;
    core.recordId = record.recordId.toStdString();
    core.sessionId = record.sessionId.toStdString();
    core.peerDeviceId = record.peerDeviceId.toStdString();
    core.peerName = record.peerName.toStdString();
    core.direction = toCoreDirection(record.direction);
    core.displayName = record.displayName.toStdString();
    core.isDirectory = record.isDirectory;
    core.fileCount = record.fileCount;
    core.totalBytes = record.totalBytes;
    core.status = record.status.toStdString();
    core.startedAtMs = record.startedAt.toMSecsSinceEpoch();
    core.finishedAtMs = toOptionalMillis(record.finishedAt);
    core.errorCode = record.errorCode;
    core.errorMessage = record.errorMessage.toStdString();
    return core;
}

// 核心传输历史记录转 Qt 记录
TransferRecord fromCore(const gy::storage::TransferRecord &record)
{
    TransferRecord qt;
    qt.recordId = QString::fromStdString(record.recordId);
    qt.sessionId = QString::fromStdString(record.sessionId);
    qt.peerDeviceId = QString::fromStdString(record.peerDeviceId);
    qt.peerName = QString::fromStdString(record.peerName);
    qt.direction = fromCoreDirection(record.direction);
    qt.displayName = QString::fromStdString(record.displayName);
    qt.isDirectory = record.isDirectory;
    qt.fileCount = record.fileCount;
    qt.totalBytes = record.totalBytes;
    qt.status = QString::fromStdString(record.status);
    qt.startedAt = QDateTime::fromMSecsSinceEpoch(record.startedAtMs, QTimeZone::UTC);
    qt.finishedAt = fromOptionalMillis(record.finishedAtMs);
    qt.errorCode = record.errorCode;
    qt.errorMessage = QString::fromStdString(record.errorMessage);
    return qt;
}

// 聊天分页游标转核心游标
gy::storage::MessageCursor toCore(const MessageCursor &cursor)
{
    gy::storage::MessageCursor core;
    core.peerDeviceId = cursor.peerDeviceId.toStdString();
    core.beforeSentAtMs = toOptionalMillis(cursor.beforeSentAt);
    core.beforeMessageId = cursor.beforeMessageId.toStdString();
    return core;
}

// 传输历史筛选条件转核心筛选
gy::storage::TransferQuery toCore(const TransferQuery &query)
{
    gy::storage::TransferQuery core;
    core.peerDeviceId = query.peerDeviceId.toStdString();
    core.status = query.status.toStdString();
    core.beforeStartedAtMs = toOptionalMillis(query.beforeStartedAt);
    core.beforeRecordId = query.beforeRecordId.toStdString();
    return core;
}

// 核心记录列表转 Qt 列表
template <typename Core, typename Qt>
QList<Qt> fromCoreList(const std::vector<Core> &records)
{
    QList<Qt> result;
    result.reserve(static_cast<int>(records.size()));
    for (const Core &record : records) {
        result.append(fromCore(record));
    }
    return result;
}

// SQL 语句的统一执行出口：执行到完成返回真，出错时回填任务错误
bool runStatement(gy::storage::SqliteStmt &statement, std::string *taskError)
{
    if (auto row = statement.step(); row.has_value()) {
        return true;
    } else if (taskError) {
        *taskError = row.error().message;
    }
    return false;
}

}  // namespace

// 构造函数
LocalDataBroker::LocalDataBroker(QObject *parent)
    : QObject{parent}
    , _storage{std::make_unique<gy::storage::DatabaseCore>()}
    , _deviceRepository{std::make_unique<gy::storage::DeviceRepositoryCore>(_storage.get())}
    , _messageRepository{std::make_unique<gy::storage::MessageRepositoryCore>(_storage.get())}
    , _transferRepository{std::make_unique<gy::storage::TransferRepositoryCore>(_storage.get())}
    , _storageThread{new QThread{this}}
    , _storageWorker{new DatabaseWorker{_storage.get()}}
{
}

// 析构函数
LocalDataBroker::~LocalDataBroker()
{
    closeStorage();
}

// 初始化数据库和存储任务线程
bool LocalDataBroker::initialize(const QString &databasePath, QString *errorMessage)
{
    std::string coreError;
    const bool initialized = _storage->initialize(databasePath.toStdString(), &coreError);
    if (errorMessage) {
        *errorMessage = QString::fromStdString(coreError);
    }

    // 库损坏被备份重建时置位只读属性，界面据此提示历史已清零重置
    if (_storage->lastInitializeRebuilt()) {
        _historyDatabaseRebuilt = true;
        _rebuiltBackupPath = QString::fromStdString(_storage->rebuiltBackupPath());
        emit historyDatabaseRebuiltChanged();
        emit rebuiltBackupPathChanged();
    }

    // Worker 没有 parent，才能移动到存储线程；析构由 closeStorage 在 quit 前
    // 于归属线程内同步完成，不依赖线程退出时 DeferredDelete 的冲刷时序。
    _storageWorker->moveToThread(_storageThread);
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

// 获取本次启动是否因库损坏重建了本地历史库
bool LocalDataBroker::historyDatabaseRebuilt() const
{
    return _historyDatabaseRebuilt;
}

// 获取重建前损坏库的备份路径（未重建时为空）
QString LocalDataBroker::rebuiltBackupPath() const
{
    return _rebuiltBackupPath;
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

    const gy::storage::PeerRecord coreRecord = toCore(record);
    const bool isPseudoId = gy::domain::isManualPseudoDeviceId(record.deviceId);
    _storageWorker->submitTask(
        [this, coreRecord, isPseudoId](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            // 设备目录存在节流，短时间内重复心跳不会实际写磁盘
            if (!_deviceRepository->upsertPeer(coreRecord, &error)) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }
            // 真身心跳到达时并入同 IP 手动伪条目的管理标记（独立小事务，
            // 幂等：伪行删除后为空操作；伪 ID 自身的心跳不做迁移）
            if (isPseudoId) {
                return true;
            }
            if (!_deviceRepository->mergeManualPeerMarkers(
                    coreRecord.deviceId,
                    gy::domain::manualPseudoDeviceId(
                        QString::fromStdString(coreRecord.lastIpAddress)).toStdString(),
                    &error)) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }
            return true;
        });
}

// 持久化聊天消息及其设备活动时间
void LocalDataBroker::persistChatMessage(const MessageRecord &record, const QVariantMap &endpoint)
{
    PeerRecord peer;
    peer.deviceId = record.peerDeviceId;
    // endpoint 来自发现服务快照，设备名优先用快照值，回退到消息发送方名称
    peer.deviceName = endpoint.value(gy::keys::kEndpointDeviceName, record.senderName).toString();
    peer.lastIpAddress = endpoint.value(gy::keys::kEndpointIpAddress).toString();
    peer.lastTcpPort = static_cast<quint16>(endpoint.value(gy::keys::kEndpointTcpPort).toUInt());
    peer.firstSeenAt = QDateTime::currentDateTimeUtc();
    peer.lastSeenAt = peer.firstSeenAt;

    const gy::storage::PeerRecord corePeer = toCore(peer);
    const gy::storage::MessageRecord coreRecord = toCore(record);
    const int64_t sentAtMs = record.sentAt.toMSecsSinceEpoch();
    _storageWorker->submitTask(
        [this, corePeer, coreRecord, sentAtMs](gy::storage::DatabaseCore &db, QString *errorMessage) {
            std::string error;
            // 组合三个步骤在同一事务内执行，任何一步失败全部回滚
            std::vector<gy::storage::DatabaseCore::TransactionTask> steps;
            steps.push_back(gy::storage::DeviceRepositoryCore::upsertPeerStep(corePeer));
            steps.push_back(gy::storage::DeviceRepositoryCore::markChatActivityStep(
                corePeer.deviceId, sentAtMs));
            steps.push_back(gy::storage::MessageRepositoryCore::saveMessageStep(coreRecord));
            if (!db.runSteps(steps, &error)) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }
            // 事务成功后刷新设备目录节流缓存
            _deviceRepository->noteWritten(corePeer);
            return true;
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
    peer.deviceName = endpoint.value(gy::keys::kEndpointDeviceName, record.peerName).toString();
    peer.lastIpAddress = endpoint.value(gy::keys::kEndpointIpAddress).toString();
    peer.lastTcpPort = static_cast<quint16>(endpoint.value(gy::keys::kEndpointTcpPort).toUInt());
    // 传输记录可独立触发设备目录写入，活动时间与传输结束时刻对齐
    peer.firstSeenAt = activityAt;
    peer.lastSeenAt = activityAt;

    const gy::storage::PeerRecord corePeer = toCore(peer);
    const gy::storage::TransferRecord coreRecord = toCore(record);
    const int64_t activityAtMs = activityAt.toMSecsSinceEpoch();
    _storageWorker->submitTask(
        [this, corePeer, coreRecord, activityAtMs](gy::storage::DatabaseCore &db, QString *errorMessage) {
            std::string error;
            // 组合三个步骤在同一事务内执行，任何一步失败全部回滚
            std::vector<gy::storage::DatabaseCore::TransactionTask> steps;
            steps.push_back(gy::storage::DeviceRepositoryCore::upsertPeerStep(corePeer));
            steps.push_back(gy::storage::DeviceRepositoryCore::markTransferActivityStep(
                corePeer.deviceId, activityAtMs));
            steps.push_back(gy::storage::TransferRepositoryCore::upsertFinishedTransferStep(coreRecord));
            if (!db.runSteps(steps, &error)) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }
            // 事务成功后刷新设备目录节流缓存
            _deviceRepository->noteWritten(corePeer);
            return true;
        });
}

// 批量删除传输历史
void LocalDataBroker::deleteTransfers(const QStringList &recordIds)
{
    if (recordIds.isEmpty()) {
        return;
    }

    _storageWorker->submitTask(
        [this, recordIds](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 批量删除保持在同一存储任务中，避免界面侧频繁触发数据库队列。
            for (const QString &recordId : recordIds) {
                std::string error;
                if (!_transferRepository->deleteTransfer(recordId.toStdString(), &error)) {
                    if (errorMessage) {
                        *errorMessage = QString::fromStdString(error);
                    }
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository || !_deviceRepository) {
        // 降级时也必须回调一次，否则调用方的等待状态无法复位
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            // 启动只恢复最近设备，避免历史量随使用时长线性拖慢首屏。
            const std::vector<gy::storage::PeerRecord> recentDevices =
                _deviceRepository->recentPeers(10, &error);
            if (recentDevices.empty() && error.empty()) {
                return true;
            }
            if (!error.empty()) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }

            QHash<QString, QList<MessageRecord>> histories;
            for (const gy::storage::PeerRecord &peer : recentDevices) {
                gy::storage::MessageCursor cursor;
                cursor.peerDeviceId = peer.deviceId;
                // 每个会话限制一页，向上翻页由后续历史界面负责。
                const std::vector<gy::storage::MessageRecord> messages =
                    _messageRepository->loadMessages(cursor, 50, &error);
                if (!error.empty()) {
                    if (errorMessage) {
                        *errorMessage = QString::fromStdString(error);
                    }
                    return false;
                }
                if (!messages.empty()) {
                    histories.insert(QString::fromStdString(peer.deviceId),
                                     fromCoreList<gy::storage::MessageRecord, MessageRecord>(messages));
                }
            }

            gy::bridge::postToUiThread(receiver, [callback, histories] {
                callback(histories, true);
            });
            return true;
        });
}

// 异步加载最近传输历史
void LocalDataBroker::loadRecentTransferHistories(QObject *receiver,
                                                  const TransferHistoriesCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        // 降级时也必须回调一次，否则调用方的等待状态无法复位
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            gy::storage::TransferQuery query;
            const std::vector<gy::storage::TransferRecord> records =
                _transferRepository->queryTransfers(query, 100, &error);
            if (!error.empty()) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }

            const QList<TransferRecord> qtRecords =
                fromCoreList<gy::storage::TransferRecord, TransferRecord>(records);
            gy::bridge::postToUiThread(receiver, [callback, qtRecords] {
                callback(qtRecords, true);
            });
            return true;
        });
}

// 异步加载最近设备目录
void LocalDataBroker::loadRecentPeers(QObject *receiver, int limit,
                                      const PeersCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, limit, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            const std::vector<gy::storage::PeerRecord> records =
                _deviceRepository->recentPeers(limit, &error);
            const bool succeeded = error.empty();
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            const QList<PeerRecord> qtRecords =
                fromCoreList<gy::storage::PeerRecord, PeerRecord>(records);
            gy::bridge::postToUiThread(receiver, [callback, qtRecords, succeeded] {
                callback(qtRecords, succeeded);
            });
            return succeeded;
        });
}

// 异步按关键字检索设备目录
void LocalDataBroker::searchPeers(QObject *receiver, const QString &keyword, int limit,
                                  const PeersCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, keyword, limit, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 筛选与排序都在存储层完成，应用层只拿领域记录
            std::string error;
            const std::vector<gy::storage::PeerRecord> records =
                _deviceRepository->searchPeers(keyword.toStdString(), limit, &error);
            const bool succeeded = error.empty();
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            const QList<PeerRecord> qtRecords =
                fromCoreList<gy::storage::PeerRecord, PeerRecord>(records);
            gy::bridge::postToUiThread(receiver, [callback, qtRecords, succeeded] {
                callback(qtRecords, succeeded);
            });
            return succeeded;
        });
}

// 异步加载指定会话的一页聊天历史
void LocalDataBroker::loadMessages(QObject *receiver, const MessageCursor &cursor, int limit,
                                   const MessagesCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, cursor, limit, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 游标分页按时间倒序取消息，cursor.beforeSentAt 为空时取首页
            std::string error;
            const std::vector<gy::storage::MessageRecord> records =
                _messageRepository->loadMessages(toCore(cursor), limit, &error);
            const bool succeeded = error.empty();
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            // 回投到 receiver 所在线程（通常是主线程），避免跨线程操作模型
            const QList<MessageRecord> qtRecords =
                fromCoreList<gy::storage::MessageRecord, MessageRecord>(records);
            gy::bridge::postToUiThread(receiver, [callback, qtRecords, succeeded] {
                callback(qtRecords, succeeded);
            });
            return succeeded;
        });
}

// 异步按条件查询传输历史
void LocalDataBroker::queryTransfers(QObject *receiver, const TransferQuery &query, int limit,
                                     const TransfersCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, query, limit, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 筛选条件由调用方组合，空条件等价于全量查询
            std::string error;
            const std::vector<gy::storage::TransferRecord> records =
                _transferRepository->queryTransfers(toCore(query), limit, &error);
            const bool succeeded = error.empty();
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            const QList<TransferRecord> qtRecords =
                fromCoreList<gy::storage::TransferRecord, TransferRecord>(records);
            gy::bridge::postToUiThread(receiver, [callback, qtRecords, succeeded] {
                callback(qtRecords, succeeded);
            });
            return succeeded;
        });
}

// 异步删除单条聊天消息
void LocalDataBroker::deleteMessage(QObject *receiver, const QString &messageId,
                                    const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, messageId, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // messageId 是网络层 UUID，不存在时视为已成功删除
            std::string error;
            const bool succeeded = _messageRepository->deleteMessage(messageId.toStdString(), &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步删除指定设备的聊天会话（外键 CASCADE 同时清理消息）
void LocalDataBroker::deleteConversation(QObject *receiver, const QString &deviceId,
                                         const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 删除父会话行时 SQLite CASCADE 自动清理关联消息，无需手动删消息
            std::string error;
            const bool succeeded = _messageRepository->deleteConversation(deviceId.toStdString(), &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步删除单条传输历史（不触碰本地文件）
void LocalDataBroker::deleteTransfer(QObject *receiver, const QString &recordId,
                                     const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, recordId, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 只删除数据库记录，已接收的本地文件由调用方决定是否清理
            std::string error;
            const bool succeeded = _transferRepository->deleteTransfer(recordId.toStdString(), &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步设置设备置顶状态（幂等，设备行不存在时同样回调成功）
void LocalDataBroker::setDevicePinned(QObject *receiver, const QString &deviceId, bool pinned,
                                      const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, pinned, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            const bool succeeded =
                _deviceRepository->setDevicePinned(deviceId.toStdString(), pinned, &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步设置设备收藏状态（幂等，设备行不存在时同样回调成功）
void LocalDataBroker::setDeviceFavorite(QObject *receiver, const QString &deviceId, bool favorite,
                                        const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, favorite, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            const bool succeeded =
                _deviceRepository->setDeviceFavorite(deviceId.toStdString(), favorite, &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步设置设备隐藏状态（幂等，设备行不存在时同样回调成功）
void LocalDataBroker::setDeviceHidden(QObject *receiver, const QString &deviceId, bool hidden,
                                      const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, hidden, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            const bool succeeded =
                _deviceRepository->setDeviceHidden(deviceId.toStdString(), hidden, &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步设置设备本地备注别名（幂等，设备行不存在时同样回调成功）
void LocalDataBroker::setDeviceAlias(QObject *receiver, const QString &deviceId,
                                     const QString &alias, const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, alias, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            const bool succeeded =
                _deviceRepository->setDeviceAlias(deviceId.toStdString(), alias.toStdString(), &error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步删除设备及其聊天与传输历史（组合事务，不删除已接收的本地文件）
void LocalDataBroker::deleteDeviceWithHistory(QObject *receiver, const QString &deviceId,
                                              const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository || !_transferRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 先删该设备传输历史再删设备行（RESTRICT 外键次序），聊天记录走 CASCADE，
            // 已接收的本地文件不在此清理范围内
            std::string error;
            const bool succeeded =
                _deviceRepository->deleteDeviceWithHistory(deviceId.toStdString(), &error);
            if (succeeded) {
                // 清除发现节流缓存，该设备再次被发现时按全新设备重新入目录
                _deviceRepository->noteDeviceDeleted(deviceId.toStdString());
            } else if (errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步清空全部聊天记录（保留设备目录）
void LocalDataBroker::clearAllMessages(QObject *receiver, const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            // CASCADE 清理会话和消息行，设备目录表不受影响
            std::string error;
            const bool succeeded = _messageRepository->clearAllMessages(&error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步清空全部传输历史（保留设备目录）
void LocalDataBroker::clearAllTransfers(QObject *receiver, const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, callback](gy::storage::DatabaseCore &, QString *errorMessage) {
            std::string error;
            const bool succeeded = _transferRepository->clearAllTransfers(&error);
            if (!succeeded && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            gy::bridge::postToUiThread(receiver, [callback, succeeded] {
                callback(succeeded);
            });
            return succeeded;
        });
}

// 异步加载备份导出所需的全量数据：单任务内完成三张表的读取
void LocalDataBroker::loadBackupData(QObject *receiver, bool includeChat, bool includeTransfers,
                                     const BackupDataCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, {}, {}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, includeChat, includeTransfers, callback](gy::storage::DatabaseCore &,
                                                                  QString *errorMessage) {
            std::string error;
            const std::vector<gy::storage::PeerRecord> devices =
                _deviceRepository->allPeers(&error);
            const bool succeeded = error.empty();
            std::vector<gy::storage::MessageRecord> messages;
            std::vector<gy::storage::TransferRecord> transfers;
            if (succeeded && includeChat) {
                messages = _messageRepository->allMessages(&error);
            }
            if (error.empty() && includeTransfers) {
                transfers = _transferRepository->allTransfers(&error);
            }
            const bool allDone = error.empty();
            if (!allDone && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            const QList<PeerRecord> qtDevices =
                fromCoreList<gy::storage::PeerRecord, PeerRecord>(devices);
            const QList<MessageRecord> qtMessages =
                fromCoreList<gy::storage::MessageRecord, MessageRecord>(messages);
            const QList<TransferRecord> qtTransfers =
                fromCoreList<gy::storage::TransferRecord, TransferRecord>(transfers);
            gy::bridge::postToUiThread(receiver, [callback, qtDevices, qtMessages, qtTransfers, allDone] {
                callback(qtDevices, qtMessages, qtTransfers, allDone);
            });
            return allDone;
        });
}

// 异步预分析备份数据：加载三张表的主键集合做冲突计数，附带本机条目数
void LocalDataBroker::analyzeBackupRecords(QObject *receiver, const QList<PeerRecord> &devices,
                                           const QList<MessageRecord> &messages,
                                           const QList<TransferRecord> &transfers,
                                           const BackupAnalyzeCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, devices, messages, transfers, callback](gy::storage::DatabaseCore &db,
                                                                 QString *errorMessage) {
            std::string error;
            gy::storage::SqliteConn &database = db.connection();
            // 全量主键集合：库内条目数与冲突数一次扫描得出
            const auto loadIdSet = [&database, &error](const char *sql) {
                std::unordered_set<std::string> ids;
                gy::storage::SqliteStmt query{database, sql};
                for (;;) {
                    const auto row = query.step();
                    if (!row.has_value()) {
                        error = row.error().message;
                        return ids;
                    }
                    if (!*row) {
                        return ids;
                    }
                    ids.insert(query.columnText(0));
                }
            };

            const auto deviceIds = loadIdSet("SELECT device_id FROM peer_devices");
            const auto messageIds = loadIdSet("SELECT message_id FROM chat_messages");
            const auto sessionIds = loadIdSet("SELECT session_id FROM transfer_history");
            if (!error.empty()) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                return false;
            }

            int deviceConflicts = 0;
            for (const PeerRecord &record : devices) {
                if (deviceIds.count(record.deviceId.toStdString()) > 0) {
                    ++deviceConflicts;
                }
            }
            int messageConflicts = 0;
            for (const MessageRecord &record : messages) {
                if (messageIds.count(record.messageId.toStdString()) > 0) {
                    ++messageConflicts;
                }
            }
            int transferConflicts = 0;
            for (const TransferRecord &record : transfers) {
                if (sessionIds.count(record.sessionId.toStdString()) > 0) {
                    ++transferConflicts;
                }
            }

            QVariantMap stats;
            stats.insert(QStringLiteral("libraryDevices"), int(deviceIds.size()));
            stats.insert(QStringLiteral("libraryMessages"), int(messageIds.size()));
            stats.insert(QStringLiteral("libraryTransfers"), int(sessionIds.size()));
            stats.insert(QStringLiteral("deviceConflicts"), deviceConflicts);
            stats.insert(QStringLiteral("messageConflicts"), messageConflicts);
            stats.insert(QStringLiteral("transferConflicts"), transferConflicts);
            gy::bridge::postToUiThread(receiver, [callback, stats] {
                callback(stats, true);
            });
            return true;
        });
}

// 单事务导入备份数据：设备行按字段级合并（备注取非空一方、
// 置顶/隐藏/收藏取或、last_seen 取新），聊天按 message_id、传输按
// session_id 幂等跳过已有条目，任何一步失败整体回滚
void LocalDataBroker::importBackupRecords(QObject *receiver, const QList<PeerRecord> &devices,
                                          const QList<MessageRecord> &messages,
                                          const QList<TransferRecord> &transfers,
                                          const BackupImportCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback({}, false);
        });
        return;
    }

    // Qt 记录在任务边界先转核心记录，任务内只做 SQL 组合
    std::vector<gy::storage::PeerRecord> coreDevices;
    coreDevices.reserve(devices.size());
    for (const PeerRecord &record : devices) {
        coreDevices.push_back(toCore(record));
    }
    std::vector<gy::storage::MessageRecord> coreMessages;
    coreMessages.reserve(messages.size());
    for (const MessageRecord &record : messages) {
        coreMessages.push_back(toCore(record));
    }
    std::vector<gy::storage::TransferRecord> coreTransfers;
    coreTransfers.reserve(transfers.size());
    for (const TransferRecord &record : transfers) {
        coreTransfers.push_back(toCore(record));
    }

    _storageWorker->submitTask(
        [this, receiver, coreDevices, coreMessages, coreTransfers, callback](gy::storage::DatabaseCore &db,
                                                                            QString *errorMessage) {
            int messagesAdded = 0;
            int transfersAdded = 0;

            std::vector<gy::storage::DatabaseCore::TransactionTask> steps;
            steps.reserve(coreDevices.size() + coreMessages.size() + coreTransfers.size());

            // 设备行字段级合并：不存在则插入为离线条目，已存在按下述规则合并
            for (const gy::storage::PeerRecord &record : coreDevices) {
                steps.push_back([record](gy::storage::SqliteConn &database, std::string *taskError) {
                    gy::storage::SqliteStmt query{
                        database,
                        "INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                        "last_tcp_port, first_seen_at, last_seen_at, alias, pinned, hidden, "
                        "favorite) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(device_id) DO UPDATE SET "
                        "last_seen_at=MAX(last_seen_at, excluded.last_seen_at), "
                        "pinned=MAX(pinned, excluded.pinned), "
                        "hidden=MAX(hidden, excluded.hidden), "
                        "favorite=MAX(favorite, excluded.favorite), "
                        "alias=CASE WHEN alias IS NULL OR alias='' THEN excluded.alias "
                        "ELSE alias END"};
                    query.bind(1, record.deviceId);
                    query.bind(2, record.deviceName);
                    query.bind(3, record.lastIpAddress);
                    query.bind(4, static_cast<int64_t>(record.lastTcpPort));
                    query.bind(5, gy::storage::toIso8601Ms(record.firstSeenAtMs));
                    query.bind(6, gy::storage::toIso8601Ms(record.lastSeenAtMs));
                    query.bind(7, record.alias);
                    query.bind(8, record.pinned ? 1 : 0);
                    query.bind(9, record.hidden ? 1 : 0);
                    query.bind(10, record.favorite ? 1 : 0);
                    return runStatement(query, taskError);
                });
            }

            // 聊天消息：先占会话行再写消息，message_id 冲突即视为已存在跳过
            for (const gy::storage::MessageRecord &record : coreMessages) {
                steps.push_back([&record, &messagesAdded](gy::storage::SqliteConn &database,
                                                          std::string *taskError) {
                    gy::storage::SqliteStmt conv{
                        database,
                        "INSERT INTO chat_conversations(peer_device_id, created_at, "
                        "last_message_at) VALUES(?, ?, ?) "
                        "ON CONFLICT(peer_device_id) DO NOTHING"};
                    conv.bind(1, record.peerDeviceId);
                    conv.bind(2, gy::storage::toIso8601Ms(record.createdAtMs));
                    conv.bind(3, gy::storage::toIso8601Ms(record.sentAtMs));
                    if (!runStatement(conv, taskError)) {
                        return false;
                    }
                    gy::storage::SqliteStmt msg{
                        database,
                        "INSERT INTO chat_messages(message_id, peer_device_id, "
                        "direction, sender_device_id, sender_name, content, sent_at, "
                        "local_status, created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(message_id) DO NOTHING"};
                    msg.bind(1, record.messageId);
                    msg.bind(2, record.peerDeviceId);
                    msg.bind(3, static_cast<int>(record.direction));
                    msg.bind(4, record.senderDeviceId);
                    msg.bind(5, record.senderName);
                    msg.bind(6, record.content);
                    msg.bind(7, gy::storage::toIso8601Ms(record.sentAtMs));
                    msg.bind(8, record.localStatus);
                    msg.bind(9, gy::storage::toIso8601Ms(record.createdAtMs));
                    if (!runStatement(msg, taskError)) {
                        return false;
                    }
                    if (database.changesCount() > 0) {
                        ++messagesAdded;
                        // 新消息推动会话最近消息时间，重复条目不影响既有排序
                        gy::storage::SqliteStmt act{
                            database,
                            "UPDATE chat_conversations SET "
                            "last_message_at=MAX(last_message_at, ?) "
                            "WHERE peer_device_id=?"};
                        act.bind(1, gy::storage::toIso8601Ms(record.sentAtMs));
                        act.bind(2, record.peerDeviceId);
                        return runStatement(act, taskError);
                    }
                    return true;
                });
            }

            // 传输历史：record_id 是主键、session_id 唯一，重导入命中
            // session_id 冲突按幂等跳过，不覆盖本地已有行
            for (const gy::storage::TransferRecord &record : coreTransfers) {
                steps.push_back([&record, &transfersAdded](gy::storage::SqliteConn &database,
                                                           std::string *taskError) {
                    gy::storage::SqliteStmt query{
                        database,
                        "INSERT INTO transfer_history(record_id, session_id, peer_device_id, "
                        "peer_name, direction, display_name, is_directory, file_count, "
                        "total_bytes, status, started_at, finished_at, error_code, "
                        "error_message) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(session_id) DO NOTHING"};
                    query.bind(1, record.recordId);
                    query.bind(2, record.sessionId);
                    query.bind(3, record.peerDeviceId);
                    query.bind(4, record.peerName);
                    query.bind(5, static_cast<int>(record.direction));
                    query.bind(6, record.displayName);
                    query.bind(7, record.isDirectory ? 1 : 0);
                    query.bind(8, record.fileCount);
                    query.bind(9, record.totalBytes);
                    query.bind(10, record.status);
                    query.bind(11, gy::storage::toIso8601Ms(record.startedAtMs));
                    if (record.finishedAtMs.has_value()) {
                        query.bind(12, gy::storage::toIso8601Ms(*record.finishedAtMs));
                    } else {
                        query.bindNull(12);
                    }
                    query.bind(13, record.errorCode);
                    query.bind(14, record.errorMessage);
                    if (!runStatement(query, taskError)) {
                        return false;
                    }
                    if (database.changesCount() > 0) {
                        ++transfersAdded;
                    }
                    return true;
                });
            }

            std::string error;
            if (!db.runSteps(steps, &error)) {
                // 单事务内任一步失败整体回滚；按代管者契约仍需恰好回调一次
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                gy::bridge::postToUiThread(receiver, [callback] {
                    callback({}, false);
                });
                return false;
            }
            for (const gy::storage::PeerRecord &record : coreDevices) {
                _deviceRepository->noteWritten(record);
            }

            QVariantMap stats;
            stats.insert(QStringLiteral("devices"), int(coreDevices.size()));
            stats.insert(QStringLiteral("messages"), int(coreMessages.size()));
            stats.insert(QStringLiteral("messagesAdded"), messagesAdded);
            stats.insert(QStringLiteral("transfers"), int(coreTransfers.size()));
            stats.insert(QStringLiteral("transfersAdded"), transfersAdded);
            gy::bridge::postToUiThread(receiver, [callback, stats] {
                callback(stats, true);
            });
            return true;
        });
}

// 异步统计设备名下的聊天与传输条数（关联向导预览用）
void LocalDataBroker::countDeviceRecords(QObject *receiver, const QString &deviceId,
                                         const DeviceCountsCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(0, 0, false);
        });
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, callback](gy::storage::DatabaseCore &db, QString *errorMessage) {
            gy::storage::SqliteConn &database = db.connection();
            // 两条 COUNT 各用独立语句，绑定与执行口径与旧版逐字一致
            gy::storage::SqliteStmt query{database,
                                          "SELECT COUNT(*) FROM chat_messages WHERE peer_device_id = ?"};
            query.bind(1, deviceId.toStdString());
            const auto row = query.step();
            if (!row.has_value() || !*row) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(
                        row.has_value() ? std::string("统计聊天条数失败") : row.error().message);
                }
                return false;
            }
            const int messages = static_cast<int>(query.columnInt64(0));

            gy::storage::SqliteStmt transferQuery{database,
                                                  "SELECT COUNT(*) FROM transfer_history WHERE peer_device_id = ?"};
            transferQuery.bind(1, deviceId.toStdString());
            const auto transfersRow = transferQuery.step();
            if (!transfersRow.has_value() || !*transfersRow) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(
                        transfersRow.has_value() ? std::string("统计传输条数失败")
                                                 : transfersRow.error().message);
                }
                return false;
            }
            const int transfers = static_cast<int>(transferQuery.columnInt64(0));
            gy::bridge::postToUiThread(receiver, [callback, messages, transfers] {
                callback(messages, transfers, true);
            });
            return true;
        });
}

// 单事务合并设备：管理标记并入新行（备注取非空一方、置顶/隐藏/收藏取或、
// last_seen 取新），勾选历史迁移时聊天与传输记录整体改挂新行（同
// message_id 的旧消息视为重复跳过并随旧行清理），最后删除旧行
void LocalDataBroker::mergeDeviceRecords(QObject *receiver, const PeerRecord &newDevice,
                                         const QString &oldDeviceId, bool includeHistory,
                                         const OperationCallback &callback)
{
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_deviceRepository) {
        gy::bridge::postToUiThread(receiver, [callback] {
            callback(false);
        });
        return;
    }

    const gy::storage::PeerRecord coreNewDevice = toCore(newDevice);
    const std::string coreOldDeviceId = oldDeviceId.toStdString();
    _storageWorker->submitTask(
        [this, receiver, coreNewDevice, coreOldDeviceId, includeHistory, callback](gy::storage::DatabaseCore &db,
                                                                                   QString *errorMessage) {
            std::vector<gy::storage::DatabaseCore::TransactionTask> steps;
            steps.push_back(gy::storage::DeviceRepositoryCore::upsertPeerStep(coreNewDevice));
            // 历史迁移须先于标记合并：标记步骤尾部会移除旧行，历史必须先行搬走
            if (includeHistory) {
                steps.push_back([newDevice = coreNewDevice, oldDeviceId = coreOldDeviceId](
                                    gy::storage::SqliteConn &database, std::string *taskError) {
                    // 聊天迁移：会话行是子表消息的外键父行，直接 UPDATE 父键会被
                    // 拒绝（schema 无 ON UPDATE 动作），统一走"新行 + 搬消息 + 删旧行"
                    gy::storage::SqliteStmt probe{
                        database,
                        "SELECT created_at, last_message_at FROM chat_conversations "
                        "WHERE peer_device_id = ?"};
                    probe.bind(1, oldDeviceId);
                    const auto probeRow = probe.step();
                    if (!probeRow.has_value()) {
                        if (taskError) {
                            *taskError = probeRow.error().message;
                        }
                        return false;
                    }
                    if (!*probeRow) {
                        return true;  // 旧设备没有会话行，无历史可迁
                    }
                    const std::string createdAt = probe.columnText(0);
                    const std::string lastMessageAt = probe.columnText(1);

                    // 新会话行不存在则按旧行时间戳建行（存在则沿用，时间取较新）
                    gy::storage::SqliteStmt ensure{
                        database,
                        "INSERT INTO chat_conversations(peer_device_id, created_at, "
                        "last_message_at) VALUES(?, ?, ?) "
                        "ON CONFLICT(peer_device_id) DO UPDATE SET "
                        "last_message_at=MAX(last_message_at, excluded.last_message_at)"};
                    ensure.bind(1, newDevice.deviceId);
                    ensure.bind(2, createdAt);
                    ensure.bind(3, lastMessageAt);
                    if (!runStatement(ensure, taskError)) {
                        return false;
                    }

                    // 消息改挂新会话（同 message_id 的重复消息跳过并随旧行清理）
                    gy::storage::SqliteStmt move{
                        database,
                        "UPDATE chat_messages SET peer_device_id = ? "
                        "WHERE peer_device_id = ? AND message_id NOT IN "
                        "(SELECT message_id FROM chat_messages WHERE peer_device_id = ?)"};
                    move.bind(1, newDevice.deviceId);
                    move.bind(2, oldDeviceId);
                    move.bind(3, newDevice.deviceId);
                    if (!runStatement(move, taskError)) {
                        return false;
                    }

                    // 传输历史整体改挂新行（record_id/session_id 不变，无唯一冲突）
                    gy::storage::SqliteStmt transfers{
                        database,
                        "UPDATE transfer_history SET peer_device_id = ? "
                        "WHERE peer_device_id = ?"};
                    transfers.bind(1, newDevice.deviceId);
                    transfers.bind(2, oldDeviceId);
                    return runStatement(transfers, taskError);
                });
            } else {
                // 不迁移历史：旧传输记录显式删除（RESTRICT 外键要求先清），
                // 旧聊天随旧行级联清理，语义为"放弃旧记录"
                steps.push_back([oldDeviceId = coreOldDeviceId](gy::storage::SqliteConn &database,
                                                                std::string *taskError) {
                    gy::storage::SqliteStmt query{
                        database, "DELETE FROM transfer_history WHERE peer_device_id = ?"};
                    query.bind(1, oldDeviceId);
                    return runStatement(query, taskError);
                });
            }
            // 标记合并收尾（含移除旧行），旧历史此时已搬空
            steps.push_back(gy::storage::DeviceRepositoryCore::mergeManualPeerMarkersStep(
                coreNewDevice.deviceId, coreOldDeviceId));

            std::string error;
            if (!db.runSteps(steps, &error)) {
                if (errorMessage) {
                    *errorMessage = QString::fromStdString(error);
                }
                gy::bridge::postToUiThread(receiver, [callback] {
                    callback(false);
                });
                return false;
            }
            _deviceRepository->noteWritten(coreNewDevice);
            _deviceRepository->noteDeviceDeleted(coreOldDeviceId);
            gy::bridge::postToUiThread(receiver, [callback] {
                callback(true);
            });
            return true;
        });
}

// 异步删除指定时间前的聊天和传输历史（由保留期限定时器触发）
void LocalDataBroker::deleteExpiredRecords(const QDateTime &before)
{
    if (!_storage->isAvailable() || !_messageRepository || !_transferRepository) {
        return;
    }

    const int64_t beforeMs = before.toMSecsSinceEpoch();
    _storageWorker->submitTask(
        [this, beforeMs](gy::storage::DatabaseCore &, QString *errorMessage) {
            // 两张表独立清理，任一失败则整体报告失败
            std::string error;
            const bool messagesOk =
                _messageRepository->deleteExpiredMessages(beforeMs, &error);
            const bool transfersOk = messagesOk
                ? _transferRepository->deleteExpiredTransfers(beforeMs, &error)
                : false;
            if (!transfersOk && errorMessage) {
                *errorMessage = QString::fromStdString(error);
            }
            return transfersOk;
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

// 关闭存储线程并释放数据库连接
void LocalDataBroker::closeStorage()
{
    if (_storageThread && _storageThread->isRunning()) {
        // 先在归属线程内同步析构 Worker：析构函数会排空任务并回收内部线程，
        // 确保不依赖线程退出时 DeferredDelete 事件的冲刷时序，删除必然执行。
        QMetaObject::invokeMethod(
            _storageWorker,
            [worker = _storageWorker] { delete worker; },
            Qt::BlockingQueuedConnection);
        _storageThread->quit();
        _storageThread->wait();
    }

    _storageWorker = nullptr;
    _storageThread = nullptr;
    _transferRepository.reset();
    _messageRepository.reset();
    _deviceRepository.reset();
    _storage.reset();
}
