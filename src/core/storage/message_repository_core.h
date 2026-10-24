/**
* @file    message_repository_core.h
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   纯 C++23 SQLite 聊天消息仓储——不依赖任何 Qt 类型
*
* 负责聊天消息的幂等写入、游标分页加载、删除和过期清理，SQL 语义由存储测试锁定。
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/storage/storage_records.h"

namespace gy::storage {

class DatabaseCore;
class SqliteConn;

// 聊天消息仓储纯 C++ 实现
class MessageRepositoryCore {
public:
    // 只执行 SQL、不自开事务的任务，供调用方把多表写入组合进单个事务
    using SqlStep = std::function<bool(SqliteConn &, std::string *)>;

    // 构造消息仓储
    explicit MessageRepositoryCore(DatabaseCore *database);

    // 幂等写入消息及其所属会话
    bool saveMessage(const MessageRecord &record, std::string *errorMessage);
    // 按稳定游标倒序读取一页消息
    std::vector<MessageRecord> loadMessages(const MessageCursor &cursor, int limit,
                                            std::string *errorMessage) const;
    // 加载全部聊天记录（备份导出用）
    std::vector<MessageRecord> allMessages(std::string *errorMessage) const;
    // 删除单条聊天消息
    bool deleteMessage(const std::string &messageId, std::string *errorMessage);
    // 删除会话及其级联消息
    bool deleteConversation(const std::string &deviceId, std::string *errorMessage);
    // 删除早于指定时间的消息
    bool deleteExpiredMessages(int64_t beforeMs, std::string *errorMessage);
    // 清空全部聊天消息
    bool clearAllMessages(std::string *errorMessage);

    // 以下 Step 返回纯 SQL 步骤，由调用方置于同一事务内组合执行（幂等，不自开事务）
    static SqlStep saveMessageStep(const MessageRecord &record);
    // 提交方需在事务成功后自己更新会话排序（last_message_at）

private:
    DatabaseCore *_database = nullptr;  // 数据库连接和事务入口
};

}  // namespace gy::storage
