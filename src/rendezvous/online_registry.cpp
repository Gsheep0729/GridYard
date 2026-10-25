/**
* @file    online_registry.cpp
* @version 8.0.0
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点在线设备注册表实现
*/

#include "online_registry.h"
#include "rendezvous_limits.h"
#include "rendezvous_log.h"
#include "rendezvous_time.h"

// 构造函数
OnlineRegistry::OnlineRegistry(int defaultTtlSeconds)
    : _defaultTtlSeconds{defaultTtlSeconds}
    , _maxRooms{gy::rendezvous::kMaxRegistryRooms}
    , _maxDevicesPerRoom{gy::rendezvous::kMaxDevicesPerRoom}
    , _relayInviteTtlSeconds{gy::rendezvous::kRelayInviteTtlSeconds}
    , _maxRelayInvites{gy::rendezvous::kMaxRelayInvites}
{
}

// 调整房间数与每房设备数上限
void OnlineRegistry::setRegistryLimits(int maxRooms, int maxDevicesPerRoom)
{
    if (maxRooms > 0) {
        _maxRooms = maxRooms;
    }
    if (maxDevicesPerRoom > 0) {
        _maxDevicesPerRoom = maxDevicesPerRoom;
    }
}

// 调整待领取邀请的登记上限
void OnlineRegistry::setMaxRelayInvites(int maxInvites)
{
    if (maxInvites > 0) {
        _maxRelayInvites = maxInvites;
    }
}

// 调整中继邀请有效期
void OnlineRegistry::setRelayInviteTtlSeconds(int ttlSeconds)
{
    if (ttlSeconds > 0) {
        _relayInviteTtlSeconds = ttlSeconds;
    }
}

// 注册或更新设备
bool OnlineRegistry::upsertPeer(const std::string &room, const PeerInfo &peer)
{
    const auto roomIt = _rooms.find(room);
    if (roomIt != _rooms.end()) {
        // 已注册设备的心跳刷新不受上限限制，否则正常客户端反而被拒
        if (roomIt->second.find(peer.deviceId) == roomIt->second.end()
            && static_cast<int>(roomIt->second.size()) >= _maxDevicesPerRoom) {
            gy::rendezvous::logWarn("[OnlineRegistry] 房间 " + room + " 设备数达到上限，拒绝 "
                                    + peer.deviceId);
            return false;
        }
    } else if (static_cast<int>(_rooms.size()) >= _maxRooms) {
        gy::rendezvous::logWarn("[OnlineRegistry] 房间数达到上限，拒绝新房间 " + room);
        return false;
    }

    _rooms[room][peer.deviceId] = peer;

    gy::rendezvous::logInfo("[OnlineRegistry] 注册设备 " + peer.deviceId + " 到房间 " + room
                            + "，TTL " + std::to_string(peer.ttlSeconds) + " 秒");
    return true;
}

// 获取房间内所有未过期的设备
std::vector<OnlineRegistry::PeerInfo> OnlineRegistry::peersForRoom(const std::string &room) const
{
    std::vector<PeerInfo> result;
    const auto roomIt = _rooms.find(room);
    if (roomIt == _rooms.end()) {
        return result;
    }

    result.reserve(roomIt->second.size());
    for (const auto &entry : roomIt->second) {
        if (!isExpired(entry.second)) {
            result.push_back(entry.second);
        }
    }

    return result;
}

// 判断设备是否在房间内且未过期
bool OnlineRegistry::hasPeer(const std::string &room, const std::string &deviceId) const
{
    const auto roomIt = _rooms.find(room);
    if (roomIt == _rooms.end()) {
        return false;
    }
    const auto deviceIt = roomIt->second.find(deviceId);
    return deviceIt != roomIt->second.end() && !isExpired(deviceIt->second);
}

// 登记一条中继邀请，等待目标设备轮询领取；待领取条目达到上限时拒绝
bool OnlineRegistry::addRelayInvite(const std::string &room, const RelayInvite &invite)
{
    // 同 relayId 的重复邀请按更新处理不占新名额，与既有幂等语义一致
    const bool exists = _relayInvites.find(invite.relayId) != _relayInvites.end();
    if (!exists && static_cast<int>(_relayInvites.size()) >= _maxRelayInvites) {
        gy::rendezvous::logWarn("[OnlineRegistry] 待领取邀请达到上限 " + std::to_string(_maxRelayInvites)
                                + "，拒绝 " + invite.relayId + " 目标 " + invite.targetDeviceId);
        return false;
    }
    RelayInvite stored = invite;
    stored.room = room;
    stored.createdAtMs = gy::rendezvous::nowUtcMs();
    _relayInvites.insert_or_assign(stored.relayId, stored);

    gy::rendezvous::logInfo("[OnlineRegistry] 登记中继邀请 " + stored.relayId + " 目标 "
                            + stored.targetDeviceId + " 房间 " + room);
    return true;
}

// 领取目标为本设备的所有中继邀请（一次性消费）
std::vector<OnlineRegistry::RelayInvite> OnlineRegistry::consumeRelayInvites(
    const std::string &room, const std::string &targetDeviceId)
{
    std::vector<RelayInvite> result;

    for (auto it = _relayInvites.begin(); it != _relayInvites.end();) {
        if (it->second.room == room && it->second.targetDeviceId == targetDeviceId) {
            result.push_back(it->second);
            it = _relayInvites.erase(it);
        } else {
            ++it;
        }
    }

    if (!result.empty()) {
        gy::rendezvous::logInfo("[OnlineRegistry] 设备 " + targetDeviceId + " 领取中继邀请 "
                                + std::to_string(result.size()) + " 条");
    }
    return result;
}

// 清理过期设备和过期邀请
int OnlineRegistry::pruneExpired()
{
    int pruned = 0;

    for (auto roomIt = _rooms.begin(); roomIt != _rooms.end();) {
        auto &devices = roomIt->second;
        for (auto deviceIt = devices.begin(); deviceIt != devices.end();) {
            if (isExpired(deviceIt->second)) {
                gy::rendezvous::logInfo("[OnlineRegistry] 清理过期设备 " + deviceIt->second.deviceId);
                deviceIt = devices.erase(deviceIt);
                pruned++;
            } else {
                ++deviceIt;
            }
        }
        // 空房间一并回收，避免长驻进程累积空容器
        if (devices.empty()) {
            roomIt = _rooms.erase(roomIt);
        } else {
            ++roomIt;
        }
    }

    // 过期邀请同样回收，避免目标设备离线后邀请无限堆积
    const int64_t nowMs = gy::rendezvous::nowUtcMs();
    for (auto it = _relayInvites.begin(); it != _relayInvites.end();) {
        if (it->second.createdAtMs + static_cast<int64_t>(_relayInviteTtlSeconds) * 1000 < nowMs) {
            gy::rendezvous::logInfo("[OnlineRegistry] 清理过期中继邀请 " + it->first);
            it = _relayInvites.erase(it);
            pruned++;
        } else {
            ++it;
        }
    }

    return pruned;
}

// 设备总数量
int OnlineRegistry::totalCount() const
{
    int total = 0;
    for (const auto &entry : _rooms) {
        total += static_cast<int>(entry.second.size());
    }
    return total;
}

// 指定房间的设备数量
int OnlineRegistry::roomCount(const std::string &room) const
{
    const auto roomIt = _rooms.find(room);
    return roomIt == _rooms.end() ? 0 : static_cast<int>(roomIt->second.size());
}

// 判断设备是否已过期
bool OnlineRegistry::isExpired(const PeerInfo &peer) const
{
    const int ttl = peer.ttlSeconds > 0 ? peer.ttlSeconds : _defaultTtlSeconds;
    return peer.registeredAtMs + static_cast<int64_t>(ttl) * 1000 < gy::rendezvous::nowUtcMs();
}
