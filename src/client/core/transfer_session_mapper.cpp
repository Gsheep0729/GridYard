/**
* @file    transfer_session_mapper.cpp
* @version 7.15.16
* @date    2026-10-04
* @author  GridYard Team
* @brief   传输会话与持久化记录的映射实现
*
* Change Log:
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
* [v7.10.0] GY   2026-10-02
* * 自 TransferSessionManager 拆出记录映射与接收文件清理策略
*/

#include "transfer_session_mapper.h"
#include "transfer_session_model.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

using namespace gy::session;

// 将一条持久化历史记录恢复成 QML 可消费的会话行
QVariantMap TransferSessionMapper::sessionFromRecord(const TransferRecord &record)
{
    QVariantMap session;
    session[kRecordId] = record.recordId;
    session[kSessionId] = record.sessionId;
    session[kType] = record.direction == RecordDirection::Outgoing ? kTypeSend : kTypeReceive;
    session[kDeviceId] = record.peerDeviceId;
    session[kPeerDeviceName] = record.peerName;
    session[kFilePath] = "";
    session[kFileName] = record.displayName;
    session[kIsDirectory] = record.isDirectory;
    session[kFileCount] = record.fileCount;
    session[kStatus] = record.status;
    session[kProgress] = record.status == kStatusCompleted ? 100 : 0;
    session[kBytesTransferred] = record.status == kStatusCompleted ? record.totalBytes : 0;
    session[kTotalBytes] = record.totalBytes;
    session[kCreatedAt] = record.startedAt.toUTC().toString(Qt::ISODateWithMs);
    session[kFileList] = QVariantList{};
    session[kLocalPath] = "";
    session[kCanDeleteLocalFile] = false;
    session[kErrorCode] = record.errorCode;
    session[kErrorMsg] = record.errorMessage;
    return session;
}

// 将已收敛为最终状态的会话行转成可持久化的记录快照
TransferRecord TransferSessionMapper::recordFromSession(const QVariantMap &session)
{
    TransferRecord record;
    record.recordId = session.value(kRecordId).toString();
    record.sessionId = session.value(kSessionId).toString();
    record.peerDeviceId = session.value(kDeviceId).toString();
    record.peerName = session.value(kPeerDeviceName).toString();
    record.direction = session.value(kType).toString() == kTypeSend
                           ? RecordDirection::Outgoing
                           : RecordDirection::Incoming;
    record.displayName = session.value(kFileName).toString();
    record.isDirectory = session.value(kIsDirectory).toBool();
    record.fileCount = session.value(kFileCount).toInt();
    record.totalBytes = session.value(kTotalBytes).toLongLong();
    record.status = session.value(kStatus).toString();
    record.startedAt = QDateTime::fromString(session.value(kCreatedAt).toString(), Qt::ISODateWithMs);
    record.finishedAt = QDateTime::currentDateTimeUtc();

    const bool completed = record.status == kStatusCompleted;
    record.errorCode = completed ? 0 : session.value(kErrorCode).toInt();
    record.errorMessage = completed ? QString{} : session.value(kErrorMsg).toString();
    return record;
}

// 按安全策略删除已接收的本地文件；失败时经 failedPath 回传路径
TransferSessionMapper::DeleteResult TransferSessionMapper::deleteReceivedFile(
    const QVariantMap &session, QString *failedPath)
{
    const bool eligible = session.value(kCanDeleteLocalFile).toBool()
                          && session.value(kType).toString() == kTypeReceive
                          && session.value(kStatus).toString() == kStatusCompleted;
    if (!eligible) {
        return DeleteResult::NotEligible;
    }

    const QString localPath = QDir::cleanPath(session.value(kLocalPath).toString());
    const QFileInfo info{localPath};
    // 路径必须是绝对路径且不能是文件系统根，防止误删整个目录
    if (!info.isAbsolute() || localPath == QDir::rootPath() || !info.exists()) {
        return DeleteResult::InvalidPath;
    }

    // 符号链接按文件删除，避免递归进入链接目标
    const bool removed = info.isDir() && !info.isSymLink()
                         ? QDir{localPath}.removeRecursively()
                         : QFile::remove(localPath);
    if (!removed) {
        if (failedPath) {
            *failedPath = localPath;
        }
        return DeleteResult::RemoveFailed;
    }
    return DeleteResult::Deleted;
}
