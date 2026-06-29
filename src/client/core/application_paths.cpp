/**
* @file    application_paths.cpp
* @version 6.8.1
* @date    2026-06-28
* @author  GridYard Team
* @brief   应用数据目录统一入口实现
*
* Change Log:
* [v6.8.1] GY   2026-06-29
* * 恢复系统标准配置和应用数据目录，避免发布包目录承载运行数据
* [v6.7.0] GY   2026-06-28
* * 默认路径改为可执行文件所在目录策略
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增数据库和日志目录管理
*/

#include "application_paths.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString testBaseDir;  // 测试环境覆盖的应用数据根目录，为空时使用系统标准目录

// 获取系统配置根目录，找不到时回退到用户主目录下的 .config
static QString systemConfigDirectory()
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!configDir.isEmpty()) {
        return QDir(configDir).absolutePath();
    }
    return QDir::home().filePath(".config/GridYard");
}

// 获取系统应用数据根目录，找不到时回退到用户主目录下的 .local/share
static QString systemDataDirectory()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty()) {
        return QDir::home().filePath(".local/share/GridYard");
    }
    return QDir(dataDir).absolutePath();
}

// 获取配置根目录：测试使用覆盖目录，正式运行使用系统标准配置目录
static QString configBaseDirectory()
{
    if (!testBaseDir.isEmpty()) {
        return QDir(testBaseDir).filePath("config");
    }
    return systemConfigDirectory();
}

// 确保目录存在并返回绝对路径
static QString ensureDirectory(const QString &path)
{
    QDir dir(path);
    dir.mkpath(".");  // 首次启动可能没有系统应用目录
    return dir.absolutePath();
}

// 确保子目录存在并返回绝对路径
static QString ensureChildDirectory(const QString &baseDir, const QString &name)
{
    QDir root(baseDir);
    root.mkpath(name);  // 数据目录首次使用时创建，避免调用方分别处理目录存在性
    return QDir(root.filePath(name)).absolutePath();
}
}

// 获取配置文件目录
QString ApplicationPaths::configDir()
{
    return ensureDirectory(configBaseDirectory());
}

// 获取本地 SQLite 数据库目录
QString ApplicationPaths::databaseDir()
{
    const QString baseDir = testBaseDir.isEmpty() ? systemDataDirectory() : testBaseDir;
    return ensureChildDirectory(baseDir, "database");
}

// 获取运行日志目录
QString ApplicationPaths::logDir()
{
    const QString baseDir = testBaseDir.isEmpty() ? systemDataDirectory() : testBaseDir;
    return ensureChildDirectory(baseDir, "logs");
}

// 为测试指定独立的应用数据根目录，隔离测试和生产环境的磁盘写入
void ApplicationPaths::coverForTest(const QString &baseDir)
{
    testBaseDir = QDir(baseDir).absolutePath();
}

// 清除测试目录覆盖，恢复程序目录策略
void ApplicationPaths::clearTestCover()
{
    testBaseDir.clear();
}
