/**
* @file    rendezvous_server.cpp
* @version 7.2.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点服务器实现
*
* Change Log:
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
    emit finished();
}

// 处理 JSON 请求消息
void RendezvousSession::processRequest(const QJsonObject &json)
{
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

    default:
        sendResponse(RendezvousProtocol::buildError(QStringLiteral("不支持的消息类型")));
        break;
    }
}

// 发送 JSON 响应
void RendezvousSession::sendResponse(const QJsonObject &json)
{
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

    // 定期清理过期设备
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
        emit clientDisconnected(address);
    });

    session->start();
}

// 处理会话关闭
void RendezvousServer::onSessionFinished()
{
}