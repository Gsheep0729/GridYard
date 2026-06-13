/**
* @file    protocol.h
* @version 4.10.0
* @date    2026-06-13
* @author  GY
* @brief   GridYard 应用层通信协议（TLV 帧 + Type 码）
*
* 全局唯一的协议常量定义入口。所有 Type 码、帧头字段、Payload 字段名
* 常量都集中在 gy::protocol 命名空间下，避免散落到各模块复制粘贴。
* TLV 帧格式见《V1.0 架构设计说明书》§7：8 字节帧头（uint32 Type +
* uint32 Length 大端序）+ Length 字节载荷。
*
* Change Log:
* [v0.1.0] GY   2026-06-02
* * Stage 1：定义 V1.0 Type 码集合
* [v0.0.1] GY   2026-05-24
* * Stage 0 占位：仅声明命名空间与默认端口
*/

#pragma once

#include <QtGlobal>

namespace gy::protocol {

// 帧头总长度（uint32 Type + uint32 Length）
inline constexpr quint32 kHeaderBytes = 8;

// 默认网络端口
inline constexpr quint16 kDefaultDiscoveryPort = 45678;   // UDP 设备发现
inline constexpr quint16 kDefaultP2pPort       = 35100;   // TCP P2P 文件传输

// ---- V1.0 Type 码 --------------------------------------------------------
inline constexpr quint32 kTypeHello        = 0x0001;   // UDP 广播：设备上线 / 心跳
inline constexpr quint32 kTypeTransferReq  = 0x0101;   // TCP：文件元数据握手请求
inline constexpr quint32 kTypeTransferRsp  = 0x0102;   // TCP：握手响应（接受/拒绝）
inline constexpr quint32 kTypeDataChunk    = 0x0201;   // TCP：文件数据分块
inline constexpr quint32 kTypeChunkAck     = 0x0301;   // TCP：单文件完成确认与校验
inline constexpr quint32 kTypeTransferDone = 0x0302;   // TCP：全部文件发送完毕
inline constexpr quint32 kTypeCancel       = 0x0401;   // TCP：取消本次传输

}  // namespace gy::protocol
