/**
 * @file    online_registry.h
 * @version 8.0.0
 * @date    2026-10-24
 * @author  GridYard Team
 * @brief   协调节点在线设备注册表
 *
 * 内存中的在线设备表，以 room -> deviceId 两级哈希存储，
 * 房间名可以包含任意字符。维护设备的候选端点、TTL 过期清理，
 * 同时保管待领取的中继邀请。纯 C++ 实现，时间统一为 UTC 毫秒时戳。
 */

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class OnlineRegistry {
public:
    // 在线设备信息
    struct PeerInfo {
        std::string deviceId;                            // 设备唯一标识
        std::string deviceName;                          // 展示名
        std::vector<std::string> addresses;              // 候选地址列表
        uint16_t tcpPort = 0;                            // 直连 TCP 端口
        uint16_t discoveryPort = 0;                      // 发现服务 UDP 端口
        int64_t registeredAtMs = 0;                      // 注册时刻（UTC 毫秒）
        int ttlSeconds = 0;                              // 心跳租约时长（秒）

        bool operator==(const PeerInfo &other) const {
            return deviceId == other.deviceId;
        }
    };

    // 待领取的中继邀请：发送端登记，目标设备轮询领取后凭 relay_id 加入中继会话
    struct RelayInvite {
        std::string room;              // 所属房间
        std::string relayId;           // 中继会话标识
        std::string senderDeviceId;    // 发送端设备
        std::string targetDeviceId;    // 目标设备
        std::string fileName;          // 传输文件名
        int64_t totalBytes = 0;        // 传输总字节数
        int64_t createdAtMs = 0;       // 登记时刻（UTC 毫秒）
    };

    explicit OnlineRegistry(int defaultTtlSeconds = 30);

    // 注册或更新设备；房间数或每房设备数达到上限时拒绝新条目
    bool upsertPeer(const std::string &room, const PeerInfo &peer);

    // 调整房间数与每房设备数上限（测试可调小；对后续注册生效）
    void setRegistryLimits(int maxRooms, int maxDevicesPerRoom);
    // 调整中继邀请有效期（测试可调小）
    void setRelayInviteTtlSeconds(int ttlSeconds);
    // 调整待领取邀请的登记上限（测试可调小）
    void setMaxRelayInvites(int maxInvites);

    // 获取房间内所有未过期的设备
    std::vector<PeerInfo> peersForRoom(const std::string &room) const;

    // 判断设备是否在房间内且未过期
    bool hasPeer(const std::string &room, const std::string &deviceId) const;

    // 登记一条中继邀请，等待目标设备轮询领取；待领取条目达到上限时拒绝并返回假
    bool addRelayInvite(const std::string &room, const RelayInvite &invite);

    // 领取目标为本设备的所有中继邀请（一次性消费）
    std::vector<RelayInvite> consumeRelayInvites(const std::string &room,
                                                 const std::string &targetDeviceId);

    // 清理过期设备和过期邀请
    int pruneExpired();

    // 设备数量
    int totalCount() const;
    int roomCount(const std::string &room) const;

private:
    // 判断设备是否已过期
    bool isExpired(const PeerInfo &peer) const;

    int _defaultTtlSeconds = 30;    // 在线注册表默认心跳租约过期时长（秒）
    int _maxRooms = 0;              // 房间数上限，0 表示未初始化（构造时取常量默认）
    int _maxDevicesPerRoom = 0;     // 每房设备数上限
    int _relayInviteTtlSeconds = 0; // 中继邀请有效期，0 表示未初始化
    int _maxRelayInvites = 0;       // 待领取邀请登记上限，0 表示未初始化
    std::unordered_map<std::string, std::unordered_map<std::string, PeerInfo>> _rooms;  // room -> (deviceId -> peer)
    std::unordered_map<std::string, RelayInvite> _relayInvites;  // relayId -> invite，过期由 pruneExpired 统一回收
};
