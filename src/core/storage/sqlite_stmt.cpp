/**
* @file    sqlite_stmt.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite3 预处理语句 RAII 封装实现
*
* 所有绑定一律 SQLITE_TRANSIENT，由 SQLite 同步拷贝参数值，
* 调用方可安全使用临时字符串。BUSY 重试与连接级 busy_timeout
* 叠加：先由 PRAGMA 阻塞等待五秒，仍忙时语句层再短重试三次。
*/

#include "core/storage/sqlite_stmt.h"

#include <chrono>
#include <thread>

namespace gy::storage {
namespace {
// BUSY/LOCKED 时的重试次数上限与重试间隔（不含首次尝试）
constexpr int kBusyMaxRetries = 2;
constexpr auto kBusyRetryInterval = std::chrono::milliseconds{10};
}

// 预编译 SQL，失败延迟到 step 报告
SqliteStmt::SqliteStmt(SqliteConn &conn, const std::string &sql)
{
    if (!conn.isOpen()) {
        deferError(SQLITE_MISUSE, "连接未打开");
        return;
    }
    const int rc = sqlite3_prepare_v2(conn.handle(), sql.c_str(), -1, &_stmt, nullptr);
    if (rc != SQLITE_OK) {
        deferError(rc, conn.handle() ? sqlite3_errmsg(conn.handle()) : "预编译失败");
        if (_stmt) {
            sqlite3_finalize(_stmt);
            _stmt = nullptr;
        }
    }
}

// 析构函数，释放预编译语句
SqliteStmt::~SqliteStmt()
{
    if (_stmt) {
        sqlite3_finalize(_stmt);
        _stmt = nullptr;
    }
}

// 绑定 64 位整数
SqliteStmt &SqliteStmt::bind(int idx, int64_t value)
{
    if (!_stmt || sqlite3_bind_int64(_stmt, idx, value) != SQLITE_OK) {
        deferError(_stmt ? sqlite3_errcode(sqlite3_db_handle(_stmt)) : SQLITE_MISUSE,
                   _stmt ? std::string{sqlite3_errmsg(sqlite3_db_handle(_stmt))} : "语句未就绪");
    }
    return *this;
}

// 绑定普通整数（转发到 64 位重载）
SqliteStmt &SqliteStmt::bind(int idx, int value)
{
    return bind(idx, static_cast<int64_t>(value));
}

// 绑定字面量文本
SqliteStmt &SqliteStmt::bind(int idx, const char *text)
{
    if (!_stmt || sqlite3_bind_text(_stmt, idx, text, -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        deferError(_stmt ? sqlite3_errcode(sqlite3_db_handle(_stmt)) : SQLITE_MISUSE,
                   _stmt ? std::string{sqlite3_errmsg(sqlite3_db_handle(_stmt))} : "语句未就绪");
    }
    return *this;
}

// 绑定 UTF-8 文本
SqliteStmt &SqliteStmt::bind(int idx, const std::string &text)
{
    if (!_stmt
        || sqlite3_bind_text(_stmt, idx, text.data(), static_cast<int>(text.size()),
                             SQLITE_TRANSIENT) != SQLITE_OK) {
        deferError(_stmt ? sqlite3_errcode(sqlite3_db_handle(_stmt)) : SQLITE_MISUSE,
                   _stmt ? std::string{sqlite3_errmsg(sqlite3_db_handle(_stmt))} : "语句未就绪");
    }
    return *this;
}

// 绑定二进制块
SqliteStmt &SqliteStmt::bind(int idx, const std::vector<uint8_t> &blob)
{
    if (!_stmt
        || sqlite3_bind_blob(_stmt, idx, blob.data(), static_cast<int>(blob.size()),
                             SQLITE_TRANSIENT) != SQLITE_OK) {
        deferError(_stmt ? sqlite3_errcode(sqlite3_db_handle(_stmt)) : SQLITE_MISUSE,
                   _stmt ? std::string{sqlite3_errmsg(sqlite3_db_handle(_stmt))} : "语句未就绪");
    }
    return *this;
}

// 绑定 NULL
SqliteStmt &SqliteStmt::bindNull(int idx)
{
    if (!_stmt || sqlite3_bind_null(_stmt, idx) != SQLITE_OK) {
        deferError(_stmt ? sqlite3_errcode(sqlite3_db_handle(_stmt)) : SQLITE_MISUSE,
                   _stmt ? std::string{sqlite3_errmsg(sqlite3_db_handle(_stmt))} : "语句未就绪");
    }
    return *this;
}

// 执行一步，含 BUSY 短重试与延迟错误出口
StepResult SqliteStmt::step()
{
    if (_failed || !_stmt) {
        return std::unexpected(_deferred);
    }

    for (int attempt = 0;;) {
        const int rc = sqlite3_step(_stmt);
        if (rc == SQLITE_ROW) {
            return true;  // 当前行可读
        }
        if (rc == SQLITE_DONE) {
            return false;  // 遍历完成
        }
        // 忙锁时先复位语句再短重试，避免带着未完成状态重复 step
        if ((rc == SQLITE_BUSY || rc == SQLITE_LOCKED) && attempt < kBusyMaxRetries) {
            ++attempt;
            sqlite3_reset(_stmt);
            std::this_thread::sleep_for(kBusyRetryInterval);
            continue;
        }
        return std::unexpected(
            StorageError{sqlite3_extended_errcode(sqlite3_db_handle(_stmt)),
                         sqlite3_errmsg(sqlite3_db_handle(_stmt))});
    }
}

// 读取当前行的 64 位整数列
int64_t SqliteStmt::columnInt64(int col) const
{
    return sqlite3_column_int64(_stmt, col);
}

// 读取当前行的浮点列
double SqliteStmt::columnDouble(int col) const
{
    return sqlite3_column_double(_stmt, col);
}

// 读取当前行的 UTF-8 文本列
std::string SqliteStmt::columnText(int col) const
{
    const unsigned char *text = sqlite3_column_text(_stmt, col);
    const int size = sqlite3_column_bytes(_stmt, col);
    if (!text || size <= 0) {
        return {};
    }
    return std::string{reinterpret_cast<const char *>(text), static_cast<size_t>(size)};
}

// 读取当前行的二进制块列
std::vector<uint8_t> SqliteStmt::columnBlob(int col) const
{
    const void *blob = sqlite3_column_blob(_stmt, col);
    const int size = sqlite3_column_bytes(_stmt, col);
    if (!blob || size <= 0) {
        return {};
    }
    const auto *bytes = static_cast<const uint8_t *>(blob);
    return std::vector<uint8_t>{bytes, bytes + size};
}

// 判断当前行指定列是否为 NULL
bool SqliteStmt::columnIsNull(int col) const
{
    return sqlite3_column_type(_stmt, col) == SQLITE_NULL;
}

// 重置语句以便重新执行
void SqliteStmt::reset()
{
    if (_stmt) {
        sqlite3_reset(_stmt);
    }
}

// 清除全部已绑定参数值
void SqliteStmt::clearBindings()
{
    if (_stmt) {
        sqlite3_clear_bindings(_stmt);
    }
}

// 记录首个延迟错误
void SqliteStmt::deferError(int code, const std::string &message)
{
    if (_failed) {
        return;  // 首个错误优先，后续错误不覆盖
    }
    _failed = true;
    _deferred = StorageError{code, message};
}

}  // namespace gy::storage
