/**
* @file    application_paths.h
* @version 6.0.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   应用数据目录统一入口
*
* 日志和数据库共享应用数据根目录，测试可覆盖为临时目录。
*
* Change Log:
* [v6.0.0] GY 2026-06-25
* * 新增数据库和日志目录管理
*/

#pragma once

#include <QString>

class ApplicationPaths {
public:
    // 获取本地 SQLite 数据库目录
    static QString databaseDir();
    // 获取运行日志目录
    static QString logDir();
    // 为测试指定独立的应用数据根目录
    static void coverForTest(const QString &baseDir);
    // 清除测试目录覆盖，恢复系统应用数据目录
    static void clearTestCover();
};
