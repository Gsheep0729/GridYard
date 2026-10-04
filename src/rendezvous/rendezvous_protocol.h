/**
* @file    rendezvous_protocol.h
* @version 7.17.2
* @date    2026-10-04
* @author  GridYard Team
* @brief   协调节点协议处理
*
* 解析和构建协调节点的 JSON 协议消息，覆盖设备注册、候选拉取
* 和中继邀请信令（relay_invite / relay_poll）。
*
* Change Log:
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
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
* [v7.13.0] GY   2026-10-02
* * 同步文件头版本与当前主版本
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请与轮询消息的解析和构建
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点协议处理
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
