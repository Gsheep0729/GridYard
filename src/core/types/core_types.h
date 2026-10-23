/**
* @file    core_types.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 核心数据类型——禁止引入任何 Qt 头文件
*
* 本文件定义核心层（gy_core 目标）内部使用的值类型，字段与
* src/shared/data_types.h 及 src/shared/chat_message.h 中的 Qt 版本一一对应，
* 便于后续 Phase 平移；与 Qt 类型的双向转换由桥接层负责。
* 字符串统一使用 std::string，除另有说明外均为 UTF-8 编码。
*/

#pragma once

#include <cstdint>
#include <string>

namespace gy::core {

// 已发现的对端设备信息（对应 shared/data_types.h 的 PeerInfo）
struct PeerInfo {
    std::string device_id;        // 设备唯一标识（UUID，小写无花括号）
    std::string device_name;      // 用户自定义名或 hostname
    std::string ip_address;       // 对端最近一次广播来源地址
    uint16_t    tcp_port = 0;     // 对端 TCP 监听端口
    bool        is_online = false;  // 是否处于发现心跳有效期内
    uint16_t    protocol_version = 0;  // 对端协议版本
    int64_t     last_seen_ms = 0;  // 最后心跳时间（Unix 毫秒，UTC）
    std::string source;           // 来源标签：broadcast/directed/rendezvous/manual/history
};

// 文件元数据（对应 shared/data_types.h 的 FileEntry；目录占位条目以
// relative_path 尾部 '/' 标记，与 DirSerializer 既有约定一致）
struct FileEntry {
    std::string relative_path;  // 相对传输根目录的路径，'/' 分隔
    int64_t     file_size = 0;  // 文件字节数，目录占位条目为 0
    std::string sha256;         // 64 字符小写十六进制哈希，空串表示未计算
};

// 传输会话状态（对应 shared/data_types.h 的 TransferSession）
struct TransferSession {
    std::string session_id;      // 会话唯一标识（UUID）
    std::string peer_device_id;  // 对端设备 ID
    int32_t     file_count = 0;  // 会话包含的文件数量
    int64_t     total_bytes = 0;  // 会话总字节数
    int64_t     sent_bytes = 0;   // 已发送或已接收字节数
    bool        is_active = false;  // 会话是否仍在传输中
};

// 在线聊天消息（对应 shared/chat_message.h 的 ChatMessage；时间为 Unix 毫秒）
struct ChatMsg {
    std::string message_id;      // 消息唯一标识（UUID）
    std::string from_device_id;  // 发送方设备标识
    std::string from_name;       // 发送方显示名称
    std::string content;         // 消息正文（UTF-8）
    int64_t     sent_at_ms = 0;  // 发送时间（Unix 毫秒，UTC）
};

}  // namespace gy::core
