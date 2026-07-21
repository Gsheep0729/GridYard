/**
* @file    rendezvous_protocol.cpp
* @version 7.2.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点协议处理实现
*
* Change Log:
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点协议处理
*/

#include "rendezvous_protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// 解析请求消息类型
RendezvousProtocol::MessageType RendezvousProtocol::parseRequest(const QJsonObject &json, QString *errorString)
{
    const QString type = json[QStringLiteral("type")].toString();

    if (type == QStringLiteral("register")) {
        return MessageType::Register;
    } else if (type == QStringLiteral("list_peers")) {
        return MessageType::ListPeers;
    }

    *errorString = QStringLiteral("未知消息类型: ") + type;
    return MessageType::Error;
}

// 构建注册响应
QJsonObject RendezvousProtocol::buildRegisterAck(int ttlSeconds)
{
    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("register_ack");
    json[QStringLiteral("ttl_seconds")] = ttlSeconds;
    json[QStringLiteral("server_time")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return json;
}

// 构建候选列表响应
QJsonObject RendezvousProtocol::buildPeersResponse(const QList<OnlineRegistry::PeerInfo> &peers)
{
    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("peers");

    QJsonArray items;
    for (const OnlineRegistry::PeerInfo &peer : peers) {
        QJsonObject item;
        item[QStringLiteral("device_id")] = peer.deviceId;
        item[QStringLiteral("device_name")] = peer.deviceName;
        item[QStringLiteral("addresses")] = QJsonArray::fromStringList(peer.addresses);
        item[QStringLiteral("tcp_port")] = peer.tcpPort;
        item[QStringLiteral("discovery_port")] = peer.discoveryPort;
        item[QStringLiteral("updated_at")] = peer.registeredAt.toString(Qt::ISODate);
        items.append(item);
    }

    json[QStringLiteral("items")] = items;
    return json;
}

// 构建错误响应
QJsonObject RendezvousProtocol::buildError(const QString &message)
{
    QJsonObject json;
    json[QStringLiteral("type")] = QStringLiteral("error");
    json[QStringLiteral("message")] = message;
    return json;
}

// 提取 room 字段
QString RendezvousProtocol::extractRoom(const QJsonObject &json)
{
    return json[QStringLiteral("room")].toString();
}

// 提取 token 字段
QString RendezvousProtocol::extractToken(const QJsonObject &json)
{
    return json[QStringLiteral("token")].toString();
}

// 提取 device_id 字段
QString RendezvousProtocol::extractDeviceId(const QJsonObject &json)
{
    return json[QStringLiteral("device_id")].toString();
}

// 提取 PeerInfo 字段
OnlineRegistry::PeerInfo RendezvousProtocol::extractPeerInfo(const QJsonObject &json)
{
    OnlineRegistry::PeerInfo peer;
    peer.deviceId = json[QStringLiteral("device_id")].toString();
    peer.deviceName = json[QStringLiteral("device_name")].toString();

    const QJsonArray addresses = json[QStringLiteral("addresses")].toArray();
    for (const QJsonValue &addr : addresses) {
        peer.addresses.append(addr.toString());
    }

    peer.tcpPort = static_cast<quint16>(json[QStringLiteral("tcp_port")].toInt());
    peer.discoveryPort = static_cast<quint16>(json[QStringLiteral("discovery_port")].toInt(45678));
    peer.registeredAt = QDateTime::currentDateTimeUtc();
    peer.ttlSeconds = json[QStringLiteral("ttl_seconds")].toInt(30);

    return peer;
}

// 验证 token
bool RendezvousProtocol::validateToken(const QString &provided, const QString &expected)
{
    if (expected.isEmpty()) {
        return true;  // 未配置 token 时跳过验证
    }
    return provided == expected;
}