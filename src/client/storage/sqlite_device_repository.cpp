/**
* @file    sqlite_device_repository.cpp
* @version 7.17.3
* @date 2026-10-04
* @author  GridYard Team
* @brief   SQLite 设备目录 Repository 实现
*
* 所有 SQL 均采用预编译参数绑定；业务活动时间不参与发现节流。
*
* Change Log:
* [v7.17.3] GY   2026-10-04
* * 新增 setDeviceAlias 与 Step 变体：空串清除、幂等成功
* * 备注更新与 upsert 列清单互不重叠，心跳不清备注
* [v7.17.2] GY   2026-10-04
* * 设备目录恢复排序加 pinned 次序，置顶设备优先恢复
* [v7.17.1] GY   2026-10-04
* * 新增 setDevicePinned/setDeviceHidden/deleteDeviceWithHistory 管理接口与 Step 变体
* * 新增 noteDeviceDeleted 清除节流缓存，删除后再次发现按全新设备重新入目录
* [v7.17.0] GY   2026-10-04
* * recentPeers 恢复链路带出 alias/pinned/hidden 三列
* * upsert 补注释固定 ON CONFLICT 不触碰管理三列的心跳契约
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
* * 版本头对齐到 v7.15.6
* [v7.15.5] GY   2026-10-03
* * 接口方法内部复用 *Step，消除双份 upsert/update SQL 字面量
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.1.0] GY   2026-06-25
* * 新增设备目录 SQLite Repository
*/

#include "sqlite_device_repository.h"

#include "sqlite_database_broker.h"
#include "sqlite_transfer_history_repository.h"

#include <QSqlError>
#include <QSqlQuery>
#include <vector>

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

    // SQL 与组合事务共用同一 Step，避免双份字面量漂移
    const bool saved = _database->runInTransaction(upsertPeerStep(record), errorMessage);
    if (saved)
        _recentWrites.insert(record.deviceId, record);
    return saved;
}

// 更新设备最近聊天活动时间
bool SqliteDeviceRepository::markChatActivity(const QString &deviceId, const QDateTime &time,
                                         QString *errorMessage) {
    return _database && _database->runInTransaction(markChatActivityStep(deviceId, time),
                                                    errorMessage);
}

// 更新设备最近传输活动时间
bool SqliteDeviceRepository::markTransferActivity(const QString &deviceId, const QDateTime &time,
                                             QString *errorMessage) {
    return _database && _database->runInTransaction(markTransferActivityStep(deviceId, time),
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
    // 置顶设备优先于普通设备恢复，其余按最近活动排序，COALESCE 优先取聊天时间，
    // 其次传输时间，最后心跳时间
    query.prepare("SELECT device_id, device_name, last_ip_address, last_tcp_port, "
                  "first_seen_at, last_seen_at, last_chat_at, last_transfer_at, "
                  "alias, pinned, hidden FROM "
                  "peer_devices ORDER BY pinned DESC, COALESCE(last_chat_at, last_transfer_at, "
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
        record.alias = query.value(8).toString();
        record.pinned = query.value(9).toInt() != 0;
        record.hidden = query.value(10).toInt() != 0;
        records.append(record);
    }
    return records;
}

// 只执行 upsert，不自开事务；节流由 noteWritten 在事务外控制
SqliteDeviceRepository::SqlStep SqliteDeviceRepository::upsertPeerStep(const PeerRecord &record)
{
    // 插入列清单与 ON CONFLICT 更新列刻意不含 alias/pinned/hidden：
    // 心跳与发现更新不得清除用户的备注、置顶和隐藏状态
    return[record](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare(
            "INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
            "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(device_id) DO UPDATE SET "
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
    };
}

// 只执行活动字段更新，不自开事务
SqliteDeviceRepository::SqlStep SqliteDeviceRepository::markChatActivityStep(
    const QString &deviceId, const QDateTime &time)
{
    return[deviceId, time](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare("UPDATE peer_devices SET last_chat_at=? WHERE device_id=?");
        query.addBindValue(sqlTime(time));
        query.addBindValue(deviceId);
        if (query.exec())
            return true;  // UPDATE 找不到行不算错误
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

SqliteDeviceRepository::SqlStep SqliteDeviceRepository::markTransferActivityStep(
    const QString &deviceId, const QDateTime &time)
{
    return[deviceId, time](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare("UPDATE peer_devices SET last_transfer_at=? WHERE device_id=?");
        query.addBindValue(sqlTime(time));
        query.addBindValue(deviceId);
        if (query.exec())
            return true;
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

// 设置设备置顶状态（幂等，设备行不存在时同样返回成功）
bool SqliteDeviceRepository::setDevicePinned(const QString &deviceId, bool pinned,
                                             QString *errorMessage)
{
    return _database && _database->runInTransaction(setDevicePinnedStep(deviceId, pinned),
                                                    errorMessage);
}

// 设置设备隐藏状态（幂等，设备行不存在时同样返回成功）
bool SqliteDeviceRepository::setDeviceHidden(const QString &deviceId, bool hidden,
                                             QString *errorMessage)
{
    return _database && _database->runInTransaction(setDeviceHiddenStep(deviceId, hidden),
                                                    errorMessage);
}

// 设置设备本地备注别名（空串表示清除；幂等，设备行不存在时同样返回成功）
bool SqliteDeviceRepository::setDeviceAlias(const QString &deviceId, const QString &alias,
                                            QString *errorMessage)
{
    return _database && _database->runInTransaction(setDeviceAliasStep(deviceId, alias),
                                                    errorMessage);
}

// 删除设备及其聊天与传输历史；不删除已接收的本地文件
bool SqliteDeviceRepository::deleteDeviceWithHistory(const QString &deviceId,
                                                     QString *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    // 传输历史对设备行是 RESTRICT 外键，必须先删该设备的传输历史再删设备行；
    // 聊天会话与消息依赖两级 CASCADE 随设备行一并清理，已接收的本地文件不受影响
    const std::vector steps = {
        SqliteTransferHistoryRepository::deleteForDeviceStep(deviceId),
        deleteDeviceStep(deviceId)
    };
    return _database->runSteps(steps, errorMessage);
}

// 只执行置顶更新，不自开事务；UPDATE 找不到行即幂等成功
SqliteDeviceRepository::SqlStep SqliteDeviceRepository::setDevicePinnedStep(const QString &deviceId,
                                                                            bool pinned)
{
    return[deviceId, pinned](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare("UPDATE peer_devices SET pinned=? WHERE device_id=?");
        query.addBindValue(pinned ? 1 : 0);
        query.addBindValue(deviceId);
        if (query.exec())
            return true;
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

// 只执行隐藏更新，不自开事务；UPDATE 找不到行即幂等成功
SqliteDeviceRepository::SqlStep SqliteDeviceRepository::setDeviceHiddenStep(const QString &deviceId,
                                                                            bool hidden)
{
    return[deviceId, hidden](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare("UPDATE peer_devices SET hidden=? WHERE device_id=?");
        query.addBindValue(hidden ? 1 : 0);
        query.addBindValue(deviceId);
        if (query.exec())
            return true;
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

// 只执行备注更新，不自开事务；UPDATE 找不到行即幂等成功。
// 与 upsert 的 ON CONFLICT 列清单互不重叠，心跳更新天然不清备注
SqliteDeviceRepository::SqlStep SqliteDeviceRepository::setDeviceAliasStep(const QString &deviceId,
                                                                          const QString &alias)
{
    return[deviceId, alias](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare("UPDATE peer_devices SET alias=? WHERE device_id=?");
        query.addBindValue(alias);
        query.addBindValue(deviceId);
        if (query.exec())
            return true;
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

// 只删除设备目录行，不自开事务；供删除组合事务在清空传输历史后调用
SqliteDeviceRepository::SqlStep SqliteDeviceRepository::deleteDeviceStep(const QString &deviceId)
{
    return[deviceId](QSqlDatabase &database, QString *taskError) {
        QSqlQuery query(database);
        query.prepare("DELETE FROM peer_devices WHERE device_id=?");
        query.addBindValue(deviceId);
        if (query.exec())
            return true;  // 设备行不存在时幂等成功
        if (taskError)
            *taskError = query.lastError().text();
        return false;
    };
}

// 事务成功后刷新内存节流缓存，避免下一条写入因时间未推进而被节流拒绝
void SqliteDeviceRepository::noteWritten(const PeerRecord &record)
{
    _recentWrites.insert(record.deviceId, record);
}

// 设备删除成功后清除节流缓存，避免再次发现的心跳被旧缓存节流而无法重新入目录
void SqliteDeviceRepository::noteDeviceDeleted(const QString &deviceId)
{
    _recentWrites.remove(deviceId);
}
