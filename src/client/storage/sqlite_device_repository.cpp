/**
* @file    sqlite_device_repository.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 设备目录 Repository 实现
*
* 所有 SQL 均采用预编译参数绑定；业务活动时间不参与发现节流。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.1.0] GY   2026-06-25
* * 新增设备目录 SQLite Repository
*/

#include "sqlite_device_repository.h"

#include "sqlite_database_broker.h"

#include <QSqlError>
#include <QSqlQuery>

namespace {
constexpr qint64 kDiscoveryWriteIntervalMs = 30000; // 相同发现快照最短写入间隔：30 秒

// 将 UTC 时间转换为 SQLite 使用的 ISO 文本
QString sqlTime(const QDateTime &time) { return time.toUTC().toString(Qt::ISODateWithMs); }
}

// 构造函数
SqliteDeviceRepository::SqliteDeviceRepository(SqliteDatabaseBroker *database) : _database(database) {}

// 新增或更新设备目录快照
bool SqliteDeviceRepository::upsertPeer(const PeerRecord &record, QString *errorMessage) {
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    const auto existing = _recentWrites.constFind(record.deviceId);
    if (existing != _recentWrites.cend()) {
        // 设备心跳频率高，未变化的发现快照只保留内存时间，减少无意义磁盘写入。
        const bool unchanged = existing->deviceName == record.deviceName &&
                               existing->lastIpAddress == record.lastIpAddress &&
                               existing->lastTcpPort == record.lastTcpPort;
        const qint64 elapsed = existing->lastSeenAt.msecsTo(record.lastSeenAt);
        if (unchanged && elapsed >= 0 && elapsed < kDiscoveryWriteIntervalMs) {
            return true; // 重复心跳不写磁盘
        }
    }

    const bool saved = _database->runInTransaction(
        [&record](QSqlDatabase &database, QString *taskError) {
            QSqlQuery query(database);
            // first_seen_at 只在首次插入时写入，后续发现只刷新可变快照字段。
            query.prepare("INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
                          "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, "
                          "?) ON CONFLICT(device_id) DO UPDATE SET "
                          "device_name=excluded.device_name, "
                          "last_ip_address=excluded.last_ip_address, "
                          "last_tcp_port=excluded.last_tcp_port, "
                          "last_seen_at=excluded.last_seen_at");
            query.addBindValue(record.deviceId);
            query.addBindValue(record.deviceName);
            query.addBindValue(record.lastIpAddress);
            query.addBindValue(record.lastTcpPort);
            query.addBindValue(sqlTime(record.firstSeenAt));
            query.addBindValue(sqlTime(record.lastSeenAt));
            if (query.exec())
                return true;
            if (taskError)
                *taskError = query.lastError().text();
            return false;
        },
        errorMessage);
    if (saved)
        _recentWrites.insert(record.deviceId, record);
    return saved;
}

// 更新设备最近聊天活动时间
bool SqliteDeviceRepository::markChatActivity(const QString &deviceId, const QDateTime &time,
                                         QString *errorMessage) {
    return _database &&
           _database->runInTransaction(
               [&](QSqlDatabase &database, QString *taskError) {
                   QSqlQuery query(database);
                   query.prepare("UPDATE peer_devices SET last_chat_at=? WHERE device_id=?");
                   query.addBindValue(sqlTime(time));
                   query.addBindValue(deviceId);
                   if (query.exec())
                       return true;
                   if (taskError)
                       *taskError = query.lastError().text();
                   return false;
               },
               errorMessage);
}

// 更新设备最近传输活动时间
bool SqliteDeviceRepository::markTransferActivity(const QString &deviceId, const QDateTime &time,
                                             QString *errorMessage) {
    return _database && _database->runInTransaction(
                            [&](QSqlDatabase &database, QString *taskError) {
                                QSqlQuery query(database);
                                query.prepare("UPDATE peer_devices SET last_transfer_at=? WHERE "
                                              "device_id=?");
                                query.addBindValue(sqlTime(time));
                                query.addBindValue(deviceId);
                                if (query.exec())
                                    return true;
                                if (taskError)
                                    *taskError = query.lastError().text();
                                return false;
                            },
                            errorMessage);
}

// 获取按最近活动排序的设备目录
QList<PeerRecord> SqliteDeviceRepository::recentPeers(int limit, QString *errorMessage) const {
    QList<PeerRecord> records;
    if (!_database) {
        if (errorMessage)
            *errorMessage = "数据库入口未初始化";
        return records;
    }
    QSqlDatabase database = _database->connectionForWorkerThread(errorMessage);
    if (!database.isValid())
        return records;
    QSqlQuery query(database);
    // 最近活动优先于发现时间，确保有聊天或传输的设备排在普通心跳设备前。
    // 按最近活动排序，COALESCE 优先取聊天时间，其次传输时间，最后心跳时间
    query.prepare("SELECT device_id, device_name, last_ip_address, last_tcp_port, "
                  "first_seen_at, last_seen_at, last_chat_at, last_transfer_at FROM "
                  "peer_devices ORDER BY COALESCE(last_chat_at, last_transfer_at, "
                  "last_seen_at) DESC LIMIT ?");
    query.addBindValue(limit);
    if (!query.exec()) {
        if (errorMessage)
            *errorMessage = query.lastError().text();
        return records;
    }
    while (query.next()) {
        // 存储层完成行到领域值对象的映射，调用方不接触 QSqlQuery。
        PeerRecord record;
        record.deviceId = query.value(0).toString();
        record.deviceName = query.value(1).toString();
        record.lastIpAddress = query.value(2).toString();
        record.lastTcpPort = static_cast<quint16>(query.value(3).toUInt());
        record.firstSeenAt = QDateTime::fromString(query.value(4).toString(), Qt::ISODateWithMs);
        record.lastSeenAt = QDateTime::fromString(query.value(5).toString(), Qt::ISODateWithMs);
        record.lastChatAt = QDateTime::fromString(query.value(6).toString(), Qt::ISODateWithMs);
        record.lastTransferAt = QDateTime::fromString(query.value(7).toString(), Qt::ISODateWithMs);
        records.append(record);
    }
    return records;
}
