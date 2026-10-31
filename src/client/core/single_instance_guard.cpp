/**
* @file    single_instance_guard.cpp
* @version 8.2.1
* @date 2026-10-31
* @author  GridYard Team
* @brief   单实例守卫实现
*/

#include "single_instance_guard.h"

#include "application_paths.h"

#include <QDateTime>
#include <QFile>
#include <QFileSystemWatcher>
#include <QLockFile>
#include <QtGlobal>

namespace {

// 唤回激活文件落点：与锁文件同住实例配置目录，按实例号天然隔离
QString activateFilePathForConfigDir()
{
    return ApplicationPaths::configDir() + QStringLiteral("/single-instance.activate");
}

}  // namespace

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : QObject{parent}
{
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    // QLockFile 析构时释放并移除锁文件，正常退出不留陈旧锁
    delete _lock;
}

// 尝试成为本实例目录下的唯一实例
bool SingleInstanceGuard::tryAcquire()
{
    const QString configDir = ApplicationPaths::configDir();
    _lock = new QLockFile{configDir + QStringLiteral("/single-instance.lock")};
    if (!_lock->tryLock(0)) {
        delete _lock;
        _lock = nullptr;
        return false;
    }

    // 激活文件必须先存在才能被监听，空文件即可
    _activateFilePath = activateFilePathForConfigDir();
    QFile activateFile{_activateFilePath};
    if (!activateFile.exists()) {
        if (!activateFile.open(QIODevice::WriteOnly)) {
            qWarning() << "SingleInstanceGuard: 唤回激活文件创建失败" << _activateFilePath;
        }
    }

    _watcher = new QFileSystemWatcher{this};
    connect(_watcher, &QFileSystemWatcher::fileChanged,
            this, &SingleInstanceGuard::handleActivateFileChanged);
    _watcher->addPath(_activateFilePath);
    return true;
}

// 向既有实例投递唤回请求
bool SingleInstanceGuard::notifyExistingInstance()
{
    QFile activateFile{activateFilePathForConfigDir()};
    // 覆写式写入保持同一 inode，既有实例的监听无需重挂；文件缺失时本调用顺带重建
    if (!activateFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray stamp =
        QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8() + '\n';
    const bool written = activateFile.write(stamp) == stamp.size();
    activateFile.flush();
    return written;
}

// 激活文件变化入口：通知界面唤回，并看护被清理后重建监听
void SingleInstanceGuard::handleActivateFileChanged()
{
    emit activationRequested();

    // 文件被系统清理后路径会掉出监听表：重建空文件再挂回，唤回通道保持可用
    if (_watcher && !_watcher->files().contains(_activateFilePath)) {
        QFile activateFile{_activateFilePath};
        if (activateFile.exists() || activateFile.open(QIODevice::WriteOnly)) {
            _watcher->addPath(_activateFilePath);
        }
    }
}
