/**
* @file    sqlite_message_proxy.cpp
* @version 6.2.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 聊天消息 Repository 实现
*
* 所有 SQL 均采用预编译参数绑定；消息写入前先确保会话行存在。
*
* Change Log:
* [v6.2.0] GY 2026-06-25
* * 新增聊天消息 SQLite Proxy
*/

#include "sqlite_message_proxy.h"

#include "sqlite_database_proxy.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
// 将 UTC 时间转换为 SQLite 使用的 ISO 文本
QString sqlTime(const QDateTime &time) {
    return time.toUTC().toString(Qt::ISODateWithMs);
}
} // namespace

// 构造函数
SqliteMessageProxy::SqliteMessageProxy(SqliteDatabaseProxy *database)
    : _database(database)
{
}

// 幂等保存聊天消息
bool SqliteMessageProxy::saveMessage(const MessageRecord &record, QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&record](QSqlDatabase &database, QString *taskError) {
            QSqlQuery conversationQuery(database);
            // 先占用会话行才能满足 chat_messages 的外键；重复保存不改写创建时间。
            conversationQuery.prepare(
                "INSERT INTO chat_conversations(peer_device_id, created_at, last_message_at) "
                "VALUES(?, ?, ?) ON CONFLICT(peer_device_id) DO NOTHING");
            conversationQuery.addBindValue(record.peerDeviceId);
            conversationQuery.addBindValue(sqlTime(record.createdAt));
            conversationQuery.addBindValue(sqlTime(record.sentAt));
            if (!conversationQuery.exec()) {
                if (taskError) {
                    *taskError = conversationQuery.lastError().text();
                }
                return false;
            }

            QSqlQuery messageQuery(database);
            // message_id 是网络层 UUID，冲突时视为已成功保存以支持重试和重复帧。
            messageQuery.prepare(
                "INSERT INTO chat_messages(message_id, peer_device_id, direction, "
                "sender_device_id, sender_name, content, sent_at, local_status, created_at) "
                "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT(message_id) DO NOTHING");
            messageQuery.addBindValue(record.messageId);
            messageQuery.addBindValue(record.peerDeviceId);
            messageQuery.addBindValue(static_cast<int>(record.direction));
            messageQuery.addBindValue(record.senderDeviceId);
            messageQuery.addBindValue(record.senderName);
            messageQuery.addBindValue(record.content);
            messageQuery.addBindValue(sqlTime(record.sentAt));
            messageQuery.addBindValue(record.localStatus);
            messageQuery.addBindValue(sqlTime(record.createdAt));
            if (!messageQuery.exec()) {
                if (taskError) {
                    *taskError = messageQuery.lastError().text();
                }
                return false;
            }

            if (messageQuery.numRowsAffected() == 0) {
                // 旧消息不应推动会话排序，避免迟到重试把会话顶到最新。
                return true;
            }

            QSqlQuery activityQuery(database);
            // ISO UTC 文本可按字典序比较，MAX 可防止离序消息回退最近活动时间。
            activityQuery.prepare(
                "UPDATE chat_conversations SET last_message_at=MAX(last_message_at, ?) "
                "WHERE peer_device_id=?");
            activityQuery.addBindValue(sqlTime(record.sentAt));
            activityQuery.addBindValue(record.peerDeviceId);
            if (activityQuery.exec()) {
                return true;
            }
            if (taskError) {
                *taskError = activityQuery.lastError().text();
            }
            return false;
        },
        errorMessage);
}

// 按游标分页加载聊天记录
QList<MessageRecord> SqliteMessageProxy::loadMessages(const MessageCursor &cursor,
                                                        int limit,
                                                        QString *errorMessage) const
{
    QList<MessageRecord> records;
    if (cursor.peerDeviceId.isEmpty() || limit <= 0) {
        // 空会话和非正页大小没有可定义的查询语义，避免构造宽泛 SQL。
        return records;
    }
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return records;
    }

    QSqlDatabase database = _database->connectionForWorkerThread(errorMessage);
    if (!database.isValid()) {
        return records;
    }

    QSqlQuery query(database);
    if (cursor.beforeSentAt.isNull()) {
        // 首次加载按时间倒序取得最新页，供启动恢复和会话首屏使用。
        query.prepare("SELECT message_id, peer_device_id, direction, sender_device_id, "
                      "sender_name, content, sent_at, local_status, created_at "
                      "FROM chat_messages WHERE peer_device_id=? "
                      "ORDER BY sent_at DESC, message_id DESC LIMIT ?");
        query.addBindValue(cursor.peerDeviceId);
        query.addBindValue(limit);
    } else {
        // 时间相同的消息再按 message_id 比较，保证翻页不重不漏。
        query.prepare("SELECT message_id, peer_device_id, direction, sender_device_id, "
                      "sender_name, content, sent_at, local_status, created_at "
                      "FROM chat_messages WHERE peer_device_id=? "
                      "AND (sent_at < ? OR (sent_at = ? AND message_id < ?)) "
                      "ORDER BY sent_at DESC, message_id DESC LIMIT ?");
        query.addBindValue(cursor.peerDeviceId);
        query.addBindValue(sqlTime(cursor.beforeSentAt));
        query.addBindValue(sqlTime(cursor.beforeSentAt));
        query.addBindValue(cursor.beforeMessageId);
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return records;
    }

    while (query.next()) {
        // 存储层完成行到领域值对象的映射，不向应用层泄露 QSqlQuery。
        MessageRecord record;
        record.messageId = query.value(0).toString();
        record.peerDeviceId = query.value(1).toString();
        record.direction = static_cast<RecordDirection>(query.value(2).toInt());
        record.senderDeviceId = query.value(3).toString();
        record.senderName = query.value(4).toString();
        record.content = query.value(5).toString();
        record.sentAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
        record.localStatus = query.value(7).toInt();
        record.createdAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODateWithMs);
        records.append(record);
    }

    return records;
}

// 删除指定设备的所有聊天记录（CASCADE 同时清理会话行）
bool SqliteMessageProxy::deleteConversation(const QString &deviceId, QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    // 删除父会话依赖外键 CASCADE 清理消息，避免两条删除语句之间留下孤儿记录。
    return _database->runInTransaction(
        [&deviceId](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            query.prepare("DELETE FROM chat_conversations WHERE peer_device_id=?");
            query.addBindValue(deviceId);
            if (query.exec()) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}

bool SqliteMessageProxy::deleteMessage(const QString &messageId, QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&messageId](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            query.prepare("DELETE FROM chat_messages WHERE message_id=?");
            query.addBindValue(messageId);
            if (query.exec()) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}

// 删除早于指定时间的聊天记录
bool SqliteMessageProxy::deleteExpiredMessages(const QDateTime &before, QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [&before](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            // 过期边界使用严格小于，截止时刻本身仍可被用户查询。
            query.prepare("DELETE FROM chat_messages WHERE sent_at < ?");
            query.addBindValue(sqlTime(before));
            if (query.exec()) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}

bool SqliteMessageProxy::clearAllMessages(QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库代理未初始化";
        }
        return false;
    }

    return _database->runInTransaction(
        [](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            if (query.exec("DELETE FROM chat_conversations")) {
                return true;
            }
            if (taskError) {
                *taskError = query.lastError().text();
            }
            return false;
        },
        errorMessage);
}
