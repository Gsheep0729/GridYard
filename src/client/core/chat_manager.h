/**
* @file    chat_manager.h
* @version 6.2.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   在线聊天连接与内存会话管理器
*
* 以设备标识维护可复用聊天连接和运行期消息会话。管理器只允许向
* 已发现在线的设备发起连接，不创建离线待投递队列。
*
* Change Log:
* [v6.2.0] GY   2026-06-25
* * 接入聊天消息持久化，消息成功收发后异步提交数据库存储
* [v5.2.0] DuRuoxian   2026-06-24
* * 改为按设备提供稳定消息模型
* [v5.1.0] FengChunlin   2026-06-24
* * 新增 Stage 5 聊天连接和内存消息会话管理
*/

#pragma once

#include "chat_message.h"
#include "history_records.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class ChatConnection;
class ChatMessageModel;
class ConfigManager;
class DiscoveryService;
class P2pServer;
class QTcpSocket;

class ChatManager : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS

public:
    explicit ChatManager(QObject *parent = nullptr);
    virtual ~ChatManager() override = default;

    ChatManager(const ChatManager &)            = delete;
    ChatManager &operator=(const ChatManager &) = delete;

    // 组装配置、发现服务和入站服务器
    void init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer);
    // 获取运行期消息快照
    Q_INVOKABLE QVariantList messagesForDevice(const QString &deviceId) const;
    // 获取设备对应的稳定消息模型
    Q_INVOKABLE QObject *messageModelForDevice(const QString &deviceId);
    // 向在线设备发送文本消息
    Q_INVOKABLE void sendText(const QString &deviceId, const QString &content);
    // 清理一个或全部运行期会话
    Q_INVOKABLE void clearMessages(const QString &deviceId = {});
    // 仅从当前会话模型移除一条已删除的本地历史消息
    void removeMessage(const QString &deviceId, const QString &messageId);
    // 在模型头部恢复一页更早的历史消息
    void prependHistoryMessages(const QString &deviceId, const QList<MessageRecord> &records);

signals:
    void messagesChanged(const QString &deviceId);
    void sendFailed(const QString &deviceId, gy::ChatMessageError error,
                    const QString &errorMessage);
    void connectionError(const QString &deviceId, gy::ChatMessageError error,
                         const QString &errorMessage);
    // 成功收发后请求应用层异步持久化消息
    void messageToPersist(const MessageRecord &record);

private:
    friend class AppController;

    enum class MessageStatus {
        Pending,
        Sent,
        Failed,
    };

    // 接管 P2P 服务分流的入站 socket
    void onChatConnectionReceived(QTcpSocket *socket);
    // 处理已通过协议校验的远端消息
    void onMessageReceived(ChatConnection *connection, const gy::ChatMessage &message);
    // 写入内存模型并建立持久化记录
    bool appendMessage(const QString &deviceId, const gy::ChatMessage &message,
                       bool isOutgoing, MessageStatus status);
    // 更新内存消息状态
    void updateMessageStatus(const QString &deviceId, const QString &messageId,
                             MessageStatus status);
    // 持久化已写入 socket 的出站消息
    void persistWrittenMessage(const QString &messageId);
    // 获取或创建到指定设备的连接
    ChatConnection *connectionForDevice(const QString &deviceId);
    // 获取或创建设备消息模型
    ChatMessageModel *modelForDevice(const QString &deviceId);
    // 登记连接并拒绝重复活跃连接
    bool registerConnection(const QString &deviceId, ChatConnection *connection);
    // 移除已失效连接
    void removeConnection(const QString &deviceId, ChatConnection *connection);
    // 转换为模型角色数据
    static QVariantMap messageToVariant(const QString &deviceId, const gy::ChatMessage &message,
                                        bool isOutgoing, MessageStatus status);
    // 按时间正序恢复查询到的消息
    void restoreMessages(const QString &deviceId, const QList<MessageRecord> &records);

    ConfigManager *_config = nullptr;  // 本机身份信息来源
    DiscoveryService *_discovery = nullptr;  // 在线设备与端点查询服务
    P2pServer *_p2pServer = nullptr;  // 聊天入站 socket 来源

    QHash<QString, ChatConnection *> _connections;  // 设备对应的可复用连接
    QHash<QString, ChatMessageModel *> _models;  // 设备对应的运行期消息模型
    QHash<QString, QSet<QString>> _messageIds;  // 每个会话的消息去重索引
    QHash<QString, MessageRecord> _pendingRecords;  // 等待 socket 写入确认的出站消息
};
