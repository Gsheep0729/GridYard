/**
* @file    database_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SQLite 连接、迁移与事务代管者——不依赖任何 Qt 类型
*
* 与 client/storage/SqliteDatabaseBroker 的 Qt 版职责对应：打开数据库
* 文件、应用连接参数、执行迁移、损坏库备份重建，并提供短事务组合
* 入口。区别在于连接模型：sqlite3 句柄没有全局命名注册表，单个
* 对象即单条独占连接，须在持有它的线程内访问（存储任务线程模型）。
*/

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/storage/sqlite_conn.h"

namespace gy::storage {

// 数据库代管者：一个对象对应一个数据库文件与一条独占连接
class DatabaseCore {
public:
    // 只执行 SQL、不自开事务的任务，供调用方把多表写入组合进单个事务
    using TransactionTask = std::function<bool(SqliteConn &, std::string *)>;

    DatabaseCore() = default;
    // 析构时连接由 SqliteConn 自动关闭
    ~DatabaseCore() = default;

    // 连接与迁移状态属于本对象，禁止拷贝共享
    DatabaseCore(const DatabaseCore &) = delete;
    DatabaseCore &operator=(const DatabaseCore &) = delete;

    // 打开数据库、配置连接参数并执行迁移
    bool initialize(const std::string &databasePath, std::string *errorMessage);
    // 判断数据库是否已成功初始化
    bool isAvailable() const;
    // 获取当前 Schema 版本（未初始化或查询失败返回 -1）
    int schemaVersion() const;
    // 在短事务中执行跨表持久化任务
    bool runInTransaction(const TransactionTask &task, std::string *errorMessage) const;
    // 在单个事务内依次执行多个步骤，全部成功才提交；任一步骤失败则整体回滚
    bool runSteps(const std::vector<TransactionTask> &steps, std::string *errorMessage) const;
    // 判断本次 initialize 是否从损坏备份重建了数据库
    bool lastInitializeRebuilt() const;
    // 获取本次重建前损坏库的备份路径（未重建时为空）
    const std::string &rebuiltBackupPath() const;

    // 主连接访问入口：仅允许在持有本对象的线程调用
    SqliteConn &connection();

private:
    // 打开主连接并完成参数配置与迁移
    bool openMainConnection(std::string *errorMessage);
    // 将损坏数据库备份到同目录的 .corrupt 时间戳文件，回填备份路径
    bool backupCorruptDatabase(std::string &backupPath, std::string *errorMessage) const;
    // 判断底层错误是否属于 SQLite 文件损坏
    static bool isCorruptionError(const std::string &errorMessage);

    // 连接句柄标记 mutable：经其执行 SQL 本质可变，与 Qt 版经隐式共享
    // 句柄执行语句的 const 语义对齐，保持只读查询接口可声明为 const
    mutable std::optional<SqliteConn> _conn;  // 主连接，initialize 成功后存在
    std::string _databasePath;         // SQLite 数据库绝对路径
    bool _available = false;           // 初始化及迁移是否成功
    bool _rebuiltFromBackup = false;   // 本次 initialize 是否从损坏备份重建
    std::string _rebuiltBackupPath;    // 重建前损坏库的备份文件路径
};

}  // namespace gy::storage
