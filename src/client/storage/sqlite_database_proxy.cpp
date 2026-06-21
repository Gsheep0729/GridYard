/**
* @file    sqlite_database_proxy.cpp
* @version 6.0.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 连接、参数与迁移管理实现
*
* Change Log:
* [v6.0.0] GY 2026-06-25
* * 新增 SQLite 数据库代理基础
*/
#include "sqlite_database_proxy.h"

#include "migration_runner.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

// 构造函数
SqliteDatabaseProxy::SqliteDatabaseProxy()
    : _driverProvider(QSqlDatabase::drivers)
{
}

// 使用自定义驱动来源构造，供缺失驱动场景测试
SqliteDatabaseProxy::SqliteDatabaseProxy(DriverProvider driverProvider)
    : _driverProvider(std::move(driverProvider))
{
}

// 析构函数，关闭并移除初始化线程的命名连接
SqliteDatabaseProxy::~SqliteDatabaseProxy()
{
    if (_mainConnectionName.isEmpty() || !QSqlDatabase::contains(_mainConnectionName)) {
        return;
    }

    // 移除连接前先释放 QSqlDatabase 值，避免 Qt 输出仍在使用的连接警告
    QSqlDatabase database = QSqlDatabase::database(_mainConnectionName, false);
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(_mainConnectionName);
}

// 打开数据库、配置连接参数并执行迁移
bool SqliteDatabaseProxy::initialize(const QString &databasePath, QString *errorMessage)
{
    _available = false;

    // 重复初始化时先释放旧连接，确保数据库路径和连接名称保持一致
    if (!_mainConnectionName.isEmpty() && QSqlDatabase::contains(_mainConnectionName)) {
        QSqlDatabase oldDatabase = QSqlDatabase::database(_mainConnectionName, false);
        oldDatabase.close();
        oldDatabase = {};
        QSqlDatabase::removeDatabase(_mainConnectionName);
    }

    if (!_driverProvider().contains("QSQLITE")) {
        if (errorMessage) {
            *errorMessage = "QSQLITE 驱动不可用";
        }
        qWarning() << "[Storage] QSQLITE 驱动不可用，历史功能已降级";
        return false;
    }

    const QFileInfo fileInfo(databasePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = "无法创建数据库目录";
        }
        return false;
    }

    _databasePath = fileInfo.absoluteFilePath();
    _mainConnectionName = QString("gridyard-storage-main-%1")
        .arg(reinterpret_cast<quintptr>(this));
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", _mainConnectionName);
    database.setDatabaseName(_databasePath);

    if (!database.open() || !configureConnection(database, errorMessage)
        || !MigrationRunner::migrate(database, errorMessage)) {
        qWarning() << "[Storage] 数据库初始化失败";
        database.close();
        return false;
    }

    _available = true;
    qInfo() << "[Storage] 数据库迁移完成，版本" << schemaVersion();
    return true;
}

// 返回当前线程独占的数据库连接
QSqlDatabase SqliteDatabaseProxy::connectionForWorkerThread(QString *errorMessage) const
{
    if (!_available) {
        if (errorMessage) {
            *errorMessage = "本地历史不可用";
        }
        return {};
    }

    const QString connectionName = connectionNameForCurrentThread();
    QSqlDatabase database = QSqlDatabase::contains(connectionName)
        ? QSqlDatabase::database(connectionName)
        : QSqlDatabase::addDatabase("QSQLITE", connectionName);
    if (database.isOpen()) {
        return database;
    }

    database.setDatabaseName(_databasePath);
    if (!database.open() || !configureConnection(database, errorMessage)) {
        return {};
    }
    return database;
}

// 关闭并移除当前线程的命名连接
void SqliteDatabaseProxy::closeConnectionForCurrentThread() const
{
    const QString connectionName = connectionNameForCurrentThread();
    if (!QSqlDatabase::contains(connectionName)) {
        return;
    }

    // QSqlDatabase 句柄离开作用域后才能安全移除全局命名连接
    QSqlDatabase database = QSqlDatabase::database(connectionName, false);
    database.close();
    database = {};
    QSqlDatabase::removeDatabase(connectionName);
}

// 在短事务中执行跨表持久化任务
bool SqliteDatabaseProxy::runInTransaction(const TransactionTask &task, QString *errorMessage) const
{
    QSqlDatabase database = connectionForWorkerThread(errorMessage);
    if (!database.isValid() || !database.transaction()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    if (!task(database, errorMessage)) {
        database.rollback();  // 业务步骤失败时撤销本次跨表变更
        return false;
    }

    if (!database.commit()) {
        if (errorMessage) {
            *errorMessage = database.lastError().text();
        }
        database.rollback();
        return false;
    }
    return true;
}

// 获取当前 Schema 版本
int SqliteDatabaseProxy::schemaVersion() const
{
    QString errorMessage;
    QSqlDatabase database = connectionForWorkerThread(&errorMessage);
    if (!database.isValid()) {
        return -1;
    }
    return MigrationRunner::schemaVersion(database, &errorMessage);
}

// 判断数据库是否已成功初始化
bool SqliteDatabaseProxy::isAvailable() const
{
    return _available;
}

// 应用所有线程都需要的 SQLite 连接参数
bool SqliteDatabaseProxy::configureConnection(QSqlDatabase &database, QString *errorMessage) const
{
    QSqlQuery query(database);
    const QStringList pragmas = {
        "PRAGMA foreign_keys = ON",
        "PRAGMA journal_mode = WAL",
        "PRAGMA synchronous = NORMAL",
        "PRAGMA busy_timeout = 5000"  // 短暂锁竞争最多等待五秒
    };

    for (const QString &pragma : pragmas) {
        if (query.exec(pragma)) {
            continue;
        }
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }
    return true;
}

// 根据当前线程生成唯一连接名称
QString SqliteDatabaseProxy::connectionNameForCurrentThread() const
{
    return QString("gridyard-storage-%1-%2")
        .arg(reinterpret_cast<quintptr>(this))
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}
