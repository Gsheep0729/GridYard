/**
* @file    chat_manager.cpp
* @version 4.16.5
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天连接与内存会话管理器实现
*
* 发送路径先检查发现结果，再复用或建立 TCP 连接。接收路径按发送方
* 设备标识归档消息，并使用 messageId 防止重复帧污染会话。
*
* Change Log:
* [v4.16.5] GY   2026-06-24
* * 实现 Stage 5 聊天连接、消息会话和断线清理
*/

#include "chat_manager.h"
#include "chat_connection.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"

#include <algorithm>
#include <QDateTime>
#include <QHostAddress>
#include <QTcpSocket>
#include <QUuid>
#include <utility>

// 构造函数
ChatManager::ChatManager(QObject *parent)
    : QObject{parent}
{
}

// 组装聊天所需的配置、发现和入站服务器依赖
void ChatManager::init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer)
{
    _config = config;
    _discovery = discovery;
    _p2pServer = p2pServer;

    connect(_p2pServer, &P2pServer::chatConnectionReceived,
            this,       &ChatManager::onChatConnectionReceived);
}

// 获取指定设备的运行期消息快照
QVariantList ChatManager::messagesForDevice(const QString &deviceId) const
{
    return _sessions.value(deviceId);
}

// 向在线设备发送一条文本消息
void ChatManager::sendText(const QString &deviceId, const QString &content)
{
    if (!_config || !_discovery || !_p2pServer) {
        emit sendFailed(deviceId, gy::ChatMessageError::ConnectionFailed, tr("聊天服务尚未初始化"));
        return;
    }

    const QVariantMap endpoint = _discovery->transferEndpoint(deviceId);
    if (endpoint.isEmpty()) {
        emit sendFailed(deviceId, gy::ChatMessageError::PeerOffline, tr("目标设备不在线"));
        return;
    }

    gy::ChatMessage message;
    message.messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    message.fromDeviceId = _config->deviceId();
    message.fromName = _config->deviceName();
    message.content = content;
    message.sentAt = QDateTime::currentDateTimeUtc();

    ChatConnection *connection = connectionForDevice(deviceId);
    if (!connection) {
        emit sendFailed(deviceId, gy::ChatMessageError::ConnectionFailed, tr("无法创建聊天连接"));
        return;
    }

    gy::ChatMessageError error = gy::ChatMessageError::None;
    QString errorMessage;
    QByteArray payload;
    if (!gy::ChatMessageCodec::encode(message, &payload, &error, &errorMessage)) {
        emit sendFailed(deviceId, error, errorMessage);
        return;
    }

    appendMessage(deviceId, message, true, MessageStatus::Pending);
    if (!connection->sendMessage(message, &error, &errorMessage)) {
        updateMessageStatus(deviceId, message.messageId, MessageStatus::Failed);
        emit sendFailed(deviceId, error, errorMessage);
    }
}

// 清理指定设备或全部设备的运行期消息
void ChatManager::clearMessages(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        const QList<QString> deviceIds = _sessions.keys();
        _sessions.clear();
        _messageIds.clear();
        for (const QString &id : deviceIds) {
            emit messagesChanged(id);
        }
        return;
    }

    if (_sessions.remove(deviceId) > 0) {
        _messageIds.remove(deviceId);
        emit messagesChanged(deviceId);
    }
}

// 接管 P2pServer 分流后的入站聊天 socket
void ChatManager::onChatConnectionReceived(QTcpSocket *socket)
{
    if (!socket) {
        return;
    }

    auto *connection = new ChatConnection{this};
    connect(connection, &ChatConnection::messageReceived,
            this, [this, connection](const gy::ChatMessage &message) {
        onMessageReceived(connection, message);
    });
    connect(connection, &ChatConnection::protocolError,
            this, [this, connection](gy::ChatMessageError error, const QString &errorMessage) {
        const auto it = std::find_if(_connections.cbegin(), _connections.cend(),
                                     [connection](ChatConnection *value) {
            return value == connection;
        });
        if (it != _connections.cend()) {
            emit connectionError(it.key(), error, errorMessage);
        }
    });
    connect(connection, &ChatConnection::disconnected,
            this, [this, connection]() {
        const auto it = std::find_if(_connections.cbegin(), _connections.cend(),
                                     [connection](ChatConnection *value) {
            return value == connection;
        });
        if (it != _connections.cend()) {
            removeConnection(it.key(), connection);
        }
        connection->deleteLater();
    });

    connection->adoptSocket(socket);
}

// 处理一条已通过协议校验的入站消息
void ChatManager::onMessageReceived(ChatConnection *connection, const gy::ChatMessage &message)
{
    const QString deviceId = message.fromDeviceId;
    if (!registerConnection(deviceId, connection)) {
        connection->close();
    }

    appendMessage(deviceId, message, false, MessageStatus::Sent);
}

// 将消息写入内存会话，重复 messageId 不重复插入
bool ChatManager::appendMessage(const QString &deviceId, const gy::ChatMessage &message,
                                bool isOutgoing, MessageStatus status)
{
    QSet<QString> &messageIds = _messageIds[deviceId];
    if (messageIds.contains(message.messageId)) {
        return false;
    }

    messageIds.insert(message.messageId);
    _sessions[deviceId].append(messageToVariant(deviceId, message, isOutgoing, status));
    emit messagesChanged(deviceId);
    return true;
}

// 更新指定消息的发送状态
void ChatManager::updateMessageStatus(const QString &deviceId, const QString &messageId,
                                      MessageStatus status)
{
    auto sessionIt = _sessions.find(deviceId);
    if (sessionIt == _sessions.end()) {
        return;
    }

    QVariantList &session = sessionIt.value();
    for (QVariant &item : session) {
        QVariantMap message = item.toMap();
        if (message.value("messageId").toString() == messageId) {
            if (message.value("status").toInt() == static_cast<int>(status)) {
                return;
            }
            message.insert("status", static_cast<int>(status));
            item = message;
            emit messagesChanged(deviceId);
            return;
        }
    }
}

// 获取连接或按发现端点创建出站连接
ChatConnection *ChatManager::connectionForDevice(const QString &deviceId)
{
    ChatConnection *connection = _connections.value(deviceId);
    if (connection && connection->isUsable()) {
        return connection;
    }

    if (connection) {
        removeConnection(deviceId, connection);
        connection->deleteLater();
    }

    const QVariantMap endpoint = _discovery->transferEndpoint(deviceId);
    const QHostAddress address{endpoint.value("ipAddress").toString()};
    const quint16 port = endpoint.value("tcpPort").toUInt();
    if (address.isNull() || port == 0) {
        return nullptr;
    }

    connection = new ChatConnection{this};
    connect(connection, &ChatConnection::messageReceived,
            this, [this, connection](const gy::ChatMessage &message) {
        onMessageReceived(connection, message);
    });
    connect(connection, &ChatConnection::messageWritten,
            this, [this, deviceId](const QString &messageId) {
        updateMessageStatus(deviceId, messageId, MessageStatus::Sent);
    });
    connect(connection, &ChatConnection::messageWriteFailed,
            this, [this, deviceId](const QString &messageId, gy::ChatMessageError error,
                                   const QString &errorMessage) {
        updateMessageStatus(deviceId, messageId, MessageStatus::Failed);
        emit sendFailed(deviceId, error, errorMessage);
    });
    connect(connection, &ChatConnection::protocolError,
            this, [this, deviceId](gy::ChatMessageError error, const QString &errorMessage) {
        emit connectionError(deviceId, error, errorMessage);
    });
    connect(connection, &ChatConnection::disconnected,
            this, [this, deviceId, connection]() {
        removeConnection(deviceId, connection);
        connection->deleteLater();
    });

    _connections.insert(deviceId, connection);
    connection->connectToHost(address, port);
    return connection;
}

// 将连接登记到设备，并按既有可用连接优先的规则去重
bool ChatManager::registerConnection(const QString &deviceId, ChatConnection *connection)
{
    ChatConnection *existing = _connections.value(deviceId);
    if (!existing || existing == connection || !existing->isUsable()) {
        if (existing && existing != connection) {
            existing->close();
            existing->deleteLater();
        }
        _connections.insert(deviceId, connection);
        return true;
    }

    return false;
}

// 连接失效后从设备表中移除
void ChatManager::removeConnection(const QString &deviceId, ChatConnection *connection)
{
    if (_connections.value(deviceId) == connection) {
        _connections.remove(deviceId);
    }
}

// 转换为供后续 QML 模型使用的稳定角色名称
QVariantMap ChatManager::messageToVariant(const QString &deviceId, const gy::ChatMessage &message,
                                          bool isOutgoing, MessageStatus status)
{
    return {
        {"messageId", message.messageId},
        {"deviceId", deviceId},
        {"senderName", message.fromName},
        {"content", message.content},
        {"sentAt", message.sentAt.toUTC().toString(Qt::ISODateWithMs)},
        {"isOutgoing", isOutgoing},
        {"status", static_cast<int>(status)},
    };
}
