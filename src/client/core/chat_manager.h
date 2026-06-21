/**
* @file    chat_manager.h
* @version 4.16.6
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天连接与内存会话管理器
*
* 以设备标识维护可复用聊天连接和运行期消息会话。管理器只允许向
* 已发现在线的设备发起连接，不创建离线待投递队列。
*
* Change Log:
* [v4.16.6] DuRuoxian   2026-06-24
* * 改为按设备提供稳定消息模型
* [v4.16.5] FengChunlin   2026-06-24
* * 新增 Stage 5 聊天连接和内存消息会话管理
*/

#pragma once

#include "chat_message_model.h"
#include "chat_message.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>

class ChatConnection;
class ConfigManager;
class DiscoveryService;
class P2pServer;
class QTcpSocket;

class ChatManager : public QObject {
    Q_OBJECT

public:
    explicit ChatManager(QObject *parent = nullptr);
    virtual ~ChatManager() override = default;

    ChatManager(const ChatManager &)            = delete;
    ChatManager &operator=(const ChatManager &) = delete;

    // 组装聊天所需的配置、发现和入站服务器依赖
    void init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer);
    // 获取指定设备的运行期消息快照
    Q_INVOKABLE QVariantList messagesForDevice(const QString &deviceId) const;
    // 获取指定设备的稳定消息模型
    Q_INVOKABLE QObject *messageModelForDevice(const QString &deviceId);
    // 向在线设备发送一条文本消息
    Q_INVOKABLE void sendText(const QString &deviceId, const QString &content);
    // 清理指定设备或全部设备的运行期消息
    Q_INVOKABLE void clearMessages(const QString &deviceId = {});

signals:
    void messagesChanged(const QString &deviceId);
    void sendFailed(const QString &deviceId, gy::ChatMessageError error,
                    const QString &errorMessage);
    void connectionError(const QString &deviceId, gy::ChatMessageError error,
                         const QString &errorMessage);

private:
    enum class MessageStatus {
        Pending,
        Sent,
        Failed,
    };

    // 接管 P2pServer 分流后的入站聊天 socket
    void onChatConnectionReceived(QTcpSocket *socket);
    // 处理一条已通过协议校验的入站消息
    void onMessageReceived(ChatConnection *connection, const gy::ChatMessage &message);
    // 将消息写入内存会话，重复 messageId 不重复插入
    bool appendMessage(const QString &deviceId, const gy::ChatMessage &message,
                       bool isOutgoing, MessageStatus status);
    // 更新指定消息的发送状态
    void updateMessageStatus(const QString &deviceId, const QString &messageId,
                             MessageStatus status);
    // 获取连接或按发现端点创建出站连接
    ChatConnection *connectionForDevice(const QString &deviceId);
    // 获取或创建指定设备的内存消息模型
    ChatMessageModel *modelForDevice(const QString &deviceId);
    // 将连接登记到设备，并按既有可用连接优先的规则去重
    bool registerConnection(const QString &deviceId, ChatConnection *connection);
    // 连接失效后从设备表中移除
    void removeConnection(const QString &deviceId, ChatConnection *connection);
    // 转换为供后续 QML 模型使用的稳定角色名称
    static QVariantMap messageToVariant(const QString &deviceId, const gy::ChatMessage &message,
                                        bool isOutgoing, MessageStatus status);

    ConfigManager *_config = nullptr;          // 本机身份信息来源
    DiscoveryService *_discovery = nullptr;    // 在线设备与端点查询服务
    P2pServer *_p2pServer = nullptr;           // 聊天入站 socket 来源

    QHash<QString, ChatConnection *> _connections; // deviceId 对应的可用连接
    QHash<QString, ChatMessageModel *> _models;     // deviceId 对应的运行期消息模型
    QHash<QString, QSet<QString>> _messageIds;      // 每个会话的消息去重索引
};
