/**
* @file    application_paths.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   应用数据目录统一入口实现
*/

#include "application_paths.h"

#include "protocol.h"

#include <QDir>
#include <QStandardPaths>

// 当前开发者模式实例号：解析 GRIDYARD_INSTANCE，未设置或非法时按正常模式处理，
// 越界数值夹紧到合法范围；实例号只在启动期由入口写入，此处是唯一的解析点
int ApplicationPaths::instanceNumber()
{
    const QString raw = qEnvironmentVariable("GRIDYARD_INSTANCE");
    if (raw.isEmpty()) {
        return 0;
    }

    bool ok = false;
    const int value = raw.toInt(&ok);
    if (!ok) {
        return 0;
    }
    return qBound(0, value, gy::protocol::kMaxInstanceNumber);
}

// 实例目录名：实例 0 沿用默认名，开发者实例追加 -devN 后缀
QString ApplicationPaths::instanceDirectoryName(int instance)
{
    const int n = qBound(0, instance, gy::protocol::kMaxInstanceNumber);
    if (n == 0) {
        return QStringLiteral("GridYard");
    }
    return QStringLiteral("GridYard-dev%1").arg(n);
}

// 对系统推导出的应用目录追加实例后缀：取上级目录拼接实例目录名，
// 实例 0 原样返回原字符串，保证正常模式的推导路径逐字符不变
QString ApplicationPaths::applyInstanceSuffix(const QString &systemDir, int instance)
{
    if (instance <= 0 || systemDir.isEmpty()) {
        return systemDir;
    }

    const QString parent = QDir(systemDir).absolutePath();
    const int lastSlash = parent.lastIndexOf('/');
    // 无路径分隔符的裸目录名（正常装配不会出现）：直接对名称追加后缀
    if (lastSlash < 0) {
        return parent + "-dev" + QString::number(qBound(0, instance, gy::protocol::kMaxInstanceNumber));
    }
    return parent.left(lastSlash) + '/' + instanceDirectoryName(instance);
}

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

// 当前实例号下的系统配置根目录（开发者实例追加 GridYard-devN 后缀）
static QString configBaseDirectory()
{
    const QString baseDir = testBaseDir.isEmpty() ? systemConfigDirectory() : QDir(testBaseDir).filePath("config");
    return ApplicationPaths::applyInstanceSuffix(baseDir, ApplicationPaths::instanceNumber());
}

// 当前实例号下的系统数据根目录（开发者实例追加 GridYard-devN 后缀）
static QString dataBaseDirectory()
{
    const QString baseDir = testBaseDir.isEmpty() ? systemDataDirectory() : testBaseDir;
    return ApplicationPaths::applyInstanceSuffix(baseDir, ApplicationPaths::instanceNumber());
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
    return ensureChildDirectory(dataBaseDirectory(), "database");
}

// 获取运行日志目录
QString ApplicationPaths::logDir()
{
    return ensureChildDirectory(dataBaseDirectory(), "logs");
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
