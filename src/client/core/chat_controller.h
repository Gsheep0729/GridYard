/**
* @file    chat_controller.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   面向 QML 的聊天控制器
*
* 只暴露表现层需要的消息模型、发送命令和提示信号，内部聊天连接、
* 去重索引和持久化事件由 ChatManager 持有。
*/

#pragma once

#include "chat_message.h"
#include "protocol.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class ChatManager;

class ChatController : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    // 聊天内容长度上限（协议层常量只读暴露，QML 不再硬编码）
    Q_PROPERTY(int maxChatContentLength READ maxChatContentLength CONSTANT)

public:
    explicit ChatController(ChatManager *manager, QObject *parent = nullptr);
    virtual ~ChatController() override = default;

    ChatController(const ChatController &) = delete;
    ChatController &operator=(const ChatController &) = delete;

    // 获取聊天内容长度上限
    int maxChatContentLength() const;
    // 获取指定设备的运行期消息快照
    Q_INVOKABLE QVariantList messagesForDevice(const QString &deviceId) const;
    // 获取指定设备的稳定消息模型
    Q_INVOKABLE QObject *messageModelForDevice(const QString &deviceId);
    // 向在线设备发送文本消息，返回消息是否被接受（false 时 QML 保留输入内容）
    Q_INVOKABLE bool sendText(const QString &deviceId, const QString &content);
    // 清理指定设备或全部设备的运行期消息
    Q_INVOKABLE void clearMessages(const QString &deviceId = {});

signals:
    void messagesChanged(const QString &deviceId);
    void sendFailed(const QString &deviceId, gy::ChatMessageError error,
                    const QString &errorMessage);
    void connectionError(const QString &deviceId, gy::ChatMessageError error,
                         const QString &errorMessage);
    void incomingMessageReceived(const QString &deviceId, const QString &senderName,
                                 const QString &preview);

private:
    ChatManager *_manager = nullptr;  // 内部聊天连接与消息会话管理器
};
