/**
* @file    rendezvous_client.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点客户端实现
*/

#include "rendezvous_client.h"
#include "config_manager.h"
#include "posix_tcp_socket.h"
#include "rendezvous_protocol_keys_qt.h"

using namespace gy::rendezvous;

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

RendezvousClient::RendezvousClient(QObject *parent)
    : QObject{parent}
{
}

RendezvousClient::~RendezvousClient()
{
    disconnectFromServer();
}

void RendezvousClient::connectToServer(const QString &host, int port)
{
    _wantConnected = true;
    _serverHost = host;
    _serverPort = port;
    _buffer.clear();
    _isConnected = false;

    stopHeartbeat();
    closeSocket();

    _socket = new gy::net::PosixTcpSocket(this);
    connect(_socket, &gy::net::PosixTcpSocket::connected, this, &RendezvousClient::onSocketConnected);
    connect(_socket, &gy::net::PosixTcpSocket::disconnected, this, &RendezvousClient::onSocketDisconnected);
    connect(_socket, &gy::net::PosixTcpSocket::errorOccurred, this, &RendezvousClient::onSocketError);
    connect(_socket, &gy::net::PosixTcpSocket::readyRead, this, &RendezvousClient::onReadyRead);

    qDebug() << "RendezvousClient: 连接协调服务器" << host << ":" << port;
    _socket->connectToHost(host, port);
}

// 设置访问令牌
void RendezvousClient::setToken(const QString &token)
{
    _token = token;
}

void RendezvousClient::disconnectFromServer()
{
    const bool wasConnected = _isConnected;
    _wantConnected = false;
    _reconnectScheduled = false;
    _isConnected = false;
    stopHeartbeat();
    closeSocket();
    if (wasConnected) {
        emit disconnected();
    }
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
    json[key(kKeyType)] = key(kTypeRegister);
    json[key(kKeyRoom)] = _room;
    json[key(kKeyDeviceId)] = _deviceId;
    json[key(kKeyDeviceName)] = _deviceName;
    json[key(kKeyAddresses)] = QJsonArray::fromStringList(_addresses);
    json[key(kKeyTcpPort)] = _tcpPort;
    json[key(kKeyDiscoveryPort)] = _discoveryPort;
    json[key(kKeyTtlSeconds)] = _ttlSeconds;

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
    json[key(kKeyType)] = key(kTypeListPeers);
    json[key(kKeyRoom)] = room;
    // 携带本机 ID，服务端按此过滤请求者自身（与 register 同源）
    if (!_deviceId.isEmpty()) {
        json[key(kKeyDeviceId)] = _deviceId;
    }

    sendJson(json);
    qDebug() << "RendezvousClient: 发送查询设备请求 room =" << room;
}

// 请求协调服务器向目标设备转发中继邀请
void RendezvousClient::requestRelayInvite(const QString &relayId, const QString &targetDeviceId,
                                          const QString &fileName, qint64 totalBytes)
{
    if (!_isConnected) {
        qWarning() << "RendezvousClient: 未连接协调服务器，无法发送中继邀请";
        return;
    }

    QJsonObject json;
    json[key(kKeyType)] = key(kTypeRelayInvite);
    json[key(kKeyRoom)] = _room;
    json[key(kKeyRelayId)] = relayId;
    json[key(kKeyTargetDeviceId)] = targetDeviceId;
    json[key(kKeySenderDeviceId)] = _deviceId;
    json[key(kKeySenderName)] = _deviceName;
    json[key(kKeyFileName)] = fileName;
    json[key(kKeyTotalBytes)] = totalBytes;

    sendJson(json);
    qDebug() << "RendezvousClient: 已发送中继邀请" << relayId << "目标" << targetDeviceId;
}

// 按本机 deviceId 轮询待领取的中继邀请
void RendezvousClient::sendRelayPoll()
{
    if (!_isConnected || _deviceId.isEmpty()) {
        return;
    }

    QJsonObject json;
    json[key(kKeyType)] = key(kTypeRelayPoll);
    json[key(kKeyRoom)] = _room;
    json[key(kKeyDeviceId)] = _deviceId;

    sendJson(json);
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
    stopHeartbeat();
    qDebug() << "RendezvousClient: 与协调服务器断开连接";
    emit disconnected();

    scheduleReconnect();
}

void RendezvousClient::onSocketError(gy::net::SocketError socketError)
{
    Q_UNUSED(socketError);
    QString errorMsg = _socket ? _socket->errorString() : QStringLiteral("未知错误");
    qWarning() << "RendezvousClient: 套接字错误" << errorMsg;
    emit errorOccurred(errorMsg);
    scheduleReconnect();
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
    if (!_socket || _socket->state() != gy::net::SocketState::ConnectedState) {
        return;
    }

    // 所有控制报文在此统一注入令牌，服务器未启用认证时字段为空即不发送
    QJsonObject authenticated = json;
    if (!_token.isEmpty()) {
        authenticated[key(kKeyToken)] = _token;
    }

    QJsonDocument doc(authenticated);
    QByteArray data = doc.toJson(QJsonDocument::Compact);
    data.append("\n");  // 每条消息以换行符分隔
    _socket->write(data);
    _socket->flush();
}

void RendezvousClient::closeSocket()
{
    if (!_socket) {
        return;
    }

    gy::net::PosixTcpSocket *socket = _socket;
    _socket = nullptr;
    disconnect(socket, nullptr, this, nullptr);
    if (socket->state() != gy::net::SocketState::UnconnectedState) {
        // 等写队列排空并完成断连后再删除，避免最后一条控制报文被提前丢弃
        QObject::connect(socket, &gy::net::PosixTcpSocket::disconnected,
                         socket, &QObject::deleteLater);
        QTimer::singleShot(1000, socket, &QObject::deleteLater);
        socket->disconnectFromHost();
    } else {
        socket->deleteLater();
    }
}

void RendezvousClient::stopHeartbeat()
{
    if (_heartbeatTimerId != 0) {
        killTimer(_heartbeatTimerId);
        _heartbeatTimerId = 0;
    }
}

void RendezvousClient::scheduleReconnect()
{
    if (!_wantConnected || _serverHost.isEmpty() || _serverPort <= 0 || _reconnectScheduled) {
        return;
    }

    _reconnectScheduled = true;
    QTimer::singleShot(kReconnectDelayMs, this, [this]() {
        _reconnectScheduled = false;
        if (!_wantConnected || _serverHost.isEmpty() || _serverPort <= 0) {
            return;
        }
        if (_socket && (_socket->state() == gy::net::SocketState::HostLookupState
                        || _socket->state() == gy::net::SocketState::ConnectingState
                        || _socket->state() == gy::net::SocketState::ConnectedState)) {
            return;
        }

        qDebug() << "RendezvousClient: 尝试重新连接...";
        connectToServer(_serverHost, _serverPort);
    });
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
    const QString type = json[key(kKeyType)].toString();

    if (type == key(kTypeRegisterAck)) {
        int ttl = json[key(kKeyTtlSeconds)].toInt(30);
        _ttlSeconds = ttl;
        qDebug() << "RendezvousClient: 收到注册确认，TTL =" << ttl;
        emit registerAckReceived(ttl);

        // 启动心跳定时器
        if (_heartbeatTimerId == 0) {
            _heartbeatTimerId = startTimer(kHeartbeatIntervalMs);
        }

        // 注册后立即轮询一次中继邀请，之后随心跳周期刷新
        sendRelayPoll();

    } else if (type == key(kTypePeers)) {
        QList<QVariantMap> peers;
        const QJsonArray items = json[key(kKeyItems)].toArray();
        for (const QJsonValue &item : items) {
            QJsonObject obj = item.toObject();
            PeerEndpoint peer;
            peer.deviceId = obj[key(kKeyDeviceId)].toString();
            peer.deviceName = obj[key(kKeyDeviceName)].toString();
            const QJsonArray addresses = obj[key(kKeyAddresses)].toArray();
            for (const QJsonValue &addr : addresses) {
                peer.addresses.append(addr.toString());
            }
            peer.tcpPort = static_cast<quint16>(obj[key(kKeyTcpPort)].toInt());
            peer.discoveryPort = static_cast<quint16>(obj[key(kKeyDiscoveryPort)].toInt(45678));
            const QString lastSeenStr = obj[key(kKeyUpdatedAt)].toString();
            if (!lastSeenStr.isEmpty()) {
                peer.lastSeen = QDateTime::fromString(lastSeenStr, Qt::ISODate);
            }
            peers.append(peerEndpointToVariantMap(peer));
        }
        qDebug() << "RendezvousClient: 收到候选设备列表，" << peers.count() << " 个设备";
        emit peersReceived(peers);

    } else if (type == key(kTypeRelayInviteAck)) {
        const QString relayId = json[key(kKeyRelayId)].toString();
        qDebug() << "RendezvousClient: 中继邀请已受理" << relayId;
        emit relayInviteAckReceived(relayId);

    } else if (type == key(kTypeRelayInvites)) {
        QList<QVariantMap> invites;
        const QJsonArray items = json[key(kKeyItems)].toArray();
        for (const QJsonValue &item : items) {
            QJsonObject obj = item.toObject();
            QVariantMap invite;
            invite[QStringLiteral("relayId")] = obj[key(kKeyRelayId)].toString();
            invite[QStringLiteral("senderDeviceId")] = obj[key(kKeySenderDeviceId)].toString();
            invite[QStringLiteral("fileName")] = obj[key(kKeyFileName)].toString();
            invite[QStringLiteral("totalBytes")] = obj[key(kKeyTotalBytes)].toInteger();
            invites.append(invite);
        }
        if (!invites.isEmpty()) {
            qDebug() << "RendezvousClient: 轮询到" << invites.count() << "条中继邀请";
            emit relayInvitesReceived(invites);
        }

    } else if (type == key(kTypeError)) {
        QString message = json[key(kKeyMessage)].toString();
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
        // 心跳时重新注册以刷新 TTL，并顺带轮询中继邀请
        doRegister();
        sendRelayPoll();
        return;
    }
    QObject::timerEvent(event);
}
