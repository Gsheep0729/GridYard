/**
* @file    migration_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SQLite Schema 版本迁移执行器
*
* SQL 文本与 client/storage/migration_runner 的 Qt 版逐字一致：
* 同一文件可被任一实现打开并继续升级，语义等价由存储测试锁定。
*/

#pragma once

#include <string>

namespace gy::storage {

class SqliteConn;

// Schema 版本迁移执行器：各版本迁移在各自事务内按序执行
class MigrationCore {
public:
    // 执行尚未应用的 Schema 迁移
    static bool migrate(SqliteConn &conn, std::string *errorMessage);
    // 获取数据库已应用的最高 Schema 版本（查询失败返回 -1）
    static int schemaVersion(SqliteConn &conn, std::string *errorMessage);
};

}  // namespace gy::storage
