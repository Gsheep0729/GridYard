/**
* @file    application_paths.cpp
* @version 6.7.0
* @date    2026-06-28
* @author  GridYard Team
* @brief   应用数据目录统一入口实现
*
* Change Log:
* [v6.7.0] GY   2026-06-28
* * 默认路径改为可执行文件所在目录策略
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增数据库和日志目录管理
*/

#include "application_paths.h"

#include <QCoreApplication>
#include <QDir>

namespace {
QString testBaseDir;  // 测试环境覆盖的应用数据根目录，为空时使用程序目录策略

// 获取可执行文件所在目录，找不到时回退到当前工作目录
static QString executableDirectory()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        return QDir(appDir).absolutePath();
    }
    return QDir::currentPath();
}

// 获取数据库和日志根目录：优先测试覆盖，其次可执行文件所在目录
static QString runtimeBaseDirectory()
{
    if (!testBaseDir.isEmpty()) {
        return testBaseDir;
    }
    return executableDirectory();
}

// 获取配置根目录：开发构建的 client 子目录上提一级，打包运行时使用程序目录
static QString configBaseDirectory()
{
    if (!testBaseDir.isEmpty()) {
        return testBaseDir;
    }

    QDir appDir(executableDirectory());
    if (appDir.dirName() == "client") {
        appDir.cdUp();  // 开发构建：.../client/appGridYard -> .../config/gridyard.ini
    }
    return appDir.absolutePath();
}

// 确保子目录存在并返回绝对路径
static QString ensureDirectory(const QString &baseDir, const QString &name)
{
    QDir root(baseDir);
    root.mkpath(name);  // 数据目录首次使用时创建，避免调用方分别处理目录存在性
    return root.filePath(name);
}
}

// 获取配置文件目录
QString ApplicationPaths::configDir()
{
    return ensureDirectory(configBaseDirectory(), "config");
}

// 获取本地 SQLite 数据库目录
QString ApplicationPaths::databaseDir()
{
    return ensureDirectory(runtimeBaseDirectory(), "database");
}

// 获取运行日志目录
QString ApplicationPaths::logDir()
{
    return ensureDirectory(runtimeBaseDirectory(), "logs");
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
