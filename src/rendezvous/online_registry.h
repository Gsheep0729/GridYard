/**
* @file    online_registry.h
* @version 7.12.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点在线设备注册表
*
* 内存中的在线设备表，以 room -> deviceId 两级哈希存储，
* 房间名可以包含任意字符。维护设备的候选端点、TTL 过期清理，
* 同时保管待领取的中继邀请。
*
* Change Log:
* [v7.12.0] GY   2026-10-02
* * 房间存储改为二级哈希，消除复合键前缀扫描在 room 含 ":" 时的隔离绕过
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请登记与按目标设备一次性领取
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点注册表
*/

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
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

    // 待领取的中继邀请：发送端登记，目标设备轮询领取后凭 relay_id 加入中继会话
    struct RelayInvite {
        QString room;
        QString relayId;
        QString senderDeviceId;
        QString targetDeviceId;
        QString fileName;
        qint64 totalBytes = 0;
        QDateTime createdAt;
    };

    explicit OnlineRegistry(int defaultTtlSeconds = 30, QObject *parent = nullptr);

    // 注册或更新设备
    bool upsertPeer(const QString &room, const PeerInfo &peer);

    // 获取房间内所有未过期的设备
    QList<PeerInfo> peersForRoom(const QString &room) const;

    // 判断设备是否在房间内且未过期
    bool hasPeer(const QString &room, const QString &deviceId) const;

    // 登记一条中继邀请，等待目标设备轮询领取
    void addRelayInvite(const QString &room, const RelayInvite &invite);

    // 领取目标为本设备的所有中继邀请（一次性消费）
    QList<RelayInvite> consumeRelayInvites(const QString &room, const QString &targetDeviceId);

    // 清理过期设备和过期邀请
    int pruneExpired();

    // 设备数量
    int totalCount() const;
    int roomCount(const QString &room) const;

private:
    bool isExpired(const PeerInfo &peer) const;

    int _defaultTtlSeconds = 30;
    QHash<QString, QHash<QString, PeerInfo>> _rooms;  // room -> (deviceId -> peer)
    QHash<QString, RelayInvite> _relayInvites;        // relayId -> invite，过期由 pruneExpired 统一回收
};
