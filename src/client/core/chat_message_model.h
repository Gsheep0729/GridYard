/**
* @file    chat_message_model.h
* @version 7.17.4
* @date 2026-10-04
* @author  GridYard Team
* @brief   在线聊天内存消息列表模型
*
* 为单个设备会话保存运行期消息，并向 QML 提供稳定角色。模型只承担
* 列表通知与状态变更，不负责网络、协议解析或消息持久化。
*
* Change Log:
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v5.2.0] DuRuoxian   2026-06-24
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
    // 在当前首条消息前插入一页更早的历史消息
    void prependMessages(const QList<QVariantMap> &messages);
    // 修改指定消息的发送状态
    bool updateMessageStatus(const QString &messageId, int status);
    // 从模型中移除指定消息
    bool removeMessage(const QString &messageId);
    // 返回兼容旧调用方的消息快照
    QVariantList messages() const;
    // 清空当前设备的运行期消息
    void clear();

signals:
    void countChanged();

private:
    QList<QVariantMap> _messages;  // 当前设备的运行期消息列表
};
