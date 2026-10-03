/**
* @file    conversation_timeline_model.cpp
* @version 7.15.11
* @date    2026-10-03
* @author  GridYard Team
* @brief   聊天消息与传输会话的统一时间线模型实现
*
* 来源模型的行级信号在此换算为合并列表的增量通知：会话进度刷新只
* 触发对应行的 dataChanged，设备切换或消息模型重置才整体重建。
*
* Change Log:
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.13.0] GY   2026-10-02
* * 实现合并、过滤与行级增量通知逻辑
*/

#include "conversation_timeline_model.h"

#include "chat_controller.h"
#include "chat_message_model.h"
#include "transfer_session_model.h"

#include <QDateTime>

#include <algorithm>
#include <utility>

using namespace gy::session;

// 构造函数
ConversationTimelineModel::ConversationTimelineModel(QObject *parent)
    : QAbstractListModel{parent}
{
}

// 返回时间线条目数量
int ConversationTimelineModel::count() const
{
    return _entries.size();
}

// 返回模型行数，parent 有效时返回 0（非树形模型）
int ConversationTimelineModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : _entries.size();
}

// 根据角色索引返回行数据，消息行与传输行各取自己的字段
QVariant ConversationTimelineModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _entries.size()) {
        return {};
    }

    const TimelineEntry &entry = _entries.at(index.row());
    if (role == KindRole) {
        return entry.isMessage ? QStringLiteral("message") : QStringLiteral("transfer");
    }
    if (role == ShowAvatarRole) {
        return entry.isMessage && entry.showAvatar;
    }

    if (entry.isMessage) {
        switch (role) {
        case MessageIdRole:   return entry.data.value(QStringLiteral("messageId"));
        case SenderNameRole:  return entry.data.value(QStringLiteral("senderName"));
        case ContentRole:     return entry.data.value(QStringLiteral("content"));
        case SentAtRole:      return entry.data.value(QStringLiteral("sentAt"));
        case IsOutgoingRole:  return entry.data.value(QStringLiteral("isOutgoing"));
        case StatusRole:      return entry.data.value(QStringLiteral("status"));
        default:              return {};
        }
    }

    switch (role) {
    case SessionIdRole:          return entry.data.value(kSessionId);
    case TypeRole:               return entry.data.value(kType);
    case FileNameRole:           return entry.data.value(kFileName);
    case StatusRole:             return entry.data.value(kStatus);
    case ProgressRole:           return entry.data.value(kProgress);
    case BytesTransferredRole:   return entry.data.value(kBytesTransferred);
    case TotalBytesRole:         return entry.data.value(kTotalBytes);
    case CreatedAtRole:          return entry.data.value(kCreatedAt);
    case PeerDeviceNameRole:     return entry.data.value(kPeerDeviceName);
    case IsDirectoryRole:        return entry.data.value(kIsDirectory);
    case FileListRole:           return entry.data.value(kFileList);
    case CanDeleteLocalFileRole: return entry.data.value(kCanDeleteLocalFile);
    case ErrorMsgRole:           return entry.data.value(kErrorMsg);
    default:                     return {};
    }
}

// 返回 QML delegate 使用的角色名称映射
QHash<int, QByteArray> ConversationTimelineModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {ShowAvatarRole, "showAvatar"},
        {MessageIdRole, "messageId"},
        {SenderNameRole, "senderName"},
        {ContentRole, "content"},
        {SentAtRole, "sentAt"},
        {IsOutgoingRole, "isOutgoing"},
        {StatusRole, "status"},
        {SessionIdRole, "sessionId"},
        {TypeRole, "type"},
        {FileNameRole, "fileName"},
        {ProgressRole, "progress"},
        {BytesTransferredRole, "bytesTransferred"},
        {TotalBytesRole, "totalBytes"},
        {CreatedAtRole, "createdAt"},
        {PeerDeviceNameRole, "peerDeviceName"},
        {IsDirectoryRole, "isDirectory"},
        {FileListRole, "fileList"},
        {CanDeleteLocalFileRole, "canDeleteLocalFile"},
        {ErrorMsgRole, "errorMsg"},
    };
}

// 返回时间线归属的设备
QString ConversationTimelineModel::deviceId() const
{
    return _deviceId;
}

// 返回注入的聊天控制器
QObject *ConversationTimelineModel::chatController() const
{
    return _chatController;
}

// 返回注入的传输会话模型
QObject *ConversationTimelineModel::transferSessions() const
{
    return _transferSource;
}

// 切换时间线归属的设备并重建合并列表
void ConversationTimelineModel::setDeviceId(const QString &deviceId)
{
    if (_deviceId == deviceId) {
        return;
    }

    _deviceId = deviceId;
    emit deviceIdChanged();
    refreshSources();
}

// 注入聊天控制器，用于解析设备对应的消息模型
void ConversationTimelineModel::setChatController(QObject *controller)
{
    if (_chatController == controller) {
        return;
    }

    _chatController = qobject_cast<ChatController *>(controller);
    refreshSources();
}

// 注入全局传输会话模型
void ConversationTimelineModel::setTransferSessions(QObject *source)
{
    auto *transferModel = qobject_cast<TransferSessionModel *>(source);
    if (_transferSource == transferModel) {
        return;
    }

    if (_transferSource) {
        QObject::disconnect(_transferSource, nullptr, this, nullptr);
    }

    _transferSource = transferModel;
    if (_transferSource) {
        connect(_transferSource, &QAbstractItemModel::rowsInserted,
                this, &ConversationTimelineModel::onSessionsInserted);
        connect(_transferSource, &QAbstractItemModel::rowsRemoved,
                this, &ConversationTimelineModel::onSessionsRemoved);
        connect(_transferSource, &QAbstractItemModel::dataChanged,
                this, &ConversationTimelineModel::onSessionsDataChanged);
    }
    rebuildFromSources();
}

// 按当前设备重新解析消息模型并全量重建
void ConversationTimelineModel::refreshSources()
{
    // 空设备不解析消息模型，避免在管理器里留下空键会话
    ChatMessageModel *messageSource = nullptr;
    if (_chatController && !_deviceId.isEmpty()) {
        messageSource = qobject_cast<ChatMessageModel *>(
            _chatController->messageModelForDevice(_deviceId));
    }

    if (messageSource != _messageSource) {
        if (_messageSource) {
            QObject::disconnect(_messageSource, nullptr, this, nullptr);
        }

        _messageSource = messageSource;
        if (_messageSource) {
            connect(_messageSource, &QAbstractItemModel::rowsInserted,
                    this, &ConversationTimelineModel::onMessagesInserted);
            connect(_messageSource, &QAbstractItemModel::rowsRemoved,
                    this, &ConversationTimelineModel::onMessagesRemoved);
            connect(_messageSource, &QAbstractItemModel::dataChanged,
                    this, &ConversationTimelineModel::onMessagesDataChanged);
            connect(_messageSource, &QAbstractItemModel::modelReset,
                    this, &ConversationTimelineModel::rebuildFromSources);
        }
    }

    rebuildFromSources();
}

// 从两个来源模型重建合并列表并发出重置通知
void ConversationTimelineModel::rebuildFromSources()
{
    beginResetModel();
    _entries.clear();

    if (_messageSource) {
        for (int row = 0; row < _messageSource->rowCount(); ++row) {
            _entries.append(makeMessageEntry(row));
        }
    }

    if (_transferSource) {
        for (int row = 0; row < _transferSource->rowCount(); ++row) {
            const QVariantMap session = readSessionRow(row);
            if (!sessionBelongsToDevice(session)) {
                continue;
            }

            TimelineEntry entry;
            entry.isMessage = false;
            entry.sourceRow = row;
            entry.sortTime = timestampOf(session.value(kCreatedAt).toString());
            entry.data = session;
            _entries.append(entry);
        }
    }

    // 按时间升序稳定排序，同时间戳消息在前、再按来源行号，与旧 JS 排序一致
    std::stable_sort(_entries.begin(), _entries.end(),
                     [](const TimelineEntry &left, const TimelineEntry &right) {
        if (left.sortTime != right.sortTime) {
            return left.sortTime < right.sortTime;
        }
        if (left.isMessage != right.isMessage) {
            return left.isMessage;
        }
        return left.sourceRow < right.sourceRow;
    });

    applyAvatarFlags(0, false);
    endResetModel();
    emit countChanged();
}

// 处理消息来源的插入通知，先平移既有行号再按排序位置插入
void ConversationTimelineModel::onMessagesInserted(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid() || !_messageSource) {
        return;
    }

    // 来源模型插入使其后消息行号整体后移
    const int insertedCount = last - first + 1;
    for (TimelineEntry &entry : _entries) {
        if (entry.isMessage && entry.sourceRow >= first) {
            entry.sourceRow += insertedCount;
        }
    }

    for (int row = first; row <= last; ++row) {
        const TimelineEntry entry = makeMessageEntry(row);
        const int position = insertionPosition(entry.sortTime, true, entry.sourceRow);
        beginInsertRows({}, position, position);
        _entries.insert(position, entry);
        endInsertRows();
        // 插入改变相邻关系，从前一行起重算头像标记
        applyAvatarFlags(qMax(0, position - 1), true);
    }
    emit countChanged();
}

// 处理消息来源的删除通知，倒序移除对应合并行并平移剩余行号
void ConversationTimelineModel::onMessagesRemoved(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid() || !_messageSource) {
        return;
    }

    QList<int> rowsToRemove;
    for (int mergedRow = 0; mergedRow < _entries.size(); ++mergedRow) {
        const TimelineEntry &entry = _entries.at(mergedRow);
        if (entry.isMessage && entry.sourceRow >= first && entry.sourceRow <= last) {
            rowsToRemove.prepend(mergedRow);  // 倒序收集，删除时不影响未处理的行号
        }
    }

    for (int mergedRow : std::as_const(rowsToRemove)) {
        beginRemoveRows({}, mergedRow, mergedRow);
        _entries.removeAt(mergedRow);
        endRemoveRows();
        applyAvatarFlags(qMax(0, mergedRow - 1), true);
    }

    const int removedCount = last - first + 1;
    for (TimelineEntry &entry : _entries) {
        if (entry.isMessage && entry.sourceRow > last) {
            entry.sourceRow -= removedCount;
        }
    }

    if (!rowsToRemove.isEmpty()) {
        emit countChanged();
    }
}

// 处理消息来源的行更新通知，只刷新对应合并行
void ConversationTimelineModel::onMessagesDataChanged(const QModelIndex &topLeft,
                                                      const QModelIndex &bottomRight,
                                                      const QList<int> &roles)
{
    Q_UNUSED(roles);
    if (topLeft.parent().isValid() || !_messageSource) {
        return;
    }

    for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
        const int mergedRow = mergedRowForMessage(row);
        if (mergedRow < 0) {
            // 合并状态与来源不同步时退回全量重建兜底
            rebuildFromSources();
            return;
        }

        _entries[mergedRow].data = readMessageRow(row);
        _entries[mergedRow].sortTime = timestampOf(
            _entries[mergedRow].data.value(QStringLiteral("sentAt")).toString());
        emit dataChanged(index(mergedRow), index(mergedRow));
    }
}

// 处理会话来源的插入通知，过滤其他设备后按排序位置插入
void ConversationTimelineModel::onSessionsInserted(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid() || !_transferSource) {
        return;
    }

    for (int row = first; row <= last; ++row) {
        const QVariantMap session = readSessionRow(row);
        if (!sessionBelongsToDevice(session)) {
            continue;
        }

        TimelineEntry entry;
        entry.isMessage = false;
        entry.sourceRow = row;
        entry.sortTime = timestampOf(session.value(kCreatedAt).toString());
        entry.data = session;

        const int position = insertionPosition(entry.sortTime, false, entry.sourceRow);
        beginInsertRows({}, position, position);
        _entries.insert(position, entry);
        endInsertRows();
        // 会话行不参与头像分组，但会打断相邻消息的连续性
        applyAvatarFlags(qMax(0, position - 1), true);
    }
    emit countChanged();
}

// 处理会话来源的删除通知，按会话标识核对后移除对应合并行
void ConversationTimelineModel::onSessionsRemoved(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    if (parent.isValid() || !_transferSource) {
        return;
    }

    // 删除通知拿不到被移除的数据，逐行核对来源模型中已不存在的会话
    QList<int> rowsToRemove;
    for (int mergedRow = 0; mergedRow < _entries.size(); ++mergedRow) {
        const TimelineEntry &entry = _entries.at(mergedRow);
        if (!entry.isMessage
                && !_transferSource->hasSession(entry.data.value(kSessionId).toString())) {
            rowsToRemove.prepend(mergedRow);
        }
    }

    for (int mergedRow : std::as_const(rowsToRemove)) {
        beginRemoveRows({}, mergedRow, mergedRow);
        _entries.removeAt(mergedRow);
        endRemoveRows();
    }

    if (!rowsToRemove.isEmpty()) {
        applyAvatarFlags(0, true);  // 相邻消息的连续性可能被移除的会话行改变
        emit countChanged();
    }
}

// 处理会话来源的行更新通知，进度刷新只更新对应合并行
void ConversationTimelineModel::onSessionsDataChanged(const QModelIndex &topLeft,
                                                      const QModelIndex &bottomRight,
                                                      const QList<int> &roles)
{
    Q_UNUSED(roles);
    if (topLeft.parent().isValid() || !_transferSource) {
        return;
    }

    for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
        const QString sessionId = _transferSource->data(
            _transferSource->index(row, 0), TransferSessionModel::SessionIdRole).toString();
        const int mergedRow = mergedRowForSession(sessionId);
        if (mergedRow < 0) {
            // 合并状态与来源不同步时退回全量重建兜底
            rebuildFromSources();
            return;
        }

        _entries[mergedRow].data = readSessionRow(row);
        _entries[mergedRow].sortTime = timestampOf(
            _entries[mergedRow].data.value(kCreatedAt).toString());
        emit dataChanged(index(mergedRow), index(mergedRow));
    }
}

// 构造消息来源指定行的合并条目
ConversationTimelineModel::TimelineEntry ConversationTimelineModel::makeMessageEntry(int row) const
{
    TimelineEntry entry;
    entry.isMessage = true;
    entry.sourceRow = row;
    entry.data = readMessageRow(row);
    entry.sortTime = timestampOf(entry.data.value(QStringLiteral("sentAt")).toString());
    return entry;
}

// 读取消息来源指定行的数据快照
QVariantMap ConversationTimelineModel::readMessageRow(int row) const
{
    const QModelIndex modelIndex = _messageSource->index(row, 0);
    return {
        {QStringLiteral("messageId"),  _messageSource->data(modelIndex, ChatMessageModel::MessageIdRole)},
        {QStringLiteral("senderName"), _messageSource->data(modelIndex, ChatMessageModel::SenderNameRole)},
        {QStringLiteral("content"),    _messageSource->data(modelIndex, ChatMessageModel::ContentRole)},
        {QStringLiteral("sentAt"),     _messageSource->data(modelIndex, ChatMessageModel::SentAtRole)},
        {QStringLiteral("isOutgoing"), _messageSource->data(modelIndex, ChatMessageModel::IsOutgoingRole)},
        {QStringLiteral("status"),     _messageSource->data(modelIndex, ChatMessageModel::StatusRole)},
    };
}

// 读取会话来源指定行的数据快照
QVariantMap ConversationTimelineModel::readSessionRow(int row) const
{
    const QModelIndex modelIndex = _transferSource->index(row, 0);
    return {
        {kSessionId,          _transferSource->data(modelIndex, TransferSessionModel::SessionIdRole)},
        {kType,               _transferSource->data(modelIndex, TransferSessionModel::TypeRole)},
        {kDeviceId,           _transferSource->data(modelIndex, TransferSessionModel::DeviceIdRole)},
        {kFileName,           _transferSource->data(modelIndex, TransferSessionModel::FileNameRole)},
        {kStatus,             _transferSource->data(modelIndex, TransferSessionModel::StatusRole)},
        {kProgress,           _transferSource->data(modelIndex, TransferSessionModel::ProgressRole)},
        {kBytesTransferred,   _transferSource->data(modelIndex, TransferSessionModel::BytesTransferredRole)},
        {kTotalBytes,         _transferSource->data(modelIndex, TransferSessionModel::TotalBytesRole)},
        {kCreatedAt,          _transferSource->data(modelIndex, TransferSessionModel::CreatedAtRole)},
        {kPeerDeviceName,     _transferSource->data(modelIndex, TransferSessionModel::PeerDeviceNameRole)},
        {kIsDirectory,        _transferSource->data(modelIndex, TransferSessionModel::IsDirectoryRole)},
        {kFileList,           _transferSource->data(modelIndex, TransferSessionModel::FileListRole)},
        {kCanDeleteLocalFile, _transferSource->data(modelIndex, TransferSessionModel::CanDeleteLocalFileRole)},
        {kErrorMsg,           _transferSource->data(modelIndex, TransferSessionModel::ErrorMsgRole)},
    };
}

// 判断会话是否属于时间线当前设备
bool ConversationTimelineModel::sessionBelongsToDevice(const QVariantMap &session) const
{
    return !_deviceId.isEmpty()
           && session.value(kDeviceId).toString() == _deviceId;
}

// 返回按时间与稳定顺序的插入位置
int ConversationTimelineModel::insertionPosition(qint64 sortTime, bool isMessage, int sourceRow) const
{
    int position = 0;
    while (position < _entries.size()) {
        const TimelineEntry &entry = _entries.at(position);
        if (entry.sortTime > sortTime) {
            break;
        }
        if (entry.sortTime == sortTime) {
            // 同时间戳消息排在传输之前，同类型按来源行号稳定排序
            if (!entry.isMessage && isMessage) {
                break;
            }
            if (entry.isMessage == isMessage && entry.sourceRow >= sourceRow) {
                break;
            }
        }
        ++position;
    }
    return position;
}

// 返回指定来源行号对应的合并行；不存在返回 -1
int ConversationTimelineModel::mergedRowForMessage(int sourceRow) const
{
    for (int mergedRow = 0; mergedRow < _entries.size(); ++mergedRow) {
        const TimelineEntry &entry = _entries.at(mergedRow);
        if (entry.isMessage && entry.sourceRow == sourceRow) {
            return mergedRow;
        }
    }
    return -1;
}

// 返回指定会话标识对应的合并行；不存在返回 -1
int ConversationTimelineModel::mergedRowForSession(const QString &sessionId) const
{
    for (int mergedRow = 0; mergedRow < _entries.size(); ++mergedRow) {
        const TimelineEntry &entry = _entries.at(mergedRow);
        if (!entry.isMessage && entry.data.value(kSessionId).toString() == sessionId) {
            return mergedRow;
        }
    }
    return -1;
}

// 从 fromRow 起重算消息头像标记，notify 为真时对变化的行发出通知
void ConversationTimelineModel::applyAvatarFlags(int fromRow, bool notify)
{
    for (int row = qMax(0, fromRow); row < _entries.size(); ++row) {
        if (!_entries.at(row).isMessage) {
            continue;
        }

        bool show = row == 0;
        if (row > 0) {
            // 连续同方向同发送者的消息只在首条显示头像
            const TimelineEntry &previous = _entries.at(row - 1);
            const TimelineEntry &current = _entries.at(row);
            show = !previous.isMessage
                   || previous.data.value(QStringLiteral("isOutgoing")).toBool()
                      != current.data.value(QStringLiteral("isOutgoing")).toBool()
                   || previous.data.value(QStringLiteral("senderName")).toString()
                      != current.data.value(QStringLiteral("senderName")).toString();
        }

        if (_entries[row].showAvatar == show) {
            continue;
        }

        _entries[row].showAvatar = show;
        if (notify) {
            emit dataChanged(index(row), index(row), {ShowAvatarRole});
        }
    }
}

// 将 ISO 时间字符串转为毫秒时间戳，解析失败返回 0
qint64 ConversationTimelineModel::timestampOf(const QString &isoTime)
{
    const QDateTime parsed = QDateTime::fromString(isoTime, Qt::ISODateWithMs);
    return parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;
}
