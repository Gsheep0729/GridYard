/**
* @file    rendezvous_protocol.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点协议处理实现
*/

#include "rendezvous_protocol.h"
#include "rendezvous_limits.h"
#include "rendezvous_protocol_keys.h"
#include "rendezvous_time.h"

#include <algorithm>
#include <cmath>

using namespace gy::rendezvous;

// 宽松取字符串：缺失或非字符串一律返回空串
std::string RendezvousProtocol::stringField(const nlohmann::json &json, std::string_view key)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

// 宽松取整数：缺失或非数值返回缺省，整数值浮点按旧语义折算
int RendezvousProtocol::intField(const nlohmann::json &json, std::string_view key, int defaultValue)
{
    const auto it = json.find(key);
    if (it == json.end()) {
        return defaultValue;
    }
    if (it->is_number_integer()) {
        return it->get<int>();
    }
    if (it->is_number_float()) {
        const double value = it->get<double>();
        if (std::floor(value) == value) {
            return static_cast<int>(value);
        }
    }
    return defaultValue;
}

// 宽松取 64 位整数：缺失或非整数返回 0
int64_t RendezvousProtocol::int64Field(const nlohmann::json &json, std::string_view key)
{
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number_integer()) {
        return 0;
    }
    return it->get<int64_t>();
}

// 解析请求消息类型
RendezvousProtocol::MessageType RendezvousProtocol::parseRequest(const nlohmann::json &json,
                                                                 std::string *errorString)
{
    const std::string type = stringField(json, kKeyType);

    if (type == kTypeRegister) {
        return MessageType::Register;
    } else if (type == kTypeListPeers) {
        return MessageType::ListPeers;
    } else if (type == kTypeRelayInvite) {
        return MessageType::RelayInvite;
    } else if (type == kTypeRelayPoll) {
        return MessageType::RelayPoll;
    }

    *errorString = "未知消息类型: " + type;
    return MessageType::Error;
}

// 构建注册响应
nlohmann::json RendezvousProtocol::buildRegisterAck(int ttlSeconds)
{
    nlohmann::json json;
    json[kKeyType] = kTypeRegisterAck;
    json[kKeyTtlSeconds] = ttlSeconds;
    json[kKeyServerTime] = formatIsoSecondUtc(nowUtcMs());
    return json;
}

// 构建候选列表响应
nlohmann::json RendezvousProtocol::buildPeersResponse(
    const std::vector<OnlineRegistry::PeerInfo> &peers)
{
    nlohmann::json json;
    json[kKeyType] = kTypePeers;

    nlohmann::json items = nlohmann::json::array();
    for (const OnlineRegistry::PeerInfo &peer : peers) {
        nlohmann::json item;
        item[kKeyDeviceId] = peer.deviceId;
        item[kKeyDeviceName] = peer.deviceName;
        nlohmann::json addresses = nlohmann::json::array();
        for (const std::string &address : peer.addresses) {
            addresses.push_back(address);
        }
        item[kKeyAddresses] = std::move(addresses);
        item[kKeyTcpPort] = peer.tcpPort;
        item[kKeyDiscoveryPort] = peer.discoveryPort;
        item[kKeyUpdatedAt] = formatIsoSecondUtc(peer.registeredAtMs);
        items.push_back(std::move(item));
    }

    json[kKeyItems] = std::move(items);
    return json;
}

// 构建中继邀请受理响应
nlohmann::json RendezvousProtocol::buildRelayInviteAck(const std::string &relayId)
{
    nlohmann::json json;
    json[kKeyType] = kTypeRelayInviteAck;
    json[kKeyRelayId] = relayId;
    return json;
}

// 构建轮询到的中继邀请列表响应
nlohmann::json RendezvousProtocol::buildRelayInvites(
    const std::vector<OnlineRegistry::RelayInvite> &invites)
{
    nlohmann::json json;
    json[kKeyType] = kTypeRelayInvites;

    nlohmann::json items = nlohmann::json::array();
    for (const OnlineRegistry::RelayInvite &invite : invites) {
        nlohmann::json item;
        item[kKeyRelayId] = invite.relayId;
        item[kKeySenderDeviceId] = invite.senderDeviceId;
        item[kKeyTargetDeviceId] = invite.targetDeviceId;
        item[kKeyFileName] = invite.fileName;
        item[kKeyTotalBytes] = invite.totalBytes;
        items.push_back(std::move(item));
    }

    json[kKeyItems] = std::move(items);
    return json;
}

// 构建错误响应
nlohmann::json RendezvousProtocol::buildError(const std::string &message)
{
    nlohmann::json json;
    json[kKeyType] = kTypeError;
    json[kKeyMessage] = message;
    return json;
}

// 提取 room 字段
std::string RendezvousProtocol::extractRoom(const nlohmann::json &json)
{
    return stringField(json, kKeyRoom);
}

// 提取 token 字段
std::string RendezvousProtocol::extractToken(const nlohmann::json &json)
{
    return stringField(json, kKeyToken);
}

// 提取 device_id 字段
std::string RendezvousProtocol::extractDeviceId(const nlohmann::json &json)
{
    return stringField(json, kKeyDeviceId);
}

// 提取 PeerInfo 字段
OnlineRegistry::PeerInfo RendezvousProtocol::extractPeerInfo(const nlohmann::json &json)
{
    OnlineRegistry::PeerInfo peer;
    peer.deviceId = stringField(json, kKeyDeviceId);
    peer.deviceName = stringField(json, kKeyDeviceName);

    const auto addressesIt = json.find(kKeyAddresses);
    if (addressesIt != json.end() && addressesIt->is_array()) {
        for (const auto &addr : *addressesIt) {
            if (addr.is_string()) {
                peer.addresses.push_back(addr.get<std::string>());
            }
        }
    }

    peer.tcpPort = static_cast<uint16_t>(intField(json, kKeyTcpPort, 0));
    peer.discoveryPort = static_cast<uint16_t>(intField(json, kKeyDiscoveryPort, 45678));
    peer.registeredAtMs = nowUtcMs();
    // TTL 是客户端自报字段，服务端必须夹紧，防止注入"永不过期"的伪设备
    peer.ttlSeconds = std::clamp(intField(json, kKeyTtlSeconds, 30),
                                 gy::rendezvous::kMinPeerTtlSeconds,
                                 gy::rendezvous::kMaxPeerTtlSeconds);

    return peer;
}

// 提取中继邀请字段
OnlineRegistry::RelayInvite RendezvousProtocol::extractRelayInvite(const nlohmann::json &json)
{
    OnlineRegistry::RelayInvite invite;
    invite.relayId = stringField(json, kKeyRelayId);
    invite.senderDeviceId = stringField(json, kKeySenderDeviceId);
    invite.targetDeviceId = stringField(json, kKeyTargetDeviceId);
    invite.fileName = stringField(json, kKeyFileName);
    invite.totalBytes = int64Field(json, kKeyTotalBytes);
    return invite;
}

// 验证 token
bool RendezvousProtocol::validateToken(const std::string &provided, const std::string &expected)
{
    if (expected.empty()) {
        return true;  // 未配置 token 时跳过验证
    }
    return provided == expected;
}
