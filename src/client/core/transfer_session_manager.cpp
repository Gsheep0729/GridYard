/**
* @file    transfer_session_manager.cpp
* @version 7.15.19
* @date    2026-10-04
* @author  GridYard Team
* @brief   传输会话管理器实现
*
* Change Log:
* [v7.15.19] GY   2026-10-04
* * 接收完成通知改传实际落盘路径，重名保存为 name (1) 时通知卡可正确定位
* [v7.15.18] GY   2026-10-04
* * 新增 activeSessionCount 活动会话计数：会话创建与终态迁移时通知，
*   供关闭确认弹窗提示退出将中断进行中的传输
* [v7.15.17] GY   2026-10-04
* * 新增 waitingConfirmReceiveSessions 快照，并发接收请求可被弹窗按到达序串行展示；
* * init 的信号连接收敛为 UniqueConnection，重复初始化不再叠加连接
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * cancelSession 接收分支先直调 worker->requestCancel()，传输中取消可立即中断接收，
*   等待确认态仍走 rejectTransfer 原语义
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
* [v7.15.1] GY   2026-10-03
* * waiting_confirm 会话被后端终结时发射 sessionStale，取代 QML 轮询推断过期
* [v7.15.0] GY   2026-10-03
* * relay 三档策略下沉到 C++：AutoRelay 档内部自动重试中继，
*   AskBeforeRelay 档改发 relayConfirmRequested 由 QML 纯弹窗回传
* [v7.14.1] GY   2026-10-03
* * 取消发送会话时先跨线程置位取消标志，阻塞期取消可达
* [v7.14.0] GY   2026-10-03
* * 启动发送 worker 前注入中继握手令牌
* [v7.10.0] GY   2026-10-02
* * 会话存储与增量通知委托给 TransferSessionModel
* * 记录映射与文件清理策略委托给 TransferSessionMapper
* * 接收 worker 改为独立映射管理，会话行不再内嵌 worker 指针
* * 会话字段名与状态值收敛为 gy::session 具名常量
* [v7.9.0] GY   2026-07-26
* * Relay 降级链路落地：直连候选轮询失败后按策略进入 awaiting_relay，
*   经协调服务器邀请建立中继传输
* [v7.8.0] GY   2026-07-21
* * 传输连接增加重试机制和候选端点超时
* [v6.6.2] GY   2026-06-25
* * 支持按当前设备清空已结束传输记录
* [v6.3.0] GY   2026-06-25
* * 生成结束态传输快照并支持恢复历史记录
* [v4.16.1] FengChunlin   2026-06-21
* * 接收请求和完成结果改为跨线程值传递，删除 Worker 状态读取函数
* [v4.15.0] FengChunlin   2026-06-16
* * transferFinished 信号适配 ErrorCode 参数
* * 会话模型新增 errorCode 字段
* [v4.14.0] GY   2026-06-15
* * 支持清理传输记录并删除已接收的本地文件
* [v4.13.3] DuRuoxian   2026-06-15
* * 仅在可见进度变化时通知会话列表，减少文件夹传输任务闪烁
* [v4.13.2] DuRuoxian   2026-06-15
* * 统一生成文件夹根目录预览并传递给接收确认弹窗
* [v4.13.1] DuRuoxian   2026-06-15
* * 添加 isDirectory 和 fileList 字段到会话
* [v4.11.0] FengChunlin   2026-06-13
* * 支持按配置自动接受并保存接收文件
* [v4.10.1] DuRuoxian   2026-06-11
* * 补齐接收会话字段，保证 QML 模型角色一致
* [v4.10.0] GY   2026-06-13
* * 新增 removeSession() 方法，添加 createdAt 时间戳
* [v4.8.3] FengChunlin   2026-06-10
* * 文件传输使用发送方设备别名
* [v4.7.1] GY   2026-06-07
* * 移除 QML_SINGLETON，改为通过 AppController 暴露
* [v4.3.4] GY   2026-05-27
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.3.1] GY   2026-05-21
* * Stage 3.10：实现 cancelSession()；保存/清理发送方 worker
* [v0.3.0] FengChunlin   2026-05-19
* * 接收会话连接 transferFinished 信号；设置接收路径
* [v0.2.0] GY   2026-05-08
* * Stage 3：初始版本
*/

#include "transfer_session_manager.h"
#include "config_manager.h"
#include "dir_serializer.h"
#include "discovery_service.h"
#include "file_receiver_worker.h"
#include "file_sender_worker.h"
#include "p2p_server.h"
#include "rendezvous_client.h"
#include "transfer_session_mapper.h"
#include "transfer_session_model.h"

// 确保跨线程信号投递与测试中 QVariant 取回可用
Q_DECLARE_METATYPE(FileReceiverWorker*)

#include <QDebug>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QDateTime>

#include <algorithm>
#include <utility>

using namespace gy::session;

namespace {

// 判断会话状态是否为已结束（完成、失败、拒绝、取消）
bool isFinishedStatus(const QString &status)
{
    return status == kStatusCompleted || status == kStatusFailed
           || status == kStatusRejected || status == kStatusCancelled;
}

// 根据 Worker 结果归一化最终状态，避免取消和拒绝被错误折叠成 failed
QString normalizedFinalStatus(bool success, gy::protocol::ErrorCode errorCode,
                              const QString &currentStatus)
{
    if (currentStatus == kStatusCancelled) {
        return kStatusCancelled;
    }
    if (currentStatus == kStatusRejected) {
        return kStatusRejected;
    }
    if (success) {
        return kStatusCompleted;
    }
    if (errorCode == gy::protocol::ErrorCode::UserRejected) {
        return kStatusRejected;
    }
    if (errorCode == gy::protocol::ErrorCode::UserCancelled) {
        return kStatusCancelled;
    }
    return kStatusFailed;
}

// 统计发送任务中的真实文件数和总字节数，为早失败场景保留完整历史快照
QPair<int, qint64> transferStatsForPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {0, 0};
    }
    if (!info.isDir()) {
        return {1, info.size()};
    }

    const auto items = gy::DirSerializer::serializeNoHash(path);
    int fileCount = 0;
    qint64 totalBytes = 0;
    for (const auto &item : items) {
        // 目录占位项只用于恢复层级，不计入实际文件数量和传输字节数。
        if (item.relativePath.endsWith('/')) {
            continue;
        }
        ++fileCount;
        totalBytes += item.sizeBytes;
    }
    return {fileCount, totalBytes};
}

// 构建文件夹根目录预览（只显示顶层文件和目录）
QVariantList buildRootPreview(const QStringList &paths)
{
    QVariantList result;
    QSet<QString> seen;

    for (const QString &path : paths) {
        // 只展示根层条目，深层文件夹折叠为它所属的顶层目录。
        const QString cleanPath = path.endsWith('/') ? path.chopped(1) : path;
        const QStringList parts = cleanPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }

        const bool isRootDirectory = parts.size() > 1 || path.endsWith('/');
        const QString rootEntry = parts.first() + (isRootDirectory ? "/" : "");
        if (!seen.contains(rootEntry)) {
            seen.insert(rootEntry);
            result.append(rootEntry);
        }
    }

    return result;
}

// 等待用户做出中继决策的超时时间
static constexpr int kRelayDecisionTimeoutMs = 120000;
// 等待协调服务器受理中继邀请的超时
static constexpr int kRelayInviteTimeoutMs = 5000;

}

// 构造函数
TransferSessionManager::TransferSessionManager(QObject *parent)
    : QObject{parent}
    , _model{new TransferSessionModel{this}}
{
}

// 获取会话列表（供 QML 绑定）
QVariantList TransferSessionManager::sessions() const
{
    return _model->sessions();
}

// 返回当前全部等待确认的接收会话快照（按到达序），供确认弹窗串行展示
QVariantList TransferSessionManager::waitingConfirmReceiveSessions() const
{
    QVariantList result;
    const QVariantList all = _model->sessions();
    for (const QVariant &entry : all) {
        const QVariantMap session = entry.toMap();
        if (session.value(kType).toString() == kTypeReceive
            && session.value(kStatus).toString() == kStatusWaitingConfirm) {
            result.append(session);
        }
    }
    return result;
}

// 返回当前活动（未终态）会话数量
int TransferSessionManager::activeSessionCount() const
{
    int count = 0;
    const QVariantList all = _model->sessions();
    for (const QVariant &entry : all) {
        if (!isFinishedStatus(entry.toMap().value(kStatus).toString())) {
            ++count;
        }
    }
    return count;
}

// 重新统计活动会话数，数量变化时发 NOTIFY
void TransferSessionManager::refreshActiveSessionCount()
{
    const int count = activeSessionCount();
    if (count != _activeSessionCount) {
        _activeSessionCount = count;
        emit activeSessionCountChanged();
    }
}

// 获取承载会话行的增量通知模型
TransferSessionModel *TransferSessionManager::sessionModel() const
{
    return _model;
}

// 初始化：绑定配置、发现服务、P2P 服务器和协调客户端
void TransferSessionManager::init(ConfigManager *config, DiscoveryService *discovery,
                                   P2pServer *p2pServer, RendezvousClient *rendezvousClient)
{
    _config = config;
    _discovery = discovery;
    _p2pServer = p2pServer;
    _rendezvous = rendezvousClient;

    // 连接 P2pServer 的传输请求信号（UniqueConnection：init 被重复调用时不再叠加连接，
    // 否则一次请求会触发多次建会话，测试夹具多次 re-init 时即会复现）
    connect(_p2pServer, &P2pServer::transferRequestReceived,
            this,       &TransferSessionManager::onTransferRequestReceived,
            static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::UniqueConnection));

    if (_rendezvous) {
        // 中继邀请受理后建立中继发送链路（同样防重复连接）
        connect(_rendezvous, &RendezvousClient::relayInviteAckReceived,
                this,        &TransferSessionManager::onRelayInviteAck,
                static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::UniqueConnection));
    }
}

// 创建发送会话（建立 TCP 连接并启动文件传输）
void TransferSessionManager::createSendSession(const QString &deviceId, const QString &filePath)
{
    qDebug() << "[TransferSession] 创建发送会话";
    qDebug() << "[TransferSession] 目标设备ID:" << deviceId;
    qDebug() << "[TransferSession] 文件路径:" << filePath;

    // 获取用于发送的对端快照
    const QVariantMap endpoint = _discovery->transferEndpoint(deviceId);
    if (endpoint.isEmpty()) {
        qWarning() << "[TransferSession] 目标设备不存在或已离线:" << deviceId;
        emit errorOccurred(tr("目标设备不存在或已离线"));
        return;
    }

    const QString host = endpoint[gy::keys::kEndpointIpAddress].toString();
    quint16 port = static_cast<quint16>(endpoint[gy::keys::kEndpointTcpPort].toUInt());
    if (port == 0) {
        port = _config->tcpPort();
    }
    const QString peerName = endpoint[gy::keys::kEndpointDeviceName].toString();
    const auto [fileCount, totalBytes] = transferStatsForPath(filePath);

    qDebug() << "[TransferSession] 目标设备信息:";
    qDebug() << "  设备名:" << peerName;
    qDebug() << "  IP 地址:" << host;
    qDebug() << "  端口:" << port;

    // 候选端点：广播发现的直连 IP 优先，协调节点缓存的多地址作为备选
    QList<QPair<QString, quint16>> endpoints;
    endpoints.append({host, port});
    quint16 alternatePort = 0;
    const QStringList alternates = _discovery->rendezvousAlternateAddresses(deviceId, &alternatePort);
    QSet<QString> seenHosts{host};
    for (const QString &address : alternates) {
        if (address.isEmpty() || seenHosts.contains(address)) {
            continue;
        }
        seenHosts.insert(address);
        endpoints.append({address, alternatePort != 0 ? alternatePort : port});
    }

    // 创建发送会话
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QVariantMap session;
    session[kSessionId] = sessionId;
    session[kType]      = kTypeSend;
    session[kDeviceId]  = deviceId;
    session[kPeerDeviceName] = peerName;
    session[kFilePath]  = filePath;
    session[kFileName]  = QFileInfo{filePath}.fileName();
    session[kIsDirectory] = QFileInfo{filePath}.isDir();
    session[kFileCount] = fileCount;
    session[kStatus]    = kStatusConnecting;
    session[kProgress]  = 0;
    session[kBytesTransferred] = 0;
    session[kTotalBytes] = totalBytes;
    session[kCreatedAt] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    session[kFileList]  = QVariantList{};
    session[kLocalPath] = "";
    session[kCanDeleteLocalFile] = false;

    // 委托 ConfigManager 填充发送方信息（Tell, Don't Ask）
    _config->fillSenderInfo(session);

    // 如果是文件夹，获取文件列表
    if (QFileInfo{filePath}.isDir()) {
        auto fileList = gy::DirSerializer::serializeNoHash(filePath);
        QStringList paths;
        for (const auto &item : fileList) {
            paths.append(item.relativePath);
        }
        session[kFileList] = buildRootPreview(paths);
    }

    _model->appendSession(session);
    emit sessionsChanged();
    refreshActiveSessionCount();  // 新建发送会话进入 connecting，活动数加一

    qDebug() << "[TransferSession] 会话已创建，ID:" << sessionId;

    startSendWorker(session, endpoints, QString(), true);
}

// 创建发送 worker 并在工作线程中运行；allowRelayFallback 标记直连失败后可降级
void TransferSessionManager::startSendWorker(const QVariantMap &session,
                                             const QList<QPair<QString, quint16>> &endpoints,
                                             const QString &relayId, bool allowRelayFallback)
{
    const QString sessionId = session.value(kSessionId).toString();
    const QString filePath = session.value(kFilePath).toString();
    const QString senderDeviceId = session.value(kSenderDeviceId).toString();
    const QString senderName = session.value(kSenderName).toString();

    // 创建 FileSenderWorker 并在工作线程中运行
    auto *worker = new FileSenderWorker{};
    // 中继握手的令牌在移线程前注入，避开跨线程读取配置
    if (_config) {
        worker->setRelayToken(_config->rendezvousToken());
    }
    auto *thread = new QThread{this};

    worker->moveToThread(thread);

    // 线程结束后按 Qt 惯用法销毁 worker 和线程：worker 在自己的事件循环里被 deleteLater，
    // 线程随后自删。不能用 quit()+wait()+deleteLater()——wait 后事件循环已停，
    // DeferredDelete 无人处理会导致 worker 连同 socket 永久泄漏，wait 本身还会阻塞 UI 线程。
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // 保存 worker 引用
    _sendWorkers[sessionId] = worker;

    connect(thread, &QThread::started, worker,
            [worker, endpoints, filePath, senderDeviceId, senderName, relayId]() {
        worker->startTransfer(endpoints, filePath, senderDeviceId, senderName, relayId);
    });

    connect(worker, &FileSenderWorker::progressChanged,
            this, [this, sessionId](qint64 bytesSent, qint64 totalBytes) {
        // 就地更新发送会话进度，模型只发出该行的 dataChanged
        _model->updateSession(sessionId, [this, bytesSent, totalBytes](QVariantMap &session) {
            const QString previousStatus = session.value(kStatus).toString();
            const int previousProgress = session.value(kProgress).toInt();
            const qint64 previousTotalBytes = session.value(kTotalBytes).toLongLong();
            const int progress = totalBytes > 0 ? (bytesSent * 100 / totalBytes) : 0;  // 百分比计算

            session[kStatus] = kStatusTransferring;
            session[kProgress] = progress;
            session[kBytesTransferred] = bytesSent;
            session[kTotalBytes] = totalBytes;
            // 仅在状态、进度或总字节数实际变化时通知 QML，避免文件夹传输任务频繁闪烁
            if (previousStatus != kStatusTransferring
                || previousProgress != progress
                || previousTotalBytes != totalBytes) {
                emit sessionsChanged();
            }
        });
    });

    connect(worker, &FileSenderWorker::transferFinished,
            this, [this, sessionId, thread, allowRelayFallback](bool success, gy::protocol::ErrorCode errorCode, const QString &errorMsg) {
        // 先读取当前会话状态，用户手动取消/拒绝的状态不应被 Worker 结果覆盖
        const QVariantMap snapshot = _model->sessionById(sessionId);
        const QString currentStatus = snapshot.value(kStatus).toString();

        // 直连阶段失败时按 Relay 策略进入中继降级决策，不立即终结会话；
        // 用户主动取消/拒绝、或中继阶段自身的失败不再次降级
        if (!success && allowRelayFallback && currentStatus == kStatusConnecting
            && errorCode != gy::protocol::ErrorCode::UserCancelled
            && errorCode != gy::protocol::ErrorCode::UserRejected
            && relayDegradationAvailable()) {
            enterAwaitingRelay(sessionId);
        } else {
            const QString finalStatus = normalizedFinalStatus(success, errorCode, currentStatus);  // 归一化最终状态
            finalizeSession(sessionId, finalStatus, errorCode, errorMsg);  // 生成快照并通知持久化
        }

        // 清理 worker 引用
        _sendWorkers.remove(sessionId);

        // 只请求线程退出，销毁由上面的 finished→deleteLater 链接完成，不在 UI 线程 wait
        thread->quit();
    });

    // 启动线程
    thread->start();

    qDebug() << "TransferSessionManager: 启动发送 worker" << sessionId
             << "候选端点" << endpoints.size() << "中继" << relayId;
}

// 判断当前是否具备中继降级条件：策略允许且协调服务器在线
bool TransferSessionManager::relayDegradationAvailable() const
{
    return _config && _config->relayMode() != RelayMode::NeverRelay
           && _rendezvous && _rendezvous->isConnected();
}

// 直连失败后进入等待中继决策状态，并启动决策超时保护
void TransferSessionManager::enterAwaitingRelay(const QString &sessionId)
{
    const bool updated = _model->updateSession(sessionId, [](QVariantMap &session) {
        session[kStatus] = kStatusAwaitingRelay;
        session[kErrorMsg] = QObject::tr("直连失败，等待中继决策");
    });
    if (!updated) {
        return;
    }
    emit sessionsChanged();

    // Ask 档 QML 长时间不响应（弹窗被忽略）时自动失败，避免会话悬挂
    // （Auto 档随后 retryViaRelay 会撤销此计时器）
    auto *timer = new QTimer{this};
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, sessionId]() {
        QTimer *pendingTimer = _relayDecisionTimers.take(sessionId);
        if (pendingTimer) {
            pendingTimer->deleteLater();
        }
        if (_model->sessionById(sessionId).value(kStatus).toString() == kStatusAwaitingRelay) {
            finalizeSession(sessionId, kStatusFailed, gy::protocol::ErrorCode::TransferTimeout,
                            tr("等待中继确认超时"));
        }
    });
    timer->start(kRelayDecisionTimeoutMs);
    _relayDecisionTimers[sessionId] = timer;

    const QString deviceId = _model->sessionById(sessionId).value(kDeviceId).toString();
    qDebug() << "[TransferSession] 直连失败，进入中继决策状态" << sessionId;

    // 三档策略在 C++ 侧分流：Auto 档直接复用既有守卫重试中继；Ask 档请求 QML 弹窗确认；
    // Never 档在 relayDegradationAvailable 已被过滤，不会到达这里
    if (_config && _config->relayMode() == RelayMode::AutoRelay) {
        retryViaRelay(sessionId);
        emit messageOccurred(tr("直连失败，已自动切换中继传输"));
        return;
    }
    emit relayConfirmRequested(sessionId, deviceId);
}

// 用户确认后经中继通道重新建立发送（直连失败降级入口）
void TransferSessionManager::retryViaRelay(const QString &sessionId)
{
    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty()) {
        emit errorOccurred(tr("会话不存在，无法使用中继"));
        return;
    }
    if (snapshot.value(kType).toString() != kTypeSend
        || snapshot.value(kStatus).toString() != kStatusAwaitingRelay) {
        emit errorOccurred(tr("会话已结束，无法使用中继"));
        return;
    }

    if (!_rendezvous || !_rendezvous->isConnected()) {
        // 协调服务器不可用时中继无从建立，直接终结并说明原因
        clearRelayPendingState(sessionId);
        finalizeSession(sessionId, kStatusFailed, gy::protocol::ErrorCode::ConnectionLost,
                        tr("协调服务器未连接，无法使用中继"));
        return;
    }

    clearRelayPendingState(sessionId);  // 决策已做出，超时保护随之撤销

    const QString deviceId = snapshot.value(kDeviceId).toString();
    const QString fileName = snapshot.value(kFileName).toString();
    const qint64 totalBytes = snapshot.value(kTotalBytes).toLongLong();

    const QString relayId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _model->updateSession(sessionId, [&relayId](QVariantMap &session) {
        session[kRelayId] = relayId;
        session[kStatus] = kStatusConnecting;
        session[kErrorMsg] = QObject::tr("正在建立中继通道");
    });
    emit sessionsChanged();

    // 受理超时保护：5 秒内未收到协调服务器确认则终结会话
    _pendingRelayInvites[relayId] = sessionId;
    auto *timer = new QTimer{this};
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, relayId]() {
        onRelayInviteTimeout(relayId);
    });
    timer->start(kRelayInviteTimeoutMs);
    _relayInviteTimers[relayId] = timer;

    _rendezvous->requestRelayInvite(relayId, deviceId, fileName, totalBytes);
}

// 清理会话关联的中继决策与邀请等待状态
void TransferSessionManager::clearRelayPendingState(const QString &sessionId)
{
    if (QTimer *timer = _relayDecisionTimers.take(sessionId)) {
        timer->deleteLater();
    }

    for (auto it = _pendingRelayInvites.begin(); it != _pendingRelayInvites.end();) {
        if (it.value() == sessionId) {
            if (QTimer *timer = _relayInviteTimers.take(it.key())) {
                timer->deleteLater();
            }
            it = _pendingRelayInvites.erase(it);
        } else {
            ++it;
        }
    }
}

// 中继邀请已被协调服务器受理，建立中继发送
void TransferSessionManager::onRelayInviteAck(const QString &relayId)
{
    const QString sessionId = _pendingRelayInvites.take(relayId);
    if (QTimer *timer = _relayInviteTimers.take(relayId)) {
        timer->deleteLater();
    }
    if (sessionId.isEmpty()) {
        return;  // 迟到的确认
    }

    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty()
        || snapshot.value(kStatus).toString() != kStatusConnecting
        || snapshot.value(kRelayId).toString() != relayId) {
        return;  // 会话已终结或已发起新一轮中继
    }

    // 中继与协调服务同端口复用，直接使用协调服务器地址
    QList<QPair<QString, quint16>> endpoints;
    endpoints.append({_config->rendezvousHost(),
                      static_cast<quint16>(_config->rendezvousPort())});

    qDebug() << "[TransferSession] 中继邀请已受理，建立中继发送" << sessionId;
    startSendWorker(snapshot, endpoints, relayId, false);
}

// 中继邀请未在期限内得到协调服务器受理
void TransferSessionManager::onRelayInviteTimeout(const QString &relayId)
{
    const QString sessionId = _pendingRelayInvites.take(relayId);
    if (QTimer *timer = _relayInviteTimers.take(relayId)) {
        timer->deleteLater();
    }
    if (sessionId.isEmpty()) {
        return;
    }

    finalizeSession(sessionId, kStatusFailed, gy::protocol::ErrorCode::TransferTimeout,
                    tr("协调服务器未响应中继请求"));
}

// 接收会话（用户确认接收文件）
void TransferSessionManager::acceptReceiveSession(const QString &sessionId)
{
    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty() || snapshot.value(kType).toString() != kTypeReceive) {
        return;
    }
    if (isFinishedStatus(snapshot.value(kStatus).toString())) {
        return;  // 会话已结束，worker 已释放，忽略迟到的接受
    }
    FileReceiverWorker *worker = _receiveWorkers.value(sessionId);
    if (!worker) {
        return;
    }

    // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
    QMetaObject::invokeMethod(worker, [worker]() {
        worker->acceptTransfer();
    }, Qt::QueuedConnection);
    _model->updateSession(sessionId, [](QVariantMap &session) {
        session[kStatus] = kStatusTransferring;
    });
    emit sessionsChanged();
}

// 拒绝接收会话
void TransferSessionManager::rejectReceiveSession(const QString &sessionId)
{
    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty() || snapshot.value(kType).toString() != kTypeReceive) {
        return;
    }
    if (isFinishedStatus(snapshot.value(kStatus).toString())) {
        return;  // 会话已结束，worker 已释放，忽略迟到的拒绝
    }
    FileReceiverWorker *worker = _receiveWorkers.value(sessionId);
    if (!worker) {
        return;
    }

    // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
    QMetaObject::invokeMethod(worker, [worker]() {
        worker->rejectTransfer();
    }, Qt::QueuedConnection);
    _model->updateSession(sessionId, [](QVariantMap &session) {
        session[kStatus] = kStatusRejected;
    });
    emit sessionsChanged();
    refreshActiveSessionCount();  // 拒绝即进入终态，活动数减一
}

// 取消传输会话
void TransferSessionManager::cancelSession(const QString &sessionId)
{
    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty()) {
        return;
    }
    if (isFinishedStatus(snapshot.value(kStatus).toString())) {
        return;  // 已结束会话不能再取消，其 worker 可能已释放
    }
    const QString type = snapshot.value(kType).toString();

    // 取消也撤销未决的中继决策/邀请等待
    clearRelayPendingState(sessionId);

    if (type == kTypeSend) {
        FileSenderWorker *worker = _sendWorkers.value(sessionId);
        if (worker) {
            // 先跨线程置位原子取消标志：worker 可能正卡在不可中断的阻塞等待里，
            // 排队的 cancel() 槽要等事件循环恢复才能送达，置位则立即生效
            worker->requestCancel();
            // 事件循环空闲时由 cancel() 槽补发取消帧，已终结则自动跳过
            QMetaObject::invokeMethod(worker, "cancel");
        }
    } else if (type == kTypeReceive) {
        // 接收方：等待确认态走拒绝（原语义）；已进入传输的会话由原子标志中断
        FileReceiverWorker *worker = _receiveWorkers.value(sessionId);
        if (worker) {
            // 先跨线程置位原子取消标志：排队槽要等 worker 事件循环空闲才送达，
            // 置位则 worker 在下一个分块处理边界立即中断，不再继续收完整文件
            worker->requestCancel();
            // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
            QMetaObject::invokeMethod(worker, [worker]() {
                worker->rejectTransfer(QObject::tr("用户取消"));
            }, Qt::QueuedConnection);
        }
    }

    // 更新状态
    _model->updateSession(sessionId, [](QVariantMap &session) {
        session[kStatus] = kStatusCancelled;
    });
    emit sessionsChanged();
    refreshActiveSessionCount();  // 取消即进入终态，活动数减一

    qDebug() << "TransferSessionManager: 取消会话" << sessionId;
}

// 移除已结束的传输记录
void TransferSessionManager::removeSession(const QString &sessionId)
{
    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty()) {
        return;
    }

    // 只允许移除已完成、失败、取消的会话
    if (!isFinishedStatus(snapshot.value(kStatus).toString())) {
        qWarning() << "TransferSessionManager: 无法移除进行中的会话" << sessionId;
        return;
    }

    const QString recordId = snapshot.value(kRecordId).toString();
    _model->removeSession(sessionId);
    emit sessionsChanged();
    if (!recordId.isEmpty()) {
        emit transferHistoryDeleteRequested({recordId});
    }
    qDebug() << "TransferSessionManager: 移除会话" << sessionId;
}

// 移除传输记录并删除已接收的本地文件
void TransferSessionManager::removeSessionAndDeleteFile(const QString &sessionId)
{
    const QVariantMap snapshot = _model->sessionById(sessionId);
    if (snapshot.isEmpty()) {
        return;
    }

    QString failedPath;
    switch (TransferSessionMapper::deleteReceivedFile(snapshot, &failedPath)) {
    case TransferSessionMapper::DeleteResult::Deleted:
        break;
    case TransferSessionMapper::DeleteResult::NotEligible:
        emit errorOccurred(tr("该记录没有可删除的已接收文件"));
        return;
    case TransferSessionMapper::DeleteResult::InvalidPath:
        emit errorOccurred(tr("本地保存路径无效或文件不存在，未移除记录"));
        return;
    case TransferSessionMapper::DeleteResult::RemoveFailed:
        emit errorOccurred(tr("无法删除本地文件：%1").arg(failedPath));
        return;
    }

    const QString recordId = snapshot.value(kRecordId).toString();
    _model->removeSession(sessionId);
    emit sessionsChanged();
    if (!recordId.isEmpty()) {
        emit transferHistoryDeleteRequested({recordId});
    }
    emit messageOccurred(tr("已删除本地文件并移除传输记录"));
}

// 清空所有已结束的传输记录（可选删除已接收文件）
void TransferSessionManager::clearFinishedSessions(bool deleteReceivedFiles, const QString &deviceId)
{
    int removedCount = 0;
    int deletedCount = 0;
    QStringList recordIds;
    QStringList removedSessionIds;

    // 先基于快照决定要移除哪些会话（含文件删除），再从模型中精确移除，
    // 删除失败的记录保持原样，便于用户重新处理
    const QVariantList snapshot = _model->sessions();
    for (const QVariant &entry : snapshot) {
        const QVariantMap session = entry.toMap();
        if (!isFinishedStatus(session.value(kStatus).toString())) {
            continue;
        }
        if (!deviceId.isEmpty() && session.value(kDeviceId).toString() != deviceId) {
            continue;
        }

        if (deleteReceivedFiles && session.value(kCanDeleteLocalFile).toBool()) {
            QString failedPath;
            const auto result = TransferSessionMapper::deleteReceivedFile(session, &failedPath);
            if (result == TransferSessionMapper::DeleteResult::InvalidPath) {
                emit errorOccurred(tr("本地保存路径无效或文件不存在，未移除记录"));
                continue;
            }
            if (result != TransferSessionMapper::DeleteResult::Deleted) {
                emit errorOccurred(tr("无法删除本地文件：%1").arg(failedPath));
                continue;
            }
            ++deletedCount;
        }

        const QString recordId = session.value(kRecordId).toString();
        if (!recordId.isEmpty()) {
            recordIds.append(recordId);
        }
        removedSessionIds.append(session.value(kSessionId).toString());
        ++removedCount;
    }

    if (removedCount == 0) {
        return;
    }

    for (const QString &id : removedSessionIds) {
        _model->removeSession(id);
    }
    emit sessionsChanged();
    if (!recordIds.isEmpty()) {
        emit transferHistoryDeleteRequested(recordIds);
    }
    emit messageOccurred(deleteReceivedFiles
                         ? tr("已清理 %1 条记录并删除 %2 个本地项目")
                               .arg(removedCount).arg(deletedCount)
                         : tr("已清理 %1 条传输记录").arg(removedCount));
}

// 处理新的传输请求（创建接收会话，通知 UI 弹窗确认）
void TransferSessionManager::onTransferRequestReceived(FileReceiverWorker *worker,
                                                        const QVariantMap &request)
{
    const QString sessionId = request.value(kSessionId).toString();
    const QString senderDeviceId = request.value(kSenderDeviceId).toString();
    const QString senderName = request.value(kSenderName).toString();
    const QString fileName = request.value(kFileName).toString();
    const qint64 fileSize = request.value(kFileSize).toLongLong();
    const int totalFiles = request.value(kTotalFiles).toInt();
    const qint64 totalBytes = request.value(kTotalBytes).toLongLong();
    qDebug() << "[TransferSession] 收到传输请求";
    qDebug() << "[TransferSession] 发送方设备ID:" << senderDeviceId;
    qDebug() << "[TransferSession] 发送方:" << senderName;
    qDebug() << "[TransferSession] 文件名:" << fileName;
    qDebug() << "[TransferSession] 文件大小:" << fileSize;
    qDebug() << "[TransferSession] 总文件数:" << totalFiles;
    qDebug() << "[TransferSession] 总大小:" << totalBytes;

    // 设置接收路径（使用 QMetaObject::invokeMethod 在 worker 的线程中调用）
    if (_config) {
        QMetaObject::invokeMethod(worker, [worker, config = _config]() {
            worker->setReceivePath(config->receivePath());
        }, Qt::QueuedConnection);
        qDebug() << "[TransferSession] 接收路径:" << _config->receivePath();
    }

    QVariantMap session = request;
    session[kType]      = kTypeReceive;
    session[kDeviceId]  = senderDeviceId;
    session[kPeerDeviceName] = senderName;
    session[kSenderName] = senderName;
    session[kFilePath]  = "";
    session[kStatus]    = kStatusWaitingConfirm;
    session[kProgress]  = 0;
    session[kBytesTransferred] = 0;
    session[kCreatedAt] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    session[kFileCount] = totalFiles;
    session[kLocalPath] = "";
    session[kCanDeleteLocalFile] = false;

    // 接收 worker 以独立映射管理生命周期，会话行不再内嵌 worker 指针
    _receiveWorkers[sessionId] = worker;
    _model->appendSession(session);
    emit sessionsChanged();
    refreshActiveSessionCount();  // 接收请求进入 waiting_confirm，活动数加一

    qDebug() << "[TransferSession] 接收会话已创建，ID:" << sessionId;

    // 连接接收进度信号（通过 QMetaObject 跨线程投递回主线程）
    connect(worker, &FileReceiverWorker::progressChanged,
            this, [this, sessionId](qint64 bytesReceived, qint64 totalBytes) {
        _model->updateSession(sessionId, [this, bytesReceived, totalBytes](QVariantMap &session) {
            const QString previousStatus = session.value(kStatus).toString();
            const int previousProgress = session.value(kProgress).toInt();
            const qint64 previousTotalBytes = session.value(kTotalBytes).toLongLong();
            const int progress = totalBytes > 0 ? (bytesReceived * 100 / totalBytes) : 0;

            session[kStatus] = kStatusTransferring;
            session[kProgress] = progress;
            session[kBytesTransferred] = bytesReceived;
            session[kTotalBytes] = totalBytes;
            // 仅在可见字段变化时通知 QML，减少高频进度更新导致的不必要刷新
            if (previousStatus != kStatusTransferring
                || previousProgress != progress
                || previousTotalBytes != totalBytes) {
                emit sessionsChanged();
            }
        });
    });

    // 连接接收完成信号，生成最终快照并触发持久化
    connect(worker, &FileReceiverWorker::transferFinished,
            this, [this, sessionId](bool success, gy::protocol::ErrorCode errorCode,
                                    const QString &errorMsg, const QString &savedPath) {
        qDebug() << "[TransferSession] 接收传输完成，成功:" << success << "错误:" << errorMsg;
        const QVariantMap snapshot = _model->sessionById(sessionId);
        const QString finalStatus = normalizedFinalStatus(success, errorCode,
                                                          snapshot.value(kStatus).toString());
        finalizeSession(sessionId, finalStatus, errorCode, errorMsg, savedPath);
        // worker 的销毁由 P2pServer 的线程清理链负责（thread finished → deleteLater），
        // 这里不再重复删除：本处理器排队执行时 worker 可能已被该链销毁
    });

    if (_config && _config->autoAcceptFiles()) {
        qDebug() << "[TransferSession] 自动接受接收请求:" << sessionId;
        acceptReceiveSession(sessionId);
        emit messageOccurred(tr("已自动接受 \"%1\"，正在保存").arg(fileName));
    } else {
        // 通知 QML 弹窗确认
        const QVariantList previewFiles = session.value(kFileList).toList();
        emit receiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                    fileSize, totalFiles, totalBytes,
                                    session.value(kIsDirectory).toBool(), previewFiles);
    }

    qDebug() << "TransferSessionManager: 收到接收请求" << sessionId
             << "来自" << senderName << "文件" << fileName;
}

// 将已完成传输历史恢复到会话列表
void TransferSessionManager::restoreFinishedTransfers(const QList<TransferRecord> &records)
{
    bool changed = false;
    for (auto it = records.crbegin(); it != records.crend(); ++it) {
        if (_model->hasSession(it->sessionId)) {
            continue;
        }
        _model->appendSession(TransferSessionMapper::sessionFromRecord(*it));
        changed = true;
    }

    if (changed) {
        emit sessionsChanged();
    }
}

// 将运行期会话收敛为可持久化的最终快照
void TransferSessionManager::finalizeSession(const QString &sessionId, const QString &finalStatus,
                                             gy::protocol::ErrorCode errorCode,
                                             const QString &errorMessage,
                                             const QString &savedPath)
{
    bool found = false;
    QString previousStatus;
    QVariantMap updated;
    _model->updateSession(sessionId, [&](QVariantMap &session) {
        found = true;
        previousStatus = session.value(kStatus).toString();

        session[kStatus] = finalStatus;
        session[kProgress] = finalStatus == kStatusCompleted ? 100 : session.value(kProgress).toInt();
        session[kErrorMsg] = errorMessage;
        session[kErrorCode] = static_cast<quint16>(errorCode);
        if (finalStatus == kStatusCompleted) {
            session[kBytesTransferred] = session.value(kTotalBytes);
        }
        if (!savedPath.isEmpty()) {
            session[kLocalPath] = savedPath;
            session[kCanDeleteLocalFile] = finalStatus == kStatusCompleted;  // 仅接收成功时允许删除本地文件
        }

        QString recordId = session.value(kRecordId).toString();
        if (recordId.isEmpty()) {
            recordId = QUuid::createUuid().toString(QUuid::WithoutBraces);  // 首次完成时生成持久化记录 ID
            session[kRecordId] = recordId;
        }
        updated = session;
    });

    if (!found) {
        return;
    }

    // 会话终结时兜底清理中继相关等待状态
    clearRelayPendingState(sessionId);
    // 接收 worker 已终结，从映射中移除，后续 cancel/accept 不再触达该对象
    _receiveWorkers.remove(sessionId);

    emit sessionsChanged();
    refreshActiveSessionCount();  // 终态迁移，活动数减一（已终结会话的迟到终结此处无变化）

    // 会话仍停在等待确认时被后端终结（对方取消/超时/断连），通知弹窗关闭；
    // 用户自己的拒绝/取消会先把状态改掉，不会走到这里
    if (previousStatus == kStatusWaitingConfirm) {
        emit sessionStale(sessionId);
    }

    const bool isSend = updated.value(kType).toString() == kTypeSend;
    const QString displayName = updated.value(kFileName).toString();
    if (finalStatus == kStatusCompleted) {
        if (!isSend && _config) {
            // 通知卡"打开所在位置"用实际落盘路径（重名时形如 "name (1)"），
            // 快照缺失时回退接收根目录
            const QString savedPath = updated.value(kLocalPath).toString();
            emit transferCompleted(sessionId, displayName,
                                   savedPath.isEmpty() ? _config->receivePath() : savedPath);
        }
        emit messageOccurred(isSend
                                 ? tr("文件 \"%1\" 发送成功").arg(displayName)
                                 : tr("文件 \"%1\" 接收成功").arg(displayName));
    } else if (isSend) {
        emit errorOccurred(tr("发送失败：%1").arg(errorMessage));
    } else {
        emit errorOccurred(tr("接收失败：%1").arg(errorMessage));
    }

    emit transferToPersist(TransferSessionMapper::recordFromSession(updated));
}
