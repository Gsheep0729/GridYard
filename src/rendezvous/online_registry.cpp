/**
* @file    online_registry.cpp
* @version 7.17.1
* @date    2026-10-04
* @author  GridYard Team
* @brief   协调节点在线设备注册表实现
*
* Change Log:
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
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
* [v7.13.4] GY   2026-10-03
* * upsertPeer 增加房间数与每房设备数上限，邀请 TTL 从常量改为可配置
* [v7.12.0] GY   2026-10-02
* * 房间存储改为二级哈希，消除复合键前缀扫描在 room 含 ":" 时的隔离绕过
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请的登记、领取与过期回收
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点注册表
*/

#include "online_registry.h"
#include "rendezvous_limits.h"

#include <QDebug>

// 构造函数
OnlineRegistry::OnlineRegistry(int defaultTtlSeconds, QObject *parent)
    : QObject{parent}
    , _defaultTtlSeconds{defaultTtlSeconds}
    , _maxRooms{gy::rendezvous::kMaxRegistryRooms}
    , _maxDevicesPerRoom{gy::rendezvous::kMaxDevicesPerRoom}
    , _relayInviteTtlSeconds{gy::rendezvous::kRelayInviteTtlSeconds}
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

// 调整中继邀请有效期
void OnlineRegistry::setRelayInviteTtlSeconds(int ttlSeconds)
{
    if (ttlSeconds > 0) {
        _relayInviteTtlSeconds = ttlSeconds;
    }
}

// 注册或更新设备
bool OnlineRegistry::upsertPeer(const QString &room, const PeerInfo &peer)
{
    const auto roomIt = _rooms.constFind(room);
    if (roomIt != _rooms.constEnd()) {
        // 已注册设备的心跳刷新不受上限限制，否则正常客户端反而被拒
        if (!roomIt->contains(peer.deviceId) && roomIt->size() >= _maxDevicesPerRoom) {
            qWarning() << "[OnlineRegistry] 房间" << room << "设备数达到上限，拒绝"
                       << peer.deviceId;
            return false;
        }
    } else if (_rooms.size() >= _maxRooms) {
        qWarning() << "[OnlineRegistry] 房间数达到上限，拒绝新房间" << room;
        return false;
    }

    _rooms[room][peer.deviceId] = peer;

    qDebug() << "[OnlineRegistry] 注册设备" << peer.deviceId
             << "到房间" << room << "，TTL" << peer.ttlSeconds << "秒";
    return true;
}

// 获取房间内所有未过期的设备
QList<OnlineRegistry::PeerInfo> OnlineRegistry::peersForRoom(const QString &room) const
{
    QList<PeerInfo> result;
    const QHash<QString, PeerInfo> &devices = _rooms.value(room);

    result.reserve(devices.size());
    for (const PeerInfo &peer : devices) {
        if (!isExpired(peer)) {
            result.append(peer);
        }
    }

    return result;
}

// 判断设备是否在房间内且未过期
bool OnlineRegistry::hasPeer(const QString &room, const QString &deviceId) const
{
    const auto roomIt = _rooms.constFind(room);
    if (roomIt == _rooms.constEnd()) {
        return false;
    }
    const auto deviceIt = roomIt->constFind(deviceId);
    return deviceIt != roomIt->constEnd() && !isExpired(deviceIt.value());
}

// 登记一条中继邀请，等待目标设备轮询领取
void OnlineRegistry::addRelayInvite(const QString &room, const RelayInvite &invite)
{
    RelayInvite stored = invite;
    stored.room = room;
    stored.createdAt = QDateTime::currentDateTimeUtc();
    _relayInvites.insert(stored.relayId, stored);

    qDebug() << "[OnlineRegistry] 登记中继邀请" << stored.relayId
             << "目标" << stored.targetDeviceId << "房间" << room;
}

// 领取目标为本设备的所有中继邀请（一次性消费）
QList<OnlineRegistry::RelayInvite> OnlineRegistry::consumeRelayInvites(
    const QString &room, const QString &targetDeviceId)
{
    QList<RelayInvite> result;

    for (auto it = _relayInvites.begin(); it != _relayInvites.end();) {
        if (it->room == room && it->targetDeviceId == targetDeviceId) {
            result.append(it.value());
            it = _relayInvites.erase(it);
        } else {
            ++it;
        }
    }

    if (!result.isEmpty()) {
        qDebug() << "[OnlineRegistry] 设备" << targetDeviceId
                 << "领取中继邀请" << result.size() << "条";
    }
    return result;
}

// 清理过期设备和过期邀请
int OnlineRegistry::pruneExpired()
{
    int pruned = 0;

    for (auto roomIt = _rooms.begin(); roomIt != _rooms.end();) {
        auto &devices = roomIt.value();
        for (auto deviceIt = devices.begin(); deviceIt != devices.end();) {
            if (isExpired(deviceIt.value())) {
                qDebug() << "[OnlineRegistry] 清理过期设备" << deviceIt->deviceId;
                deviceIt = devices.erase(deviceIt);
                pruned++;
            } else {
                ++deviceIt;
            }
        }
        // 空房间一并回收，避免长驻进程累积空容器
        if (devices.isEmpty()) {
            roomIt = _rooms.erase(roomIt);
        } else {
            ++roomIt;
        }
    }

    // 过期邀请同样回收，避免目标设备离线后邀请无限堆积
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = _relayInvites.begin(); it != _relayInvites.end();) {
        if (it->createdAt.addSecs(_relayInviteTtlSeconds) < now) {
            qDebug() << "[OnlineRegistry] 清理过期中继邀请" << it.key();
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
    for (const auto &devices : _rooms) {
        total += devices.size();
    }
    return total;
}

// 指定房间的设备数量
int OnlineRegistry::roomCount(const QString &room) const
{
    return _rooms.value(room).size();
}

// 判断设备是否已过期
bool OnlineRegistry::isExpired(const PeerInfo &peer) const
{
    const int ttl = peer.ttlSeconds > 0 ? peer.ttlSeconds : _defaultTtlSeconds;
    return peer.registeredAt.addSecs(ttl) < QDateTime::currentDateTimeUtc();
}
