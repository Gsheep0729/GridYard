/**
* @file    sqlite_conn.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite3 C API RAII 连接封装——不依赖任何 Qt 类型
*
* 每个连接对象独占一个 sqlite3 句柄，构造即应用连接级 PRAGMA，
* 析构自动关闭。连接线程不安全：调用方必须保证同一连接只在
* 单个线程内访问（存储任务线程模型已满足该约束）。
*/

#pragma once

#include <sqlite3.h>

#include <expected>
#include <string>

namespace gy::storage {

// SQLite 操作失败的错误值：原生错误码 + 人类可读说明
struct StorageError {
    int code = 0;         // sqlite3 原生错误码（或扩展码）
    std::string message;  // sqlite3_errmsg 或上下文说明（UTF-8）
};

// 无返回值操作的结果（exec/commit 等）
using SqlResult = std::expected<void, StorageError>;

// 单步执行的结果：true 表示当前有行，false 表示遍历完成
using StepResult = std::expected<bool, StorageError>;

// 连接封装：持有并最终释放一个 sqlite3 句柄
class SqliteConn {
public:
    // 打开数据库文件（不存在则创建）并应用连接级 PRAGMA；失败返回错误
    static std::expected<SqliteConn, StorageError> open(const std::string &path);

    // 构造空连接（不持有句柄），供延迟打开场景
    SqliteConn() = default;
    // 析构时关闭句柄
    ~SqliteConn();

    // 仅允许移动：句柄所有权唯一，禁止拷贝共享
    SqliteConn(const SqliteConn &) = delete;
    SqliteConn &operator=(const SqliteConn &) = delete;
    SqliteConn(SqliteConn &&other) noexcept;
    SqliteConn &operator=(SqliteConn &&other) noexcept;

    // 是否持有已打开的句柄
    bool isOpen() const { return _db != nullptr; }
    // 返回底层句柄，仅供 SqliteStmt 与事务封装使用
    sqlite3 *handle() const { return _db; }

    // 执行不需要绑定参数的 SQL（DDL、PRAGMA）
    SqlResult exec(const std::string &sql);

    // 最后一次 INSERT 的 rowid
    int64_t lastInsertRowid() const;
    // 最近一条语句影响的行数（不含清理触发器的级联部分）
    int changesCount() const;
    // 是否处于显式事务中（即未处于自动提交模式）
    bool inTransaction() const;

    // 组装当前底层错误（含上下文前缀），供封装层统一报告
    StorageError lastError(const std::string &context) const;

private:
    explicit SqliteConn(sqlite3 *db);

    // 应用所有连接都需要的 PRAGMA（WAL、busy_timeout、外键等）
    SqlResult applyPragmas();

    sqlite3 *_db = nullptr;  // SQLite3 连接句柄
};

}  // namespace gy::storage
