/**
* @file    protocol.h
* @date    2026-05-24
* @author  GY
* @brief   GridYard 应用层通信协议（TLV 帧 + Type 码）
*
* 全局唯一的协议常量定义入口。所有 Type 码、帧头字段、Payload 字段名
* 常量都集中在 gy::protocol 命名空间下，避免散落到各模块复制粘贴。
* TLV 帧格式见《V1.0 架构设计说明书》§7：8 字节帧头（uint32 Type +
* uint32 Length 大端序）+ Length 字节载荷。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0 占位：仅声明命名空间与默认端口；Type 码集合留待 Stage 1
*/

#pragma once

#include <cstdint>

namespace gy::protocol {

// 帧头总长度（uint32 Type + uint32 Length）
inline constexpr quint32 kHeaderBytes = 8;

// 默认网络端口
inline constexpr quint16 kDefaultDiscoveryPort = 45678;   // UDP 设备发现
inline constexpr quint16 kDefaultP2pPort       = 35100;   // TCP P2P 文件传输

// ---- V1.0 Type 码（Stage 1 任务 1.1 填充）-----------------------------
// 预留示意，真正的值在 Stage 1 按设计书 §7.2 落地：
// inline constexpr quint32 kTypeHello       = 0x0001;
// inline constexpr quint32 kTypeTransferReq = 0x0101;
// ...

}  // namespace gy::protocol
