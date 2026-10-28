/**
* @file    tcp_server_core.h
* @version 8.2.0
* @date    2026-10-29
* @author  GridYard Team
* @brief   纯 POSIX TCP 监听服务器——accept 回调驱动，不依赖 Qt 事件循环
*
* 监听描述符经 Boost.Asio acceptor 挂接所属 NetReactor 的事件引擎，接入
* 连接以原生描述符形式经回调移交给调用方（由调用方包装为 TcpSocketCore
* 接管）；关闭与取消一律投递到本服务的 strand 串行执行。双栈模式显式
* 关闭 IPV6_V6ONLY，内核不支持时回退为 IPv4 仅栈。
*/

#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "net_reactor.h"

namespace gy::net {

class TcpServerCore {
public:
    // 接入回调：descriptor 为已连接非阻塞描述符，所有权移交调用方
    using ConnectionCallback = std::function<void(int descriptor, const std::string &peerAddress)>;

    // 构造监听服务器：在所属事件泵的引擎上创建接入器与串行化执行域
    explicit TcpServerCore(NetReactor &reactor);
    // 析构：停止监听并关闭描述符（已移交的接入连接不受影响）
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

    // 设置接入回调（每接入一条连接触发一次）
    void setConnectionCallback(ConnectionCallback cb) { _onConnection = std::move(cb); }

private:
    // 挂接引擎接入事件：外线程调用时经投递屏障执行
    bool armAccept();
    // 引擎线程内的接入挂接主体：描述符交由 acceptor 驱动并挂首只异步 accept
    bool armAcceptOnEngine();
    // 挂下一只异步 accept（仅引擎线程调用）
    void armAcceptLoop();
    // 异步 accept 完成入口
    void onAcceptCompleted(const boost::system::error_code &ec);
    // 创建监听描述符（按指定地址族绑定）
    bool createListener(int family, uint16_t port);
    // 摘除监听并关闭描述符（仅引擎线程内执行）
    void closeDescriptor();

    NetReactor &_reactor;        // 所属事件泵（不拥有）
    boost::asio::ip::tcp::acceptor _acceptor;  // 引擎侧监听器（接管监听描述符）
    boost::asio::strand<boost::asio::io_context::executor_type> _strand;  // 本服务的串行化执行域
    std::shared_ptr<std::atomic_bool> _alive;  // 存活哨兵：完成回调据此识别已析构对象
    boost::asio::ip::tcp::socket _peer;  // 异步 accept 的接入套接字（逐个移交）
    int _fd = -1;                // 监听描述符
    int _family = 0;             // 监听地址族（acceptor 接管时对齐协议）
    uint64_t _generation = 0;    // 监听代际（每次监听递增，语义保留）
    bool _acceptArmed = false;   // 是否挂有异步 accept
    uint16_t _port = 0;          // 实际监听端口
    std::string _errorText;      // 最近一次失败原因
    ConnectionCallback _onConnection;  // 接入回调
};

}  // namespace gy::net
