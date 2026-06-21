/**
* @file    database_worker.cpp
* @version 6.0.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 异步任务执行线程实现
*
* Change Log:
* [v6.0.0] GY 2026-06-25
* * 新增数据库串行任务 Worker
*/

#include "database_worker.h"

#include "sqlite_database_proxy.h"

#include <QMetaObject>

DatabaseWorker::DatabaseWorker(SqliteDatabaseProxy *database)
    : _database(database)
{
}

DatabaseWorker::~DatabaseWorker()
{
    if (_database) {
        // Worker 在所属数据库线程析构，清理该线程专属 SQLite 连接
        _database->closeConnectionForCurrentThread();
    }
}

void DatabaseWorker::submitSave(const DatabaseTask &task)
{
    submitTask(task);
}

void DatabaseWorker::submitLoad(const DatabaseTask &task)
{
    submitTask(task);
}

void DatabaseWorker::submitDelete(const DatabaseTask &task)
{
    submitTask(task);
}

void DatabaseWorker::submitTask(const DatabaseTask &task)
{
    // 调用方通过 QueuedConnection 投递，任务在 Worker 所在线程串行执行
    QMetaObject::invokeMethod(this, [this, task] {
        executeTask(task);
    }, Qt::QueuedConnection);
}

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
