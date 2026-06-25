/**
* @file    application_paths.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   应用数据目录统一入口实现
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增数据库和日志目录管理
*/

#include "application_paths.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString testBaseDir;  // 测试环境覆盖的应用数据根目录，为空时使用系统默认路径

// 确保子目录存在并返回绝对路径
QString ensureDirectory(const QString &name)
{
    const QString baseDir = testBaseDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : testBaseDir;
    QDir root(baseDir);

    root.mkpath(name);  // 数据目录首次使用时创建，避免调用方分别处理目录存在性
    return root.filePath(name);
}
}

// 获取本地 SQLite 数据库目录
QString ApplicationPaths::databaseDir()
{
    return ensureDirectory("database");
}

// 获取运行日志目录
QString ApplicationPaths::logDir()
{
    return ensureDirectory("logs");
}

// 为测试指定独立的应用数据根目录，隔离测试和生产环境的磁盘写入
void ApplicationPaths::coverForTest(const QString &baseDir)
{
    testBaseDir = QDir(baseDir).absolutePath();
}

// 清除测试目录覆盖，恢复系统应用数据目录
void ApplicationPaths::clearTestCover()
{
    testBaseDir.clear();
}
