/**
* @file    migration_runner.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   SQLite Schema 版本迁移执行器实现
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增版本一 Schema 迁移
*/

#include "migration_runner.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace {
// 执行一条 DDL 语句，并将底层错误返回给调用方
bool execute(QSqlQuery &query, const QString &statement, QString *errorMessage)
{
    if (query.exec(statement)) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = query.lastError().text();
    }
    return false;
}
}

// 查询 schema_version 表获取最高版本号
int MigrationRunner::schemaVersion(QSqlDatabase &database, QString *errorMessage)
{
    QSqlQuery query(database);
    // 表不存在时 COALESCE 返回 0，兼容首次启动场景
    if (!query.exec("SELECT COALESCE(MAX(version), 0) FROM schema_version") || !query.next()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        return -1;
    }
    return query.value(0).toInt();
}

// 执行尚未应用的 Schema 迁移
bool MigrationRunner::migrate(QSqlDatabase &database, QString *errorMessage)
{
    QSqlQuery query(database);
    // 版本表必须最先创建，后续所有迁移通过它判断是否需要执行
    if (!execute(query, "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL)", errorMessage)) {
        return false;
    }

    const int version = schemaVersion(database, errorMessage);
    if (version < 0 || version >= 1) {  // 当前仅发布版本一迁移
        return version >= 0;
    }

    // 整个迁移在事务内执行，任一 DDL 失败则全部回滚，防止留下半成品 Schema
    if (!database.transaction()) {
        if (errorMessage) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    // 版本一：4 张业务表 + 3 个查询索引
    const QStringList statements = {
        // 设备目录：存储局域网内发现的对端设备快照
        "CREATE TABLE peer_devices (device_id TEXT PRIMARY KEY NOT NULL, device_name TEXT NOT NULL, last_ip_address TEXT, last_tcp_port INTEGER, first_seen_at TEXT NOT NULL, last_seen_at TEXT NOT NULL, last_chat_at TEXT, last_transfer_at TEXT)",
        // 聊天会话：每台对端设备一行，外键级联删除消息
        "CREATE TABLE chat_conversations (peer_device_id TEXT PRIMARY KEY NOT NULL, created_at TEXT NOT NULL, last_message_at TEXT NOT NULL, FOREIGN KEY (peer_device_id) REFERENCES peer_devices(device_id) ON DELETE CASCADE)",
        // 聊天消息：按会话分区，消息 UUID 为幂等写入键
        "CREATE TABLE chat_messages (message_id TEXT PRIMARY KEY NOT NULL, peer_device_id TEXT NOT NULL, direction INTEGER NOT NULL CHECK (direction IN (0, 1)), sender_device_id TEXT NOT NULL, sender_name TEXT NOT NULL, content TEXT NOT NULL, sent_at TEXT NOT NULL, local_status INTEGER NOT NULL, created_at TEXT NOT NULL, FOREIGN KEY (peer_device_id) REFERENCES chat_conversations(peer_device_id) ON DELETE CASCADE)",
        // 聊天消息索引：支持按会话分页（时间倒序 + 消息 ID 稳定排序）
        "CREATE INDEX idx_chat_messages_peer_time ON chat_messages(peer_device_id, sent_at DESC, message_id DESC)",
        // 传输历史：只保存结束态快照，不保存发送源路径或文件内容
        "CREATE TABLE transfer_history (record_id TEXT PRIMARY KEY NOT NULL, session_id TEXT NOT NULL UNIQUE, peer_device_id TEXT NOT NULL, peer_name TEXT NOT NULL, direction INTEGER NOT NULL CHECK (direction IN (0, 1)), display_name TEXT NOT NULL, is_directory INTEGER NOT NULL CHECK (is_directory IN (0, 1)), file_count INTEGER NOT NULL, total_bytes INTEGER NOT NULL, status TEXT NOT NULL, started_at TEXT NOT NULL, finished_at TEXT, error_code INTEGER, error_message TEXT, FOREIGN KEY (peer_device_id) REFERENCES peer_devices(device_id) ON DELETE RESTRICT)",
        // 传输历史索引：按设备+时间分页
        "CREATE INDEX idx_transfer_history_peer_time ON transfer_history(peer_device_id, started_at DESC, record_id DESC)",
        // 传输历史索引：按状态筛选
        "CREATE INDEX idx_transfer_history_status_time ON transfer_history(status, started_at DESC)"
    };

    for (const QString &statement : statements) {
        if (!execute(query, statement, errorMessage)) {
            database.rollback();  // DDL 失败时撤销本版本已创建的表和索引
            return false;
        }
    }

    // 记录迁移完成时间，供 schemaVersion() 查询
    query.prepare("INSERT INTO schema_version(version, applied_at) VALUES(?, ?)");
    query.addBindValue(1);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) {
        if (errorMessage) {
            *errorMessage = query.lastError().text();
        }
        database.rollback();
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
