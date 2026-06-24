/**
* @file    chat_message_model.h
* @version 4.16.6
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天内存消息列表模型
*
* 为单个设备会话保存运行期消息，并向 QML 提供稳定角色。模型只承担
* 列表通知与状态变更，不负责网络、协议解析或消息持久化。
*
* Change Log:
* [v4.16.6] DuRuoxian   2026-06-24
* * 新增 Stage 5 聊天消息列表模型
*/

#pragma once

#include <QAbstractListModel>
#include <QVariantList>

class ChatMessageModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        MessageIdRole = Qt::UserRole + 1,
        DeviceIdRole,
        SenderNameRole,
        ContentRole,
        SentAtRole,
        IsOutgoingRole,
        StatusRole,
    };
    Q_ENUM(Role)

    explicit ChatMessageModel(QObject *parent = nullptr);
    virtual ~ChatMessageModel() override = default;

    ChatMessageModel(const ChatMessageModel &) = delete;
    ChatMessageModel &operator=(const ChatMessageModel &) = delete;

    // 返回模型中的消息数量
    int count() const;
    // 返回模型行数
    virtual int rowCount(const QModelIndex &parent = {}) const override;
    // 返回指定角色的消息字段
    virtual QVariant data(const QModelIndex &index, int role) const override;
    // 返回 QML delegate 使用的角色名称
    virtual QHash<int, QByteArray> roleNames() const override;
    // 追加一条新消息并发出精确插入通知
    void appendMessage(const QVariantMap &message);
    // 修改指定消息的发送状态
    bool updateMessageStatus(const QString &messageId, int status);
    // 返回兼容旧调用方的消息快照
    QVariantList messages() const;
    // 清空当前设备的运行期消息
    void clear();

signals:
    void countChanged();

private:
    QList<QVariantMap> _messages;  // 当前设备的运行期消息列表
};
