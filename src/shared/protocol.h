/**
* @file    protocol.h
* @version 4.13.0
* @date    2026-06-15
* @author  GridYard Team
* @brief   GridYard 应用层通信协议（TLV 帧 + Type 码）
*
* 全局唯一的协议常量定义入口。所有 Type 码、帧头字段、Payload 字段名
* 常量都集中在 gy::protocol 命名空间下，避免散落到各模块复制粘贴。
* TLV 帧格式见《V1.0 架构设计说明书》§7：8 字节帧头（uint32 Type +
* uint32 Length 大端序）+ Length 字节载荷。
*
* Change Log:
* [v4.13.0] GY   2026-06-15
* * 添加协议版本常量 kProtocolVersion
* * 添加最大帧载荷长度常量 kMaxPayloadBytes（256MB）
* * 添加稳定错误码枚举 ErrorCode
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

// 最大帧载荷长度（256MB，覆盖 8MB chunk + JSON 元数据）
inline constexpr quint32 kMaxPayloadBytes = 256 * 1024 * 1024;

// 协议版本（用于 Hello 帧和传输协商）
inline constexpr quint16 kProtocolVersion = 1;

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

// ---- 错误码 ---------------------------------------------------------------
enum class ErrorCode : quint16 {
    Success             = 0,    // 成功
    ConnectionTimeout   = 1001, // 连接超时
    ConnectionLost      = 1002, // 连接断开
    TransferTimeout     = 1003, // 传输超时
    InvalidFrame        = 2001, // 无效帧格式
    FrameTooLarge       = 2002, // 帧载荷超限
    InvalidPayload      = 2003, // 无效载荷内容
    ProtocolMismatch    = 3001, // 协议版本不匹配
    InvalidFileName     = 3002, // 无效文件名称
    InvalidFilePath     = 3003, // 无效文件路径
    FileListMismatch    = 3004, // 文件列表数量不一致
    DiskWriteFailed     = 4001, // 磁盘写入失败
    DiskSpaceInsufficient = 4002, // 磁盘空间不足
    Sha256Mismatch      = 4003, // SHA-256 校验失败
    UserRejected        = 5001, // 用户拒绝
    UserCancelled       = 5002, // 用户取消
    UnknownError        = 9999, // 未知错误
};

}  // namespace gy::protocol
