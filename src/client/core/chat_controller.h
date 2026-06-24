/**
* @file    chat_controller.h
* @version 6.6.2
* @date    2026-06-27
* @author  GridYard Team
* @brief   面向 QML 的聊天控制器
*
* 只暴露表现层需要的消息模型、发送命令和提示信号，内部聊天连接、
* 去重索引和持久化事件由 ChatManager 持有。
*
* Change Log:
* [v6.6.2] GY   2026-06-27
* * 新增聊天 UI API 门面，避免 QML 直接依赖内部 Manager
*/

#pragma once

#include "chat_message.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class ChatManager;

class ChatController : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit ChatController(ChatManager *manager, QObject *parent = nullptr);
    virtual ~ChatController() override = default;

    ChatController(const ChatController &) = delete;
    ChatController &operator=(const ChatController &) = delete;

    // 获取指定设备的运行期消息快照
    Q_INVOKABLE QVariantList messagesForDevice(const QString &deviceId) const;
    // 获取指定设备的稳定消息模型
    Q_INVOKABLE QObject *messageModelForDevice(const QString &deviceId);
    // 向在线设备发送文本消息
    Q_INVOKABLE void sendText(const QString &deviceId, const QString &content);
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
