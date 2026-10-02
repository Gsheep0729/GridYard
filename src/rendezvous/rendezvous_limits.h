/**
* @file    rendezvous_limits.h
* @version 7.12.0
* @date    2026-10-02
* @author  GridYard Team
* @brief   协调节点与中继服务的资源上限常量
*
* 集中定义分帧行长度、超时、连接/会话数量与转发队列等上限，
* 服务端据此抵御慢连接与恶意流量，测试可按需调小阈值。
*
* Change Log:
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

}
