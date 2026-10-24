/**
* @file    db_seed.h
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   测试用本地历史库打开与种子数据工具
*
* 收拢存储类测试重复的临时库打开（含 Schema 迁移）、设备/消息/传输
* 种子写入和表行数查询；底层走纯 C++ 存储核心（gy_storage_core），
* Qt 领域记录在种子边界做机械互转，与 LocalDataBroker 任务边界同口径；
* 种子字段值由调用方给定，保持各测试原有数据形态。
*/

#pragma once

#include <QDateTime>
#include <QString>
#include <QtGlobal>

#include <memory>

#include "history_records.h"

#include "core/storage/database_core.h"
#include "core/storage/device_repository_core.h"
#include "core/storage/message_repository_core.h"
#include "core/storage/sqlite_stmt.h"
#include "core/storage/transfer_repository_core.h"

namespace gy::test {

// 打开（或创建）basePath 下相对路径的临时历史库，失败返回空指针
inline std::unique_ptr<gy::storage::DatabaseCore> openDatabase(const QString &basePath,
                                                               const QString &relativePath)
{
    auto database = std::make_unique<gy::storage::DatabaseCore>();
    std::string error;
    if (!database->initialize((basePath + "/" + relativePath).toStdString(), &error)) {
        qWarning() << "数据库初始化失败:" << QString::fromStdString(error);
        return nullptr;
    }
    return database;
}

// 无效 QDateTime 归一为核心记录的可选空值，有效时间换 Unix 毫秒
inline std::optional<int64_t> toOptionalMillis(const QDateTime &time)
{
    return time.isValid() ? std::optional<int64_t>{time.toMSecsSinceEpoch()} : std::nullopt;
}

// Qt 设备记录转核心记录
inline gy::storage::PeerRecord toCoreRecord(const PeerRecord &record)
{
    gy::storage::PeerRecord core;
    core.deviceId = record.deviceId.toStdString();
    core.deviceName = record.deviceName.toStdString();
    core.lastIpAddress = record.lastIpAddress.toStdString();
    core.lastTcpPort = record.lastTcpPort;
    core.firstSeenAtMs = record.firstSeenAt.toMSecsSinceEpoch();
    core.lastSeenAtMs = record.lastSeenAt.toMSecsSinceEpoch();
    core.lastChatAtMs = toOptionalMillis(record.lastChatAt);
    core.lastTransferAtMs = toOptionalMillis(record.lastTransferAt);
    core.alias = record.alias.toStdString();
    core.pinned = record.pinned;
    core.hidden = record.hidden;
    core.favorite = record.favorite;
    return core;
}

// Qt 消息记录转核心记录
inline gy::storage::MessageRecord toCoreRecord(const MessageRecord &record)
{
    gy::storage::MessageRecord core;
    core.messageId = record.messageId.toStdString();
    core.peerDeviceId = record.peerDeviceId.toStdString();
    core.direction = static_cast<gy::storage::RecordDirection>(static_cast<int>(record.direction));
    core.senderDeviceId = record.senderDeviceId.toStdString();
    core.senderName = record.senderName.toStdString();
    core.content = record.content.toStdString();
    core.sentAtMs = record.sentAt.toMSecsSinceEpoch();
    core.localStatus = record.localStatus;
    core.createdAtMs = record.createdAt.toMSecsSinceEpoch();
    return core;
}

// Qt 传输记录转核心记录
inline gy::storage::TransferRecord toCoreRecord(const TransferRecord &record)
{
    gy::storage::TransferRecord core;
    core.recordId = record.recordId.toStdString();
    core.sessionId = record.sessionId.toStdString();
    core.peerDeviceId = record.peerDeviceId.toStdString();
    core.peerName = record.peerName.toStdString();
    core.direction = static_cast<gy::storage::RecordDirection>(static_cast<int>(record.direction));
    core.displayName = record.displayName.toStdString();
    core.isDirectory = record.isDirectory;
    core.fileCount = record.fileCount;
    core.totalBytes = record.totalBytes;
    core.status = record.status.toStdString();
    core.startedAtMs = record.startedAt.toMSecsSinceEpoch();
    core.finishedAtMs = toOptionalMillis(record.finishedAt);
    core.errorCode = record.errorCode;
    core.errorMessage = record.errorMessage.toStdString();
    return core;
}

// 写入一条设备记录（消息/传输表的外键依赖）
inline bool seedDevice(gy::storage::DatabaseCore &database, const QString &deviceId,
                       const QString &deviceName, const QDateTime &firstSeenAt,
                       const QString &lastIpAddress = {}, quint16 lastTcpPort = 0)
{
    gy::storage::DeviceRepositoryCore repository(&database);
    PeerRecord peer;
    peer.deviceId = deviceId;
    peer.deviceName = deviceName;
    peer.lastIpAddress = lastIpAddress;
    peer.lastTcpPort = lastTcpPort;
    peer.firstSeenAt = firstSeenAt;
    peer.lastSeenAt = firstSeenAt;
    std::string error;
    return repository.upsertPeer(toCoreRecord(peer), &error);
}

// 向消息仓储写入一条记录
inline bool seedMessage(gy::storage::MessageRepositoryCore &repository, const MessageRecord &record)
{
    std::string error;
    return repository.saveMessage(toCoreRecord(record), &error);
}

// 向传输历史仓储写入一条结束态记录
inline bool seedTransfer(gy::storage::TransferRepositoryCore &repository,
                         const TransferRecord &record)
{
    std::string error;
    return repository.upsertFinishedTransfer(toCoreRecord(record), &error);
}

// 统计指定表的行数，查询失败返回 -1
inline int tableRowCount(gy::storage::DatabaseCore &database, const QString &tableName)
{
    gy::storage::SqliteStmt query{database.connection(),
                                  "SELECT COUNT(*) FROM " + tableName.toStdString()};
    const auto row = query.step();
    if (!row.has_value() || !row.value()) {
        return -1;
    }
    return static_cast<int>(query.columnInt64(0));
}

}
