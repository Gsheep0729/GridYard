/**
* @file    transfer_repository_core.h
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   纯 C++23 SQLite 传输历史仓储——不依赖任何 Qt 类型
*
* 只保存最终状态快照，不保存发送源绝对路径、文件内容或调试堆栈；
* SQL 语义由存储测试锁定。
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/storage/storage_records.h"

namespace gy::storage {

class DatabaseCore;
class SqliteConn;

// 传输历史仓储纯 C++ 实现
class TransferRepositoryCore {
public:
    // 只执行 SQL、不自开事务的任务，供调用方把多表写入组合进单个事务
    using SqlStep = std::function<bool(SqliteConn &, std::string *)>;

    // 构造传输历史仓储
    explicit TransferRepositoryCore(DatabaseCore *database);

    // 以 session_id 为幂等键保存结束态传输记录
    bool upsertFinishedTransfer(const TransferRecord &record, std::string *errorMessage);
    // 按设备、状态和时间游标查询一页历史
    std::vector<TransferRecord> queryTransfers(const TransferQuery &query, int limit,
                                               std::string *errorMessage) const;
    // 加载全部传输历史（备份导出用）
    std::vector<TransferRecord> allTransfers(std::string *errorMessage) const;
    // 删除单条传输历史
    bool deleteTransfer(const std::string &recordId, std::string *errorMessage);
    // 删除早于指定时间的传输历史
    bool deleteExpiredTransfers(int64_t beforeMs, std::string *errorMessage);
    // 清空全部传输历史
    bool clearAllTransfers(std::string *errorMessage);

    // 纯 SQL 步骤，不自开事务；供调用方在单个事务内组合
    static SqlStep upsertFinishedTransferStep(const TransferRecord &record);
    // 删除指定设备的全部传输历史，供设备删除的组合事务调用（不自开事务）
    static SqlStep deleteForDeviceStep(const std::string &deviceId);

private:
    DatabaseCore *_database = nullptr;  // 数据库连接和事务入口
};

}  // namespace gy::storage
