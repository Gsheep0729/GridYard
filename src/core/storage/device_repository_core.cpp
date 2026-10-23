/**
* @file    device_repository_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SQLite 设备目录仓储实现
*
* 所有 SQL 均逐字复用 Qt 版字面量并采用预编译参数绑定；业务活动
* 时间不参与发现节流。
*/

#include "core/storage/device_repository_core.h"

#include "core/storage/database_core.h"
#include "core/storage/iso_time.h"
#include "core/storage/sqlite_stmt.h"
#include "core/storage/transfer_repository_core.h"

#include <cstdint>

namespace gy::storage {
namespace {
// 相同发现快照最短写入间隔：30 秒
constexpr int64_t kDiscoveryWriteIntervalMs = 30000;

// 将设备目录查询的当前行映射为领域记录，列序与 SELECT 清单一致
PeerRecord readPeerRecordRow(const SqliteStmt &query)
{
    PeerRecord record;
    record.deviceId = query.columnText(0);
    record.deviceName = query.columnText(1);
    record.lastIpAddress = query.columnText(2);
    record.lastTcpPort = static_cast<uint16_t>(query.columnInt64(3));
    record.firstSeenAtMs = query.columnIsNull(4) ? 0 : parseIso8601Ms(query.columnText(4)).value_or(0);
    record.lastSeenAtMs = query.columnIsNull(5) ? 0 : parseIso8601Ms(query.columnText(5)).value_or(0);
    record.lastChatAtMs = query.columnIsNull(6)
        ? std::nullopt : parseIso8601Ms(query.columnText(6));
    record.lastTransferAtMs = query.columnIsNull(7)
        ? std::nullopt : parseIso8601Ms(query.columnText(7));
    record.alias = query.columnText(8);
    record.pinned = query.columnInt64(9) != 0;
    record.hidden = query.columnInt64(10) != 0;
    record.favorite = query.columnInt64(11) != 0;
    return record;
}
}

// 构造函数
DeviceRepositoryCore::DeviceRepositoryCore(DatabaseCore *database) : _database(database)
{
}

// 新增或更新设备目录快照
bool DeviceRepositoryCore::upsertPeer(const PeerRecord &record, std::string *errorMessage)
{
    if (!_database) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return false;
    }

    const auto existing = _recentWrites.find(record.deviceId);
    if (existing != _recentWrites.end()) {
        // 设备心跳频率高，未变化的发现快照只保留内存时间，减少无意义磁盘写入。
        const bool unchanged = existing->second.deviceName == record.deviceName
                               && existing->second.lastIpAddress == record.lastIpAddress
                               && existing->second.lastTcpPort == record.lastTcpPort;
        const int64_t elapsed = record.lastSeenAtMs - existing->second.lastSeenAtMs;
        if (unchanged && elapsed >= 0 && elapsed < kDiscoveryWriteIntervalMs) {
            return true;  // 重复心跳不写磁盘
        }
    }

    // SQL 与组合事务共用同一 Step，避免双份字面量漂移
    const bool saved = _database->runInTransaction(upsertPeerStep(record), errorMessage);
    if (saved) {
        _recentWrites.insert_or_assign(record.deviceId, record);
    }
    return saved;
}

// 更新设备最近聊天活动时间
bool DeviceRepositoryCore::markChatActivity(const std::string &deviceId, int64_t timeMs,
                                            std::string *errorMessage)
{
    return _database && _database->runInTransaction(markChatActivityStep(deviceId, timeMs),
                                                    errorMessage);
}

// 更新设备最近传输活动时间
bool DeviceRepositoryCore::markTransferActivity(const std::string &deviceId, int64_t timeMs,
                                                std::string *errorMessage)
{
    return _database && _database->runInTransaction(markTransferActivityStep(deviceId, timeMs),
                                                    errorMessage);
}

// 获取按最近活动排序的设备目录
std::vector<PeerRecord> DeviceRepositoryCore::recentPeers(int limit,
                                                          std::string *errorMessage) const
{
    std::vector<PeerRecord> records;
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }
    SqliteStmt query{_database->connection(),
                     "SELECT device_id, device_name, last_ip_address, last_tcp_port, "
                     "first_seen_at, last_seen_at, last_chat_at, last_transfer_at, "
                     "alias, pinned, hidden, favorite FROM "
                     "peer_devices ORDER BY favorite DESC, pinned DESC, "
                     "COALESCE(last_chat_at, last_transfer_at, last_seen_at) DESC LIMIT ?"};
    // 置顶设备优先于普通设备恢复，其余按最近活动排序，COALESCE 优先取聊天时间，
    // 其次传输时间，最后心跳时间
    query.bind(1, limit);
    auto row = query.step();
    while (row.has_value() && row.value()) {
        // 存储层完成行到领域值对象的映射，调用方不接触底层语句。
        records.push_back(readPeerRecordRow(query));
        row = query.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }
    return records;
}

// 加载设备目录全表（备份导出用），按 device_id 排序保证两次导出顺序稳定
std::vector<PeerRecord> DeviceRepositoryCore::allPeers(std::string *errorMessage) const
{
    std::vector<PeerRecord> records;
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }
    SqliteStmt query{_database->connection(),
                     "SELECT device_id, device_name, last_ip_address, last_tcp_port, "
                     "first_seen_at, last_seen_at, last_chat_at, last_transfer_at, "
                     "alias, pinned, hidden, favorite FROM peer_devices ORDER BY device_id"};
    auto row = query.step();
    while (row.has_value() && row.value()) {
        records.push_back(readPeerRecordRow(query));
        row = query.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }
    return records;
}

// 按关键字模糊检索设备目录：设备名、备注与最近 IP 任一包含即命中；
// 隐藏态设备与列表过滤语义一致不返回，排序口径与 recentPeers 相同
std::vector<PeerRecord> DeviceRepositoryCore::searchPeers(const std::string &keyword, int limit,
                                                          std::string *errorMessage) const
{
    std::vector<PeerRecord> records;
    if (!_database || !_database->isAvailable()) {
        if (errorMessage) {
            *errorMessage = "数据库入口未初始化";
        }
        return records;
    }
    // 与 Qt 版相同的空白裁剪判定：纯空白关键字等价于空结果
    const auto nonspace = keyword.find_first_not_of(" \t\r\n");
    if (nonspace == std::string::npos || limit <= 0) {
        return records;  // 空关键字或零上限等价于空结果，不必发起查询
    }

    // 转义 LIKE 通配符后前后加 %，用户输入按字面包含匹配而非通配模式
    std::string pattern;
    pattern.reserve(keyword.size() * 2 + 2);
    for (const char c : keyword) {
        if (c == '\\' || c == '%' || c == '_') {
            pattern.push_back('\\');
        }
        pattern.push_back(c);
    }
    pattern = "%" + pattern + "%";

    SqliteStmt query{_database->connection(),
                     "SELECT device_id, device_name, last_ip_address, last_tcp_port, "
                     "first_seen_at, last_seen_at, last_chat_at, last_transfer_at, "
                     "alias, pinned, hidden, favorite FROM peer_devices "
                     "WHERE hidden = 0 AND (device_name LIKE ? ESCAPE '\\' "
                     "OR alias LIKE ? ESCAPE '\\' OR last_ip_address LIKE ? ESCAPE '\\') "
                     "ORDER BY favorite DESC, pinned DESC, "
                     "COALESCE(last_chat_at, last_transfer_at, last_seen_at) DESC LIMIT ?"};
    // 排序口径与 recentPeers 一致：收藏与置顶优先，其次按最近活动时间倒序
    query.bind(1, pattern);
    query.bind(2, pattern);
    query.bind(3, pattern);
    query.bind(4, limit);
    auto row = query.step();
    while (row.has_value() && row.value()) {
        records.push_back(readPeerRecordRow(query));
        row = query.step();
    }
    if (!row.has_value() && errorMessage) {
        *errorMessage = row.error().message;
    }
    return records;
}

// 真身同 IP 合并出现时，把手动伪条目的管理标记迁入真行并删除伪行；
// 备注取非空一方，置顶/隐藏/收藏取或，伪行或真行不存在时为空操作
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::mergeManualPeerMarkersStep(
        const std::string &realDeviceId, const std::string &pseudoDeviceId)
{
    return [realDeviceId, pseudoDeviceId](SqliteConn &database, std::string *taskError) -> bool {
        SqliteStmt query{database,
                         "SELECT alias, pinned, hidden, favorite FROM peer_devices WHERE device_id = ?"};
        query.bind(1, pseudoDeviceId);
        auto row = query.step();
        if (!row.has_value()) {
            if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        }
        if (!row.value()) {
            return true;  // 无伪行（已迁移或从未手动添加）为空操作
        }
        const std::string pseudoAlias = query.columnText(0);
        const bool pseudoPinned = query.columnInt64(1) != 0;
        const bool pseudoHidden = query.columnInt64(2) != 0;
        const bool pseudoFavorite = query.columnInt64(3) != 0;

        query.reset();
        query.clearBindings();
        query.bind(1, realDeviceId);
        row = query.step();
        if (!row.has_value()) {
            if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        }
        if (!row.value()) {
            return true;  // 真行不存在时无迁移落点（upsert 步骤在前，正常不发生）
        }
        std::string mergedAlias = query.columnText(0);
        if (mergedAlias.empty()) {
            mergedAlias = pseudoAlias;
        }
        const bool mergedPinned = query.columnInt64(1) != 0 || pseudoPinned;
        const bool mergedHidden = query.columnInt64(2) != 0 || pseudoHidden;
        const bool mergedFavorite = query.columnInt64(3) != 0 || pseudoFavorite;

        // UPDATE 的参数个数与 SELECT 不同，须独立预编译（参数下标越界属硬错误）
        SqliteStmt update{database,
                          "UPDATE peer_devices SET alias = ?, pinned = ?, hidden = ?, favorite = ? "
                          "WHERE device_id = ?"};
        update.bind(1, mergedAlias);
        update.bind(2, mergedPinned ? 1 : 0);
        update.bind(3, mergedHidden ? 1 : 0);
        update.bind(4, mergedFavorite ? 1 : 0);
        update.bind(5, realDeviceId);
        row = update.step();
        if (!row.has_value()) {
            if (taskError) {
                *taskError = row.error().message;
            }
            return false;
        }

        // 删除伪行：传输历史是 RESTRICT 外键，先清伪行名下的传输与聊天记录
        //（伪 ID 不可达正常无历史，防御性清理防孤儿行）
        const char *cleanups[] = {
            "DELETE FROM transfer_history WHERE peer_device_id = ?",
            "DELETE FROM chat_conversations WHERE peer_device_id = ?",
            "DELETE FROM peer_devices WHERE device_id = ?"
        };
        for (const char *cleanup : cleanups) {
            SqliteStmt statement{database, cleanup};
            statement.bind(1, pseudoDeviceId);
            if (auto cleanupRow = statement.step(); !cleanupRow.has_value()) {
                if (taskError) {
                    *taskError = cleanupRow.error().message;
                }
                return false;
            }
        }
        return true;
    };
}

// 只执行 upsert，不自开事务；节流由 noteWritten 在事务外控制
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::upsertPeerStep(const PeerRecord &record)
{
    // 插入列清单与 ON CONFLICT 更新列刻意不含 alias/pinned/hidden：
    // 心跳与发现更新不得清除用户的备注、置顶和隐藏状态
    return [record](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database,
            "INSERT INTO peer_devices(device_id, device_name, last_ip_address, "
            "last_tcp_port, first_seen_at, last_seen_at) VALUES(?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(device_id) DO UPDATE SET "
            "device_name=excluded.device_name, "
            "last_ip_address=excluded.last_ip_address, "
            "last_tcp_port=excluded.last_tcp_port, "
            "last_seen_at=excluded.last_seen_at"};
        query.bind(1, record.deviceId);
        query.bind(2, record.deviceName);
        query.bind(3, record.lastIpAddress);
        query.bind(4, static_cast<int64_t>(record.lastTcpPort));
        query.bind(5, toIso8601Ms(record.firstSeenAtMs));
        query.bind(6, toIso8601Ms(record.lastSeenAtMs));
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 只执行活动字段更新，不自开事务
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::markChatActivityStep(
    const std::string &deviceId, int64_t timeMs)
{
    return [deviceId, timeMs](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "UPDATE peer_devices SET last_chat_at=? WHERE device_id=?"};
        query.bind(1, toIso8601Ms(timeMs));
        query.bind(2, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;  // UPDATE 找不到行不算错误
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 只执行传输活动字段更新，不自开事务
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::markTransferActivityStep(
    const std::string &deviceId, int64_t timeMs)
{
    return [deviceId, timeMs](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "UPDATE peer_devices SET last_transfer_at=? WHERE device_id=?"};
        query.bind(1, toIso8601Ms(timeMs));
        query.bind(2, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 设置设备置顶状态（幂等，设备行不存在时同样返回成功）
bool DeviceRepositoryCore::setDevicePinned(const std::string &deviceId, bool pinned,
                                           std::string *errorMessage)
{
    return _database && _database->runInTransaction(setDevicePinnedStep(deviceId, pinned),
                                                    errorMessage);
}

// 设置设备隐藏状态（幂等，设备行不存在时同样返回成功）
bool DeviceRepositoryCore::setDeviceHidden(const std::string &deviceId, bool hidden,
                                           std::string *errorMessage)
{
    return _database && _database->runInTransaction(setDeviceHiddenStep(deviceId, hidden),
                                                    errorMessage);
}

// 设置设备本地备注别名（空串表示清除；幂等，设备行不存在时同样返回成功）
bool DeviceRepositoryCore::setDeviceAlias(const std::string &deviceId, const std::string &alias,
                                          std::string *errorMessage)
{
    return _database && _database->runInTransaction(setDeviceAliasStep(deviceId, alias),
                                                    errorMessage);
}

// 设置设备收藏状态（幂等，设备行不存在时同样返回成功）
bool DeviceRepositoryCore::setDeviceFavorite(const std::string &deviceId, bool favorite,
                                             std::string *errorMessage)
{
    return _database && _database->runInTransaction(setDeviceFavoriteStep(deviceId, favorite),
                                                    errorMessage);
}

// 真身同 IP 合并出现时，把手动伪条目的管理标记迁入真行并删除伪行
//（独立小事务，幂等：伪行不存在时为空操作）
bool DeviceRepositoryCore::mergeManualPeerMarkers(const std::string &realDeviceId,
                                                  const std::string &pseudoDeviceId,
                                                  std::string *errorMessage)
{
    return _database && _database->runInTransaction(
        mergeManualPeerMarkersStep(realDeviceId, pseudoDeviceId), errorMessage);
}

// 删除设备及其聊天与传输历史；不删除已接收的本地文件
bool DeviceRepositoryCore::deleteDeviceWithHistory(const std::string &deviceId,
                                                   std::string *errorMessage)
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
        TransferRepositoryCore::deleteForDeviceStep(deviceId),
        deleteDeviceStep(deviceId)
    };
    return _database->runSteps(steps, errorMessage);
}

// 只执行置顶更新，不自开事务；UPDATE 找不到行即幂等成功
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::setDevicePinnedStep(
    const std::string &deviceId, bool pinned)
{
    return [deviceId, pinned](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "UPDATE peer_devices SET pinned=? WHERE device_id=?"};
        query.bind(1, pinned ? 1 : 0);
        query.bind(2, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 只执行隐藏更新，不自开事务；UPDATE 找不到行即幂等成功
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::setDeviceHiddenStep(
    const std::string &deviceId, bool hidden)
{
    return [deviceId, hidden](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "UPDATE peer_devices SET hidden=? WHERE device_id=?"};
        query.bind(1, hidden ? 1 : 0);
        query.bind(2, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 只执行备注更新，不自开事务；UPDATE 找不到行即幂等成功。
// 与 upsert 的 ON CONFLICT 列清单互不重叠，心跳更新天然不清备注
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::setDeviceAliasStep(
    const std::string &deviceId, const std::string &alias)
{
    return [deviceId, alias](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "UPDATE peer_devices SET alias=? WHERE device_id=?"};
        query.bind(1, alias);
        query.bind(2, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 只执行收藏更新，不自开事务；UPDATE 找不到行即幂等成功。
// 与 upsert 的 ON CONFLICT 列清单互不重叠，心跳更新天然不动收藏
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::setDeviceFavoriteStep(
    const std::string &deviceId, bool favorite)
{
    return [deviceId, favorite](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "UPDATE peer_devices SET favorite=? WHERE device_id=?"};
        query.bind(1, favorite ? 1 : 0);
        query.bind(2, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 只删除设备目录行，不自开事务；供删除组合事务在清空传输历史后调用
DeviceRepositoryCore::SqlStep DeviceRepositoryCore::deleteDeviceStep(
    const std::string &deviceId)
{
    return [deviceId](SqliteConn &database, std::string *taskError) {
        SqliteStmt query{database, "DELETE FROM peer_devices WHERE device_id=?"};
        query.bind(1, deviceId);
        if (auto row = query.step(); row.has_value()) {
            return true;  // 设备行不存在时幂等成功
        } else if (taskError) {
            *taskError = row.error().message;
        }
        return false;
    };
}

// 事务成功后刷新内存节流缓存，避免下一条写入因时间未推进而被节流拒绝
void DeviceRepositoryCore::noteWritten(const PeerRecord &record)
{
    _recentWrites.insert_or_assign(record.deviceId, record);
}

// 设备删除成功后清除节流缓存，避免再次发现的心跳被旧缓存节流而无法重新入目录
void DeviceRepositoryCore::noteDeviceDeleted(const std::string &deviceId)
{
    _recentWrites.erase(deviceId);
}

}  // namespace gy::storage
