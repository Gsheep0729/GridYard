/**
* @file    rendezvous_protocol_keys.h
* @version 7.14.0
* @date    2026-10-03
* @author  GridYard Team
* @brief   协调/中继行协议字段名与类型常量
*
* 行协议的 JSON 字段名与 type 串此前在服务端与客户端各维护一份字符串，
* 双端加字段时漏改一端只能靠运行时发现。本头文件是双端共用的单一来源，
* 服务端与客户端共同链接 gy_protocol_contracts 接口目标获得编译期锚点。
* 全面迁移分批进行：先覆盖当前任务触碰的字段，其余字面量随后续任务收敛。
*
* Change Log:
* [v7.14.0] GY   2026-10-03
* * 初始版本：token 全链路任务触碰的字段名与 relay 握手类型常量化
*/

#pragma once

#include <QLatin1String>

namespace gy::rendezvous {

// 行协议 JSON 字段名
inline constexpr QLatin1String kKeyType{"type"};
inline constexpr QLatin1String kKeyToken{"token"};
inline constexpr QLatin1String kKeyDeviceId{"device_id"};
inline constexpr QLatin1String kKeyDeviceName{"device_name"};
inline constexpr QLatin1String kKeyRoom{"room"};
inline constexpr QLatin1String kKeyAddresses{"addresses"};
inline constexpr QLatin1String kKeyTcpPort{"tcp_port"};
inline constexpr QLatin1String kKeyTtlSeconds{"ttl_seconds"};
inline constexpr QLatin1String kKeyRelayId{"relay_id"};
inline constexpr QLatin1String kKeyTargetDeviceId{"target_device_id"};
inline constexpr QLatin1String kKeyFileName{"file_name"};
inline constexpr QLatin1String kKeyTotalBytes{"total_bytes"};

// relay 握手与控制类型串
inline constexpr QLatin1String kTypeRelayCreate{"relay_create"};
inline constexpr QLatin1String kTypeRelayJoin{"relay_join"};
inline constexpr QLatin1String kTypeRelayReady{"relay_ready"};
inline constexpr QLatin1String kTypeRelayError{"relay_error"};

}
