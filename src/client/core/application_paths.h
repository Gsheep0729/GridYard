/**
* @file    application_paths.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   应用数据目录统一入口
*
* 配置、数据库和日志默认放在系统标准目录，测试可覆盖为临时目录。
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
    // 当前开发者模式实例号：解析 GRIDYARD_INSTANCE 并夹紧到合法范围，0 表示正常模式
    static int instanceNumber();
    // 实例目录名：实例 0 为 GridYard，实例 N 为 GridYard-devN（实例号先夹紧）
    static QString instanceDirectoryName(int instance);
    // 对系统推导出的应用目录追加实例后缀（实例 0 原样返回），纯函数便于单测
    static QString applyInstanceSuffix(const QString &systemDir, int instance);
    // 为测试指定独立的应用数据根目录
    static void coverForTest(const QString &baseDir);
    // 清除测试目录覆盖，恢复程序目录策略
    static void clearTestCover();
};
