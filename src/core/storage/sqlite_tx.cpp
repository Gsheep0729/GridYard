/**
* @file    sqlite_tx.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite3 RAII 事务封装实现
*
* BEGIN IMMEDIATE 在事务开始即取写锁，避免延迟事务升级写锁时
* 与其他写者互相等待；单写者任务线程下该选择无副作用。
*/

#include "core/storage/sqlite_tx.h"

#include <cstdio>

namespace gy::storage {

// 构造并开启事务
SqliteTx::SqliteTx(SqliteConn &conn)
    : _conn(conn)
{
    if (auto result = _conn.exec("BEGIN IMMEDIATE"); !result.has_value()) {
        _beginError = result.error();
        return;
    }
    _begun = true;
}

// 析构时未提交则回滚
SqliteTx::~SqliteTx()
{
    if (_begun && !_committed) {
        rollbackQuietly();
    }
}

// 判断事务是否开启且未提交
bool SqliteTx::isActive() const
{
    return _begun && !_committed;
}

// 提交事务
SqlResult SqliteTx::commit()
{
    if (!_begun) {
        // BEGIN 阶段已失败：统一在此出口返回，调用方无需关心失败时机
        return std::unexpected(_beginError);
    }
    if (_committed) {
        return std::unexpected(StorageError{SQLITE_MISUSE, "事务已提交"});
    }
    auto result = _conn.exec("COMMIT");
    if (result.has_value()) {
        _committed = true;
        return {};
    }
    // 提交失败时事务仍持有：回滚保持数据库一致，错误向上报告
    rollbackQuietly();
    return result;
}

// 手动回滚（幂等）
void SqliteTx::rollback() noexcept
{
    if (_begun && !_committed) {
        rollbackQuietly();
        _committed = true;  // 防止析构时重复回滚
    }
}

// 执行回滚并吞掉错误
void SqliteTx::rollbackQuietly() noexcept
{
    auto result = _conn.exec("ROLLBACK");
    if (!result.has_value()) {
        // 回滚失败只写日志，绝不向上传播
        std::fprintf(stderr, "[StorageCore] 事务回滚失败: %s\n",
                     result.error().message.c_str());
    }
}

}  // namespace gy::storage
