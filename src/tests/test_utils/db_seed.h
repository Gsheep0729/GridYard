/**
* @file    db_seed.h
* @version 7.15.10
* @date    2026-10-03
* @author  GY
* @brief   测试用本地历史库打开与种子数据工具
*
* 收拢存储类测试重复的临时库打开（含 Schema 迁移）、设备/消息/传输
* 种子写入和表行数查询；种子字段值由调用方给定，保持各测试原有数据形态。
*
* Change Log:
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.7] GY   2026-10-03
* * 从 test_history_controller / test_storage_message / test_storage_transfer_history / test_storage_device 抽取
*/

#pragma once

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <memory>

#include "history_records.h"
#include "sqlite_database_broker.h"
#include "sqlite_device_repository.h"
#include "sqlite_message_repository.h"
#include "sqlite_transfer_history_repository.h"

namespace gy::test {

// 打开（或创建）basePath 下相对路径的临时历史库，失败返回空指针
inline std::unique_ptr<SqliteDatabaseBroker> openDatabase(const QString &basePath,
                                                          const QString &relativePath)
{
    auto database = std::make_unique<SqliteDatabaseBroker>();
    QString error;
    if (!database->initialize(basePath + "/" + relativePath, &error)) {
        qWarning() << "数据库初始化失败:" << error;
        return nullptr;
    }
    return database;
}

// 写入一条设备记录（消息/传输表的外键依赖）
inline bool seedDevice(SqliteDatabaseBroker &database, const QString &deviceId,
                       const QString &deviceName, const QDateTime &firstSeenAt,
                       const QString &lastIpAddress = {}, quint16 lastTcpPort = 0)
{
    SqliteDeviceRepository repository(&database);
    PeerRecord peer;
    peer.deviceId = deviceId;
    peer.deviceName = deviceName;
    peer.lastIpAddress = lastIpAddress;
    peer.lastTcpPort = lastTcpPort;
    peer.firstSeenAt = firstSeenAt;
    peer.lastSeenAt = firstSeenAt;
    QString error;
    return repository.upsertPeer(peer, &error);
}

// 写入一条聊天消息记录
inline bool seedMessage(SqliteDatabaseBroker &database, const MessageRecord &record)
{
    SqliteMessageRepository repository(&database);
    QString error;
    return repository.saveMessage(record, &error);
}

// 向已有消息 Repository 写入一条记录
inline bool seedMessage(SqliteMessageRepository &repository, const MessageRecord &record)
{
    QString error;
    return repository.saveMessage(record, &error);
}

// 向已有传输历史 Repository 写入一条结束态记录
inline bool seedTransfer(SqliteTransferHistoryRepository &repository, const TransferRecord &record)
{
    QString error;
    return repository.upsertFinishedTransfer(record, &error);
}

// 统计指定表的行数，查询失败返回 -1
inline int tableRowCount(SqliteDatabaseBroker &database, const QString &tableName)
{
    QString error;
    QSqlDatabase connection = database.connectionForWorkerThread(&error);
    if (!connection.isValid()) {
        return -1;
    }
    QSqlQuery query(connection);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName))) {
        return -1;
    }
    if (!query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

}
