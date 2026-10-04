/**
* @file    sqlite_transfer_history_repository.cpp
* @version 7.18.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   SQLite 传输历史 Repository 实现
*
* 只保存最终状态快照，不保存发送源绝对路径、文件内容或调试堆栈。
*
* Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 新增 deleteForDeviceStep 按设备删除传输历史，不自开事务
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
* [v7.15.5] GY   2026-10-03
* * upsertFinishedTransfer 复用 upsertFinishedTransferStep（ADR-006 方案 c）
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.3.0] GY 2026-06-25
* * 新增传输历史 SQLite Repository
*/

#include "sqlite_transfer_history_repository.h"

#include "protocol.h"
#include "sqlite_database_broker.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {
// 将 UTC 时间转换为 SQLite 使用的 ISO 文本
QString sqlTime(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}
}

// 构造传输历史 Repository
SqliteTransferHistoryRepository::SqliteTransferHistoryRepository(SqliteDatabaseBroker *database)
    : _database(database)
{
}

// 幂等保存结束态传输记录
bool SqliteTransferHistoryRepository::upsertFinishedTransfer(const TransferRecord &record,
                                                        QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    // SQL 与组合事务共用同一 Step，避免双份字面量漂移
    return _database->runInTransaction(upsertFinishedTransferStep(record), errorMessage);
}

// 按设备、状态和时间游标查询一页历史
QList<TransferRecord> SqliteTransferHistoryRepository::queryTransfers(const TransferQuery &query,
                                                                 int limit,
                                                                 QString *errorMessage) const
{
    QList<TransferRecord> records;
    if (limit <= 0) {
        return records;
    }
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }

    QSqlDatabase database = _database->connectionForWorkerThread(errorMessage);
    if (!database.isValid()) {
        return records;
    }

    // 筛选条件按需拼接，参数值仍使用 bindValue，避免把用户文本拼进 SQL。
    QString statement =
        "SELECT record_id, session_id, peer_device_id, peer_name, direction, display_name, "
        "is_directory, file_count, total_bytes, status, started_at, finished_at, error_code, "
        "error_message FROM transfer_history";
    QStringList filters;
    if (!query.peerDeviceId.isEmpty()) {
        filters.append("peer_device_id=?");
    }
    if (!query.status.isEmpty()) {
        filters.append("status=?");
    }
    if (query.beforeStartedAt.isValid()) {
        // 次键游标与 ORDER BY (started_at, record_id) 完全对齐，同毫秒多条记录翻页不重不漏
        if (query.beforeRecordId.isEmpty()) {
            filters.append("started_at < ?");
        } else {
            filters.append("(started_at < ? OR (started_at = ? AND record_id < ?))");
        }
    }
    if (!filters.isEmpty()) {
        statement += " WHERE " + filters.join(" AND ");
    }
    statement += " ORDER BY started_at DESC, record_id DESC LIMIT ?";

    QSqlQuery sqlQuery(database);
    sqlQuery.prepare(statement);
    if (!query.peerDeviceId.isEmpty()) {
        sqlQuery.addBindValue(query.peerDeviceId);
    }
    if (!query.status.isEmpty()) {
        sqlQuery.addBindValue(query.status);
    }
    if (query.beforeStartedAt.isValid()) {
        if (query.beforeRecordId.isEmpty()) {
            sqlQuery.addBindValue(sqlTime(query.beforeStartedAt));
        } else {
            sqlQuery.addBindValue(sqlTime(query.beforeStartedAt));
            sqlQuery.addBindValue(sqlTime(query.beforeStartedAt));
            sqlQuery.addBindValue(query.beforeRecordId);
        }
    }
    sqlQuery.addBindValue(limit);

    if (!sqlQuery.exec()) {
        if (errorMessage) {
            *errorMessage = sqlQuery.lastError().text();
        }
        return records;
    }

    while (sqlQuery.next()) {
        // 存储层负责字段映射和类型转换，应用层只处理 TransferRecord。
        TransferRecord record;
        record.recordId = sqlQuery.value(0).toString();
        record.sessionId = sqlQuery.value(1).toString();
        record.peerDeviceId = sqlQuery.value(2).toString();
        record.peerName = sqlQuery.value(3).toString();
        record.direction = static_cast<RecordDirection>(sqlQuery.value(4).toInt());
        record.displayName = sqlQuery.value(5).toString();
        record.isDirectory = sqlQuery.value(6).toInt() != 0;
        record.fileCount = sqlQuery.value(7).toInt();
        record.totalBytes = sqlQuery.value(8).toLongLong();
        record.status = sqlQuery.value(9).toString();
        record.startedAt = QDateTime::fromString(sqlQuery.value(10).toString(), Qt::ISODateWithMs);
        record.finishedAt = QDateTime::fromString(sqlQuery.value(11).toString(), Qt::ISODateWithMs);
        record.errorCode = sqlQuery.value(12).isNull() ? 0 : sqlQuery.value(12).toInt();
        record.errorMessage = sqlQuery.value(13).toString();
        records.append(record);
    }

    return records;
}

// 删除单条传输历史
bool SqliteTransferHistoryRepository::deleteTransfer(const QString &recordId, QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&recordId](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            // 这里只删除历史行，本地或发送源文件均不在数据库职责范围内。
            query.prepare("DELETE FROM transfer_history WHERE record_id=?");
            query.addBindValue(recordId);
            if (query.exec()) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}

// 删除早于指定时间的传输历史
bool SqliteTransferHistoryRepository::deleteExpiredTransfers(const QDateTime &before,
                                                        QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&before](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            // 保留边界时刻记录，清理只删除严格早于保留期限的历史。
            query.prepare("DELETE FROM transfer_history WHERE started_at < ?");
            query.addBindValue(sqlTime(before));
            if (query.exec()) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}

// 纯 SQL 幂等写入步骤，不自开事务；供 LocalDataBroker 在单个事务内组合
SqliteTransferHistoryRepository::SqlStep
SqliteTransferHistoryRepository::upsertFinishedTransferStep(const TransferRecord &record)
{
    return[record](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        // session_id 幂等键，重复写入覆盖为最新快照
        query.prepare(
            "INSERT INTO transfer_history(record_id, session_id, peer_device_id, peer_name, "
            "direction, display_name, is_directory, file_count, total_bytes, status, "
            "started_at, finished_at, error_code, error_message) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(session_id) DO UPDATE SET "
            "peer_device_id=excluded.peer_device_id, "
            "peer_name=excluded.peer_name, "
            "direction=excluded.direction, "
            "display_name=excluded.display_name, "
            "is_directory=excluded.is_directory, "
            "file_count=excluded.file_count, "
            "total_bytes=excluded.total_bytes, "
            "status=excluded.status, "
            "started_at=excluded.started_at, "
            "finished_at=excluded.finished_at, "
            "error_code=excluded.error_code, "
            "error_message=excluded.error_message");
        query.addBindValue(record.recordId);
        query.addBindValue(record.sessionId);
        query.addBindValue(record.peerDeviceId);
        query.addBindValue(record.peerName);
        query.addBindValue(static_cast<int>(record.direction));
        query.addBindValue(record.displayName);
        query.addBindValue(record.isDirectory ? 1 : 0);
        query.addBindValue(record.fileCount);
        query.addBindValue(record.totalBytes);
        query.addBindValue(record.status);
        query.addBindValue(sqlTime(record.startedAt));
        query.addBindValue(record.finishedAt.isValid()
                               ? QVariant(sqlTime(record.finishedAt))
                               : QVariant{});
        query.addBindValue(record.status == gy::protocol::kTransferStatusCompleted
                               ? QVariant{}
                               : QVariant(record.errorCode));
        query.addBindValue(record.status == gy::protocol::kTransferStatusCompleted || record.errorMessage.isEmpty()
                               ? QVariant{}
                               : QVariant(record.errorMessage));
        if (query.exec())
            return true;
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

// 清空全部传输历史
bool SqliteTransferHistoryRepository::clearAllTransfers(QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            // 清空历史只删除数据库记录，不触碰已接收或已发送的本地文件。
            if (query.exec("DELETE FROM transfer_history")) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}

// 删除指定设备的全部传输历史，不自开事务；供设备删除的组合事务调用
SqliteTransferHistoryRepository::SqlStep
SqliteTransferHistoryRepository::deleteForDeviceStep(const QString &deviceId)
{
    return[deviceId](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        // 传输历史对设备行是 RESTRICT 外键，删除设备前必须先清空本表对应行
        query.prepare("DELETE FROM transfer_history WHERE peer_device_id=?");
        query.addBindValue(deviceId);
        if (query.exec())
            return true;  // 无历史记录时幂等成功
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}
