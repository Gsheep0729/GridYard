/**
* @file    shutdown_controller.h
* @version 7.15.12
* @date    2026-10-03
* @author  GridYard Team
* @brief   应用退出与缓存清理控制器
*
* 收拢托盘退出与清除本地缓存两条收尾路径：先排空存储线程任务再退出，
* 带超时兜底防止后台进程滞留；清除缓存时按安全清单删除配置、
* 历史数据库和日志目录。重复触发由内部标志守卫。
*
* Change Log:
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.11.0] GY   2026-10-02
* * 自 AppController 拆出退出排空与缓存清理策略
*/

#pragma once

#include <QObject>
#include <QString>

class LocalDataBroker;

class ShutdownController : public QObject {
    Q_OBJECT

public:
    explicit ShutdownController(LocalDataBroker *dataBroker, QObject *parent = nullptr);

    // 请求退出应用：排空存储任务后退出，3 秒兜底强制退出
    void requestQuit();
    // 清除配置、历史数据库和日志后退出应用
    void requestClearCacheAndQuit();

private:
    // 删除本地持久化文件和目录
    void removeLocalCacheFiles();

    LocalDataBroker *_dataBroker = nullptr;  // 存储排空来源

    bool _quitRequested = false;      // 防止托盘退出动作重复请求排空同一任务队列
    bool _cacheClearRequested = false;  // 防止重复触发清除缓存流程
    bool _cacheClearFinished = false;  // 防止正常排空和超时兜底重复删除缓存
};
