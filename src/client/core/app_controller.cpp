/**
* @file    app_controller.cpp
* @version 6.5.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   应用全局控制器实现
*
* 构造时创建并组装 ConfigManager、DiscoveryService、P2pServer、
* TransferSessionManager，启动 P2P 服务器并初始化传输会话管理器。
*
* Change Log:
* [v6.5.0] GY   2026-06-25
* * 向表现层发布本地历史可用性与异步保存失败状态
* [v6.3.0] GY   2026-06-25
* * 接入传输历史持久化与启动恢复
* [v6.2.0] GY   2026-06-25
* * 接入聊天消息持久化，监听 messageToPersist 信号并异步提交存储
* [v6.1.0] GY   2026-06-25
* * 接入设备目录 Proxy，异步投递发现设备快照
* [v6.0.0] GY   2026-06-25
* * 集中管理本地历史数据库与数据库任务线程
* [v5.1.0] FengChunlin   2026-06-24
* * 创建并初始化在线聊天管理器
* [v4.7.1] GY   2026-06-06
* * 创建 TransferSessionManager 并初始化
* [v0.2.0] FengChunlin   2026-04-27
* * Stage 2：持有 ConfigManager 和 DiscoveryService
* [v0.1.0] FengChunlin   2026-04-14
* * Stage 0：实现 applicationName / applicationVersion / quit
*/

#include "app_controller.h"
#include "application_paths.h"
#include "chat_manager.h"
#include "config_manager.h"
#include "database_worker.h"
#include "discovery_service.h"
#include "history_records.h"
#include "history_controller.h"
#include "p2p_server.h"
#include "sqlite_database_proxy.h"
#include "sqlite_device_proxy.h"
#include "sqlite_message_proxy.h"
#include "sqlite_transfer_history_proxy.h"
#include "transfer_session_manager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

// 构造并组装应用运行期依赖
AppController::AppController(QObject *parent)
    : QObject{parent}
    , _config{ConfigManager::create(nullptr, nullptr)}
    , _discovery{new DiscoveryService{_config, this}}
    , _p2pServer{new P2pServer{_config, this}}
    , _transfer{new TransferSessionManager{this}}
    , _chat{new ChatManager{this}}
    , _storage{std::make_unique<SqliteDatabaseProxy>()}
    , _deviceRepository{std::make_unique<SqliteDeviceProxy>(_storage.get())}
    , _messageRepository{std::make_unique<SqliteMessageProxy>(_storage.get())}
    , _transferRepository{std::make_unique<SqliteTransferHistoryProxy>(_storage.get())}
    , _storageThread{new QThread{this}}
    , _storageWorker{new DatabaseWorker{_storage.get()}}
    , _history{new HistoryController{_chat, _transfer, _config, _storageWorker,
                                     _messageRepository.get(), _transferRepository.get(), this}}
    , _retentionTimer{new QTimer{this}}
{
    QString storageError;
    if (!_storage->initialize(ApplicationPaths::databaseDir() + "/gridyard-history.sqlite", &storageError)) {
        // 历史库不可用时保留在线收发能力，避免本地磁盘问题影响 P2P 主链路。
        qWarning() << "[Storage] 本地历史不可用:" << storageError;
    }
    _localHistoryAvailable = _storage->isAvailable();

    // Worker 没有 parent，才能移动到存储线程并由 finished 安全回收。
    _storageWorker->moveToThread(_storageThread);
    connect(_storageThread, &QThread::finished, _storageWorker, &QObject::deleteLater);
    connect(_storageThread, &QThread::finished, _storageThread, &QObject::deleteLater);
    _storageThread->start();

    // 存储失败只记录降级状态，不影响已完成的网络收发。
    connect(_storageWorker, &DatabaseWorker::taskFinished,
            this, [this](bool succeeded, const QString &) {
                if (!succeeded) {
                    qWarning() << "[Storage] 存储任务失败，当前操作未写入本地历史";
                    emit localHistoryOperationFailed();
                }
            });

    // 发现结果异步写入设备目录，避免 UDP 心跳阻塞主线程。
    connect(_discovery, &DiscoveryService::peerUpdated,
            this, [this](const PeerInfo &peer) {
                PeerRecord record;
                record.deviceId = peer.deviceId;
                record.deviceName = peer.deviceName;
                record.lastIpAddress = peer.ipAddress;
                record.lastTcpPort = peer.tcpPort;
                record.firstSeenAt = peer.lastSeen;
                record.lastSeenAt = peer.lastSeen;

                // 发现服务只维护在线快照，磁盘写入统一串行化到存储线程。
                _storageWorker->submitSave(
                    [this, record](SqliteDatabaseProxy &, QString *errorMessage) {
                        return _deviceRepository->upsertPeer(record, errorMessage);
                    });
            });

    _p2pServer->start();

    _transfer->init(_config, _discovery, _p2pServer);

    _chat->init(_config, _discovery, _p2pServer);

    // 消息收发成功后按顺序确保设备目录和聊天记录均已落库。
    connect(_chat, &ChatManager::messageToPersist,
            this, [this](const MessageRecord &record) {
                const QVariantMap endpoint = _discovery->transferEndpoint(record.peerDeviceId);
                PeerRecord peer;
                peer.deviceId = record.peerDeviceId;
                peer.deviceName = endpoint.value("deviceName", record.senderName).toString();
                peer.lastIpAddress = endpoint.value("ipAddress").toString();
                peer.lastTcpPort = static_cast<quint16>(endpoint.value("tcpPort").toUInt());
                peer.firstSeenAt = QDateTime::currentDateTimeUtc();
                peer.lastSeenAt = peer.firstSeenAt;
                // 同一 Worker 队列内先写设备，再写消息，满足会话外键前置条件。
                _storageWorker->submitSave(
                    [this, peer, record](SqliteDatabaseProxy &, QString *errorMessage) {
                        return _deviceRepository->upsertPeer(peer, errorMessage)
                               && _messageRepository->saveMessage(record, errorMessage);
                    });
            });

    loadRecentChatHistories();

    connect(_transfer, &TransferSessionManager::transferToPersist,
            this, [this](const TransferRecord &record) {
                const QVariantMap endpoint = _discovery->transferEndpoint(record.peerDeviceId);
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

                // 先确保设备目录存在，再更新活动时间和写入历史，满足外键约束。
                _storageWorker->submitSave(
                    [this, peer, record, activityAt](SqliteDatabaseProxy &, QString *errorMessage) {
                        return _deviceRepository->upsertPeer(peer, errorMessage)
                               && _deviceRepository->markTransferActivity(peer.deviceId, activityAt,
                                                                          errorMessage)
                               && _transferRepository->upsertFinishedTransfer(record, errorMessage);
                    });
            });

    connect(_transfer, &TransferSessionManager::transferHistoryDeleteRequested,
            this, [this](const QStringList &recordIds) {
                if (recordIds.isEmpty()) {
                    return;
                }

                _storageWorker->submitDelete(
                    [this, recordIds](SqliteDatabaseProxy &, QString *errorMessage) {
                        for (const QString &recordId : recordIds) {
                            if (!_transferRepository->deleteTransfer(recordId, errorMessage)) {
                                return false;
                            }
                        }
                        return true;
                    });
            });

    loadRecentTransferHistories();

    _history->cleanupExpiredRecords();
    _retentionTimer->setInterval(60 * 60 * 1000);
    connect(_retentionTimer, &QTimer::timeout,
            _history, &HistoryController::cleanupExpiredRecords);
    _retentionTimer->start();
}

// 停止存储线程，避免存储对象先于 Worker 销毁
AppController::~AppController()
{
    if (_storageThread && _storageThread->isRunning()) {
        // 等待已入队任务结束，防止 Worker 继续访问即将销毁的 Repository。
        _storageThread->quit();
        _storageThread->wait();
    }
}

// 创建 QML 单例实例
AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    return new AppController{};
}

// 获取应用名称
QString AppController::applicationName() const
{
    return QCoreApplication::applicationName();
}

// 获取应用版本
QString AppController::applicationVersion() const
{
    return QCoreApplication::applicationVersion();
}

// 获取设备发现服务
DiscoveryService *AppController::discovery() const
{
    return _discovery;
}

// 获取传输会话管理器
TransferSessionManager *AppController::transfer() const
{
    return _transfer;
}

// 获取在线聊天管理器
ChatManager *AppController::chat() const
{
    return _chat;
}

HistoryController *AppController::history() const
{
    return _history;
}

bool AppController::localHistoryAvailable() const
{
    return _localHistoryAvailable;
}

// 请求退出应用
void AppController::quit()
{
    if (_quitRequested) {
        return;
    }
    _quitRequested = true;
    qDebug() << "AppController::quit invoked from QML";

    if (!_storageThread || !_storageThread->isRunning() || !_storageWorker) {
        QCoreApplication::quit();
        return;
    }

    // 将停止标记排入 Worker 队列尾部，确保退出前不会丢失已提交的历史写入。
    connect(_storageWorker, &DatabaseWorker::drained,
            this, [] { QCoreApplication::quit(); }, Qt::SingleShotConnection);
    QMetaObject::invokeMethod(_storageWorker, &DatabaseWorker::beginShutdown,
                              Qt::QueuedConnection);
}

// 验证 QML 调用链路
void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}

// 在存储线程读取最近历史并回投到主线程恢复模型
void AppController::loadRecentChatHistories()
{
    if (!_storage->isAvailable() || !_messageRepository) {
        return;
    }

    _storageWorker->submitLoad(
        [this](SqliteDatabaseProxy &, QString *errorMessage) {
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

            // 模型属于主线程，不能在数据库线程直接追加行。
            QMetaObject::invokeMethod(this, [this, histories] {
                for (auto it = histories.cbegin(); it != histories.cend(); ++it) {
                    _chat->restoreMessages(it.key(), it.value());
                }
            }, Qt::QueuedConnection);
            return true;
        });
}

void AppController::loadRecentTransferHistories()
{
    if (!_storage->isAvailable() || !_transferRepository) {
        return;
    }

    _storageWorker->submitLoad(
        [this](SqliteDatabaseProxy &, QString *errorMessage) {
            TransferQuery query;
            const QList<TransferRecord> records = _transferRepository->queryTransfers(
                query, 100, errorMessage);
            if (!errorMessage->isEmpty()) {
                return false;
            }

            QMetaObject::invokeMethod(this, [this, records] {
                _transfer->restoreFinishedTransfers(records);
            }, Qt::QueuedConnection);
            return true;
        });
}
