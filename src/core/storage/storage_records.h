/**
* @file    storage_records.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 本地历史领域记录——不依赖任何 Qt 类型
*
* 与 client/domain/history_records.h 的 Qt 版一一对应：QString 换
* std::string（UTF-8）、QDateTime 换 Unix 毫秒时间戳，可选时间用
* std::optional 表达"无效时间"。字段集合与语义完全一致，Phase 4
* 桥接层负责两种记录的机械互转。
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace gy::storage {

// 收发方向：数值与数据库 direction 列的取值绑定
enum class RecordDirection : int {
    Incoming = 0,  // 接收自对端的记录
    Outgoing = 1,  // 发送给对端的记录
};

// 传输完成状态常量：取值与 gy::protocol::kTransferStatusCompleted 一致，
// 核心层不引入协议头，改值时两处必须同步
inline constexpr char kTransferStatusCompleted[] = "completed";

// 设备目录记录
struct PeerRecord {
    std::string deviceId;         // 对端稳定设备标识
    std::string deviceName;       // 最近一次发现的设备名称
    std::string lastIpAddress;    // 最近一次发现的网络地址
    uint16_t lastTcpPort = 0;     // 最近一次发现的 TCP 端口
    int64_t firstSeenAtMs = 0;    // 首次写入设备目录的 UTC 毫秒时间戳
    int64_t lastSeenAtMs = 0;     // 最近一次发现设备的 UTC 毫秒时间戳
    std::optional<int64_t> lastChatAtMs;     // 最近一次聊天活动时间，未发生为空
    std::optional<int64_t> lastTransferAtMs; // 最近一次传输活动时间，未发生为空
    std::string alias;            // 用户设置的本地备注，空表示未设置
    bool pinned = false;          // 用户置顶标记
    bool hidden = false;          // 用户隐藏标记（不显示该聊天）
    bool favorite = false;        // 用户收藏标记（关系语义，段内排序优先）
};

// 聊天消息记录
struct MessageRecord {
    std::string messageId;        // 消息 UUID，也是幂等写入键
    std::string peerDeviceId;     // 对端稳定设备标识
    RecordDirection direction = RecordDirection::Incoming;  // 收发方向
    std::string senderDeviceId;   // 实际发送方设备标识
    std::string senderName;       // 发送时的设备名称快照
    std::string content;          // 文本消息内容
    int64_t sentAtMs = 0;         // 协议消息时间
    int localStatus = 0;          // 本地发送状态快照
    int64_t createdAtMs = 0;      // 写入本地历史的 UTC 毫秒时间戳
};

// 聊天消息分页游标
struct MessageCursor {
    std::string peerDeviceId;             // 当前分页所属会话
    std::optional<int64_t> beforeSentAtMs; // 下一页仅查询早于该时间的消息
    std::string beforeMessageId;          // 同一时间下保证稳定排序的消息标识
};

// 传输历史记录
struct TransferRecord {
    std::string recordId;         // 历史记录 UUID
    std::string sessionId;        // 传输会话 UUID
    std::string peerDeviceId;     // 对端稳定设备标识
    std::string peerName;         // 传输结束时的对端名称快照
    RecordDirection direction = RecordDirection::Incoming;  // 收发方向
    std::string displayName;      // 文件或目录的展示名称
    bool isDirectory = false;     // 是否为目录传输
    int fileCount = 0;            // 文件总数
    int64_t totalBytes = 0;       // 内容总字节数
    std::string status;           // 完成、失败、拒绝或取消状态
    int64_t startedAtMs = 0;      // 会话开始 UTC 毫秒时间戳
    std::optional<int64_t> finishedAtMs; // 会话结束时间，未结束为空
    int errorCode = 0;            // 最终失败错误码
    std::string errorMessage;     // 可展示的错误说明
};

// 传输历史筛选条件
struct TransferQuery {
    std::string peerDeviceId;              // 可选的设备筛选条件，空表示不过滤
    std::string status;                    // 可选的最终状态筛选条件，空表示不过滤
    std::optional<int64_t> beforeStartedAtMs; // 下一页仅查询早于该时间的记录
    std::string beforeRecordId;            // 同一时间下的次键游标，与排序键对齐
};

}  // namespace gy::storage
