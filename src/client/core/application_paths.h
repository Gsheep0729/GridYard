/**
* @file    application_paths.h
* @version 7.15.14
* @date    2026-10-04
* @author  GridYard Team
* @brief   应用数据目录统一入口
*
* 配置、数据库和日志默认放在系统标准目录，测试可覆盖为临时目录。
*
* Change Log:
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v6.8.1] GY   2026-06-29
* * 恢复系统标准配置和应用数据目录
* [v6.7.0] GY   2026-06-28
* * 默认路径改为可执行文件所在目录策略
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增数据库和日志目录管理
*/

#pragma once

#include <QString>

class ApplicationPaths {
public:
    // 获取配置文件目录
    static QString configDir();
    // 获取本地 SQLite 数据库目录
    static QString databaseDir();
    // 获取运行日志目录
    static QString logDir();
    // 为测试指定独立的应用数据根目录
    static void coverForTest(const QString &baseDir);
    // 清除测试目录覆盖，恢复程序目录策略
    static void clearTestCover();
};
