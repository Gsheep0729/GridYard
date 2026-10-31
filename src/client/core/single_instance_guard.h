/**
* @file    single_instance_guard.h
* @version 8.2.1
* @date 2026-10-31
* @author  GridYard Team
* @brief   单实例守卫
*
* 以 QLockFile 对实例配置目录互斥：第二实例启动时立即让位退出，只承担
* 唤回既有实例窗口的职责，避免同身份双进程并存造成监听与发现分流。
* 唤回通道用实例目录下的激活文件加 QFileSystemWatcher，不引入 Qt6::Network；
* 无托盘桌面隐藏到后台后，再次启动即成为唤回窗口的入口。
*/

#pragma once

#include <QObject>
#include <QString>

class QFileSystemWatcher;
class QLockFile;

class SingleInstanceGuard : public QObject {
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);
    virtual ~SingleInstanceGuard() override;

    SingleInstanceGuard(const SingleInstanceGuard &)            = delete;
    SingleInstanceGuard &operator=(const SingleInstanceGuard &) = delete;

    // 尝试成为本实例目录下的唯一实例；失败说明已有实例在运行
    bool tryAcquire();
    // 向既有实例投递唤回请求，返回请求是否已写入激活文件
    bool notifyExistingInstance();

signals:
    // 既有实例收到唤回请求时发出，窗口激活由表现层完成
    void activationRequested();

private:
    // 激活文件变化入口：通知界面唤回，并看护被清理后重建监听
    void handleActivateFileChanged();

    QLockFile *_lock = nullptr;             // 实例配置目录互斥锁
    QFileSystemWatcher *_watcher = nullptr; // 激活文件监听
    QString _activateFilePath;              // 唤回请求激活文件路径
};
