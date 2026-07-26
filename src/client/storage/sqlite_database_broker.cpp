/**
* @file    sqlite_database_broker.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 连接、参数与迁移管理实现
*
* 管理每个线程独立的命名连接、PRAGMA 参数配置、WAL 模式和
* migration 版本序列。连接由 DatabaseWorker 在专用线程中持有，
* 应用退出时自动关闭并移除连接名。
*
* Change Log:
* [v6.8.1] GY   2026-06-29
* * 析构时同步关闭当前线程工作连接，避免连接名复用指向旧数据库
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.6.0] GY 2026-06-25
* * 增加损坏数据库备份重建和异常验收支撑
* [v6.0.0] GY 2026-06-25
* * 新增 SQLite 数据库 Broker 基础
*/
#include "sqlite_database_broker.h"

#include "migration_runner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

#include <utility>

// 构造函数
SqliteDatabaseBroker::SqliteDatabaseBroker()
    : _driverProvider(QSqlDatabase::drivers)
{
}

// 使用自定义驱动来源构造，供缺失驱动场景测试
SqliteDatabaseBroker::SqliteDatabaseBroker(DriverProvider driverProvider)
    : _driverProvider(std::move(driverProvider))
{
}

// 析构函数，关闭并移除初始化线程的命名连接
SqliteDatabaseBroker::~SqliteDatabaseBroker()
{
    closeConnectionForCurrentThread();
    closeMainConnection();
}

// 打开数据库、配置连接参数并执行迁移
bool SqliteDatabaseBroker::initialize(const QString &databasePath, QString *errorMessage)
{
    _available = false;
    if (errorMessage) {
        errorMessage->clear();
    }

    // 驱动不可用时静默降级，不阻塞在线收发
    if (!_driverProvider().contains("QSQLITE")) {
        if (errorMessage) {
            *errorMessage = "QSQLITE 驱动不可用";
        }
        qWarning() << "[Storage] QSQLITE 驱动不可用，历史功能已降级";
        return false;
    }

    const QFileInfo fileInfo(databasePath);
    // 首次使用时自动创建数据库目录
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        if (errorMessage) {
            *errorMessage = "无法创建数据库目录";
        }
        return false;
    }

    closeMainConnection();  // 重复初始化前先释放旧主连接，避免复用旧文件句柄
    _databasePath = fileInfo.absoluteFilePath();
    const bool existingDatabase = QFileInfo::exists(_databasePath);
    // 主连接名含实例指针地址，确保同一进程多 Broker 不冲突
    _mainConnectionName = QString("gridyard-storage-main-%1")
        .arg(reinterpret_cast<quintptr>(this));

    if (openMainConnection(errorMessage)) {
        _available = true;
        qInfo() << "[Storage] 数据库迁移完成，版本" << schemaVersion();
        return true;
    }

    // 首次打开失败后判断是否为文件损坏，尝试备份重建
    const QString firstError = errorMessage ? *errorMessage : QString{};
    qWarning() << "[Storage] 数据库初始化失败";
    closeMainConnection();  // 备份损坏库前必须释放 SQLite 文件句柄

    if (existingDatabase && isCorruptionError(firstError)) {
        QString backupError;
        if (!backupCorruptDatabase(&backupError)) {
            if (errorMessage) {
                *errorMessage = backupError;
            }
            return false;
        }

        // 备份成功后重新创建空库并迁移
        if (errorMessage) {
            errorMessage->clear();
        }
        if (openMainConnection(errorMessage)) {
            _available = true;
            qWarning() << "[Storage] 已备份损坏数据库并重建本地历史库";
            qInfo() << "[Storage] 数据库迁移完成，版本" << schemaVersion();
            return true;
        }
        qWarning() << "[Storage] 损坏数据库重建失败";
    }

    closeMainConnection();
    return false;
}

// 打开主连接并完成参数配置与迁移
bool SqliteDatabaseBroker::openMainConnection(QString *errorMessage)
{
    QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", _mainConnectionName);
    database.setDatabaseName(_databasePath);

    if (!database.open()) {
        if (errorMessage) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    if (!configureConnection(database, errorMessage)) {
        return false;
    }
    return MigrationRunner::migrate(database, errorMessage);
}

// 关闭并移除初始化线程的主连接
void SqliteDatabaseBroker::closeMainConnection()
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

// 返回当前线程独占的数据库连接
QSqlDatabase SqliteDatabaseBroker::connectionForWorkerThread(QString *errorMessage) const
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
void SqliteDatabaseBroker::closeConnectionForCurrentThread() const
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
bool SqliteDatabaseBroker::runInTransaction(const TransactionTask &task, QString *errorMessage) const
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

bool SqliteDatabaseBroker::runSteps(const std::vector<TransactionTask> &steps,
                                   QString *errorMessage) const
{
    QSqlDatabase database = connectionForWorkerThread(errorMessage);
    if (!database.isValid() || !database.transaction()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    for (const auto &step : steps) {
        if (!step(database, errorMessage)) {
            database.rollback();
            return false;
        }
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
int SqliteDatabaseBroker::schemaVersion() const
{
    QString errorMessage;
    QSqlDatabase database = connectionForWorkerThread(&errorMessage);
    if (!database.isValid()) {
        return -1;
    }
    return MigrationRunner::schemaVersion(database, &errorMessage);
}

// 判断数据库是否已成功初始化
bool SqliteDatabaseBroker::isAvailable() const
{
    return _available;
}

// 应用所有线程都需要的 SQLite 连接参数
bool SqliteDatabaseBroker::configureConnection(QSqlDatabase &database, QString *errorMessage) const
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

// 将损坏数据库备份到同目录的 .corrupt 时间戳文件
bool SqliteDatabaseBroker::backupCorruptDatabase(QString *errorMessage) const
{
    const QString timestamp = QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz");
    const QString backupPath = _databasePath + ".corrupt-" + timestamp;
    if (!QFile::rename(_databasePath, backupPath)) {
        if (errorMessage) {
            *errorMessage = "无法备份损坏数据库";
        }
        return false;
    }

    const QStringList sidecars = {"-wal", "-shm"};
    for (const QString &suffix : sidecars) {
        const QString sidecarPath = _databasePath + suffix;
        if (!QFileInfo::exists(sidecarPath)) {
            continue;
        }
        // WAL/SHM 属于同一个 SQLite 文件组，能备份则一起保留供事后排查。
        QFile::rename(sidecarPath, backupPath + suffix);
    }

    return true;
}

// 判断底层错误是否属于 SQLite 文件损坏
bool SqliteDatabaseBroker::isCorruptionError(const QString &errorMessage)
{
    const QString normalized = errorMessage.toLower();
    return normalized.contains("file is not a database")
           || normalized.contains("database disk image is malformed")
           || normalized.contains("not a database")
           || normalized.contains("malformed");
}

// 根据当前线程生成唯一连接名称
QString SqliteDatabaseBroker::connectionNameForCurrentThread() const
{
    return QString("gridyard-storage-%1-%2")
        .arg(reinterpret_cast<quintptr>(this))
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
}
