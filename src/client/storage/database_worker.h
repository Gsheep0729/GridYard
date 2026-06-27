/**
* @file    database_worker.h
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   SQLite 异步任务执行线程
*
* Worker 持有专用线程中的任务队列，任务只访问该线程自己的数据库连接，
* 不会阻塞网络收发或 QML 主线程。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.5.0] GY 2026-06-25
* * 支持退出前排空已提交的存储任务
* [v6.0.0] GY 2026-06-25
* * 新增数据库串行任务 Worker
*/
#pragma once

#include <functional>
#include <atomic>

#include <QObject>

class SqliteDatabaseProxy;

class DatabaseWorker : public QObject {
private:
    Q_OBJECT

public:
    using DatabaseTask = std::function<bool(SqliteDatabaseProxy &, QString *)>;

    explicit DatabaseWorker(SqliteDatabaseProxy *database);
    virtual ~DatabaseWorker() override;

    DatabaseWorker(const DatabaseWorker &) = delete;
    DatabaseWorker &operator=(const DatabaseWorker &) = delete;

    void submitSave(const DatabaseTask &task);
    void submitLoad(const DatabaseTask &task);
    void submitDelete(const DatabaseTask &task);
    // 停止接收新任务；此调用排在既有队列末尾，抵达时说明已提交任务均已执行。
    void beginShutdown();

signals:
    void taskFinished(bool succeeded, const QString &errorMessage);
    void drained();

private:
    // 统一投递不同类别的存储任务
    void submitTask(const DatabaseTask &task);
    // 在数据库线程中执行已投递任务
    void executeTask(const DatabaseTask &task);

    SqliteDatabaseProxy *_database = nullptr;  // 由应用层持有的连接代理
    std::atomic_bool _acceptingTasks{true};  // 退出开始后拒绝新任务，避免排空边界持续后移
};
