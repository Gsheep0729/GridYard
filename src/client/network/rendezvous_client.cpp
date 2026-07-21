/**
* @file    rendezvous_client.cpp
* @version 7.5.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点客户端实现
*
* Change Log:
* [v7.5.0] GY   2026-07-21
* * Stage 7.5 Phase B：新增协调节点客户端
*/

#include "rendezvous_client.h"
#include "config_manager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

RendezvousClient::RendezvousClient(QObject *parent)
    : QObject{parent}
{
}

RendezvousClient::~RendezvousClient()
{
    if (_heartbeatTimerId != 0) {
        killTimer(_heartbeatTimerId);
    }
    disconnectFromServer();
}

void RendezvousClient::connectToServer(const QString &host, int port)
{
    if (_socket) {
        _socket->disconnectFromHost();
        _socket->deleteLater();
    }

    _serverHost = host;
    _serverPort = port;
    _buffer.clear();

    _socket = new QTcpSocket(this);
    connect(_socket, &QTcpSocket::connected, this, &RendezvousClient::onSocketConnected);
    connect(_socket, &QTcpSocket::disconnected, this, &RendezvousClient::onSocketDisconnected);
    connect(_socket, &QTcpSocket::errorOccurred, this, &RendezvousClient::onSocketError);
    connect(_socket, &QTcpSocket::readyRead, this, &RendezvousClient::onReadyRead);

    qDebug() << "RendezvousClient: 连接协调服务器" << host << ":" << port;
    _socket->connectToHost(host, port);
}

void RendezvousClient::disconnectFromServer()
{
    _isConnected = false;
    if (_heartbeatTimerId != 0) {
        killTimer(_heartbeatTimerId);
        _heartbeatTimerId = 0;
    }
    if (_socket) {
        _socket->disconnectFromHost();
        _socket->deleteLater();
        _socket = nullptr;
    }
    emit disconnected();
}

void RendezvousClient::registerDevice(const QString &room,
                                      const QString &deviceId,
                                      const QString &deviceName,
                                      const QStringList &addresses,
                                      quint16 tcpPort,
                                      quint16 discoveryPort)
{
    // 保存注册信息用于心跳重注册
    _room = room;
    _deviceId = deviceId;
    _deviceName = deviceName;
    _addresses = addresses;
    _tcpPort = tcpPort;
    _discoveryPort = discoveryPort;

    if (!_isConnected) {
        qWarning() << "RendezvousClient: 未连接协调服务器，无法注册";
        return;
    }

    doRegister();
}

void RendezvousClient::doRegister()
{
    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("register");
    json[QStringLiteral("room")] = _room;
    json[QStringLiteral("device_id")] = _deviceId;
    json[QStringLiteral("device_name")] = _deviceName;
    json[QStringLiteral("addresses")] = QJsonArray::fromStringList(_addresses);
    json[QStringLiteral("tcp_port")] = _tcpPort;
    json[QStringLiteral("discovery_port")] = _discoveryPort;
    json[QStringLiteral("ttl_seconds")] = _ttlSeconds;

    sendJson(json);
    qDebug() << "RendezvousClient: 发送注册请求 deviceId =" << _deviceId;
}

void RendezvousClient::listPeers(const QString &room)
{
    if (!_isConnected) {
        qWarning() << "RendezvousClient: 未连接协调服务器，无法查询设备";
        return;
    }

    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("list_peers");
    json[QStringLiteral("room")] = room;

    sendJson(json);
    qDebug() << "RendezvousClient: 发送查询设备请求 room =" << room;
}

bool RendezvousClient::isConnected() const
{
    return _isConnected;
}

void RendezvousClient::onSocketConnected()
{
    _isConnected = true;
    qDebug() << "RendezvousClient: 已连接到协调服务器";
    emit connected();
}

void RendezvousClient::onSocketDisconnected()
{
    _isConnected = false;
    if (_heartbeatTimerId != 0) {
        killTimer(_heartbeatTimerId);
        _heartbeatTimerId = 0;
    }
    qDebug() << "RendezvousClient: 与协调服务器断开连接";
    emit disconnected();

    // 3 秒后尝试重连
    QTimer::singleShot(kReconnectDelayMs, this, [this]() {
        if (!_serverHost.isEmpty() && _serverPort > 0) {
            qDebug() << "RendezvousClient: 尝试重新连接...";
            connectToServer(_serverHost, _serverPort);
        }
    });
}

void RendezvousClient::onSocketError(QAbstractSocket::SocketError socketError)
{
    Q_UNUSED(socketError);
    QString errorMsg = _socket ? _socket->errorString() : QStringLiteral("未知错误");
    qWarning() << "RendezvousClient: 套接字错误" << errorMsg;
    emit errorOccurred(errorMsg);
}

void RendezvousClient::onReadyRead()
{
    if (!_socket) return;

    _buffer.append(_socket->readAll());

    // 处理缓冲区中的数据（可能有多个 JSON 对象）
    while (true) {
        QJsonObject json;
        if (!readJson(&json)) break;
        handleMessage(json);
    }
}

void RendezvousClient::sendJson(const QJsonObject &json)
{
    if (!_socket || _socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonDocument doc(json);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append("\n");  // 每条消息以换行符分隔
    _socket->write(data);
    _socket->flush();
}

bool RendezvousClient::readJson(QJsonObject *json)
{
    // 查找换行符作为消息边界
    int newlineIndex = _buffer.indexOf('\n');
    if (newlineIndex < 0) return false;

    QByteArray line = _buffer.left(newlineIndex);
    _buffer = _buffer.mid(newlineIndex + 1);

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "RendezvousClient: JSON 解析失败" << parseError.errorString();
        return false;
    }

    if (!doc.isObject()) {
        qWarning() << "RendezvousClient: 收到非对象 JSON";
        return false;
    }

    *json = doc.object();
    return true;
}

void RendezvousClient::handleMessage(const QJsonObject &json)
{
    const QString type = json[QStringLiteral("type")].toString();

    if (type == QStringLiteral("register_ack")) {
        int ttl = json[QStringLiteral("ttl_seconds")].toInt(30);
        _ttlSeconds = ttl;
        qDebug() << "RendezvousClient: 收到注册确认，TTL =" << ttl;
        emit registerAckReceived(ttl);

        // 启动心跳定时器
        if (_heartbeatTimerId == 0) {
            _heartbeatTimerId = startTimer(kHeartbeatIntervalMs);
        }

    } else if (type == QStringLiteral("peers")) {
        QList<QVariantMap> peers;
        const QJsonArray items = json[QStringLiteral("items")].toArray();
        for (const QJsonValue &item : items) {
            QJsonObject obj = item.toObject();
            PeerEndpoint peer;
            peer.deviceId = obj[QStringLiteral("device_id")].toString();
            peer.deviceName = obj[QStringLiteral("device_name")].toString();
            const QJsonArray addresses = obj[QStringLiteral("addresses")].toArray();
            for (const QJsonValue &addr : addresses) {
                peer.addresses.append(addr.toString());
            }
            peer.tcpPort = static_cast<quint16>(obj[QStringLiteral("tcp_port")].toInt());
            peer.discoveryPort = static_cast<quint16>(obj[QStringLiteral("discovery_port")].toInt(45678));
            const QString lastSeenStr = obj[QStringLiteral("updated_at")].toString();
            if (!lastSeenStr.isEmpty()) {
                peer.lastSeen = QDateTime::fromString(lastSeenStr, Qt::ISODate);
            }
            peers.append(peerEndpointToVariantMap(peer));
        }
        qDebug() << "RendezvousClient: 收到候选设备列表，" << peers.count() << " 个设备";
        emit peersReceived(peers);

    } else if (type == QStringLiteral("error")) {
        QString message = json[QStringLiteral("message")].toString();
        qWarning() << "RendezvousClient: 服务器错误" << message;
        emit errorOccurred(message);
    }
}

QVariantMap RendezvousClient::peerEndpointToVariantMap(const PeerEndpoint &peer)
{
    QVariantMap map;
    map[QStringLiteral("deviceId")] = peer.deviceId;
    map[QStringLiteral("deviceName")] = peer.deviceName;
    map[QStringLiteral("addresses")] = peer.addresses;
    map[QStringLiteral("tcpPort")] = peer.tcpPort;
    map[QStringLiteral("discoveryPort")] = peer.discoveryPort;
    return map;
}

void RendezvousClient::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == _heartbeatTimerId && _isConnected) {
        // 心跳时重新注册以刷新 TTL
        doRegister();
        return;
    }
    QObject::timerEvent(event);
}