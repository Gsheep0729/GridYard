/**
* @file    tcp_server_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 POSIX TCP 监听服务器——accept 回调驱动，不依赖 Qt 事件循环
*
* 监听描述符由 NetReactor 事件泵驱动，接入连接以原生描述符形式经回调
* 移交给调用方（由调用方包装为 TcpSocketCore 接管）。双栈模式显式关闭
* IPV6_V6ONLY，内核不支持时回退为 IPv4 仅栈。
*/

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net_reactor.h"

namespace gy::net {

class TcpServerCore {
public:
    // 接入回调：descriptor 为已连接非阻塞描述符，所有权移交调用方
    using ConnectionCallback = std::function<void(int descriptor, const std::string &peerAddress)>;

    explicit TcpServerCore(NetReactor &reactor);
    ~TcpServerCore();

    TcpServerCore(const TcpServerCore &)            = delete;
    TcpServerCore &operator=(const TcpServerCore &) = delete;

    // 绑定任意地址监听：dualStack 为真时 IPv6 双栈（内核不支持自动回退 IPv4）
    bool listen(uint16_t port, bool dualStack);
    // 停止监听并关闭描述符（已移交的接入连接不受影响）
    void close();
    // 是否正在监听
    bool isListening() const { return _fd >= 0; }
    // 实际监听端口（端口 0 由内核分配后可查）
    uint16_t serverPort() const;
    // 最近一次监听失败原因
    const std::string &errorString() const { return _errorText; }

    void setConnectionCallback(ConnectionCallback cb) { _onConnection = std::move(cb); }

private:
    // 监听描述符可读（接入到达）：循环 accept 直到 EAGAIN
    void handleAcceptable();
    // 创建监听描述符（按指定地址族绑定）
    bool createListener(int family, uint16_t port);
    // 摘除监听并关闭描述符
    void closeDescriptor();

    NetReactor &_reactor;        // 所属事件泵（不拥有）
    int _fd = -1;                // 监听描述符
    uint64_t _generation = 0;    // Reactor 监听代际（每次监听递增）
    bool _watchActive = false;   // Reactor 中是否存在监听条目
    uint16_t _port = 0;          // 实际监听端口
    std::string _errorText;      // 最近一次失败原因
    ConnectionCallback _onConnection;  // 接入回调
};

}  // namespace gy::net
