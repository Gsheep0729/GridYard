/**
* @file    sqlite_message_proxy.h
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   SQLite 聊天消息 Repository 实现
*
* 负责聊天消息的幂等写入、游标分页加载、删除和过期清理。
* 所有 SQL 均采用预编译参数绑定，禁止字符串拼接。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.2.0] GY 2026-06-25
* * 新增聊天消息 SQLite Proxy
*/

#pragma once

#include "history_repositories.h"

class SqliteDatabaseProxy;

class SqliteMessageProxy : public IMessageRepository {
public:
    // 构造消息 Repository
    explicit SqliteMessageProxy(SqliteDatabaseProxy *database);

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

private:
    SqliteDatabaseProxy *_database = nullptr;  // 数据库连接和事务入口
};
