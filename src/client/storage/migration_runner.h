/**
* @file    migration_runner.h
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   SQLite Schema 版本迁移执行器
*
* Migration 在事务内执行，失败时回滚当前版本的所有 DDL。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增版本一 Schema 迁移
*/

#pragma once

#include <QString>

class QSqlDatabase;

class MigrationRunner {
public:
    // 执行尚未应用的 Schema 迁移
    static bool migrate(QSqlDatabase &database, QString *errorMessage);
    // 获取当前数据库已应用的最高 Schema 版本
    static int schemaVersion(QSqlDatabase &database, QString *errorMessage);
};
