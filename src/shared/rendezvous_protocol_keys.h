/**
* @file    rendezvous_protocol_keys.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   协调/中继行协议字段名与类型常量
*
* 行协议的 JSON 字段名与 type 串此前在服务端与客户端各维护一份字符串，
* 双端加字段时漏改一端只能靠运行时发现。本头文件是双端共用的单一来源，
* 服务端与客户端共同链接 gy_protocol_contracts 接口目标获得编译期锚点。
*/

#pragma once

#include <QLatin1String>

namespace gy::rendezvous {

// 行协议 JSON 字段名
inline constexpr QLatin1String kKeyType{"type"};
inline constexpr QLatin1String kKeyToken{"token"};
inline constexpr QLatin1String kKeyRoom{"room"};
inline constexpr QLatin1String kKeyDeviceId{"device_id"};
inline constexpr QLatin1String kKeyDeviceName{"device_name"};
inline constexpr QLatin1String kKeyAddresses{"addresses"};
inline constexpr QLatin1String kKeyTcpPort{"tcp_port"};
inline constexpr QLatin1String kKeyDiscoveryPort{"discovery_port"};
inline constexpr QLatin1String kKeyTtlSeconds{"ttl_seconds"};
inline constexpr QLatin1String kKeyServerTime{"server_time"};
inline constexpr QLatin1String kKeyUpdatedAt{"updated_at"};
inline constexpr QLatin1String kKeyItems{"items"};
inline constexpr QLatin1String kKeyMessage{"message"};
inline constexpr QLatin1String kKeyCode{"code"};
inline constexpr QLatin1String kKeyRelayId{"relay_id"};
inline constexpr QLatin1String kKeyTargetDeviceId{"target_device_id"};
inline constexpr QLatin1String kKeySenderDeviceId{"sender_device_id"};
inline constexpr QLatin1String kKeySenderName{"sender_name"};
inline constexpr QLatin1String kKeyFileName{"file_name"};
inline constexpr QLatin1String kKeyTotalBytes{"total_bytes"};

// 协调控制面类型串
inline constexpr QLatin1String kTypeRegister{"register"};
inline constexpr QLatin1String kTypeRegisterAck{"register_ack"};
inline constexpr QLatin1String kTypeListPeers{"list_peers"};
inline constexpr QLatin1String kTypePeers{"peers"};
inline constexpr QLatin1String kTypeRelayInvite{"relay_invite"};
inline constexpr QLatin1String kTypeRelayInviteAck{"relay_invite_ack"};
inline constexpr QLatin1String kTypeRelayPoll{"relay_poll"};
inline constexpr QLatin1String kTypeRelayInvites{"relay_invites"};
inline constexpr QLatin1String kTypeError{"error"};

// relay 握手与控制类型串
inline constexpr QLatin1String kTypeRelayCreate{"relay_create"};
inline constexpr QLatin1String kTypeRelayJoin{"relay_join"};
inline constexpr QLatin1String kTypeRelayReady{"relay_ready"};
inline constexpr QLatin1String kTypeRelayError{"relay_error"};

}
