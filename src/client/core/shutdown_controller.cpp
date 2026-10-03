/**
* @file    shutdown_controller.cpp
* @version 7.15.10
* @date    2026-10-03
* @author  GridYard Team
* @brief   应用退出与缓存清理控制器实现
*
* Change Log:
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.11.0] GY   2026-10-02
* * 自 AppController 拆出退出排空与缓存清理策略
*/

#include "shutdown_controller.h"
#include "application_paths.h"
#include "local_data_broker.h"
#include "logger.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtGlobal>

namespace {

// 返回当前生效的配置文件路径（测试可通过环境变量重定向）
QString activeConfigPath()
{
    const QString envPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    return envPath.isEmpty() ? ApplicationPaths::configDir() + "/gridyard.ini" : envPath;
}

// 删除单个文件（不存在时静默跳过）
void removeFileIfExists(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }
    if (!QFile::remove(path)) {
        qWarning() << "[Cache] 删除文件失败:" << path;
    }
}

// 递归删除目录（不存在时静默跳过）
void removeDirectoryIfExists(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    QDir dir{path};
    if (!dir.exists()) {
        return;
    }
    if (!dir.removeRecursively()) {
        qWarning() << "[Cache] 删除目录失败:" << path;
    }
}

}

// 构造函数
ShutdownController::ShutdownController(LocalDataBroker *dataBroker, QObject *parent)
    : QObject{parent}
    , _dataBroker{dataBroker}
{
}

// 请求退出应用：排空存储任务后退出，3 秒兜底强制退出
void ShutdownController::requestQuit()
{
    if (_quitRequested) {
        return;
    }
    _quitRequested = true;
    qDebug() << "ShutdownController: requestQuit";

    if (!_dataBroker) {
        QCoreApplication::exit(0);
        return;
    }

    // 单次触发的排空信号 + QueuedConnection，确保退出发生在事件循环空闲时。
    connect(_dataBroker, &LocalDataBroker::drained,
            this, [] { QCoreApplication::exit(0); },
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    _dataBroker->beginShutdown();  // 通知 Worker 线程排空剩余任务后停止

    // 极端情况下 Worker 线程没有及时响应，也不能让托盘进程永久留在后台。
    QTimer::singleShot(3000, this, [] { QCoreApplication::exit(0); });  // 3 秒兜底强制退出
}

// 清除配置、历史数据库和日志后退出应用
void ShutdownController::requestClearCacheAndQuit()
{
    if (_cacheClearRequested) {
        return;
    }
    _cacheClearRequested = true;
    _quitRequested = true;
    qInfo() << "[Cache] 开始清除本地缓存";

    auto finishClear = [this] {
        if (_cacheClearFinished) {
            return;
        }
        _cacheClearFinished = true;
        if (_dataBroker) {
            _dataBroker->closeStorage();
        }
        removeLocalCacheFiles();
        QCoreApplication::exit(0);
    };

    if (!_dataBroker) {
        finishClear();
        return;
    }

    connect(_dataBroker, &LocalDataBroker::drained,
            this, finishClear,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    _dataBroker->beginShutdown();

    // 存储线程异常无响应时仍执行清理，避免用户无法退出清除流程。
    QTimer::singleShot(3000, this, finishClear);
}

// 删除本地持久化文件和目录
void ShutdownController::removeLocalCacheFiles()
{
    Logger::instance()->shutdown();  // 释放当前日志文件句柄后再删除 logs 目录
    removeFileIfExists(activeConfigPath());
    removeDirectoryIfExists(ApplicationPaths::configDir());
    removeDirectoryIfExists(ApplicationPaths::databaseDir());
    removeDirectoryIfExists(ApplicationPaths::logDir());
}
