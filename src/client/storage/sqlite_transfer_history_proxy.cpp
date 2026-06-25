/**
* @file    sqlite_transfer_history_proxy.cpp
* @version 6.3.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 传输历史 Repository 实现
*
* 只保存最终状态快照，不保存发送源绝对路径、文件内容或调试堆栈。
*
* Change Log:
* [v6.3.0] GY 2026-06-25
* * 新增传输历史 SQLite Proxy
*/

#include "sqlite_transfer_history_proxy.h"

#include "sqlite_database_proxy.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

namespace {
QString sqlTime(const QDateTime &time)
{
    return time.toUTC().toString(Qt::ISODateWithMs);
}
}

SqliteTransferHistoryProxy::SqliteTransferHistoryProxy(SqliteDatabaseProxy *database)
    : _database(database)
{
}

bool SqliteTransferHistoryProxy::upsertFinishedTransfer(const TransferRecord &record,
                                                        QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&record](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            // 同一 session_id 可能因重试或重复完成信号再次写入，这里覆盖为最新快照。
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
            query.addBindValue(record.status == "completed"
                                   ? QVariant{}
                                   : QVariant(record.errorCode));
            query.addBindValue(record.status == "completed" || record.errorMessage.isEmpty()
                                   ? QVariant{}
                                   : QVariant(record.errorMessage));
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

QList<TransferRecord> SqliteTransferHistoryProxy::queryTransfers(const TransferQuery &query,
                                                                 int limit,
                                                                 QString *errorMessage) const
{
    QList<TransferRecord> records;
    if (limit <= 0) {
        return records;
    }
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return records;
    }

    QSqlDatabase database = _database->connectionForWorkerThread(errorMessage);
    if (!database.isValid()) {
        return records;
    }

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
        filters.append("started_at < ?");
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
        sqlQuery.addBindValue(sqlTime(query.beforeStartedAt));
    }
    sqlQuery.addBindValue(limit);

    if (!sqlQuery.exec()) {
        if (errorMessage) {
            *errorMessage = sqlQuery.lastError().text();
        }
        return records;
    }

    while (sqlQuery.next()) {
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

bool SqliteTransferHistoryProxy::deleteTransfer(const QString &recordId, QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
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

bool SqliteTransferHistoryProxy::deleteExpiredTransfers(const QDateTime &before,
                                                        QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&before](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
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

bool SqliteTransferHistoryProxy::clearAllTransfers(QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
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
