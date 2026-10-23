/**
* @file    transfer_repository_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SQLite 传输历史仓储实现
*
* SQL 逐字复用 Qt 版字面量；筛选条件按需拼接，参数值一律预编译绑定。
*/

#include "core/storage/transfer_repository_core.h"

#include "core/storage/database_core.h"
#include "core/storage/iso_time.h"
#include "core/storage/sqlite_stmt.h"

#include <optional>

namespace gy::storage {
namespace {

// 将传输历史查询的当前行映射为领域记录，列序与 SELECT 清单一致
TransferRecord readTransferRecordRow(const SqliteStmt &query)
{
    TransferRecord record;
    record.recordId = query.columnText(0);
    record.sessionId = query.columnText(1);
    record.peerDeviceId = query.columnText(2);
    record.peerName = query.columnText(3);
    record.direction = static_cast<RecordDirection>(query.columnInt64(4));
    record.displayName = query.columnText(5);
    record.isDirectory = query.columnInt64(6) != 0;
    record.fileCount = static_cast<int>(query.columnInt64(7));
    record.totalBytes = query.columnInt64(8);
    record.status = query.columnText(9);
    record.startedAtMs = query.columnIsNull(10)
        ? 0 : parseIso8601Ms(query.columnText(10)).value_or(0);
    record.finishedAtMs = query.columnIsNull(11)
        ? std::nullopt : parseIso8601Ms(query.columnText(11));
    record.errorCode = query.columnIsNull(12) ? 0 : static_cast<int>(query.columnInt64(12));
    record.errorMessage = query.columnText(13);
    return record;
}

}  // namespace

// 构造函数
TransferRepositoryCore::TransferRepositoryCore(DatabaseCore *database) : _database(database)
{
}

// 幂等保存结束态传输记录
bool TransferRepositoryCore::upsertFinishedTransfer(const TransferRecord &record,
                                                    std::string *errorMessage)
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

// 加载全部传输历史（备份导出用），按 (started_at, record_id) 排序保证顺序稳定
std::vector<TransferRecord> TransferRepositoryCore::allTransfers(
    std::string *errorMessage) const
{
    std::vector<TransferRecord> records;
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }
    SqliteStmt query{_database->connection(),
                     "SELECT record_id, session_id, peer_device_id, peer_name, direction, "
                     "display_name, is_directory, file_count, total_bytes, status, started_at, "
                     "finished_at, error_code, error_message FROM transfer_history "
                     "ORDER BY started_at, record_id"};
    auto row = query.step();
    while (row.has_value() && row.value()) {
        // 存储层完成行到领域值对象的映射，列序与 queryTransfers 保持一致
        records.push_back(readTransferRecordRow(query));
        row = query.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }
    return records;
}

// 按设备、状态和时间游标查询一页历史
std::vector<TransferRecord> TransferRepositoryCore::queryTransfers(const TransferQuery &query,
                                                                   int limit,
                                                                   std::string *errorMessage) const
{
    std::vector<TransferRecord> records;
    if (limit <= 0) {
        return records;
    }
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }

    // 筛选条件按需拼接，参数值仍使用预编译绑定，避免把用户文本拼进 SQL。
    std::string statement =
        "SELECT record_id, session_id, peer_device_id, peer_name, direction, display_name, "
        "is_directory, file_count, total_bytes, status, started_at, finished_at, error_code, "
        "error_message FROM transfer_history";
    std::vector<std::string> filters;
    if (!query.peerDeviceId.empty()) {
        filters.push_back("peer_device_id=?");
    }
    if (!query.status.empty()) {
        filters.push_back("status=?");
    }
    if (query.beforeStartedAtMs.has_value()) {
        // 次键游标与 ORDER BY (started_at, record_id) 完全对齐，同毫秒多条记录翻页不重不漏
        if (query.beforeRecordId.empty()) {
            filters.push_back("started_at < ?");
        } else {
            filters.push_back("(started_at < ? OR (started_at = ? AND record_id < ?))");
        }
    }
    if (!filters.empty()) {
        statement += " WHERE ";
        for (size_t i = 0; i < filters.size(); ++i) {
            statement += filters[i];
            if (i + 1 < filters.size()) {
                statement += " AND ";
            }
        }
    }
    statement += " ORDER BY started_at DESC, record_id DESC LIMIT ?";

    SqliteStmt sqlQuery{_database->connection(), statement};
    // 过滤条件的绑定顺序与拼接顺序严格一致，limit 恒在末位
    int bindIndex = 1;
    if (!query.peerDeviceId.empty()) {
        sqlQuery.bind(bindIndex++, query.peerDeviceId);
    }
    if (!query.status.empty()) {
        sqlQuery.bind(bindIndex++, query.status);
    }
    if (query.beforeStartedAtMs.has_value()) {
        if (query.beforeRecordId.empty()) {
            sqlQuery.bind(bindIndex++, toIso8601Ms(*query.beforeStartedAtMs));
        } else {
            sqlQuery.bind(bindIndex++, toIso8601Ms(*query.beforeStartedAtMs));
            sqlQuery.bind(bindIndex++, toIso8601Ms(*query.beforeStartedAtMs));
            sqlQuery.bind(bindIndex++, query.beforeRecordId);
        }
    }
    sqlQuery.bind(bindIndex, limit);

    auto row = sqlQuery.step();
    while (row.has_value() && row.value()) {
        // 存储层负责字段映射和类型转换，应用层只处理领域记录。
        records.push_back(readTransferRecordRow(sqlQuery));
        row = sqlQuery.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }

    return records;
}

// 删除单条传输历史
bool TransferRepositoryCore::deleteTransfer(const std::string &recordId,
                                            std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&recordId](SqliteConn &database, std::string *taskError) {
            SqliteStmt query{database, "DELETE FROM transfer_history WHERE record_id=?"};
            // 这里只删除历史行，本地或发送源文件均不在数据库职责范围内。
            query.bind(1, recordId);
            if (auto row = query.step(); row.has_value()) {
                return true;
            } else if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        },
        errorMessage);
}

// 删除早于指定时间的传输历史
bool TransferRepositoryCore::deleteExpiredTransfers(int64_t beforeMs, std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [beforeMs](SqliteConn &database, std::string *taskError) {
            SqliteStmt query{database, "DELETE FROM transfer_history WHERE started_at < ?"};
            // 保留边界时刻记录，清理只删除严格早于保留期限的历史。
            query.bind(1, toIso8601Ms(beforeMs));
            if (auto row = query.step(); row.has_value()) {
                return true;
            } else if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        },
        errorMessage);
}

// 纯 SQL 幂等写入步骤，不自开事务；供调用方在单个事务内组合
TransferRepositoryCore::SqlStep TransferRepositoryCore::upsertFinishedTransferStep(
    const TransferRecord &record)
{
    return [record](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database,
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
            "error_message=excluded.error_message"};
        // session_id 幂等键，重复写入覆盖为最新快照
        query.bind(1, record.recordId);
        query.bind(2, record.sessionId);
        query.bind(3, record.peerDeviceId);
        query.bind(4, record.peerName);
        query.bind(5, static_cast<int64_t>(record.direction));
        query.bind(6, record.displayName);
        query.bind(7, record.isDirectory ? 1 : 0);
        query.bind(8, record.fileCount);
        query.bind(9, record.totalBytes);
        query.bind(10, record.status);
        query.bind(11, toIso8601Ms(record.startedAtMs));
        if (record.finishedAtMs.has_value()) {
            query.bind(12, toIso8601Ms(*record.finishedAtMs));
        } else {
            query.bindNull(12);
        }
        if (record.status == kTransferStatusCompleted) {
            query.bindNull(13);
        } else {
            query.bind(13, record.errorCode);
        }
        if (record.status == kTransferStatusCompleted || record.errorMessage.empty()) {
            query.bindNull(14);
        } else {
            query.bind(14, record.errorMessage);
        }
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 清空全部传输历史
bool TransferRepositoryCore::clearAllTransfers(std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [](SqliteConn &database, std::string *taskError) {
            // 清空历史只删除数据库记录，不触碰已接收或已发送的本地文件。
            if (database.exec("DELETE FROM transfer_history").has_value()) {
                return true;
            }
            if (taskError) {
                *taskError = database.lastError("清空传输历史失败").message;
            }
            return false;
        },
        errorMessage);
}

// 删除指定设备的全部传输历史，不自开事务；供设备删除的组合事务调用
TransferRepositoryCore::SqlStep TransferRepositoryCore::deleteForDeviceStep(
    const std::string &deviceId)
{
    return [deviceId](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "DELETE FROM transfer_history WHERE peer_device_id=?"};
        // 传输历史对设备行是 RESTRICT 外键，删除设备前必须先清空本表对应行
        query.bind(1, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;  // 无历史记录时幂等成功
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

}  // namespace gy::storage
