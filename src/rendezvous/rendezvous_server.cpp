/**
* @file    rendezvous_server.cpp
* @version 7.9.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点服务器实现
*
* Change Log:
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请信令与同端口中继连接移交
* * 会话断开后释放会话与 socket，修复长驻进程的连接泄漏
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点服务器
*/

#include "rendezvous_server.h"
#include "online_registry.h"
#include "rendezvous_protocol.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QTimer>

// 会话构造函数
RendezvousSession::RendezvousSession(QTcpSocket *socket, OnlineRegistry *registry, const QString &token, QObject *parent)
    : QObject{parent}
    , _socket{socket}
    , _registry{registry}
    , _token{token}
{
    connect(_socket, &QTcpSocket::readyRead, this, &RendezvousSession::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected, this, &RendezvousSession::onDisconnected);
}

// 启动会话处理
void RendezvousSession::start()
{
    qDebug() << "[RendezvousSession] 新会话来自" << _socket->peerAddress().toString();
}

// 处理收到的数据，按行解析 JSON 请求
void RendezvousSession::onReadyRead()
{
    _buffer.append(_socket->readAll());

    // 按行处理请求（每个请求一行 JSON）
    while (_buffer.contains('\n')) {
        int newlineIndex = _buffer.indexOf('\n');
        QByteArray line = _buffer.left(newlineIndex);
        _buffer = _buffer.mid(newlineIndex + 1);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);

        if (error.error != QJsonParseError::NoError) {
            sendResponse(RendezvousProtocol::buildError(QStringLiteral("无效的 JSON: ") + error.errorString()));
            continue;
        }

        if (!doc.isObject()) {
            sendResponse(RendezvousProtocol::buildError(QStringLiteral("请求必须是 JSON 对象")));
            continue;
        }

        processRequest(doc.object());
    }
}

// 处理连接断开
void RendezvousSession::onDisconnected()
{
    qDebug() << "[RendezvousSession] 会话断开" << _socket->peerAddress().toString();
    // socket 无父对象，断开后立即释放，避免长驻进程累积连接对象
    _socket->deleteLater();
    _socket = nullptr;
    emit finished();
}

// 处理 JSON 请求消息
void RendezvousSession::processRequest(const QJsonObject &json)
{
    // 中继管道连接：剥离握手行后整条移交，后续字节流不再按 JSON 行解析
    const QString rawType = json[QStringLiteral("type")].toString();
    if (rawType == QStringLiteral("relay_create") || rawType == QStringLiteral("relay_join")) {
        disconnect(_socket, nullptr, this, nullptr);
        emit relayPipeRequested(_socket, json, _buffer);
        _buffer.clear();
        _socket = nullptr;
        emit finished();
        return;
    }

    QString errorString;
    RendezvousProtocol::MessageType type = RendezvousProtocol::parseRequest(json, &errorString);

    if (type == RendezvousProtocol::MessageType::Error) {
        sendResponse(RendezvousProtocol::buildError(errorString));
        return;
    }

    // 验证 token
    const QString providedToken = RendezvousProtocol::extractToken(json);
    if (!RendezvousProtocol::validateToken(providedToken, _token)) {
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("无效的访问口令")));
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
        _registry->upsertPeer(room, peer);
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
    if (!_socket) {
        return;  // 会话已移交或断开
    }
    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n';
    _socket->write(data);
    _socket->flush();
}

// -------------------- RendezvousServer --------------------

// 构造函数
RendezvousServer::RendezvousServer(const QString &host, quint16 port, const QString &token, QObject *parent)
    : QObject{parent}
    , _host{host}
    , _port{port}
    , _token{token}
    , _registry{new OnlineRegistry{30, this}}
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
    pruneTimer->start(10000);  // 每 10 秒清理一次

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

// 处理新的客户端连接
void RendezvousServer::onNewConnection()
{
    QTcpSocket *socket = _server->nextPendingConnection();
    if (!socket) {
        return;
    }

    QString address = socket->peerAddress().toString();
    qDebug() << "[RendezvousServer] 新连接来自" << address;
    emit clientConnected(address);

    RendezvousSession *session = new RendezvousSession{socket, _registry, _token, this};
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

// 处理会话关闭事件
void RendezvousServer::onSessionFinished()
{
}
