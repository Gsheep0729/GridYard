/**
* @file    device_repository_core.h
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   纯 C++23 SQLite 设备目录仓储——不依赖任何 Qt 类型
*
* SQL 语义由存储测试锁定；心跳与发现更新依赖发现节流缓存避免频繁磁盘写入。
*/

#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/storage/storage_records.h"

namespace gy::storage {

class DatabaseCore;
class SqliteConn;

// 设备目录仓储纯 C++ 实现
class DeviceRepositoryCore {
public:
    // 只执行 SQL、不自开事务的任务，供调用方把多表写入组合进单个事务
    using SqlStep = std::function<bool(SqliteConn &, std::string *)>;

    // 构造设备目录仓储
    explicit DeviceRepositoryCore(DatabaseCore *database);

    // 新增或更新设备目录快照
    bool upsertPeer(const PeerRecord &record, std::string *errorMessage);
    // 更新设备最近聊天活动时间
    bool markChatActivity(const std::string &deviceId, int64_t timeMs,
                          std::string *errorMessage);
    // 更新设备最近传输活动时间
    bool markTransferActivity(const std::string &deviceId, int64_t timeMs,
                              std::string *errorMessage);
    // 获取按最近活动排序的设备目录
    std::vector<PeerRecord> recentPeers(int limit, std::string *errorMessage) const;
    // 加载设备目录全表（备份导出用）
    std::vector<PeerRecord> allPeers(std::string *errorMessage) const;
    // 按关键字模糊检索设备目录（设备名/备注/最近 IP 任一包含即命中，隐藏态不返回）
    std::vector<PeerRecord> searchPeers(const std::string &keyword, int limit,
                                        std::string *errorMessage) const;
    // 设置设备置顶状态（幂等，设备行不存在时同样返回成功）
    bool setDevicePinned(const std::string &deviceId, bool pinned, std::string *errorMessage);
    // 设置设备收藏状态（幂等，设备行不存在时同样返回成功）
    bool setDeviceFavorite(const std::string &deviceId, bool favorite,
                           std::string *errorMessage);
    // 设置设备隐藏状态（幂等，设备行不存在时同样返回成功）
    bool setDeviceHidden(const std::string &deviceId, bool hidden, std::string *errorMessage);
    // 设置设备本地备注别名（空串表示清除；幂等，设备行不存在时同样返回成功）
    bool setDeviceAlias(const std::string &deviceId, const std::string &alias,
                        std::string *errorMessage);
    // 真身同 IP 合并出现时，把手动伪条目的管理标记迁入真行并删除伪行
    //（备注取非空一方，置顶/隐藏/收藏取或；无伪行时为空操作）
    bool mergeManualPeerMarkers(const std::string &realDeviceId,
                                const std::string &pseudoDeviceId, std::string *errorMessage);
    // 删除设备及其聊天与传输历史；不删除已接收的本地文件
    bool deleteDeviceWithHistory(const std::string &deviceId, std::string *errorMessage);

    // 以下 *Step 返回纯 SQL 步骤，由调用方置于同一事务内组合执行（不含节流、不自开事务）
    static SqlStep upsertPeerStep(const PeerRecord &record);
    static SqlStep markChatActivityStep(const std::string &deviceId, int64_t timeMs);
    static SqlStep markTransferActivityStep(const std::string &deviceId, int64_t timeMs);
    static SqlStep setDevicePinnedStep(const std::string &deviceId, bool pinned);
    static SqlStep setDeviceFavoriteStep(const std::string &deviceId, bool favorite);
    static SqlStep setDeviceHiddenStep(const std::string &deviceId, bool hidden);
    static SqlStep setDeviceAliasStep(const std::string &deviceId, const std::string &alias);
    static SqlStep deleteDeviceStep(const std::string &deviceId);
    static SqlStep mergeManualPeerMarkersStep(const std::string &realDeviceId,
                                              const std::string &pseudoDeviceId);
    // 组合写入成功后刷新节流缓存，供调用方在事务提交后调用
    void noteWritten(const PeerRecord &record);
    // 设备删除成功后清除节流缓存，再次发现按全新设备重新入目录
    void noteDeviceDeleted(const std::string &deviceId);

private:
    DatabaseCore *_database = nullptr;  // 数据库连接和事务入口
    std::unordered_map<std::string, PeerRecord> _recentWrites;  // 最近持久化的发现快照
};

}  // namespace gy::storage
