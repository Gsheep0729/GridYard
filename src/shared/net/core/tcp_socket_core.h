/**
* @file    tcp_socket_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 POSIX TCP 连接核心——回调驱动，不依赖 Qt 事件循环
*
* 面向服务端接入侧（accept 得到的已连接描述符），提供用户态读写缓冲、
* 写队列背压与优雅关闭。所有方法与回调在 Reactor 线程执行/触发
* （单线程归属，无内部锁）；析构会立即关闭描述符，调用方需保证
* 回调链内不再触达已析构对象。
*/

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net_reactor.h"

namespace gy::net {

// 连接终结原因
enum class SocketCloseReason {
    kPeerClosed,  // 对端关闭（EOF）
    kError,       // 套接字错误（读写失败、连接重置）
    kLocalClose,  // 本端主动关闭完成
};

class TcpSocketCore {
public:
    // 用户缓冲有新数据可读（readAll 取走）
    using ReadCallback = std::function<void()>;
    // 写队列头部 bytes 字节已送入内核（对端方向的续传信号）
    using WrittenCallback = std::function<void(int64_t bytes)>;
    // 连接终结（保证至多触发一次），回调返回后对象方可析构
    using CloseCallback = std::function<void(SocketCloseReason reason)>;
    // 套接字错误说明（终结回调之前触发）
    using ErrorCallback = std::function<void(const std::string &message)>;

    explicit TcpSocketCore(NetReactor &reactor);
    ~TcpSocketCore();

    TcpSocketCore(const TcpSocketCore &)            = delete;
    TcpSocketCore &operator=(const TcpSocketCore &) = delete;

    // 接管 accept 得到的已连接描述符（设置非阻塞与 TCP_NODELAY）
    bool adopt(int descriptor);
    // 应用层读缓冲上限（字节）：达到后暂停读监听形成 TCP 背压，
    // 消费后自动恢复；0 表示不限制
    void setReadBufferLimit(int64_t bytes);
    // 用户缓冲未读 + 内核待读字节数
    int64_t bytesAvailable() const;
    // 用户态待写字节数（写队列背压判断依据）
    int64_t bytesToWrite() const;
    // 取走用户缓冲全部数据
    std::vector<uint8_t> readAll();
    // 追加写数据：先直写内核，余量入队由可写事件分批发送
    void write(const uint8_t *data, size_t length);
    void write(const std::vector<uint8_t> &data);
    // 优雅关闭：写队列排空后关闭描述符，终结原因为 kLocalClose
    void disconnectFromHost();
    // 立即关闭并丢弃未写数据（终结原因为 kLocalClose）
    void abort();
    // 描述符是否仍在服务
    bool isConnected() const { return _fd >= 0; }
    // 对端地址文本（IPv4 映射地址归一化为点分十进制）
    std::string peerAddressText() const;
    // 原生描述符（未接管时为 -1）
    int descriptor() const { return _fd; }

    void setReadCallback(ReadCallback cb) { _onRead = std::move(cb); }
    void setWrittenCallback(WrittenCallback cb) { _onWritten = std::move(cb); }
    void setCloseCallback(CloseCallback cb) { _onClose = std::move(cb); }
    void setErrorCallback(ErrorCallback cb) { _onError = std::move(cb); }

private:
    // Reactor 事件入口（读/写/错误统一归口）
    void handleEvents(uint32_t events);
    // 从内核读入用户缓冲并在有新数据时通知
    void pumpRead();
    // 把用户态写队列送入内核，排空后按需走优雅关闭
    void pumpWrite();
    // 按读写意愿刷新 Reactor 监听
    void syncWatch();
    // 统一收尾：注销监听、关闭描述符、通知终结
    void finishClose(SocketCloseReason reason);
    // 用户缓冲未读字节数
    int64_t bufferedBytes() const;

    NetReactor &_reactor;                 // 所属事件泵（不拥有）
    int _fd = -1;                         // 原生描述符
    uint64_t _generation = 0;             // Reactor 监听代际（每次接管递增）
    bool _watchActive = false;            // Reactor 中是否存在本 fd 的监听
    bool _readWanted = false;             // 读监听意愿（受缓冲饱和控制）
    bool _writeWanted = false;            // 写监听意愿（仅写队列非空时）
    int64_t _readBufferLimit = 0;         // 读缓冲上限，0 表示不限
    std::vector<uint8_t> _rxBuf;          // 用户态读缓冲
    size_t _rxOffset = 0;                 // 读缓冲消费偏移
    std::vector<uint8_t> _txBuf;          // 用户态写队列
    size_t _txOffset = 0;                 // 写队列发送偏移
    bool _gracefulClosePending = false;   // 写队列排空后关闭的挂起请求
    bool _inCallback = false;             // 业务回调重入保护

    ReadCallback _onRead;                 // 可读通知
    WrittenCallback _onWritten;           // 写出通知
    CloseCallback _onClose;               // 终结通知
    ErrorCallback _onError;               // 错误通知
};

}  // namespace gy::net
