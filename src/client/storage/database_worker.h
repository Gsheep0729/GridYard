/**
* @file    database_worker.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   SQLite 异步任务执行线程
*
* Worker 持有专用线程中的任务队列，任务只访问该线程自己的数据库连接，
* 不会阻塞网络收发或 QML 主线程。
* 投递入口只有 submitTask 一个；原先按保存/加载/删除分设的三个同义入口
* 已收敛，若未来需要按类别限流或统计，可在此入口加类别参数。
*/
#pragma once

#include <functional>
#include <atomic>

#include <QObject>

class SqliteDatabaseBroker;

class DatabaseWorker : public QObject {
private:
    Q_OBJECT

public:
    using DatabaseTask = std::function<bool(SqliteDatabaseBroker &, QString *)>;

    explicit DatabaseWorker(SqliteDatabaseBroker *database);
    virtual ~DatabaseWorker() override;

    DatabaseWorker(const DatabaseWorker &) = delete;
    DatabaseWorker &operator=(const DatabaseWorker &) = delete;

    // 提交一个存储任务（保存/加载/删除统一入口）
    void submitTask(const DatabaseTask &task);
    // 停止接收新任务；此调用排在既有队列末尾，抵达时说明已提交任务均已执行。
    void beginShutdown();

signals:
    void taskFinished(bool succeeded, const QString &errorMessage);
    void drained();

private:
    // 在数据库线程中执行已投递任务
    void executeTask(const DatabaseTask &task);

    SqliteDatabaseBroker *_database = nullptr;  // 由应用层持有的连接代管者
    std::atomic_bool _acceptingTasks{true};  // 退出开始后拒绝新任务，避免排空边界持续后移
};
