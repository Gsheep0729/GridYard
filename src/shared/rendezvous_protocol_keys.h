/**
 * @file    rendezvous_protocol_keys.h
 * @version 7.26.1
 * @date    2026-10-24
 * @author  GridYard Team
 * @brief   协调/中继行协议字段名与类型常量
 *
 * 行协议 JSON 字段名与 type 类型的双端唯一定义源，纯 C++ 实现，
 * 服务端（零 Qt）与客户端共同链接 gy_protocol_contracts 接口目标
 * 获得编译期强类型锚点；Qt 客户端经 rendezvous_protocol_keys_qt.h
 * 的 key() 转换为 QLatin1String 使用。键值字节与旧 Qt 版逐字一致。
 */

#pragma once

#include <string_view>

namespace gy::rendezvous {

// 行协议 JSON 字段名
inline constexpr std::string_view kKeyType{"type"};
inline constexpr std::string_view kKeyToken{"token"};
inline constexpr std::string_view kKeyRoom{"room"};
inline constexpr std::string_view kKeyDeviceId{"device_id"};
inline constexpr std::string_view kKeyDeviceName{"device_name"};
inline constexpr std::string_view kKeyAddresses{"addresses"};
inline constexpr std::string_view kKeyTcpPort{"tcp_port"};
inline constexpr std::string_view kKeyDiscoveryPort{"discovery_port"};
inline constexpr std::string_view kKeyTtlSeconds{"ttl_seconds"};
inline constexpr std::string_view kKeyServerTime{"server_time"};
inline constexpr std::string_view kKeyUpdatedAt{"updated_at"};
inline constexpr std::string_view kKeyItems{"items"};
inline constexpr std::string_view kKeyMessage{"message"};
inline constexpr std::string_view kKeyCode{"code"};
inline constexpr std::string_view kKeyRelayId{"relay_id"};
inline constexpr std::string_view kKeyTargetDeviceId{"target_device_id"};
inline constexpr std::string_view kKeySenderDeviceId{"sender_device_id"};
inline constexpr std::string_view kKeySenderName{"sender_name"};
inline constexpr std::string_view kKeyFileName{"file_name"};
inline constexpr std::string_view kKeyTotalBytes{"total_bytes"};

// 协调控制面类型串
inline constexpr std::string_view kTypeRegister{"register"};
inline constexpr std::string_view kTypeRegisterAck{"register_ack"};
inline constexpr std::string_view kTypeListPeers{"list_peers"};
inline constexpr std::string_view kTypePeers{"peers"};
inline constexpr std::string_view kTypeRelayInvite{"relay_invite"};
inline constexpr std::string_view kTypeRelayInviteAck{"relay_invite_ack"};
inline constexpr std::string_view kTypeRelayPoll{"relay_poll"};
inline constexpr std::string_view kTypeRelayInvites{"relay_invites"};
inline constexpr std::string_view kTypeError{"error"};

// relay 握手与控制类型串
inline constexpr std::string_view kTypeRelayCreate{"relay_create"};
inline constexpr std::string_view kTypeRelayJoin{"relay_join"};
inline constexpr std::string_view kTypeRelayReady{"relay_ready"};
inline constexpr std::string_view kTypeRelayError{"relay_error"};

}
