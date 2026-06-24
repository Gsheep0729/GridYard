/**
* @file    database_worker.cpp
* @version 6.5.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 异步任务执行线程实现
*
* Change Log:
* [v6.5.0] GY 2026-06-25
* * 支持退出前排空已提交的存储任务
* [v6.0.0] GY 2026-06-25
* * 新增数据库串行任务 Worker
*/

#include "database_worker.h"

#include "sqlite_database_proxy.h"

#include <QMetaObject>

// 构造函数
DatabaseWorker::DatabaseWorker(SqliteDatabaseProxy *database)
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
        emit taskFinished(false, "数据库代理未初始化");
        return;
    }

    // 代理按当前线程创建独立连接，避免跨线程传递 QSqlDatabase
    QString errorMessage;
    const bool succeeded = task(*_database, &errorMessage);
    emit taskFinished(succeeded, errorMessage);
}
