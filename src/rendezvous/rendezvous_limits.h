/**
* @file    rendezvous_limits.h
* @version 7.15.11
* @date    2026-10-03
* @author  GridYard Team
* @brief   协调节点与中继服务的资源上限常量
*
* 集中定义分帧行长度、超时、连接/会话数量与转发队列等上限，
* 服务端据此抵御慢连接与恶意流量，测试可按需调小阈值。
*
* Change Log:
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.13.4] GY   2026-10-03
* * 新增 TTL 夹紧区间、房间/设备数上限、响应写队列上限与会话等待/清理周期常量
* [v7.12.0] GY   2026-10-02
* * Stage P2-3：集中定义服务端加固所需的上限常量
*/

#pragma once

#include <QtGlobal>

namespace gy::rendezvous {

// 协调控制行长度上限（register 的地址列表远小于该值）
inline constexpr qsizetype kMaxRendezvousLineBytes = 64 * 1024;
// 中继握手行长度上限
inline constexpr qsizetype kMaxRelayHelloBytes = 4 * 1024;
// socket 内部读缓冲上限：协调会话按行的两倍，中继转发按 4MB 分块
inline constexpr qsizetype kRendezvousReadBufferBytes = kMaxRendezvousLineBytes * 2;
inline constexpr qsizetype kRelayPipeReadBufferBytes = 4 * 1024 * 1024;

// 协调会话超时：首行握手与空闲（客户端 5 秒心跳，空闲 90 秒视为死链）
inline constexpr int kRendezvousHandshakeTimeoutMs = 15000;
inline constexpr int kRendezvousIdleTimeoutMs = 90000;
// 中继握手行超时
inline constexpr int kRelayHelloTimeoutMs = 5000;

// 连接与会话数量上限
inline constexpr int kMaxRendezvousSessions = 256;
inline constexpr int kMaxRelaySessions = 128;

// 中继转发背压：对端写队列超过该值时暂停读取源端，等待排空后继续
inline constexpr qint64 kMaxRelayPeerWriteQueue = 8 * 1024 * 1024;
// 中继积压上限：会话未齐备前缓存的字节数，超限视为异常客户端
inline constexpr qsizetype kMaxRelayBacklogBytes = 1024 * 1024;

// 设备自报 TTL 的夹紧区间：下限防零值回退默认，上限防"永不过期"注入
inline constexpr int kMinPeerTtlSeconds = 1;
inline constexpr int kMaxPeerTtlSeconds = 300;

// 房间数与每房设备数上限：注册无认证时的内存防注入约束
inline constexpr int kMaxRegistryRooms = 256;
inline constexpr int kMaxDevicesPerRoom = 128;

// 协调响应写队列上限：客户端消费过慢时直接断开，防止单连接拖垮服务端内存
inline constexpr qint64 kMaxResponseWriteQueueBytes = 1024 * 1024;

// 中继会话等待对端加入的超时
inline constexpr int kRelaySessionWaitMs = 60000;
// 协调节点过期数据清理周期
inline constexpr int kRegistryPruneIntervalMs = 10000;
// 中继邀请有效期：接收端按心跳周期轮询，60 秒足够覆盖短暂离线
inline constexpr int kRelayInviteTtlSeconds = 60;

}
