/**
* @file    online_registry.cpp
* @version 7.9.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点在线设备注册表实现
*
* Change Log:
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请的登记、领取与过期回收
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点注册表
*/

#include "online_registry.h"

#include <QDebug>

// 中继邀请的有效期：接收端按心跳周期轮询，60 秒足够覆盖短暂离线
static constexpr int kRelayInviteTtlSeconds = 60;

// 构造函数
OnlineRegistry::OnlineRegistry(int defaultTtlSeconds, QObject *parent)
    : QObject{parent}
    , _defaultTtlSeconds{defaultTtlSeconds}
{
}

// 生成 room:deviceId 复合键
QString OnlineRegistry::makeKey(const QString &room, const QString &deviceId)
{
    return room + QStringLiteral(":") + deviceId;
}

// 注册或更新设备
bool OnlineRegistry::upsertPeer(const QString &room, const PeerInfo &peer)
{
    const QString key = makeKey(room, peer.deviceId);
    _peers.insert(key, peer);

    qDebug() << "[OnlineRegistry] 注册设备" << peer.deviceId
             << "到房间" << room << "，TTL" << peer.ttlSeconds << "秒";
    return true;
}

// 获取房间内所有未过期的设备
QList<OnlineRegistry::PeerInfo> OnlineRegistry::peersForRoom(const QString &room) const
{
    QList<PeerInfo> result;
    const QString prefix = room + QStringLiteral(":");

    for (auto it = _peers.lowerBound(prefix); it != _peers.end() && it.key().startsWith(prefix); ++it) {
        if (!isExpired(it.value())) {
            result.append(it.value());
        }
    }

    return result;
}

// 判断设备是否在房间内且未过期
bool OnlineRegistry::hasPeer(const QString &room, const QString &deviceId) const
{
    const QString key = makeKey(room, deviceId);
    const auto it = _peers.constFind(key);
    return it != _peers.constEnd() && !isExpired(it.value());
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
    QList<QString> expiredKeys;

    for (auto it = _peers.constBegin(); it != _peers.constEnd(); ++it) {
        if (isExpired(it.value())) {
            expiredKeys.append(it.key());
            qDebug() << "[OnlineRegistry] 清理过期设备" << it.value().deviceId;
        }
    }

    for (const QString &key : expiredKeys) {
        _peers.remove(key);
        pruned++;
    }

    // 过期邀请同样回收，避免目标设备离线后邀请无限堆积
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (auto it = _relayInvites.begin(); it != _relayInvites.end();) {
        if (it->createdAt.addSecs(kRelayInviteTtlSeconds) < now) {
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
    return _peers.size();
}

// 指定房间的设备数量
int OnlineRegistry::roomCount(const QString &room) const
{
    return peersForRoom(room).size();
}

// 判断设备是否已过期
bool OnlineRegistry::isExpired(const PeerInfo &peer) const
{
    const int ttl = peer.ttlSeconds > 0 ? peer.ttlSeconds : _defaultTtlSeconds;
    return peer.registeredAt.addSecs(ttl) < QDateTime::currentDateTimeUtc();
}