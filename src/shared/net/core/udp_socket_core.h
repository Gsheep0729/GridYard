/**
* @file    udp_socket_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 POSIX UDP 套接字核心——回调驱动，不依赖 Qt 事件循环
*
* 面向无连接数据报收发：绑定端口后由 NetReactor 驱动可读事件，
* 每个到达的数据报连同发送方地址经回调移交调用方。发送为尽力单播，
* 非阻塞语义（内核缓冲满时返回失败由调用方重试）。
*/

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net_reactor.h"

namespace gy::net {

class UdpSocketCore {
public:
    // 数据报到达回调：data 为完整报文，senderAddress/senderPort 为发送方
    using ReadCallback = std::function<void(const std::vector<uint8_t> &data,
                                            const std::string &senderAddress,
                                            uint16_t senderPort)>;

    explicit UdpSocketCore(NetReactor &reactor);
    ~UdpSocketCore();

    UdpSocketCore(const UdpSocketCore &)            = delete;
    UdpSocketCore &operator=(const UdpSocketCore &) = delete;

    // 绑定端口（port 为 0 时由内核分配）；reuseAddress 便于快速重启复用端口
    bool bind(uint16_t port, bool reuseAddress);
    // 发送单播数据报到指定 IPv4/IPv6 地址
    bool sendTo(const std::string &address, uint16_t port, const uint8_t *data, size_t length);
    // 关闭套接字并停止监听
    void close();
    // 本地绑定端口（0 表示未绑定）
    uint16_t localPort() const;
    // 最近一次失败原因
    const std::string &errorString() const { return _errorText; }

    void setReadCallback(ReadCallback cb) { _onRead = std::move(cb); }

private:
    // 可读事件：循环收取数据报直至 EAGAIN
    void handleReadable();

    NetReactor &_reactor;       // 所属事件泵（不拥有）
    int _fd = -1;               // 套接字描述符
    uint64_t _generation = 0;   // Reactor 监听代际（每次绑定递增）
    bool _watchActive = false;  // Reactor 中是否存在监听条目
    uint16_t _port = 0;         // 本地端口
    std::string _errorText;     // 最近一次失败原因
    ReadCallback _onRead;       // 数据报回调
};

}  // namespace gy::net
