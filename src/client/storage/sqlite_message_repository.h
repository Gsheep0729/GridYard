/**
* @file    sqlite_message_repository.h
* @version 7.15.19
* @date    2026-10-04
* @author  GridYard Team
* @brief   SQLite 聊天消息 Repository 实现
*
* 负责聊天消息的幂等写入、游标分页加载、删除和过期清理。
* 所有 SQL 均采用预编译参数绑定，禁止字符串拼接。
*
* Change Log:
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.15.5] GY   2026-10-03
* * saveMessage 复用 saveMessageStep（ADR-006 方案 c）
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.2.0] GY 2026-06-25
* * 新增聊天消息 SQLite Repository
*/

#pragma once

#include "history_repositories.h"

#include <QDateTime>
#include <QString>

#include <functional>

class QSqlDatabase;
class SqliteDatabaseBroker;

class SqliteMessageRepository : public IMessageRepository {
public:
    using SqlStep = std::function<bool(QSqlDatabase &, QString *)>;

    // 构造消息 Repository
    explicit SqliteMessageRepository(SqliteDatabaseBroker *database);

    // 幂等写入消息及其所属会话
    virtual bool saveMessage(const MessageRecord &record, QString *errorMessage) override;
    // 按稳定游标倒序读取一页消息
    virtual QList<MessageRecord> loadMessages(const MessageCursor &cursor, int limit, QString *errorMessage) const override;
    // 删除单条聊天消息
    virtual bool deleteMessage(const QString &messageId, QString *errorMessage) override;
    // 删除会话及其级联消息
    virtual bool deleteConversation(const QString &deviceId, QString *errorMessage) override;
    // 删除早于指定时间的消息
    virtual bool deleteExpiredMessages(const QDateTime &before, QString *errorMessage) override;
    // 清空全部聊天消息
    virtual bool clearAllMessages(QString *errorMessage) override;

    // 以下 Step 返回纯 SQL 步骤，由调用方置于同一事务内组合执行（幂等，不自开事务）
    static SqlStep saveMessageStep(const MessageRecord &record);
    // 提交方需在事务成功后自己更新会话排序（last_message_at）

private:
    SqliteDatabaseBroker *_database = nullptr;  // 数据库连接和事务入口
};
