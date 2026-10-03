/**
* @file    history_wiring.cpp
* @version 7.15.14
* @date    2026-10-04
* @author  GridYard Team
* @brief   本地历史持久化装配实现
*
* Change Log:
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.13.2] GY   2026-10-03
* * 启动恢复回调适配成功位，存储不可用时记录失败不再静默
* [v7.11.0] GY   2026-10-02
* * 自 AppController 拆出历史持久化装配与保留期清理
*/

#include "history_wiring.h"
#include "application_paths.h"
#include "chat_manager.h"
#include "discovery_service.h"
#include "history_controller.h"
#include "history_records.h"
#include "local_data_broker.h"
#include "peer_discovery_view_model.h"
#include "transfer_session_manager.h"

#include <QDebug>
#include <QTimer>
#include <QVariantMap>

// 历史保留清理间隔：1 小时
static constexpr int kRetentionIntervalMs = 60 * 60 * 1000;

// 构造函数
HistoryWiring::HistoryWiring(DiscoveryService *discovery, ChatManager *chat,
                             TransferSessionManager *transfer,
                             PeerDiscoveryViewModel *peerDiscoveryViewModel,
                             HistoryController *history, LocalDataBroker *dataBroker,
                             QObject *parent)
    : QObject{parent}
    , _discovery{discovery}
    , _chat{chat}
    , _transfer{transfer}
    , _peerDiscoveryViewModel{peerDiscoveryViewModel}
    , _history{history}
    , _dataBroker{dataBroker}
    , _retentionTimer{new QTimer{this}}
{
}

// 打开本地历史库、完成持久化接线并启动保留期清理
bool HistoryWiring::initialize()
{
    QString storageError;
    if (!_dataBroker->initialize(ApplicationPaths::databaseDir() + "/gridyard-history.sqlite",
                                 &storageError)) {
        // 历史库不可用时保留在线收发能力，避免本地磁盘问题影响 P2P 主链路。
        qWarning() << "[Storage] 本地历史不可用:" << storageError;
    }
    const bool available = _dataBroker->isAvailable();

    _peerDiscoveryViewModel->initDataBroker(_dataBroker);  // 启动时加载本地历史设备目录
    connectPersistence();

    _history->cleanupExpiredRecords();  // 启动时立即清理一次过期历史
    _retentionTimer->setInterval(kRetentionIntervalMs);
    connect(_retentionTimer, &QTimer::timeout,
            _history, &HistoryController::cleanupExpiredRecords);
    _retentionTimer->start();

    return available;
}

// 连接发现、聊天、传输三路持久化信号并恢复启动数据
void HistoryWiring::connectPersistence()
{
    // 存储失败只记录降级状态，不影响已完成的网络收发。
    connect(_dataBroker, &LocalDataBroker::operationFailed,
            this, [this] {
                qWarning() << "[Storage] 存储任务失败，当前操作未写入本地历史";
                emit operationFailed();
            });

    // 发现结果异步写入设备目录，避免 UDP 心跳阻塞主线程。
    connect(_discovery, &DiscoveryService::peerUpdated,
            this, [this](const PeerInfo &peer) {
                _dataBroker->persistDiscoveredPeer(peer);  // 设备快照投递到 Worker 线程异步写入
            });

    // 消息收发成功后按顺序确保设备目录和聊天记录均已落库。
    connect(_chat, &ChatManager::messageToPersist,
            this, [this](const MessageRecord &record) {
                // 先查询对端最新端点信息，设备目录记录可能比消息记录更早写入。
                const QVariantMap endpoint = _discovery->transferEndpoint(record.peerDeviceId);
                _dataBroker->persistChatMessage(record, endpoint);
            });

    loadRecentChatHistories();

    connect(_transfer, &TransferSessionManager::transferToPersist,
            this, [this](const TransferRecord &record) {
                // 传输结束时同步写入设备目录，保证外键引用完整。
                const QVariantMap endpoint = _discovery->transferEndpoint(record.peerDeviceId);
                _dataBroker->persistTransferRecord(record, endpoint);
            });

    connect(_transfer, &TransferSessionManager::transferHistoryDeleteRequested,
            this, [this](const QStringList &recordIds) {
                if (recordIds.isEmpty()) {
                    return;  // 空列表无需提交 Worker 任务
                }

                _dataBroker->deleteTransfers(recordIds);  // 批量删除投递到 Worker 线程
            });

    loadRecentTransferHistories();
}

// 在存储线程读取最近聊天记录并回投到主线程恢复模型
void HistoryWiring::loadRecentChatHistories()
{
    if (!_dataBroker) {
        return;
    }

    _dataBroker->loadRecentChatHistories(
        this, [this](const QHash<QString, QList<MessageRecord>> &histories, bool succeeded) {
            if (!succeeded) {
                qWarning() << "[Storage] 启动恢复聊天历史失败";
                return;
            }
            for (auto it = histories.cbegin(); it != histories.cend(); ++it) {
                _chat->restoreMessages(it.key(), it.value());
            }
        });
}

// 在存储线程读取最近传输历史并回投到主线程恢复模型
void HistoryWiring::loadRecentTransferHistories()
{
    if (!_dataBroker) {
        return;
    }

    _dataBroker->loadRecentTransferHistories(
        this, [this](const QList<TransferRecord> &records, bool succeeded) {
            if (!succeeded) {
                qWarning() << "[Storage] 启动恢复传输历史失败";
                return;
            }
            _transfer->restoreFinishedTransfers(records);
        });
}
