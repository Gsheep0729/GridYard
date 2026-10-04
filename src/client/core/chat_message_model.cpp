/**
* @file    chat_message_model.cpp
* @version 7.17.3
* @date 2026-10-04
* @author  GridYard Team
* @brief   在线聊天内存消息列表模型实现
*
* 使用 beginInsertRows 和 dataChanged 向 QML 通知最小变化范围，避免
* 每次收到消息都替换整个会话列表。
*
* Change Log:
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
* * 实现 Stage 5 聊天消息列表模型
*/

#include "chat_message_model.h"

// 构造函数
ChatMessageModel::ChatMessageModel(QObject *parent)
    : QAbstractListModel{parent}
{
}

// 返回模型中的消息数量
int ChatMessageModel::count() const
{
    return _messages.size();
}

// 返回模型行数，parent 有效时返回 0（非树形模型）
int ChatMessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : _messages.size();
}

// 根据角色索引返回消息字段，QML ListView delegate 通过角色名访问
QVariant ChatMessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _messages.size()) {
        return {};
    }

    const QVariantMap &message = _messages.at(index.row());
    switch (role) {
    case MessageIdRole: return message.value("messageId");
    case DeviceIdRole: return message.value("deviceId");
    case SenderNameRole: return message.value("senderName");
    case ContentRole: return message.value("content");
    case SentAtRole: return message.value("sentAt");
    case IsOutgoingRole: return message.value("isOutgoing");
    case StatusRole: return message.value("status");  // 0=Pending, 1=Sent, 2=Failed
    default: return {};
    }
}

// 返回 QML delegate 使用的角色名称映射，QML 通过 role name 绑定数据
QHash<int, QByteArray> ChatMessageModel::roleNames() const
{
    return {
        {MessageIdRole, "messageId"},
        {DeviceIdRole, "deviceId"},
        {SenderNameRole, "senderName"},
        {ContentRole, "content"},
        {SentAtRole, "sentAt"},
        {IsOutgoingRole, "isOutgoing"},
        {StatusRole, "status"},
    };
}

// 追加一条新消息并发出精确插入通知
void ChatMessageModel::appendMessage(const QVariantMap &message)
{
    const int row = _messages.size();
    beginInsertRows({}, row, row);
    _messages.append(message);
    endInsertRows();
    emit countChanged();
}

// 在当前首条消息前插入一页更早的历史消息，保持模型时间正序
void ChatMessageModel::prependMessages(const QList<QVariantMap> &messages)
{
    if (messages.isEmpty()) {
        return;
    }

    beginInsertRows({}, 0, messages.size() - 1);
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        _messages.prepend(*it);  // 倒序 prepend 后仍保持模型时间正序
    }
    endInsertRows();
    emit countChanged();
}

// 修改指定消息的发送状态，仅在状态实际变化时通知 QML 刷新对应行
bool ChatMessageModel::updateMessageStatus(const QString &messageId, int status)
{
    for (int row = 0; row < _messages.size(); ++row) {
        QVariantMap &message = _messages[row];
        if (message.value("messageId").toString() != messageId) {
            continue;
        }
        if (message.value("status").toInt() == status) {
            return true;  // 状态未变化，无需触发 dataChanged
        }

        message.insert("status", status);
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, {StatusRole});  // 精确通知只有 Status 角色变化
        return true;
    }

    return false;  // 消息 ID 不存在
}

// 从模型中移除指定消息，使用 beginRemoveRows 精确通知 QML 刷新
bool ChatMessageModel::removeMessage(const QString &messageId)
{
    for (int row = 0; row < _messages.size(); ++row) {
        if (_messages.at(row).value("messageId").toString() != messageId) {
            continue;
        }

        beginRemoveRows({}, row, row);
        _messages.removeAt(row);
        endRemoveRows();
        emit countChanged();
        return true;
    }
    return false;  // 消息 ID 不存在，返回 false 供调用方判断是否需要额外清理
}

// 返回兼容旧调用方的消息快照，将 QVariantMap 列表转换为 QVariantList
QVariantList ChatMessageModel::messages() const
{
    QVariantList messages;
    messages.reserve(_messages.size());
    for (const QVariantMap &message : _messages) {
        messages.append(message);
    }
    return messages;
}

// 清空当前设备的运行期消息，使用 beginResetModel 通知 QML 全量刷新
void ChatMessageModel::clear()
{
    if (_messages.isEmpty()) {
        return;  // 空列表无需触发重置
    }

    beginResetModel();
    _messages.clear();
    endResetModel();
    emit countChanged();
}
