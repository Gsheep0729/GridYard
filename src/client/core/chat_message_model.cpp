/**
* @file    chat_message_model.cpp
* @version 5.2.0
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天内存消息列表模型实现
*
* 使用 beginInsertRows 和 dataChanged 向 QML 通知最小变化范围，避免
* 每次收到消息都替换整个会话列表。
*
* Change Log:
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

// 返回模型行数
int ChatMessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : _messages.size();
}

// 返回指定角色的消息字段
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
    case StatusRole: return message.value("status");
    default: return {};
    }
}

// 返回 QML delegate 使用的角色名称
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

// 在当前首条消息前插入一页更早的历史消息
void ChatMessageModel::prependMessages(const QList<QVariantMap> &messages)
{
    if (messages.isEmpty()) {
        return;
    }

    beginInsertRows({}, 0, messages.size() - 1);
    // 调用方传入旧到新的页面；倒序 prepend 后仍保持模型时间正序。
    for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
        _messages.prepend(*it);
    }
    endInsertRows();
    emit countChanged();
}

// 修改指定消息的发送状态
bool ChatMessageModel::updateMessageStatus(const QString &messageId, int status)
{
    for (int row = 0; row < _messages.size(); ++row) {
        QVariantMap &message = _messages[row];
        if (message.value("messageId").toString() != messageId) {
            continue;
        }
        if (message.value("status").toInt() == status) {
            return true;
        }

        message.insert("status", status);
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex, {StatusRole});
        return true;
    }

    return false;
}

// 从模型中移除指定消息
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
    return false;
}

// 返回兼容旧调用方的消息快照
QVariantList ChatMessageModel::messages() const
{
    QVariantList messages;
    messages.reserve(_messages.size());
    for (const QVariantMap &message : _messages) {
        messages.append(message);
    }
    return messages;
}

// 清空当前设备的运行期消息
void ChatMessageModel::clear()
{
    if (_messages.isEmpty()) {
        return;
    }

    beginResetModel();
    _messages.clear();
    endResetModel();
    emit countChanged();
}
