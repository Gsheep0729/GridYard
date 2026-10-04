/**
* @file    migration_runner.cpp
* @version 7.18.0
* @date 2026-10-05
* @author  GY
* @brief   SQLite Schema 版本迁移执行器实现
*
* Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * Schema 支持上限抬到 2，新增版本二迁移为 peer_devices 补齐备注/置顶/隐藏三列
* * 各版本迁移拆分为独立事务执行，任一 DDL 失败整体回滚
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
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
* * 库版本高于当前支持上限时拒绝打开，防止旧程序读写新版本表结构
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
// 当前程序支持的 Schema 最高版本，随新迁移发布递增
constexpr int kCurrentSchemaVersion = 2;

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

// 在单个事务内执行一个版本的全部 DDL 并登记版本号，任一步失败整体回滚
bool applyMigrationStep(QSqlDatabase &database, const QStringList &statements, int version,
                        QString *errorMessage)
{
    if (!database.transaction()) {
        if (errorMessage) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    for (const QString &statement : statements) {
        if (!execute(query, statement, errorMessage)) {
            database.rollback();  // DDL 失败时撤销本版本已做的变更，防止留下半成品 Schema
            return false;
        }
    }

    query.prepare("INSERT INTO schema_version(version, applied_at) VALUES(?, ?)");
    query.addBindValue(version);
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
    if (version < 0) {
        return false;  // 版本查询失败，错误信息已写入
    }

    // 库版本高于当前支持上限说明由更新版本的程序创建，
    // 旧程序继续读写会误判表结构甚至写坏数据，必须拒绝打开
    if (version > kCurrentSchemaVersion) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("本地历史数据库由更新版本的程序创建，当前程序无法打开");
        }
        return false;
    }

    // 各版本迁移在各自事务内按序执行，新库从版本 0 连续升级，旧库只补缺失的版本

    // 版本一：4 张业务表 + 3 个查询索引，仅全新建库执行
    if (version < 1) {
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
        if (!applyMigrationStep(database, statements, 1, errorMessage)) {
            return false;
        }
    }

    // 版本二：设备目录补齐用户管理三列（备注/置顶/隐藏），
    // SQLite 允许带常量 DEFAULT 的 NOT NULL 加列，存量行自动取默认值
    if (version < 2) {
        const QStringList statements = {
            "ALTER TABLE peer_devices ADD COLUMN alias TEXT",
            "ALTER TABLE peer_devices ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0",
            "ALTER TABLE peer_devices ADD COLUMN hidden INTEGER NOT NULL DEFAULT 0"
        };
        if (!applyMigrationStep(database, statements, 2, errorMessage)) {
            return false;
        }
    }

    return true;
}
