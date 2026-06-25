/**
* @file    sqlite_database_proxy.h
* @version 6.6.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 连接、参数与迁移管理
*
* 每个线程按唯一连接名取得自己的数据库连接，禁止跨线程传递连接。
*
* Change Log:
* [v6.6.0] GY 2026-06-25
* * 增加损坏数据库备份重建和异常验收支撑
* [v6.0.0] GY 2026-06-25
* * 新增 SQLite 数据库代理基础
*/

#pragma once

#include <functional>

#include <QString>
#include <QStringList>

class QSqlDatabase;

class SqliteDatabaseProxy {
public:
    using TransactionTask = std::function<bool(QSqlDatabase &, QString *)>;
    using DriverProvider = std::function<QStringList()>;

    SqliteDatabaseProxy();
    explicit SqliteDatabaseProxy(DriverProvider driverProvider);
    ~SqliteDatabaseProxy();

    SqliteDatabaseProxy(const SqliteDatabaseProxy &) = delete;
    SqliteDatabaseProxy &operator=(const SqliteDatabaseProxy &) = delete;

    // 打开数据库、配置连接参数并执行迁移
    bool initialize(const QString &databasePath, QString *errorMessage);
    // 返回当前线程独占的数据库连接
    QSqlDatabase connectionForWorkerThread(QString *errorMessage) const;
    // 关闭并移除当前线程的命名连接
    void closeConnectionForCurrentThread() const;
    // 在短事务中执行跨表持久化任务
    bool runInTransaction(const TransactionTask &task, QString *errorMessage) const;
    // 获取当前 Schema 版本
    int schemaVersion() const;
    // 判断数据库是否已成功初始化
    bool isAvailable() const;

private:
    // 打开主连接并完成参数配置与迁移
    bool openMainConnection(QString *errorMessage);
    // 关闭并移除初始化线程的主连接
    void closeMainConnection();
    // 应用所有线程都需要的 SQLite 连接参数
    bool configureConnection(QSqlDatabase &database, QString *errorMessage) const;
    // 将损坏数据库备份到同目录的 .corrupt 时间戳文件
    bool backupCorruptDatabase(QString *errorMessage) const;
    // 判断底层错误是否属于 SQLite 文件损坏
    static bool isCorruptionError(const QString &errorMessage);
    // 根据当前线程生成唯一连接名称
    QString connectionNameForCurrentThread() const;

    DriverProvider _driverProvider;   // 驱动列表来源，测试可替换
    QString _databasePath;            // SQLite 数据库绝对路径
    QString _mainConnectionName;      // 初始化线程使用的连接名称
    bool _available = false;          // 初始化及迁移是否成功
};
