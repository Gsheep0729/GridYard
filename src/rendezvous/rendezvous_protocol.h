/**
* @file    rendezvous_protocol.h
* @version 8.2.0
* @date    2026-10-29
* @author  GridYard Team
* @brief   协调节点协议处理
*
* 解析和构建协调节点的 JSON 协议消息（nlohmann/json 实现），覆盖设备注册、
* 候选拉取和中继邀请信令（relay_invite / relay_poll）。字段取值保持旧
* QJson 宽松语义：类型不符或缺失一律按缺省值处理，不抛异常。
*/

#pragma once

#include "online_registry.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

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
    static MessageType parseRequest(const nlohmann::json &json, std::string *errorString);

    // 构建注册响应
    static nlohmann::json buildRegisterAck(int ttlSeconds);

    // 构建候选列表响应
    static nlohmann::json buildPeersResponse(const std::vector<OnlineRegistry::PeerInfo> &peers);

    // 构建中继邀请受理响应
    static nlohmann::json buildRelayInviteAck(const std::string &relayId);

    // 构建轮询到的中继邀请列表响应
    static nlohmann::json buildRelayInvites(const std::vector<OnlineRegistry::RelayInvite> &invites);

    // 构建错误响应
    static nlohmann::json buildError(const std::string &message);

    // 提取注册请求字段
    static std::string extractRoom(const nlohmann::json &json);
    static std::string extractToken(const nlohmann::json &json);
    static std::string extractDeviceId(const nlohmann::json &json);
    // 提取设备信息（标识、名称与地址列表）
    static OnlineRegistry::PeerInfo extractPeerInfo(const nlohmann::json &json);
    // 提取中继邀请字段
    static OnlineRegistry::RelayInvite extractRelayInvite(const nlohmann::json &json);

    // 验证 token
    static bool validateToken(const std::string &provided, const std::string &expected);

private:
    // 宽松取字符串：缺失或非字符串一律返回空串（对齐旧 QJsonValue::toString）
    static std::string stringField(const nlohmann::json &json, std::string_view key);
    // 宽松取整数：缺失或非整数返回 defaultValue（对齐旧 QJsonValue::toInt）
    static int intField(const nlohmann::json &json, std::string_view key, int defaultValue);
    // 宽松取 64 位整数：缺失或非整数返回 0（对齐旧 QJsonValue::toInteger）
    static int64_t int64Field(const nlohmann::json &json, std::string_view key);
};
