/**
* @file    database_worker.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 异步任务执行线程实现
*
* Worker 在构造时获得 SqliteDatabaseBroker 指针，moveToThread 后
* 由调用方通过 submitSave/submitLoad/submitDelete 提交任务，任务在
* 数据库线程中串行执行，执行完后发射 taskFinished 信号。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.5.0] GY 2026-06-25
* * 支持退出前排空已提交的存储任务
* [v6.0.0] GY 2026-06-25
* * 新增数据库串行任务 Worker
*/

#include "database_worker.h"

#include "sqlite_database_broker.h"

#include <QMetaObject>

// 构造函数
DatabaseWorker::DatabaseWorker(SqliteDatabaseBroker *database)
    : _database(database)
{
}

// 析构函数
DatabaseWorker::~DatabaseWorker()
{
    if (_database) {
        // Worker 在所属数据库线程析构，清理该线程专属 SQLite 连接
        _database->closeConnectionForCurrentThread();
    }
}

// 提交保存类任务
void DatabaseWorker::submitSave(const DatabaseTask &task)
{
    submitTask(task);
}

// 提交加载类任务
void DatabaseWorker::submitLoad(const DatabaseTask &task)
{
    submitTask(task);
}

// 提交删除类任务
void DatabaseWorker::submitDelete(const DatabaseTask &task)
{
    submitTask(task);
}

// 开始关闭并通知已排空
void DatabaseWorker::beginShutdown()
{
    // 此方法通过 QueuedConnection 投递到 Worker 线程；按事件队列 FIFO 语义，
    // 执行到这里时它之前提交的数据库任务已经全部处理完成。
    _acceptingTasks.store(false);
    emit drained();
}

// 将任务投递到 Worker 所在线程
void DatabaseWorker::submitTask(const DatabaseTask &task)
{
    if (!_acceptingTasks.load()) {
        return;
    }

    // 调用方通过 QueuedConnection 投递，任务在 Worker 所在线程串行执行
    QMetaObject::invokeMethod(this, [this, task] {
        executeTask(task);
    }, Qt::QueuedConnection);
}

// 执行单个数据库任务并发出结果
void DatabaseWorker::executeTask(const DatabaseTask &task)
{
    if (!_database) {
        emit taskFinished(false, "数据库入口未初始化");
        return;
    }

    // 数据库入口按当前线程创建独立连接，避免跨线程传递 QSqlDatabase
    QString errorMessage;
    const bool succeeded = task(*_database, &errorMessage);
    emit taskFinished(succeeded, errorMessage);
}
