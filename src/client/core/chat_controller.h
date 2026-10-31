/**
* @file    chat_controller.h
* @version 8.2.1
* @date 2026-11-01
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
    // 读取系统剪贴板纯文本：托盘「发送剪贴板」入口的内容来源；图片/文件型
    // 剪贴板无文本形态返回空串，由调用方经外发评估拒绝
    Q_INVOKABLE QString clipboardText() const;
    // 剪贴板外发评估：无文本形态（空白）与超过单条消息上限的文本拒绝外发并
    // 给出可直接展示的原因；可发送时 echo 为回显文本（压平换行取前 20 字符，
    // 截断补省略号），供表现层在发送前 Toast 展示防敏感内容整段静默外发
    Q_INVOKABLE QVariantMap evaluateClipboardSend(const QString &text) const;
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
