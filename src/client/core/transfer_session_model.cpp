/**
* @file    transfer_session_model.cpp
* @version 7.10.0
* @date    2026-10-02
* @author  GridYard Team
* @brief   传输会话列表模型实现
*
* Change Log:
* [v7.10.0] GY   2026-10-02
* * 自 TransferSessionManager 拆出会话列表模型，替代 QVariantList 全量重建
*/

#include "transfer_session_model.h"

#include <QVariantList>

using namespace gy::session;

// 构造函数
TransferSessionModel::TransferSessionModel(QObject *parent)
    : QAbstractListModel{parent}
{
}

// 返回模型中的会话数量
int TransferSessionModel::count() const
{
    return _sessions.size();
}

// 返回模型行数
int TransferSessionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : _sessions.size();
}

// 返回指定角色的会话字段；行中缺失的字段返回无效 QVariant，与旧 map 行为一致
QVariant TransferSessionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= _sessions.size()) {
        return {};
    }
    const char *key = keyForRole(role);
    if (!key) {
        return {};
    }
    return _sessions.at(index.row()).value(key);
}

// 返回 QML delegate 使用的角色名称
QHash<int, QByteArray> TransferSessionModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    for (int role = SessionIdRole; role <= TotalFilesRole; ++role) {
        if (const char *key = keyForRole(role)) {
            roles.insert(role, key);
        }
    }
    return roles;
}

// 追加一条会话并发出精确插入通知
int TransferSessionModel::appendSession(const QVariantMap &session)
{
    const int row = _sessions.size();
    beginInsertRows({}, row, row);
    _sessions.append(session);
    endInsertRows();
    emit countChanged();
    return row;
}

// 就地编辑指定会话并发出该行的 dataChanged
bool TransferSessionModel::updateSession(const QString &sessionId,
                                         const std::function<void(QVariantMap &)> &editor)
{
    if (!editor) {
        return false;
    }
    for (int row = 0; row < _sessions.size(); ++row) {
        if (_sessions.at(row).value(kSessionId).toString() != sessionId) {
            continue;
        }
        editor(_sessions[row]);
        emit dataChanged(index(row), index(row));
        return true;
    }
    return false;
}

// 移除指定会话并发出精确删除通知
bool TransferSessionModel::removeSession(const QString &sessionId)
{
    for (int row = 0; row < _sessions.size(); ++row) {
        if (_sessions.at(row).value(kSessionId).toString() != sessionId) {
            continue;
        }
        beginRemoveRows({}, row, row);
        _sessions.removeAt(row);
        endRemoveRows();
        emit countChanged();
        return true;
    }
    return false;
}

// 按条件批量移除会话，返回被移除的会话快照
QList<QVariantMap> TransferSessionModel::takeSessionsWhere(
    const std::function<bool(const QVariantMap &)> &predicate)
{
    QList<QVariantMap> removed;
    if (!predicate) {
        return removed;
    }

    // 倒序移除，避免删除元素后改变后续索引
    for (int row = _sessions.size() - 1; row >= 0; --row) {
        if (!predicate(_sessions.at(row))) {
            continue;
        }
        beginRemoveRows({}, row, row);
        removed.prepend(_sessions.takeAt(row));
        endRemoveRows();
    }

    if (!removed.isEmpty()) {
        emit countChanged();
    }
    return removed;
}

// 判断会话是否存在
bool TransferSessionModel::hasSession(const QString &sessionId) const
{
    for (const QVariantMap &session : _sessions) {
        if (session.value(kSessionId).toString() == sessionId) {
            return true;
        }
    }
    return false;
}

// 返回指定会话的快照；不存在时返回空 map
QVariantMap TransferSessionModel::sessionById(const QString &sessionId) const
{
    for (const QVariantMap &session : _sessions) {
        if (session.value(kSessionId).toString() == sessionId) {
            return session;
        }
    }
    return {};
}

// 返回兼容旧调用方的会话快照列表
QVariantList TransferSessionModel::sessions() const
{
    QVariantList list;
    list.reserve(_sessions.size());
    for (const QVariantMap &session : _sessions) {
        list.append(session);
    }
    return list;
}

// 返回角色对应会话字段名；未知角色返回空指针
const char *TransferSessionModel::keyForRole(int role)
{
    switch (role) {
    case SessionIdRole:          return kSessionId;
    case TypeRole:               return kType;
    case DeviceIdRole:           return kDeviceId;
    case PeerDeviceNameRole:     return kPeerDeviceName;
    case FilePathRole:           return kFilePath;
    case FileNameRole:           return kFileName;
    case IsDirectoryRole:        return kIsDirectory;
    case FileCountRole:          return kFileCount;
    case StatusRole:             return kStatus;
    case ProgressRole:           return kProgress;
    case BytesTransferredRole:   return kBytesTransferred;
    case TotalBytesRole:         return kTotalBytes;
    case CreatedAtRole:          return kCreatedAt;
    case FileListRole:           return kFileList;
    case LocalPathRole:          return kLocalPath;
    case CanDeleteLocalFileRole: return kCanDeleteLocalFile;
    case SenderDeviceIdRole:     return kSenderDeviceId;
    case SenderNameRole:         return kSenderName;
    case ErrorMsgRole:           return kErrorMsg;
    case ErrorCodeRole:          return kErrorCode;
    case RecordIdRole:           return kRecordId;
    case RelayIdRole:            return kRelayId;
    case FileSizeRole:           return kFileSize;
    case TotalFilesRole:         return kTotalFiles;
    default:                     return nullptr;
    }
}
