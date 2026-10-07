/**
* @file    local_data_broker.cpp
* @version 7.24.0
* @date 2026-10-08
* @author  GridYard Team
* @brief   本地数据层代管者实现
*
* 集中管理 SQLite 初始化、Repository 实现、数据库任务线程和历史数据编排，
* 避免 AppController 直接持有数据管理层细节。
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
#include <QSqlDatabase>
#include <QSqlError>
#include <QSet>
#include <QSqlQuery>
#include <QThread>
#include <vector>

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
    closeStorage();
}

// 初始化数据库和存储任务线程
bool LocalDataBroker::initialize(const QString &databasePath, QString *errorMessage)
{
    const bool initialized = _storage->initialize(databasePath, errorMessage);

    // 库损坏被备份重建时置位只读属性，界面据此提示历史已清零重置
    if (_storage->lastInitializeRebuilt()) {
        _historyDatabaseRebuilt = true;
        _rebuiltBackupPath = _storage->rebuiltBackupPath();
        emit historyDatabaseRebuiltChanged();
        emit rebuiltBackupPathChanged();
    }

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

    _storageWorker->submitTask(
        [this, record](SqliteDatabaseBroker &, QString *errorMessage) {
            // 设备目录存在节流，短时间内重复心跳不会实际写磁盘
            if (!_deviceRepository->upsertPeer(record, errorMessage)) {
                return false;
            }
            // 真身心跳到达时并入同 IP 手动伪条目的管理标记（独立小事务，
            // 幂等：伪行删除后为空操作；伪 ID 自身的心跳不做迁移）
            if (gy::domain::isManualPseudoDeviceId(record.deviceId)) {
                return true;
            }
            return _deviceRepository->mergeManualPeerMarkers(
                record.deviceId, gy::domain::manualPseudoDeviceId(record.lastIpAddress),
                errorMessage);
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

    _storageWorker->submitTask(
        [this, peer, record](SqliteDatabaseBroker &db, QString *errorMessage) {
            // 组合三个步骤在同一事务内执行，任何一步失败全部回滚
            std::vector<std::function<bool(QSqlDatabase &, QString *)>> steps;
            steps.push_back(SqliteDeviceRepository::upsertPeerStep(peer));
            steps.push_back(SqliteDeviceRepository::markChatActivityStep(peer.deviceId, record.sentAt));
            steps.push_back(SqliteMessageRepository::saveMessageStep(record));
            if (!db.runSteps(steps, errorMessage)) {
                return false;
            }
            // 事务成功后刷新设备目录节流缓存
            _deviceRepository->noteWritten(peer);
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

    _storageWorker->submitTask(
        [this, peer, record, activityAt](SqliteDatabaseBroker &db, QString *errorMessage) {
            // 组合三个步骤在同一事务内执行，任何一步失败全部回滚
            std::vector<std::function<bool(QSqlDatabase &, QString *)>> steps;
            steps.push_back(SqliteDeviceRepository::upsertPeerStep(peer));
            steps.push_back(SqliteDeviceRepository::markTransferActivityStep(peer.deviceId, activityAt));
            steps.push_back(SqliteTransferHistoryRepository::upsertFinishedTransferStep(record));
            if (!db.runSteps(steps, errorMessage)) {
                return false;
            }
            // 事务成功后刷新设备目录节流缓存
            _deviceRepository->noteWritten(peer);
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository || !_deviceRepository) {
        // 降级时也必须回调一次，否则调用方的等待状态无法复位
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
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
                callback(histories, true);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            TransferQuery query;
            const QList<TransferRecord> records = _transferRepository->queryTransfers(
                query, 100, errorMessage);
            if (!errorMessage->isEmpty()) {
                return false;
            }

            QMetaObject::invokeMethod(receiver, [callback, records] {
                callback(records, true);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, limit, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const QList<PeerRecord> records = _deviceRepository->recentPeers(limit, errorMessage);
            const bool succeeded = errorMessage->isEmpty();
            QMetaObject::invokeMethod(receiver, [callback, records, succeeded] {
                callback(records, succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, keyword, limit, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 筛选与排序都在存储层完成，应用层只拿领域记录
            const QList<PeerRecord> records = _deviceRepository->searchPeers(keyword, limit,
                                                                             errorMessage);
            const bool succeeded = errorMessage->isEmpty();
            QMetaObject::invokeMethod(receiver, [callback, records, succeeded] {
                callback(records, succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository) {
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_messageRepository) {
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, recordId, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 只删除数据库记录，已接收的本地文件由调用方决定是否清理
            const bool succeeded = _transferRepository->deleteTransfer(recordId, errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, pinned, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const bool succeeded = _deviceRepository->setDevicePinned(deviceId, pinned,
                                                                      errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, favorite, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const bool succeeded = _deviceRepository->setDeviceFavorite(deviceId, favorite,
                                                                        errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, hidden, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const bool succeeded = _deviceRepository->setDeviceHidden(deviceId, hidden,
                                                                      errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, alias, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const bool succeeded = _deviceRepository->setDeviceAlias(deviceId, alias,
                                                                     errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            // 先删该设备传输历史再删设备行（RESTRICT 外键次序），聊天记录走 CASCADE，
            // 已接收的本地文件不在此清理范围内
            const bool succeeded = _deviceRepository->deleteDeviceWithHistory(deviceId,
                                                                              errorMessage);
            if (succeeded) {
                // 清除发现节流缓存，该设备再次被发现时按全新设备重新入目录
                _deviceRepository->noteDeviceDeleted(deviceId);
            }
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
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
    if (!receiver) {
        return;
    }
    if (!_storage->isAvailable() || !_transferRepository) {
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            const bool succeeded = _transferRepository->clearAllTransfers(errorMessage);
            QMetaObject::invokeMethod(receiver, [callback, succeeded] {
                callback(succeeded);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, {}, {}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, includeChat, includeTransfers, callback](SqliteDatabaseBroker &,
                                                                  QString *errorMessage) {
            const QList<PeerRecord> devices = _deviceRepository->allPeers(errorMessage);
            const bool succeeded = errorMessage->isEmpty();
            QList<MessageRecord> messages;
            QList<TransferRecord> transfers;
            if (succeeded && includeChat) {
                messages = _messageRepository->allMessages(errorMessage);
            }
            if (succeeded && includeTransfers) {
                transfers = _transferRepository->allTransfers(errorMessage);
            }
            const bool allDone = errorMessage->isEmpty();
            QMetaObject::invokeMethod(receiver, [callback, devices, messages, transfers, allDone] {
                callback(devices, messages, transfers, allDone);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, devices, messages, transfers, callback](SqliteDatabaseBroker &,
                                                                 QString *errorMessage) {
            QSqlDatabase database = _storage->connectionForWorkerThread(errorMessage);
            if (!database.isValid()) {
                return false;
            }
            // 全量主键集合：库内条目数与冲突数一次扫描得出
            const auto loadIdSet = [&database, errorMessage](const QString &column,
                                                             const QString &table) {
                QSet<QString> ids;
                QSqlQuery query(database);
                query.prepare(QStringLiteral("SELECT %1 FROM %2").arg(column, table));
                if (!query.exec()) {
                    *errorMessage = query.lastError().text();
                    return ids;
                }
                while (query.next()) {
                    ids.insert(query.value(0).toString());
                }
                return ids;
            };

            const QSet<QString> deviceIds = loadIdSet(QStringLiteral("device_id"),
                                                      QStringLiteral("peer_devices"));
            const QSet<QString> messageIds = loadIdSet(QStringLiteral("message_id"),
                                                       QStringLiteral("chat_messages"));
            const QSet<QString> sessionIds = loadIdSet(QStringLiteral("session_id"),
                                                       QStringLiteral("transfer_history"));
            if (!errorMessage->isEmpty()) {
                return false;
            }

            int deviceConflicts = 0;
            for (const PeerRecord &record : devices) {
                if (deviceIds.contains(record.deviceId)) {
                    ++deviceConflicts;
                }
            }
            int messageConflicts = 0;
            for (const MessageRecord &record : messages) {
                if (messageIds.contains(record.messageId)) {
                    ++messageConflicts;
                }
            }
            int transferConflicts = 0;
            for (const TransferRecord &record : transfers) {
                if (sessionIds.contains(record.sessionId)) {
                    ++transferConflicts;
                }
            }

            QVariantMap stats;
            stats.insert(QStringLiteral("libraryDevices"), deviceIds.size());
            stats.insert(QStringLiteral("libraryMessages"), messageIds.size());
            stats.insert(QStringLiteral("libraryTransfers"), sessionIds.size());
            stats.insert(QStringLiteral("deviceConflicts"), deviceConflicts);
            stats.insert(QStringLiteral("messageConflicts"), messageConflicts);
            stats.insert(QStringLiteral("transferConflicts"), transferConflicts);
            QMetaObject::invokeMethod(receiver, [callback, stats] {
                callback(stats, true);
            }, Qt::QueuedConnection);
            return true;
        });
}

// 单事务导入备份数据：设备行按字段级规则合并（备注取非空一方、// 单事务导入备份数据：设备行按字段级规则合并（备注取非空一方、
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback({}, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, devices, messages, transfers, callback](SqliteDatabaseBroker &db,
                                                                 QString *errorMessage) {
            // UTC 时间转 ISO 文本，与各仓库的落库口径一致
            const auto isoTime = [](const QDateTime &time) {
                return time.toUTC().toString(Qt::ISODateWithMs);
            };
            int messagesAdded = 0;
            int transfersAdded = 0;

            std::vector<std::function<bool(QSqlDatabase &, QString *)>> steps;
            steps.reserve(devices.size() + messages.size() + transfers.size());

            // 设备行字段级合并：不存在则插入为离线条目，已存在按下述规则合并
            for (const PeerRecord &record : devices) {
                steps.push_back([record, isoTime](QSqlDatabase &database, QString *taskError) {
                    QSqlQuery query(database);
                    query.prepare(
                        "INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                        "last_tcp_port, first_seen_at, last_seen_at, alias, pinned, hidden, "
                        "favorite) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(device_id) DO UPDATE SET "
                        "last_seen_at=MAX(last_seen_at, excluded.last_seen_at), "
                        "pinned=MAX(pinned, excluded.pinned), "
                        "hidden=MAX(hidden, excluded.hidden), "
                        "favorite=MAX(favorite, excluded.favorite), "
                        "alias=CASE WHEN alias IS NULL OR alias='' THEN excluded.alias "
                        "ELSE alias END");
                    query.addBindValue(record.deviceId);
                    query.addBindValue(record.deviceName);
                    query.addBindValue(record.lastIpAddress);
                    query.addBindValue(record.lastTcpPort);
                    query.addBindValue(isoTime(record.firstSeenAt));
                    query.addBindValue(isoTime(record.lastSeenAt));
                    query.addBindValue(record.alias);
                    query.addBindValue(record.pinned ? 1 : 0);
                    query.addBindValue(record.hidden ? 1 : 0);
                    query.addBindValue(record.favorite ? 1 : 0);
                    if (!query.exec()) {
                        if (taskError) {
                            *taskError = query.lastError().text();
                        }
                        return false;
                    }
                    return true;
                });
            }

            // 聊天消息：先占会话行再写消息，message_id 冲突即视为已存在跳过
            for (const MessageRecord &record : messages) {
                steps.push_back([record, isoTime, &messagesAdded](QSqlDatabase &database,
                                                                  QString *taskError) {
                    QSqlQuery conv(database);
                    conv.prepare("INSERT INTO chat_conversations(peer_device_id, created_at, "
                                 "last_message_at) VALUES(?, ?, ?) "
                                 "ON CONFLICT(peer_device_id) DO NOTHING");
                    conv.addBindValue(record.peerDeviceId);
                    conv.addBindValue(isoTime(record.createdAt));
                    conv.addBindValue(isoTime(record.sentAt));
                    if (!conv.exec()) {
                        if (taskError) {
                            *taskError = conv.lastError().text();
                        }
                        return false;
                    }
                    QSqlQuery msg(database);
                    msg.prepare("INSERT INTO chat_messages(message_id, peer_device_id, "
                                "direction, sender_device_id, sender_name, content, sent_at, "
                                "local_status, created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
                                "ON CONFLICT(message_id) DO NOTHING");
                    msg.addBindValue(record.messageId);
                    msg.addBindValue(record.peerDeviceId);
                    msg.addBindValue(static_cast<int>(record.direction));
                    msg.addBindValue(record.senderDeviceId);
                    msg.addBindValue(record.senderName);
                    msg.addBindValue(record.content);
                    msg.addBindValue(isoTime(record.sentAt));
                    msg.addBindValue(record.localStatus);
                    msg.addBindValue(isoTime(record.createdAt));
                    if (!msg.exec()) {
                        if (taskError) {
                            *taskError = msg.lastError().text();
                        }
                        return false;
                    }
                    if (msg.numRowsAffected() > 0) {
                        ++messagesAdded;
                        // 新消息推动会话最近消息时间，重复条目不影响既有排序
                        QSqlQuery act(database);
                        act.prepare("UPDATE chat_conversations SET "
                                    "last_message_at=MAX(last_message_at, ?) "
                                    "WHERE peer_device_id=?");
                        act.addBindValue(isoTime(record.sentAt));
                        act.addBindValue(record.peerDeviceId);
                        if (!act.exec()) {
                            if (taskError) {
                                *taskError = act.lastError().text();
                            }
                            return false;
                        }
                    }
                    return true;
                });
            }

            // 传输历史：record_id 是主键、session_id 唯一，重导入命中
            // session_id 冲突按幂等跳过，不覆盖本地已有行
            for (const TransferRecord &record : transfers) {
                steps.push_back([record, isoTime, &transfersAdded](QSqlDatabase &database,
                                                                   QString *taskError) {
                    QSqlQuery query(database);
                    query.prepare(
                        "INSERT INTO transfer_history(record_id, session_id, peer_device_id, "
                        "peer_name, direction, display_name, is_directory, file_count, "
                        "total_bytes, status, started_at, finished_at, error_code, "
                        "error_message) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
                        "ON CONFLICT(session_id) DO NOTHING");
                    query.addBindValue(record.recordId);
                    query.addBindValue(record.sessionId);
                    query.addBindValue(record.peerDeviceId);
                    query.addBindValue(record.peerName);
                    query.addBindValue(static_cast<int>(record.direction));
                    query.addBindValue(record.displayName);
                    query.addBindValue(record.isDirectory ? 1 : 0);
                    query.addBindValue(record.fileCount);
                    query.addBindValue(record.totalBytes);
                    query.addBindValue(record.status);
                    query.addBindValue(isoTime(record.startedAt));
                    query.addBindValue(record.finishedAt.isValid() ? isoTime(record.finishedAt)
                                                                   : QString());
                    query.addBindValue(record.errorCode);
                    query.addBindValue(record.errorMessage);
                    if (!query.exec()) {
                        if (taskError) {
                            *taskError = query.lastError().text();
                        }
                        return false;
                    }
                    if (query.numRowsAffected() > 0) {
                        ++transfersAdded;
                    }
                    return true;
                });
            }

            if (!db.runSteps(steps, errorMessage)) {
                // 单事务内任一步失败整体回滚；按代管者契约仍需恰好回调一次
                QMetaObject::invokeMethod(receiver, [callback] {
                    callback({}, false);
                }, Qt::QueuedConnection);
                return false;
            }
            for (const PeerRecord &record : devices) {
                _deviceRepository->noteWritten(record);
            }

            QVariantMap stats;
            stats.insert(QStringLiteral("devices"), devices.size());
            stats.insert(QStringLiteral("messages"), messages.size());
            stats.insert(QStringLiteral("messagesAdded"), messagesAdded);
            stats.insert(QStringLiteral("transfers"), transfers.size());
            stats.insert(QStringLiteral("transfersAdded"), transfersAdded);
            QMetaObject::invokeMethod(receiver, [callback, stats] {
                callback(stats, true);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(0, 0, false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, deviceId, callback](SqliteDatabaseBroker &, QString *errorMessage) {
            QSqlDatabase database = _storage->connectionForWorkerThread(errorMessage);
            if (!database.isValid()) {
                return false;
            }
            int messages = 0;
            int transfers = 0;
            QSqlQuery query(database);
            query.prepare("SELECT COUNT(*) FROM chat_messages WHERE peer_device_id = ?");
            query.addBindValue(deviceId);
            if (!query.exec() || !query.next()) {
                if (errorMessage) {
                    *errorMessage = query.lastError().text();
                }
                return false;
            }
            messages = query.value(0).toInt();
            query.prepare("SELECT COUNT(*) FROM transfer_history WHERE peer_device_id = ?");
            query.addBindValue(deviceId);
            if (!query.exec() || !query.next()) {
                if (errorMessage) {
                    *errorMessage = query.lastError().text();
                }
                return false;
            }
            transfers = query.value(0).toInt();
            QMetaObject::invokeMethod(receiver, [callback, messages, transfers] {
                callback(messages, transfers, true);
            }, Qt::QueuedConnection);
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
        QMetaObject::invokeMethod(receiver, [callback] {
            callback(false);
        }, Qt::QueuedConnection);
        return;
    }

    _storageWorker->submitTask(
        [this, receiver, newDevice, oldDeviceId, includeHistory, callback](SqliteDatabaseBroker &db,
                                                                           QString *errorMessage) {
            const auto isoTime = [](const QDateTime &time) {
                return time.toUTC().toString(Qt::ISODateWithMs);
            };
            std::vector<std::function<bool(QSqlDatabase &, QString *)>> steps;
            steps.push_back(SqliteDeviceRepository::upsertPeerStep(newDevice));
            // 历史迁移须先于标记合并：标记步骤尾部会移除旧行，历史必须先行搬走
            if (includeHistory) {
                steps.push_back([newDevice, oldDeviceId](QSqlDatabase &database,
                                                         QString *taskError) {
                    // 聊天迁移：会话行是子表消息的外键父行，直接 UPDATE 父键会被
                    // 拒绝（schema 无 ON UPDATE 动作），统一走"新行 + 搬消息 + 删旧行"
                    QSqlQuery probe(database);
                    probe.prepare("SELECT created_at, last_message_at FROM chat_conversations "
                                  "WHERE peer_device_id = ?");
                    probe.addBindValue(oldDeviceId);
                    if (!probe.exec()) {
                        if (taskError) {
                            *taskError = probe.lastError().text();
                        }
                        return false;
                    }
                    if (!probe.next()) {
                        return true;  // 旧设备没有会话行，无历史可迁
                    }
                    const QString createdAt = probe.value(0).toString();
                    const QString lastMessageAt = probe.value(1).toString();

                    // 新会话行不存在则按旧行时间戳建行（存在则沿用，时间取较新）
                    QSqlQuery ensure(database);
                    ensure.prepare("INSERT INTO chat_conversations(peer_device_id, created_at, "
                                   "last_message_at) VALUES(?, ?, ?) "
                                   "ON CONFLICT(peer_device_id) DO UPDATE SET "
                                   "last_message_at=MAX(last_message_at, excluded.last_message_at)");
                    ensure.addBindValue(newDevice.deviceId);
                    ensure.addBindValue(createdAt);
                    ensure.addBindValue(lastMessageAt);
                    if (!ensure.exec()) {
                        if (taskError) {
                            *taskError = ensure.lastError().text();
                        }
                        return false;
                    }

                    // 消息改挂新会话（同 message_id 的重复消息跳过并随旧行清理）
                    QSqlQuery move(database);
                    move.prepare("UPDATE chat_messages SET peer_device_id = ? "
                                 "WHERE peer_device_id = ? AND message_id NOT IN "
                                 "(SELECT message_id FROM chat_messages WHERE peer_device_id = ?)");
                    move.addBindValue(newDevice.deviceId);
                    move.addBindValue(oldDeviceId);
                    move.addBindValue(newDevice.deviceId);
                    if (!move.exec()) {
                        if (taskError) {
                            *taskError = move.lastError().text();
                        }
                        return false;
                    }

                    // 传输历史整体改挂新行（record_id/session_id 不变，无唯一冲突）
                    QSqlQuery transfers(database);
                    transfers.prepare("UPDATE transfer_history SET peer_device_id = ? "
                                      "WHERE peer_device_id = ?");
                    transfers.addBindValue(newDevice.deviceId);
                    transfers.addBindValue(oldDeviceId);
                    if (!transfers.exec()) {
                        if (taskError) {
                            *taskError = transfers.lastError().text();
                        }
                        return false;
                    }
                    return true;
                });
            } else {
                // 不迁移历史：旧传输记录显式删除（RESTRICT 外键要求先清），
                // 旧聊天随旧行级联清理，语义为"放弃旧记录"
                steps.push_back([oldDeviceId](QSqlDatabase &database, QString *taskError) {
                    QSqlQuery query(database);
                    query.prepare("DELETE FROM transfer_history WHERE peer_device_id = ?");
                    query.addBindValue(oldDeviceId);
                    if (!query.exec()) {
                        if (taskError) {
                            *taskError = query.lastError().text();
                        }
                        return false;
                    }
                    return true;
                });
            }
            // 标记合并收尾（含移除旧行），旧历史此时已搬空
            steps.push_back(SqliteDeviceRepository::mergeManualPeerMarkersStep(newDevice.deviceId,
                                                                               oldDeviceId));

            if (!db.runSteps(steps, errorMessage)) {
                QMetaObject::invokeMethod(receiver, [callback] {
                    callback(false);
                }, Qt::QueuedConnection);
                return false;
            }
            _deviceRepository->noteWritten(newDevice);
            _deviceRepository->noteDeviceDeleted(oldDeviceId);
            QMetaObject::invokeMethod(receiver, [callback] {
                callback(true);
            }, Qt::QueuedConnection);
            return true;
        });
}

// 异步删除指定时间前的聊天和传输历史// 异步删除指定时间前的聊天和传输历史// 异步删除指定时间前的聊天和传输历史（由保留期限定时器触发）
void LocalDataBroker::deleteExpiredRecords(const QDateTime &before)
{
    if (!_storage->isAvailable() || !_messageRepository || !_transferRepository) {
        return;
    }

    _storageWorker->submitTask(
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

// 关闭存储线程并释放数据库连接
void LocalDataBroker::closeStorage()
{
    if (_storageThread && _storageThread->isRunning()) {
        // 等待已入队任务结束，防止 Worker 继续访问即将销毁的 Repository。
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
