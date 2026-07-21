/**
* @file    online_registry.h
* @version 7.2.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点在线设备注册表
*
* 内存中的在线设备表，以 room + deviceId 为 key 存储。
* 维护设备的候选端点和 TTL 过期清理。
*
* Change Log:
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点注册表
*/

#pragma once

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>

class OnlineRegistry : public QObject {
    Q_OBJECT

public:
    // 在线设备信息
    struct PeerInfo {
        QString deviceId;
        QString deviceName;
        QStringList addresses;
        quint16 tcpPort = 0;
        quint16 discoveryPort = 0;
        QDateTime registeredAt;
        int ttlSeconds = 0;

        bool operator==(const PeerInfo &other) const {
            return deviceId == other.deviceId;
        }
    };

    explicit OnlineRegistry(int defaultTtlSeconds = 30, QObject *parent = nullptr);

    // 注册或更新设备
    bool upsertPeer(const QString &room, const PeerInfo &peer);

    // 获取房间内所有未过期的设备
    QList<PeerInfo> peersForRoom(const QString &room) const;

    // 清理过期设备
    int pruneExpired();

    // 设备数量
    int totalCount() const;
    int roomCount(const QString &room) const;

private:
    bool isExpired(const PeerInfo &peer) const;
    static QString makeKey(const QString &room, const QString &deviceId);

    int _defaultTtlSeconds = 30;
    QMap<QString, PeerInfo> _peers;  // room:deviceId -> peer
};