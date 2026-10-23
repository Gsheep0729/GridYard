/**
* @file    message_repository_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SQLite 聊天消息仓储实现
*
* SQL 逐字复用 Qt 版字面量并采用预编译参数绑定；消息写入前先确保
* 会话行存在。
*/

#include "core/storage/message_repository_core.h"

#include "core/storage/database_core.h"
#include "core/storage/iso_time.h"
#include "core/storage/sqlite_stmt.h"

namespace gy::storage {
namespace {

// 将消息查询的当前行映射为领域记录，列序与 SELECT 清单一致
MessageRecord readMessageRecordRow(const SqliteStmt &query)
{
    MessageRecord record;
    record.messageId = query.columnText(0);
    record.peerDeviceId = query.columnText(1);
    record.direction = static_cast<RecordDirection>(query.columnInt64(2));
    record.senderDeviceId = query.columnText(3);
    record.senderName = query.columnText(4);
    record.content = query.columnText(5);
    record.sentAtMs = query.columnIsNull(6) ? 0 : parseIso8601Ms(query.columnText(6)).value_or(0);
    record.localStatus = static_cast<int>(query.columnInt64(7));
    record.createdAtMs = query.columnIsNull(8) ? 0 : parseIso8601Ms(query.columnText(8)).value_or(0);
    return record;
}

}  // namespace

// 构造函数
MessageRepositoryCore::MessageRepositoryCore(DatabaseCore *database) : _database(database)
{
}

// 幂等保存聊天消息
bool MessageRepositoryCore::saveMessage(const MessageRecord &record, std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    // SQL 与组合事务共用同一 Step，避免双份字面量漂移
    return _database->runInTransaction(saveMessageStep(record), errorMessage);
}

// 加载全部聊天记录（备份导出用），按 (sent_at, message_id) 排序保证顺序稳定
std::vector<MessageRecord> MessageRepositoryCore::allMessages(std::string *errorMessage) const
{
    std::vector<MessageRecord> records;
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }
    SqliteStmt query{_database->connection(),
                     "SELECT message_id, peer_device_id, direction, sender_device_id, "
                     "sender_name, content, sent_at, local_status, created_at "
                     "FROM chat_messages ORDER BY sent_at, message_id"};
    auto row = query.step();
    while (row.has_value() && row.value()) {
        // 存储层完成行到领域值对象的映射，列序与 loadMessages 保持一致
        records.push_back(readMessageRecordRow(query));
        row = query.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }
    return records;
}

// 按游标分页加载聊天记录
std::vector<MessageRecord> MessageRepositoryCore::loadMessages(const MessageCursor &cursor,
                                                               int limit,
                                                               std::string *errorMessage) const
{
    std::vector<MessageRecord> records;
    if (cursor.peerDeviceId.empty() || limit <= 0) {
        // 空会话和非正页大小没有可定义的查询语义，避免构造宽泛 SQL。
        return records;
    }
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }

    if (!cursor.beforeSentAtMs.has_value()) {
        // 首次加载按时间倒序取得最新页，供启动恢复和会话首屏使用。
        SqliteStmt query{_database->connection(),
                         "SELECT message_id, peer_device_id, direction, sender_device_id, "
                         "sender_name, content, sent_at, local_status, created_at "
                         "FROM chat_messages WHERE peer_device_id=? "
                         "ORDER BY sent_at DESC, message_id DESC LIMIT ?"};
        query.bind(1, cursor.peerDeviceId);
        query.bind(2, limit);
        auto row = query.step();
        while (row.has_value() && row.value()) {
            records.push_back(readMessageRecordRow(query));
            row = query.step();
        }
        if (!row.has_value() && errorMessage) {
            *errorMessage = row.error().message;
        }
        return records;
    }

    // 时间相同的消息再按 message_id 比较，保证翻页不重不漏。
    SqliteStmt query{_database->connection(),
                     "SELECT message_id, peer_device_id, direction, sender_device_id, "
                     "sender_name, content, sent_at, local_status, created_at "
                     "FROM chat_messages WHERE peer_device_id=? "
                     "AND (sent_at < ? OR (sent_at = ? AND message_id < ?)) "
                     "ORDER BY sent_at DESC, message_id DESC LIMIT ?"};
    query.bind(1, cursor.peerDeviceId);
    query.bind(2, toIso8601Ms(*cursor.beforeSentAtMs));
    query.bind(3, toIso8601Ms(*cursor.beforeSentAtMs));
    query.bind(4, cursor.beforeMessageId);
    query.bind(5, limit);
    auto row = query.step();
    while (row.has_value() && row.value()) {
        // 存储层完成行到领域值对象的映射，不向应用层泄露底层语句。
        records.push_back(readMessageRecordRow(query));
        row = query.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }
    return records;
}

// 纯 SQL 幂等写入步骤，不自开事务；调用方须在事务内使用
MessageRepositoryCore::SqlStep MessageRepositoryCore::saveMessageStep(const MessageRecord &record)
{
    return [record](SqliteConn &database, std::string *taskError) {
        // 先占用会话行（ON CONFLICT DO NOTHING 使其幂等）
        SqliteStmt conv{database,
                        "INSERT INTO chat_conversations(peer_device_id, created_at, last_message_at) "
                        "VALUES(?, ?, ?) ON CONFLICT(peer_device_id) DO NOTHING"};
        conv.bind(1, record.peerDeviceId);
        conv.bind(2, toIso8601Ms(record.createdAtMs));
        conv.bind(3, toIso8601Ms(record.sentAtMs));
        if (auto row = conv.step(); !row.has_value()) {
            if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        }

        // 写入消息（message_id 是网络层 UUID，冲突则视为已保存，支持重试和重复帧）
        SqliteStmt msg{database,
                       "INSERT INTO chat_messages(message_id, peer_device_id, direction, "
                       "sender_device_id, sender_name, content, sent_at, local_status, created_at) "
                       "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(message_id) DO NOTHING"};
        msg.bind(1, record.messageId);
        msg.bind(2, record.peerDeviceId);
        msg.bind(3, static_cast<int64_t>(record.direction));
        msg.bind(4, record.senderDeviceId);
        msg.bind(5, record.senderName);
        msg.bind(6, record.content);
        msg.bind(7, toIso8601Ms(record.sentAtMs));
        msg.bind(8, record.localStatus);
        msg.bind(9, toIso8601Ms(record.createdAtMs));
        if (auto row = msg.step(); !row.has_value()) {
            if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        }

        if (database.changesCount() == 0) {
            // 旧消息（重复帧）不推动会话排序
            return true;
        }

        // 更新会话最近消息时间
        SqliteStmt act{database,
                       "UPDATE chat_conversations SET last_message_at=MAX(last_message_at, ?) "
                       "WHERE peer_device_id=?"};
        act.bind(1, toIso8601Ms(record.sentAtMs));
        act.bind(2, record.peerDeviceId);
        if (auto row = act.step(); !row.has_value()) {
            if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        }
        return true;
    };
}

// 删除指定设备的所有聊天记录（CASCADE 同时清理会话行）
bool MessageRepositoryCore::deleteConversation(const std::string &deviceId,
                                               std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    // 删除父会话依赖外键 CASCADE 清理消息，避免两条删除语句之间留下孤儿记录。
    return _database->runInTransaction(
        [&deviceId](SqliteConn &database, std::string *taskError) {
            SqliteStmt query{database, "DELETE FROM chat_conversations WHERE peer_device_id=?"};
            query.bind(1, deviceId);
            if (auto row = query.step(); row.has_value()) {
                return true;
            } else if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        },
        errorMessage);
}

// 删除单条聊天消息
bool MessageRepositoryCore::deleteMessage(const std::string &messageId,
                                          std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&messageId](SqliteConn &database, std::string *taskError) {
            SqliteStmt query{database, "DELETE FROM chat_messages WHERE message_id=?"};
            query.bind(1, messageId);
            if (auto row = query.step(); row.has_value()) {
                return true;
            } else if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        },
        errorMessage);
}

// 删除早于指定时间的聊天记录
bool MessageRepositoryCore::deleteExpiredMessages(int64_t beforeMs, std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [beforeMs](SqliteConn &database, std::string *taskError) {
            SqliteStmt query{database, "DELETE FROM chat_messages WHERE sent_at < ?"};
            // 过期边界使用严格小于，截止时刻本身仍可被用户查询。
            query.bind(1, toIso8601Ms(beforeMs));
            if (auto row = query.step(); row.has_value()) {
                return true;
            } else if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        },
        errorMessage);
}

// 清空全部聊天消息和会话
bool MessageRepositoryCore::clearAllMessages(std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [](SqliteConn &database, std::string *taskError) {
            if (database.exec("DELETE FROM chat_conversations").has_value()) {
                return true;
            }
            if (taskError) {
                *taskError = database.lastError("清空聊天失败").message;
            }
            return false;
        },
        errorMessage);
}

}  // namespace gy::storage
