/**
* @file    rendezvous_server.cpp
* @version 7.16.0
* @date    2026-10-04
* @author  GridYard Team
* @brief   协调节点服务器实现
*
* Change Log:
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
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
* [v7.15.8] GY   2026-10-03
* * 构造函数删除收而未用的 host 死参数
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.14.0] GY   2026-10-03
* * 令牌校验前置到中继分流之前
* [v7.13.4] GY   2026-10-03
* * 注册超限回错误响应；响应写积压超限断开；清理周期与注册表上限可调
* [v7.12.0] GY   2026-10-02
* * 会话迁移到 LineSession 基类，补齐行上限、握手/空闲超时与连接数上限
* * 移除存而不用的 _host 与空槽 onSessionFinished
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请信令与同端口中继连接移交
* * 会话断开后释放会话与 socket，修复长驻进程的连接泄漏
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点服务器
*/

#include "rendezvous_server.h"
#include "online_registry.h"
#include "rendezvous_limits.h"
#include "rendezvous_protocol.h"
#include "rendezvous_protocol_keys.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QTimer>

using namespace gy::rendezvous;

// 会话构造函数
RendezvousSession::RendezvousSession(QTcpSocket *socket, OnlineRegistry *registry, const QString &token,
                                     int handshakeTimeoutMs, int idleTimeoutMs, QObject *parent)
    : LineSession{socket, kMaxRendezvousLineBytes, parent}
    , _registry{registry}
    , _token{token}
    , _handshakeTimeoutMs{handshakeTimeoutMs}
    , _idleTimeoutMs{idleTimeoutMs}
{
    connect(this, &LineSession::lineReady, this, &RendezvousSession::processLine);
    connect(this, &LineSession::closed, this, &RendezvousSession::finished);
}

// 启动会话处理：开启首行握手超时
void RendezvousSession::start()
{
    qDebug() << "[RendezvousSession] 新会话来自" << socket()->peerAddress().toString();
    startHandshakeTimeout(_handshakeTimeoutMs);
}

// 调整响应写队列上限
void RendezvousSession::setMaxResponseQueueBytes(qint64 maxBytes)
{
    if (maxBytes > 0) {
        _maxResponseQueueBytes = maxBytes;
    }
}

// 处理一条控制行：刷新空闲超时并按 JSON 解析
void RendezvousSession::processLine(const QByteArray &line)
{
    if (_idleTimeoutMs > 0) {
        startIdleTimeout(_idleTimeoutMs);
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(line, &error);

    if (error.error != QJsonParseError::NoError) {
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("无效的 JSON: ") + error.errorString()));
        return;
    }

    if (!doc.isObject()) {
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("请求必须是 JSON 对象")));
        return;
    }

    processRequest(doc.object());
}

// 处理 JSON 请求消息
void RendezvousSession::processRequest(const QJsonObject &json)
{
    // 所有请求（含中继握手）先验令牌：令牌校验必须发生在分流之前，
    // 否则配了 --token 的服务器会被中继管道绕过认证
    const QString providedToken = RendezvousProtocol::extractToken(json);
    if (!RendezvousProtocol::validateToken(providedToken, _token)) {
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("无效的访问口令")));
        return;
    }

    // 中继管道连接：剥离握手行后整条移交，后续字节流不再按 JSON 行解析
    const QString rawType = json[gy::rendezvous::kKeyType].toString();
    if (rawType == gy::rendezvous::kTypeRelayCreate || rawType == gy::rendezvous::kTypeRelayJoin) {
        const QByteArray pending = pendingBytes();
        QTcpSocket *pipeSocket = takeSocket();
        emit relayPipeRequested(pipeSocket, json, pending);
        emit closed();  // 会话职责已移交，通知服务器回收本对象
        return;
    }

    QString errorString;
    RendezvousProtocol::MessageType type = RendezvousProtocol::parseRequest(json, &errorString);

    if (type == RendezvousProtocol::MessageType::Error) {
        sendResponse(RendezvousProtocol::buildError(errorString));
        return;
    }

    const QString room = RendezvousProtocol::extractRoom(json);
    if (room.isEmpty()) {
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("缺少 room 参数")));
        return;
    }

    switch (type) {
    case RendezvousProtocol::MessageType::Register: {
        OnlineRegistry::PeerInfo peer = RendezvousProtocol::extractPeerInfo(json);
        if (!_registry->upsertPeer(room, peer)) {
            sendResponse(RendezvousProtocol::buildError(QStringLiteral("房间或设备数量已达上限")));
            break;
        }
        sendResponse(RendezvousProtocol::buildRegisterAck(peer.ttlSeconds > 0 ? peer.ttlSeconds : 30));
        break;
    }

    case RendezvousProtocol::MessageType::ListPeers: {
        const QString deviceId = RendezvousProtocol::extractDeviceId(json);
        QList<OnlineRegistry::PeerInfo> peers = _registry->peersForRoom(room);

        // 过滤掉请求者自身
        peers.erase(std::remove_if(peers.begin(), peers.end(),
                                   [&deviceId](const OnlineRegistry::PeerInfo &p) {
                                       return p.deviceId == deviceId;
                                   }),
                    peers.end());

        sendResponse(RendezvousProtocol::buildPeersResponse(peers));
        break;
    }

    case RendezvousProtocol::MessageType::RelayInvite: {
        OnlineRegistry::RelayInvite invite = RendezvousProtocol::extractRelayInvite(json);
        if (invite.relayId.isEmpty() || invite.targetDeviceId.isEmpty()) {
            sendResponse(RendezvousProtocol::buildError(QStringLiteral("中继邀请缺少 relay_id 或 target_device_id")));
            break;
        }
        // 目标不在线时直接拒绝，发送端可立即改走失败提示而不是干等超时
        if (!_registry->hasPeer(room, invite.targetDeviceId)) {
            sendResponse(RendezvousProtocol::buildError(QStringLiteral("目标设备不在线或未注册")));
            break;
        }
        _registry->addRelayInvite(room, invite);
        sendResponse(RendezvousProtocol::buildRelayInviteAck(invite.relayId));
        break;
    }

    case RendezvousProtocol::MessageType::RelayPoll: {
        const QString deviceId = RendezvousProtocol::extractDeviceId(json);
        if (deviceId.isEmpty()) {
            sendResponse(RendezvousProtocol::buildError(QStringLiteral("缺少 device_id")));
            break;
        }
        // 邀请一次性消费，领取后目标设备凭 relay_id 建立中继连接
        const QList<OnlineRegistry::RelayInvite> invites =
            _registry->consumeRelayInvites(room, deviceId);
        sendResponse(RendezvousProtocol::buildRelayInvites(invites));
        break;
    }

    default:
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("不支持的消息类型")));
        break;
    }
}

// 发送 JSON 响应
void RendezvousSession::sendResponse(const QJsonObject &json)
{
    if (!socket()) {
        return;  // 会话已移交或断开
    }
    // 写队列积压说明客户端消费过慢，断开以保护服务端内存
    if (socket()->bytesToWrite() >= _maxResponseQueueBytes) {
        qWarning() << "[RendezvousSession] 响应写积压超过上限，断开连接";
        socket()->abort();
        return;
    }
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
    socket()->write(data);
    socket()->flush();
}

// -------------------- RendezvousServer --------------------

// 构造函数
RendezvousServer::RendezvousServer(quint16 port, const QString &token,
                                   int maxSessions, QObject *parent)
    : QObject{parent}
    , _port{port}
    , _token{token}
    , _registry{new OnlineRegistry{30, this}}
    , _maxSessions{maxSessions > 0 ? maxSessions : kMaxRendezvousSessions}
    , _handshakeTimeoutMs{kRendezvousHandshakeTimeoutMs}
    , _idleTimeoutMs{kRendezvousIdleTimeoutMs}
{
    _server = new QTcpServer{this};
}

// 启动监听服务
bool RendezvousServer::start()
{
    if (!_server->listen(QHostAddress::Any, _port)) {
        emit serverStarted(false, _server->errorString());
        return false;
    }

    qInfo() << "[RendezvousServer] 监听端口" << _server->serverPort();
    connect(_server, &QTcpServer::newConnection, this, &RendezvousServer::onNewConnection);

    // 定期清理过期设备和中继邀请
    QTimer *pruneTimer = new QTimer{this};
    connect(pruneTimer, &QTimer::timeout, _registry, &OnlineRegistry::pruneExpired);
    pruneTimer->start(_pruneIntervalMs);

    emit serverStarted(true, QString());
    return true;
}

// 停止监听服务
void RendezvousServer::stop()
{
    _server->close();
    qInfo() << "[RendezvousServer] 已停止";
}

// 是否正在监听
bool RendezvousServer::isListening() const
{
    return _server->isListening();
}

// 获取服务监听端口
quint16 RendezvousServer::serverPort() const
{
    return _server->serverPort();
}

// 调整会话握手与空闲超时
void RendezvousServer::setSessionTimeouts(int handshakeTimeoutMs, int idleTimeoutMs)
{
    _handshakeTimeoutMs = handshakeTimeoutMs;
    _idleTimeoutMs = idleTimeoutMs;
}

// 调整注册表房间数与每房设备数上限
void RendezvousServer::setRegistryLimits(int maxRooms, int maxDevicesPerRoom)
{
    _registry->setRegistryLimits(maxRooms, maxDevicesPerRoom);
}

// 调整过期数据清理周期
void RendezvousServer::setPruneIntervalMs(int intervalMs)
{
    if (intervalMs > 0) {
        _pruneIntervalMs = intervalMs;
    }
}

// 调整响应写队列上限
void RendezvousServer::setMaxResponseQueueBytes(qint64 maxBytes)
{
    if (maxBytes > 0) {
        _maxResponseQueueBytes = maxBytes;
    }
}

// 处理新的客户端连接
void RendezvousServer::onNewConnection()
{
    while (_server->hasPendingConnections()) {
        QTcpSocket *socket = _server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        // 并发会话达到上限时直接拒绝，防止连接堆积拖垮服务
        if (_sessions.size() >= _maxSessions) {
            qWarning() << "[RendezvousServer] 会话数达到上限" << _maxSessions << "，拒绝新连接";
            socket->disconnectFromHost();
            socket->deleteLater();
            continue;
        }

        QString address = socket->peerAddress().toString();
        qDebug() << "[RendezvousServer] 新连接来自" << address;
        emit clientConnected(address);

        RendezvousSession *session = new RendezvousSession{socket, _registry, _token,
                                                           _handshakeTimeoutMs, _idleTimeoutMs, this};
        session->setMaxResponseQueueBytes(_maxResponseQueueBytes);
        _sessions.append(session);

        connect(session, &RendezvousSession::finished, this, [this, session, address]() {
            _sessions.removeAll(session);
            session->deleteLater();
            emit clientDisconnected(address);
        });

        // 会话移交的中继管道连接转发给接入方（同端口复用）
        connect(session, &RendezvousSession::relayPipeRequested,
                this,    &RendezvousServer::relayPipeRequested);

        session->start();
    }
}
