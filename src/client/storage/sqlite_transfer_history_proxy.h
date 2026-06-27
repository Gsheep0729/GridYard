/**
* @file    sqlite_transfer_history_proxy.h
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   SQLite 传输历史 Repository 实现
*
* 负责结束态传输记录的幂等写入、筛选查询、删除和过期清理。
* 所有 SQL 均采用预编译参数绑定，禁止字符串拼接业务参数。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.3.0] GY 2026-06-25
* * 新增传输历史 SQLite Proxy
*/

#pragma once

#include "history_repositories.h"

class SqliteDatabaseProxy;

class SqliteTransferHistoryProxy : public ITransferHistoryRepository {
public:
    // 构造传输历史 Repository
    explicit SqliteTransferHistoryProxy(SqliteDatabaseProxy *database);

    // 以 session_id 为幂等键保存结束态传输记录
    virtual bool upsertFinishedTransfer(const TransferRecord &record, QString *errorMessage) override;
    // 按设备、状态和时间游标查询一页历史
    virtual QList<TransferRecord> queryTransfers(const TransferQuery &query, int limit, QString *errorMessage) const override;
    // 删除单条传输历史
    virtual bool deleteTransfer(const QString &recordId, QString *errorMessage) override;
    // 删除早于指定时间的传输历史
    virtual bool deleteExpiredTransfers(const QDateTime &before, QString *errorMessage) override;
    // 清空全部传输历史
    virtual bool clearAllTransfers(QString *errorMessage) override;

private:
    SqliteDatabaseProxy *_database = nullptr;  // 数据库连接和事务入口
};
