/**
* @file    protocol.h
* @version 6.6.2
* @date    2026-06-23
* @author  GridYard Team
* @brief   应用层通信协议定义（TLV 帧格式 + Type 码集合）
*
* 全局唯一的协议常量定义入口。所有 Type 码、帧头字段、Payload 字段名
* 常量都集中在 gy::protocol 命名空间下，避免散落到各模块复制粘贴。
* TLV 帧格式见《V1.0 架构设计说明书》§7：8 字节帧头（uint32 Type +
* uint32 Length 大端序）+ Length 字节载荷。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.3] GY   2026-06-23
* * 新增 P2P 在线聊天 Type 码和文本消息协议字段常量
* [v4.15.0] FengChunlin   2026-06-16
* * 添加按 Type 分级的 Payload 上限（控制帧 1MB，DataChunk 256MB）
* * 添加协议版本主/次版本号常量和提取函数
* * 添加 maxPayloadForType() 函数
* [v4.13.0] GY   2026-06-14
* * 添加协议版本常量 kProtocolVersion
* * 添加最大帧载荷长度常量 kMaxPayloadBytes（256MB）
* * 添加稳定错误码枚举 ErrorCode
* [v0.1.0] GY   2026-04-20
* * Stage 1：定义 V1.0 Type 码集合
* [v0.0.1] GY   2026-04-06
* * Stage 0 占位：仅声明命名空间与默认端口
*/

#pragma once

#include <QtGlobal>

namespace gy::protocol {

// 帧头总长度（uint32 Type + uint32 Length）
inline constexpr quint32 kHeaderBytes = 8;

// 最大帧载荷长度（256MB，覆盖 8MB chunk + JSON 元数据）
inline constexpr quint32 kMaxPayloadBytes = 256 * 1024 * 1024;

// 按 Type 分级的 Payload 上限（控制帧不应允许 256MB）
inline constexpr quint32 kMaxControlPayloadBytes = 1 * 1024 * 1024;   // 1MB：控制帧上限
inline constexpr quint32 kMaxDataPayloadBytes     = 256 * 1024 * 1024; // 256MB：数据帧上限

// 协议版本（用于 Hello 帧和传输协商）
// 高 8 位为主版本号，低 8 位为次版本号
inline constexpr quint16 kProtocolVersion = 0x0100;  // v1.0
inline constexpr quint8  kProtocolMajorVersion = 1;   // 主版本号
inline constexpr quint8  kProtocolMinorVersion = 0;   // 次版本号

// 从完整版本号提取主/次版本号
inline constexpr quint8 majorVersion(quint16 version) { return static_cast<quint8>(version >> 8); }
inline constexpr quint8 minorVersion(quint16 version) { return static_cast<quint8>(version & 0xFF); }

// 默认网络端口
inline constexpr quint16 kDefaultDiscoveryPort = 45678;   // UDP 设备发现
inline constexpr quint16 kDefaultP2pPort       = 35100;   // TCP P2P 文件传输
// 协调服务器默认端口
inline constexpr quint16 kDefaultRendezvousPort = 45679;   // 协调节点（默认）
inline constexpr quint16 kDefaultRelayPort      = 45679;   // 中继服务器（默认，与协调节点同端口不同进程）

// ---- V1.0 Type 码 --------------------------------------------------------
inline constexpr quint32 kTypeHello        = 0x0001;   // UDP 广播：设备上线 / 心跳
inline constexpr quint32 kTypeTransferReq  = 0x0101;   // TCP：文件元数据握手请求
inline constexpr quint32 kTypeTransferRsp  = 0x0102;   // TCP：握手响应（接受/拒绝）
inline constexpr quint32 kTypeDataChunk    = 0x0201;   // TCP：文件数据分块
inline constexpr quint32 kTypeChunkAck     = 0x0301;   // TCP：单文件完成确认与校验
inline constexpr quint32 kTypeTransferDone = 0x0302;   // TCP：全部文件发送完毕
inline constexpr quint32 kTypeCancel       = 0x0401;   // TCP：取消本次传输
inline constexpr quint32 kTypeChatText     = 0x0501;   // TCP：P2P 在线文本消息
inline constexpr quint32 kTypeChatAck      = 0x0502;   // TCP：在线文本消息送达回执

// 聊天消息 JSON 字段
inline constexpr char kChatMessageIdField[]      = "message_id";
inline constexpr char kChatFromDeviceIdField[]   = "from_device_id";
inline constexpr char kChatFromNameField[]       = "from_name";
inline constexpr char kChatContentField[]        = "content";
inline constexpr char kChatSentAtField[]         = "sent_at";

// 聊天消息业务上限，低于通用控制帧限制，避免单条文本占用过多内存
inline constexpr qsizetype kMaxChatPayloadBytes = 64 * 1024;
inline constexpr qsizetype kMaxChatContentChars = 4000;

// 获取指定 Type 的最大 Payload 长度
inline constexpr quint32 maxPayloadForType(quint32 type)
{
    // 只有 kTypeDataChunk 允许大 payload，其他帧限制为 1MB
    return (type == kTypeDataChunk) ? kMaxDataPayloadBytes : kMaxControlPayloadBytes;
}

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
