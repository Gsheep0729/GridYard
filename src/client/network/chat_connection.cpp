/**
* @file    chat_connection.cpp
* @version 4.16.5
* @date    2026-06-24
* @author  GridYard Team
* @brief   单条在线聊天 TCP 连接实现
*
* 连接建立后连续解析 ChatText 帧，并将待发送消息按原顺序写入 socket。
* 发生协议错误、网络错误或断开时只清理本连接的待写数据。
*
* Change Log:
* [v4.16.5] FengChunlin   2026-06-24
* * 实现 Stage 5 聊天连接收发和断线处理
*/

#include "chat_connection.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QTcpSocket>

// 构造函数
ChatConnection::ChatConnection(QObject *parent)
    : QObject{parent}
    , _codec{new FrameCodec{this}}
{
    connect(_codec, &FrameCodec::frameReady,
            this,   &ChatConnection::onFrameReady);
    connect(_codec, &FrameCodec::errorOccurred,
            this,   &ChatConnection::onCodecError);
}

// 接管 P2pServer 已完成首帧路由的入站 socket
void ChatConnection::adoptSocket(QTcpSocket *socket)
{
    if (!socket || _socket) {
        return;
    }

    bindSocket(socket);
    onReadyRead();
}

// 发起到在线对端的连接
void ChatConnection::connectToHost(const QHostAddress &address, quint16 port)
{
    if (_socket) {
        return;
    }

    auto *socket = new QTcpSocket{this};
    bindSocket(socket);
    socket->connectToHost(address, port);
}

// 编码并排队发送一条聊天消息
bool ChatConnection::sendMessage(const gy::ChatMessage &message, gy::ChatMessageError *error,
                                 QString *errorMessage)
{
    QByteArray payload;
    if (!gy::ChatMessageCodec::encode(message, &payload, error, errorMessage)) {
        return false;
    }

    const QByteArray frame = FrameCodec::encode(gy::protocol::kTypeChatText, payload);
    if (frame.isEmpty()) {
        if (error) {
            *error = gy::ChatMessageError::WriteFailed;
        }
        if (errorMessage) {
            *errorMessage = tr("聊天消息编码失败");
        }
        return false;
    }

    _pendingFrames.append({message.messageId, frame});
    writePendingFrames();
    return true;
}

// 当前连接是否仍可继续承载消息
bool ChatConnection::isUsable() const
{
    if (!_socket) {
        return false;
    }

    const QAbstractSocket::SocketState state = _socket->state();
    return state == QAbstractSocket::HostLookupState
           || state == QAbstractSocket::ConnectingState
           || state == QAbstractSocket::ConnectedState;
}

// 主动关闭连接并丢弃尚未写入的数据
void ChatConnection::close()
{
    failPendingFrames(gy::ChatMessageError::ConnectionLost, tr("聊天连接已关闭"));
    if (_socket && _socket->state() != QAbstractSocket::UnconnectedState) {
        _socket->disconnectFromHost();
    }
}

// 读取 socket 中的完整聊天帧
void ChatConnection::onReadyRead()
{
    if (_socket) {
        _codec->feed(_socket->readAll());
    }
}

// 建连后写出等待中的消息
void ChatConnection::onConnected()
{
    writePendingFrames();
}

// socket 发生网络错误时结束待写消息
void ChatConnection::onSocketError(QAbstractSocket::SocketError)
{
    if (!_socket) {
        return;
    }

    const gy::ChatMessageError error = _socket->state() == QAbstractSocket::ConnectedState
        ? gy::ChatMessageError::WriteFailed
        : gy::ChatMessageError::ConnectionFailed;
    failPendingFrames(error, _socket->errorString());
}

// socket 断开后通知管理器移除连接
void ChatConnection::onDisconnected()
{
    failPendingFrames(gy::ChatMessageError::ConnectionLost, tr("聊天连接已断开"));
    emit disconnected();
}

// 处理 FrameCodec 交付的完整帧
void ChatConnection::onFrameReady(quint32 type, const QByteArray &payload)
{
    if (type != gy::protocol::kTypeChatText) {
        emit protocolError(gy::ChatMessageError::InvalidPayload, tr("聊天连接收到不支持的帧类型"));
        close();
        return;
    }

    gy::ChatMessage message;
    gy::ChatMessageError error = gy::ChatMessageError::None;
    QString errorMessage;
    if (!gy::ChatMessageCodec::decode(payload, &message, &error, &errorMessage)) {
        emit protocolError(error, errorMessage);
        close();
        return;
    }

    emit messageReceived(message);
}

// 处理 TLV 格式错误
void ChatConnection::onCodecError(gy::protocol::ErrorCode, const QString &errorMessage)
{
    emit protocolError(gy::ChatMessageError::InvalidPayload, errorMessage);
    close();
}

// 绑定 socket 信号与 FrameCodec
void ChatConnection::bindSocket(QTcpSocket *socket)
{
    _socket = socket;
    _socket->setParent(this);
    _socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    _socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

    connect(_socket, &QTcpSocket::readyRead,
            this,    &ChatConnection::onReadyRead);
    connect(_socket, &QTcpSocket::connected,
            this,    &ChatConnection::onConnected);
    connect(_socket, &QTcpSocket::errorOccurred,
            this,    &ChatConnection::onSocketError);
    connect(_socket, &QTcpSocket::disconnected,
            this,    &ChatConnection::onDisconnected);
}

// 尝试将待写帧全部交给 socket 缓冲区
void ChatConnection::writePendingFrames()
{
    if (!_socket || _socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    while (!_pendingFrames.isEmpty()) {
        const PendingFrame pending = _pendingFrames.takeFirst();
        const qint64 written = _socket->write(pending.frame);
        if (written != pending.frame.size()) {
            const QString errorMessage = _socket->errorString().isEmpty()
                ? tr("聊天消息写入失败")
                : _socket->errorString();
            emit messageWriteFailed(pending.messageId, gy::ChatMessageError::WriteFailed,
                                    errorMessage);
            failPendingFrames(gy::ChatMessageError::WriteFailed, errorMessage);
            _socket->disconnectFromHost();
            return;
        }

        emit messageWritten(pending.messageId);
    }
}

// 用统一错误状态结束所有尚未写入的消息
void ChatConnection::failPendingFrames(gy::ChatMessageError error, const QString &errorMessage)
{
    const QList<PendingFrame> pendingFrames = std::exchange(_pendingFrames, {});
    for (const PendingFrame &pending : pendingFrames) {
        emit messageWriteFailed(pending.messageId, error, errorMessage);
    }
}
