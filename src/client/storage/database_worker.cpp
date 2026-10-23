/**
* @file    database_worker.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   SQLite 异步任务执行线程实现
*
* Worker 在构造时启动内部 std::thread，submitTask 把任务压入互斥锁
* 保护的 FIFO 队列并由条件变量唤醒；beginShutdown 关闭受理开关并
* 请求排空，队列清空且此前受理的任务全部执行完毕后发射 drained。
* 数据库连接只在任务线程内创建与关闭，析构前先排空队列再回收线程。
*/

#include "database_worker.h"

#include "sqlite_database_broker.h"

#include <utility>

// 构造函数，启动内部任务线程
DatabaseWorker::DatabaseWorker(SqliteDatabaseBroker *database)
    : _database(database)
{
    _workerThread = std::thread{&DatabaseWorker::runLoop, this};
}

// 析构函数，排空剩余任务后回收线程
DatabaseWorker::~DatabaseWorker()
{
    {
        const std::lock_guard<std::mutex> lock{_queueMutex};
        _stopping = true;
    }
    _queueCond.notify_all();
    if (_workerThread.joinable()) {
        _workerThread.join();
    }
}

// 开始关闭并请求排空通知
void DatabaseWorker::beginShutdown()
{
    // 先关闭受理开关再请求排空：此后提交的任务被拒绝，
    // 排空点抵达时说明此前受理的任务均已执行完成
    _acceptingTasks.store(false);
    {
        const std::lock_guard<std::mutex> lock{_queueMutex};
        _drainRequested = true;
    }
    _queueCond.notify_all();
}

// 将任务追加到 FIFO 队列并唤醒任务线程
void DatabaseWorker::submitTask(const DatabaseTask &task)
{
    if (!_acceptingTasks.load()) {
        return;
    }

    {
        const std::lock_guard<std::mutex> lock{_queueMutex};
        _queue.push_back(task);
    }
    _queueCond.notify_one();
}

// 内部线程主循环
void DatabaseWorker::runLoop()
{
    for (;;) {
        DatabaseTask task;
        {
            std::unique_lock<std::mutex> lock{_queueMutex};
            _queueCond.wait(lock, [this] {
                return !_queue.empty() || _drainRequested || _stopping;
            });
            if (_queue.empty()) {
                if (_drainRequested && !_drainedNotified) {
                    _drainedNotified = true;
                    // 释锁后再发信号，避免接收方在信号内回查队列造成死锁
                    lock.unlock();
                    emit drained();
                    lock.lock();
                }
                if (_stopping) {
                    break;
                }
                continue;
            }
            task = std::move(_queue.front());
            _queue.pop_front();
        }
        executeTask(task);
    }

    // 任务线程退出前清理本线程专属连接，避免全局连接表泄漏
    if (_database) {
        _database->closeConnectionForCurrentThread();
    }
}

// 执行单个数据库任务并发出结果
void DatabaseWorker::executeTask(const DatabaseTask &task)
{
    if (!_database) {
        emit taskFinished(false, QStringLiteral("数据库入口未初始化"));
        return;
    }

    // 数据库入口按当前线程创建独立连接，任务全程不跨线程传递句柄
    QString errorMessage;
    const bool succeeded = task(*_database, &errorMessage);
    emit taskFinished(succeeded, errorMessage);
}
