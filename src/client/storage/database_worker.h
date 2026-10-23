/**
 * @file    database_worker.h
 * @version 7.26.1
 * @date    2026-10-24
 * @author  GridYard Team
 * @brief   SQLite 异步任务执行线程
 *
 * Worker 持有内部 std::thread 与互斥锁保护的 FIFO 任务队列，任务只
 * 访问该线程专属的数据库连接，避免阻塞网络收发与 QML 主线程。
 * 对外保留单入口 submitTask 异步调度契约与排空通知信号。
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

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
    // 停止接收新任务；已受理任务全部执行完毕后发射 drained
    void beginShutdown();

signals:
    void taskFinished(bool succeeded, const QString &errorMessage);
    void drained();

private:
    // 内部线程主循环：按提交顺序串行执行任务，排空后按需通知
    void runLoop();
    // 在任务线程中执行已出队任务并发出结果信号
    void executeTask(const DatabaseTask &task);

    SqliteDatabaseBroker *_database = nullptr;  // 由应用层持有的连接代管者
    std::atomic_bool _acceptingTasks{true};  // 退出开始后拒绝新任务，避免排空边界持续后移
    std::thread _workerThread;       // 串行执行存储任务的内部线程
    std::mutex _queueMutex;          // 保护任务队列与排空/停止标志
    std::condition_variable _queueCond;  // 新任务与排空请求的唤醒通道
    std::deque<DatabaseTask> _queue;  // 待执行任务队列（FIFO）
    bool _drainRequested = false;    // beginShutdown 已请求排空通知
    bool _stopping = false;          // 析构已请求线程退出
    bool _drainedNotified = false;   // drained 信号是否已发射，保证只发一次
};
