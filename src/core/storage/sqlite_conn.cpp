/**
* @file    sqlite_conn.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite3 C API RAII 连接封装实现
*
* 连接参数与旧 QSqlDatabase 实现逐字一致：外键、WAL、NORMAL 同步、
* 五秒忙等待，保证纯 C++ 版打开同一文件时的行为完全等价。
*/

#include "core/storage/sqlite_conn.h"

namespace gy::storage {

// 打开数据库文件并应用连接参数
std::expected<SqliteConn, StorageError> SqliteConn::open(const std::string &path)
{
    sqlite3 *db = nullptr;
    // 打开标志与 QSqlDatabase 默认等价：读写创建
    const int rc = sqlite3_open_v2(path.c_str(), &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        const StorageError error{rc,
            db ? std::string{sqlite3_errmsg(db)} : std::string{"无法打开数据库文件"}};
        if (db) {
            sqlite3_close(db);
        }
        return std::unexpected(error);
    }

    SqliteConn conn{db};
    if (auto result = conn.applyPragmas(); !result.has_value()) {
        return std::unexpected(result.error());
    }
    return conn;
}

// 构造函数，接管已打开句柄的所有权
SqliteConn::SqliteConn(sqlite3 *db)
    : _db(db)
{
}

// 析构函数，关闭句柄并释放关联的预编译语句缓存
SqliteConn::~SqliteConn()
{
    if (_db) {
        sqlite3_close_v2(_db);
        _db = nullptr;
    }
}

// 移动构造，接管句柄后置空来源
SqliteConn::SqliteConn(SqliteConn &&other) noexcept
    : _db(other._db)
{
    other._db = nullptr;
}

// 移动赋值，先关闭自身句柄再接管来源
SqliteConn &SqliteConn::operator=(SqliteConn &&other) noexcept
{
    if (this != &other) {
        if (_db) {
            sqlite3_close_v2(_db);
        }
        _db = other._db;
        other._db = nullptr;
    }
    return *this;
}

// 执行无需绑定参数的 SQL
SqlResult SqliteConn::exec(const std::string &sql)
{
    if (!_db) {
        return std::unexpected(StorageError{SQLITE_MISUSE, "连接未打开"});
    }
    char *message = nullptr;
    const int rc = sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &message);
    if (rc == SQLITE_OK) {
        return {};
    }
    StorageError error{rc, message ? std::string{message} : std::string{sqlite3_errmsg(_db)}};
    sqlite3_free(message);
    return std::unexpected(error);
}

// 获取最后一次 INSERT 的 rowid
int64_t SqliteConn::lastInsertRowid() const
{
    return _db ? sqlite3_last_insert_rowid(_db) : 0;
}

// 获取最近一条语句影响的行数
int SqliteConn::changesCount() const
{
    return _db ? sqlite3_changes(_db) : 0;
}

// 判断是否处于显式事务中
bool SqliteConn::inTransaction() const
{
    return _db && sqlite3_get_autocommit(_db) == 0;
}

// 组装带上下文前缀的底层错误
StorageError SqliteConn::lastError(const std::string &context) const
{
    return StorageError{_db ? sqlite3_extended_errcode(_db) : SQLITE_MISUSE,
                        context + ": " + (_db ? sqlite3_errmsg(_db) : "连接未打开")};
}

// 应用所有线程都需要的 SQLite 连接参数
SqlResult SqliteConn::applyPragmas()
{
    // 与旧 QSqlDatabase 实现相同的四条 PRAGMA，顺序保持一致
    const char *pragmas[] = {
        "PRAGMA foreign_keys = ON",
        "PRAGMA journal_mode = WAL",
        "PRAGMA synchronous = NORMAL",
        "PRAGMA busy_timeout = 5000"  // 短暂锁竞争最多等待五秒
    };
    for (const char *pragma : pragmas) {
        if (auto result = exec(pragma); !result.has_value()) {
            return result;
        }
    }
    return {};
}

}  // namespace gy::storage
