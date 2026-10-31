/**
* @file    migration_runner.h
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   SQLite Schema 版本迁移执行器
*
* Migration 在事务内执行，失败时回滚当前版本的所有 DDL。
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
