/**
* @file    rendezvous_protocol.cpp
* @version 7.15.2
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点协议处理实现
*
* Change Log:
* [v7.15.2] GY   2026-10-03
* * 协调控制面 type 串与 JSON 字段名全面改用 rendezvous_protocol_keys 常量
* [v7.13.0] GY   2026-10-02
* * 同步文件头版本与当前主版本
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请与轮询消息的解析和构建
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点协议处理
*/

#include "rendezvous_protocol.h"
#include "rendezvous_limits.h"
#include "rendezvous_protocol_keys.h"

using namespace gy::rendezvous;

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// 解析请求消息类型
RendezvousProtocol::MessageType RendezvousProtocol::parseRequest(const QJsonObject &json, QString *errorString)
{
    const QString type = json[kKeyType].toString();

    if (type == kTypeRegister) {
        return MessageType::Register;
    } else if (type == kTypeListPeers) {
        return MessageType::ListPeers;
    } else if (type == kTypeRelayInvite) {
        return MessageType::RelayInvite;
    } else if (type == kTypeRelayPoll) {
        return MessageType::RelayPoll;
    }

    *errorString = QStringLiteral("未知消息类型: ") + type;
    return MessageType::Error;
}

// 构建注册响应
QJsonObject RendezvousProtocol::buildRegisterAck(int ttlSeconds)
{
    QJsonObject json;
    json[kKeyType] = kTypeRegisterAck;
    json[kKeyTtlSeconds] = ttlSeconds;
    json[kKeyServerTime] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return json;
}

// 构建候选列表响应
QJsonObject RendezvousProtocol::buildPeersResponse(const QList<OnlineRegistry::PeerInfo> &peers)
{
    QJsonObject json;
    json[kKeyType] = kTypePeers;

    QJsonArray items;
    for (const OnlineRegistry::PeerInfo &peer : peers) {
        QJsonObject item;
        item[kKeyDeviceId] = peer.deviceId;
        item[kKeyDeviceName] = peer.deviceName;
        item[kKeyAddresses] = QJsonArray::fromStringList(peer.addresses);
        item[kKeyTcpPort] = peer.tcpPort;
        item[kKeyDiscoveryPort] = peer.discoveryPort;
        item[kKeyUpdatedAt] = peer.registeredAt.toString(Qt::ISODate);
        items.append(item);
    }

    json[kKeyItems] = items;
    return json;
}

// 构建中继邀请受理响应
QJsonObject RendezvousProtocol::buildRelayInviteAck(const QString &relayId)
{
    QJsonObject json;
    json[kKeyType] = kTypeRelayInviteAck;
    json[kKeyRelayId] = relayId;
    return json;
}

// 构建轮询到的中继邀请列表响应
QJsonObject RendezvousProtocol::buildRelayInvites(const QList<OnlineRegistry::RelayInvite> &invites)
{
    QJsonObject json;
    json[kKeyType] = kTypeRelayInvites;

    QJsonArray items;
    for (const OnlineRegistry::RelayInvite &invite : invites) {
        QJsonObject item;
        item[kKeyRelayId] = invite.relayId;
        item[kKeySenderDeviceId] = invite.senderDeviceId;
        item[kKeyTargetDeviceId] = invite.targetDeviceId;
        item[kKeyFileName] = invite.fileName;
        item[kKeyTotalBytes] = invite.totalBytes;
        items.append(item);
    }

    json[kKeyItems] = items;
    return json;
}

// 构建错误响应
QJsonObject RendezvousProtocol::buildError(const QString &message)
{
    QJsonObject json;
    json[kKeyType] = kTypeError;
    json[kKeyMessage] = message;
    return json;
}

// 提取 room 字段
QString RendezvousProtocol::extractRoom(const QJsonObject &json)
{
    return json[kKeyRoom].toString();
}

// 提取 token 字段
QString RendezvousProtocol::extractToken(const QJsonObject &json)
{
    return json[kKeyToken].toString();
}

// 提取 device_id 字段
QString RendezvousProtocol::extractDeviceId(const QJsonObject &json)
{
    return json[kKeyDeviceId].toString();
}

// 提取 PeerInfo 字段
OnlineRegistry::PeerInfo RendezvousProtocol::extractPeerInfo(const QJsonObject &json)
{
    OnlineRegistry::PeerInfo peer;
    peer.deviceId = json[kKeyDeviceId].toString();
    peer.deviceName = json[kKeyDeviceName].toString();

    const QJsonArray addresses = json[kKeyAddresses].toArray();
    for (const QJsonValue &addr : addresses) {
        peer.addresses.append(addr.toString());
    }

    peer.tcpPort = static_cast<quint16>(json[kKeyTcpPort].toInt());
    peer.discoveryPort = static_cast<quint16>(json[kKeyDiscoveryPort].toInt(45678));
    peer.registeredAt = QDateTime::currentDateTimeUtc();
    // TTL 是客户端自报字段，服务端必须夹紧，防止注入"永不过期"的伪设备
    peer.ttlSeconds = qBound(gy::rendezvous::kMinPeerTtlSeconds,
                             json[kKeyTtlSeconds].toInt(30),
                             gy::rendezvous::kMaxPeerTtlSeconds);

    return peer;
}

// 提取中继邀请字段
OnlineRegistry::RelayInvite RendezvousProtocol::extractRelayInvite(const QJsonObject &json)
{
    OnlineRegistry::RelayInvite invite;
    invite.relayId = json[kKeyRelayId].toString();
    invite.senderDeviceId = json[kKeySenderDeviceId].toString();
    invite.targetDeviceId = json[kKeyTargetDeviceId].toString();
    invite.fileName = json[kKeyFileName].toString();
    invite.totalBytes = json[kKeyTotalBytes].toInteger();
    return invite;
}

// 验证 token
bool RendezvousProtocol::validateToken(const QString &provided, const QString &expected)
{
    if (expected.isEmpty()) {
        return true;  // 未配置 token 时跳过验证
    }
    return provided == expected;
}
