/**
* @file    chat_connection.h
* @version 6.6.2
* @date    2026-06-24
* @author  GY
* @brief   单条在线聊天 TCP 连接
*
* 封装聊天连接的 socket、TLV 编解码和待写消息队列。连接对象只处理
* 字节流与连接状态，不维护设备会话或界面消息状态。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v5.1.0] FengChunlin   2026-06-24
* * 新增 Stage 5 聊天连接收发和断线处理
*/

#pragma once

#include "chat_message.h"
#include "protocol.h"

#include <QAbstractSocket>
#include <QList>
#include <QObject>
#include <QString>

class FrameCodec;
class QHostAddress;
class QTcpSocket;

class ChatConnection : public QObject {
    Q_OBJECT

public:
    explicit ChatConnection(QObject *parent = nullptr);
    virtual ~ChatConnection() override = default;

    ChatConnection(const ChatConnection &)            = delete;
    ChatConnection &operator=(const ChatConnection &) = delete;

    // 接管 P2pServer 已完成首帧路由的入站 socket
    void adoptSocket(QTcpSocket *socket);
    // 发起到在线对端的连接
    void connectToHost(const QHostAddress &address, quint16 port);
    // 编码并排队发送一条聊天消息
    bool sendMessage(const gy::ChatMessage &message, gy::ChatMessageError *error,
                     QString *errorMessage);
    // 当前连接是否仍可继续承载消息
    bool isUsable() const;
    // 主动关闭连接并丢弃尚未写入的数据
    void close();

signals:
    void messageReceived(const gy::ChatMessage &message);
    void messageWritten(const QString &messageId);
    void messageWriteFailed(const QString &messageId, gy::ChatMessageError error,
                            const QString &errorMessage);
    void protocolError(gy::ChatMessageError error, const QString &errorMessage);
    void disconnected();

private slots:
    // 读取 socket 中的完整聊天帧
    void onReadyRead();
    // 建连后写出等待中的消息
    void onConnected();
    // socket 发生网络错误时结束待写消息
    void onSocketError(QAbstractSocket::SocketError socketError);
    // socket 断开后通知管理器移除连接
    void onDisconnected();
    // 处理 FrameCodec 交付的完整帧
    void onFrameReady(quint32 type, const QByteArray &payload);
    // 处理 TLV 格式错误
    void onCodecError(gy::protocol::ErrorCode errorCode, const QString &errorMessage);

private:
    struct PendingFrame {
        QString messageId;  // 对应内存消息的唯一标识
        QByteArray frame;   // 等待写入 socket 的完整 TLV 帧
    };

    // 绑定 socket 信号与 FrameCodec
    void bindSocket(QTcpSocket *socket);
    // 尝试将待写帧全部交给 socket 缓冲区
    void writePendingFrames();
    // 用统一错误状态结束所有尚未写入的消息
    void failPendingFrames(gy::ChatMessageError error, const QString &errorMessage);

    QTcpSocket *_socket = nullptr;    // 当前聊天 TCP socket
    FrameCodec *_codec = nullptr;     // 连续 TLV 解码器
    QList<PendingFrame> _pendingFrames; // 已创建但尚未写入的消息
};
