/**
* @file    online_registry.cpp
* @version 7.2.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点在线设备注册表实现
*
* Change Log:
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点注册表
*/

#include "online_registry.h"

#include <QDebug>

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

// 清理过期设备
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