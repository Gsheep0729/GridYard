/**
* @file    chat_manager.cpp
* @version 6.5.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   在线聊天连接与内存会话管理器实现
*
* 发送路径先检查发现结果，再复用或建立 TCP 连接。接收路径按发送方
* 设备标识归档消息，并使用 messageId 防止重复帧污染会话。消息在
* 成功写入 socket 或收到有效远端消息后发射 messageToPersist 信号，
* 由 AppController 异步提交持久化任务。
*
* Change Log:
* [v6.5.0] GY   2026-06-25
* * 收到远端消息后发布受限长度的通知预览
* [v6.2.0] GY   2026-06-25
* * 接入聊天消息持久化，消息成功收发后发射持久化信号
* [v5.2.0] DuRuoxian   2026-06-24
* * 接入按设备维护的聊天消息模型
* [v5.1.0] FengChunlin   2026-06-24
* * 实现 Stage 5 聊天连接、消息会话和断线清理
*/

#include "chat_manager.h"
#include "chat_connection.h"
#include "chat_message_model.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "history_records.h"
#include "p2p_server.h"

#include <algorithm>
#include <QDateTime>
#include <QHostAddress>
#include <QTcpSocket>
#include <QUuid>

// 构造聊天管理器
ChatManager::ChatManager(QObject *parent)
    : QObject{parent}
{
}

// 组装聊天运行依赖并接收入站聊天连接
void ChatManager::init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer)
{
    _config = config;
    _discovery = discovery;
    _p2pServer = p2pServer;

    connect(_p2pServer, &P2pServer::chatConnectionReceived,
            this,       &ChatManager::onChatConnectionReceived);
}

// 获取设备当前消息快照
QVariantList ChatManager::messagesForDevice(const QString &deviceId) const
{
    const ChatMessageModel *model = _models.value(deviceId);
    return model ? model->messages() : QVariantList{};
}

// 获取设备对应的稳定消息模型
QObject *ChatManager::messageModelForDevice(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        return nullptr;
    }
    return modelForDevice(deviceId);
}

// 向在线设备发送文本消息
void ChatManager::sendText(const QString &deviceId, const QString &content)
{
    if (!_config || !_discovery || !_p2pServer) {
        emit sendFailed(deviceId, gy::ChatMessageError::ConnectionFailed, tr("聊天服务尚未初始化"));
        return;
    }

    const QVariantMap endpoint = _discovery->transferEndpoint(deviceId);
    if (endpoint.isEmpty()) {
        // Stage 5 不维护离线队列，离线消息不能伪装成已发送。
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
    // 先展示发送中状态，网络写入完成前不将消息视为可恢复历史。
    appendMessage(deviceId, message, true, MessageStatus::Pending);
    if (!connection->sendMessage(message, &error, &errorMessage)) {
        // 编码失败不会触发 messageWriteFailed，因此在此同步撤销待持久化记录。
        _pendingRecords.remove(message.messageId);
        updateMessageStatus(deviceId, message.messageId, MessageStatus::Failed);
        emit sendFailed(deviceId, error, errorMessage);
    }
}

// 清理指定设备或全部设备的运行期消息
void ChatManager::clearMessages(const QString &deviceId)
{
    if (deviceId.isEmpty()) {
        const QList<QString> deviceIds = _models.keys();
        _messageIds.clear();
        _pendingRecords.clear();
        for (const QString &id : deviceIds) {
            _models.value(id)->clear();
            emit messagesChanged(id);
        }
        return;
    }

    if (_models.contains(deviceId)) {
        _models.value(deviceId)->clear();
        _messageIds.remove(deviceId);
        for (auto it = _pendingRecords.begin(); it != _pendingRecords.end();) {
            if (it->peerDeviceId == deviceId) {
                // 清空会话后，迟到的写入确认不应重新把旧消息写入历史。
                it = _pendingRecords.erase(it);
            } else {
                ++it;
            }
        }
        emit messagesChanged(deviceId);
    }
}

void ChatManager::removeMessage(const QString &deviceId, const QString &messageId)
{
    ChatMessageModel *model = _models.value(deviceId);
    if (!model || !model->removeMessage(messageId)) {
        return;
    }
    _messageIds[deviceId].remove(messageId);
    _pendingRecords.remove(messageId);
    emit messagesChanged(deviceId);
}

void ChatManager::prependHistoryMessages(const QString &deviceId,
                                         const QList<MessageRecord> &records)
{
    QList<QVariantMap> messages;
    for (auto it = records.crbegin(); it != records.crend(); ++it) {
        const MessageRecord &record = *it;
        if (_messageIds[deviceId].contains(record.messageId)) {
            continue;
        }
        _messageIds[deviceId].insert(record.messageId);
        gy::ChatMessage message;
        message.messageId = record.messageId;
        message.fromDeviceId = record.senderDeviceId;
        message.fromName = record.senderName;
        message.content = record.content;
        message.sentAt = record.sentAt;
        messages.append(messageToVariant(deviceId, message,
            record.direction == RecordDirection::Outgoing,
            static_cast<MessageStatus>(record.localStatus)));
    }
    if (!messages.isEmpty()) {
        modelForDevice(deviceId)->prependMessages(messages);
        emit messagesChanged(deviceId);
    }
}

// 接管 P2P 服务分流后的入站聊天 socket
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

// 归档已通过协议校验的入站消息
void ChatManager::onMessageReceived(ChatConnection *connection, const gy::ChatMessage &message)
{
    const QString deviceId = message.fromDeviceId;
    if (!registerConnection(deviceId, connection)) {
        // 已有活跃连接时拒绝第二条入站通道，避免同一会话交错收帧。
        connection->close();
        return;
    }

    if (appendMessage(deviceId, message, false, MessageStatus::Sent)) {
        const QString preview = message.content.simplified().left(20);
        emit incomingMessageReceived(deviceId, message.fromName, preview);
    }
}

// 写入内存模型，并按收发状态决定持久化时机
bool ChatManager::appendMessage(const QString &deviceId, const gy::ChatMessage &message,
                                bool isOutgoing, MessageStatus status)
{
    QSet<QString> &messageIds = _messageIds[deviceId];
    if (messageIds.contains(message.messageId)) {
        // 内存去重先于持久化，重复帧不会重复显示或重复触发写库。
        return false;
    }

    messageIds.insert(message.messageId);
    modelForDevice(deviceId)->appendMessage(messageToVariant(deviceId, message, isOutgoing, status));
    emit messagesChanged(deviceId);

    MessageRecord record;
    record.messageId = message.messageId;
    record.peerDeviceId = deviceId;
    record.direction = isOutgoing ? RecordDirection::Outgoing : RecordDirection::Incoming;
    record.senderDeviceId = message.fromDeviceId;
    record.senderName = message.fromName;
    record.content = message.content;
    record.sentAt = message.sentAt;
    record.localStatus = static_cast<int>(status);
    record.createdAt = QDateTime::currentDateTimeUtc();
    if (isOutgoing) {
        // 出站消息需等待 socket 接收写入确认，不能把待连接数据误记为已发送。
        _pendingRecords.insert(record.messageId, record);
    } else {
        // 入站帧已完成协议校验，可立即异步提交本地历史。
        emit messageToPersist(record);
    }

    return true;
}

// 将已写入 socket 的出站消息提交给持久化层
void ChatManager::persistWrittenMessage(const QString &messageId)
{
    const auto it = _pendingRecords.find(messageId);
    if (it == _pendingRecords.end()) {
        return;
    }

    MessageRecord record = it.value();
    _pendingRecords.erase(it);
    // 此状态表示字节已交给本地 socket 缓冲，不等同于远端送达回执。
    record.localStatus = static_cast<int>(MessageStatus::Sent);
    emit messageToPersist(record);
}

// 更新内存模型中的发送状态
void ChatManager::updateMessageStatus(const QString &deviceId, const QString &messageId,
                                      MessageStatus status)
{
    ChatMessageModel *model = _models.value(deviceId);
    if (!model) {
        return;
    }

    if (model->updateMessageStatus(messageId, static_cast<int>(status))) {
        emit messagesChanged(deviceId);
    }
}

// 获取可复用连接或基于发现端点创建新连接
ChatConnection *ChatManager::connectionForDevice(const QString &deviceId)
{
    ChatConnection *connection = _connections.value(deviceId);
    if (connection && connection->isUsable()) {
        return connection;
    }

    if (connection) {
        // 失效连接保留在对象树中延迟删除，避免信号处理栈中的悬空指针。
        removeConnection(deviceId, connection);
        connection->deleteLater();
    }

    const QVariantMap endpoint = _discovery->transferEndpoint(deviceId);
    const QHostAddress address{endpoint.value("ipAddress").toString()};
    const quint16 port = endpoint.value("tcpPort").toUInt();
    if (address.isNull() || port == 0) {
        // 发现快照不完整时不创建无目标连接。
        return nullptr;
    }

    connection = new ChatConnection{this};
    connect(connection, &ChatConnection::messageReceived,
            this, [this, connection](const gy::ChatMessage &message) {
        onMessageReceived(connection, message);
    });
    connect(connection, &ChatConnection::messageWritten,
            this, [this, deviceId](const QString &messageId) {
        // 持久化仅发生在写入成功之后，失败发送不污染跨重启历史。
        updateMessageStatus(deviceId, messageId, MessageStatus::Sent);
        persistWrittenMessage(messageId);
    });
    connect(connection, &ChatConnection::messageWriteFailed,
            this, [this, deviceId](const QString &messageId, gy::ChatMessageError error,
                                   const QString &errorMessage) {
        // 移除暂存记录，防止断线后的旧确认触发错误持久化。
        _pendingRecords.remove(messageId);
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

// 获取或创建设备消息模型
ChatMessageModel *ChatManager::modelForDevice(const QString &deviceId)
{
    ChatMessageModel *model = _models.value(deviceId);
    if (!model) {
        model = new ChatMessageModel{this};
        _models.insert(deviceId, model);
    }
    return model;
}

// 登记连接并拒绝已有活跃连接时的重复入站连接
bool ChatManager::registerConnection(const QString &deviceId, ChatConnection *connection)
{
    ChatConnection *existing = _connections.value(deviceId);
    if (!existing || existing == connection || !existing->isUsable()) {
        if (existing && existing != connection) {
            // 新入站连接取代失效连接前先关闭旧通道，保证每台设备只有一个会话连接。
            existing->close();
            existing->deleteLater();
        }
        _connections.insert(deviceId, connection);
        return true;
    }
    return false;
}

// 仅移除当前设备对应的同一连接
void ChatManager::removeConnection(const QString &deviceId, ChatConnection *connection)
{
    if (_connections.value(deviceId) == connection) {
        _connections.remove(deviceId);
    }
}

// 转换为 QML 模型使用的角色数据
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

// 将倒序查询结果按时间正序追加到内存模型
void ChatManager::restoreMessages(const QString &deviceId, const QList<MessageRecord> &records)
{
    ChatMessageModel *model = modelForDevice(deviceId);
    // 查询结果按时间倒序返回，恢复到模型时保持与实时消息一致的时间正序。
    for (auto it = records.crbegin(); it != records.crend(); ++it) {
        const MessageRecord &record = *it;
        if (_messageIds.value(deviceId).contains(record.messageId)) {
            continue;
        }
        _messageIds[deviceId].insert(record.messageId);

        gy::ChatMessage message;
        message.messageId = record.messageId;
        message.fromDeviceId = record.senderDeviceId;
        message.fromName = record.senderName;
        message.content = record.content;
        message.sentAt = record.sentAt;

        model->appendMessage(messageToVariant(deviceId, message,
            record.direction == RecordDirection::Outgoing,
            static_cast<MessageStatus>(record.localStatus)));
    }
    emit messagesChanged(deviceId);
}
