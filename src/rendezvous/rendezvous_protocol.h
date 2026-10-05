/**
* @file    rendezvous_protocol.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   协调节点协议处理
*
* 解析和构建协调节点的 JSON 协议消息，覆盖设备注册、候选拉取
* 和中继邀请信令（relay_invite / relay_poll）。
*/

#pragma once

#include "online_registry.h"

#include <QJsonObject>
#include <QString>

class RendezvousProtocol {
public:
    // 消息类型
    enum class MessageType {
        Register,
        RegisterAck,
        ListPeers,
        Peers,
        RelayInvite,
        RelayInviteAck,
        RelayPoll,
        RelayInvites,
        Error
    };

    // 解析请求消息
    static MessageType parseRequest(const QJsonObject &json, QString *errorString);

    // 构建注册响应
    static QJsonObject buildRegisterAck(int ttlSeconds);

    // 构建候选列表响应
    static QJsonObject buildPeersResponse(const QList<OnlineRegistry::PeerInfo> &peers);

    // 构建中继邀请受理响应
    static QJsonObject buildRelayInviteAck(const QString &relayId);

    // 构建轮询到的中继邀请列表响应
    static QJsonObject buildRelayInvites(const QList<OnlineRegistry::RelayInvite> &invites);

    // 构建错误响应
    static QJsonObject buildError(const QString &message);

    // 提取注册请求字段
    static QString extractRoom(const QJsonObject &json);
    static QString extractToken(const QJsonObject &json);
    static QString extractDeviceId(const QJsonObject &json);
    static OnlineRegistry::PeerInfo extractPeerInfo(const QJsonObject &json);
    // 提取中继邀请字段
    static OnlineRegistry::RelayInvite extractRelayInvite(const QJsonObject &json);

    // 验证 token
    static bool validateToken(const QString &provided, const QString &expected);
};
