/**
* @file    tcp_server_core.cpp
* @version 8.1.0
* @date    2026-10-27
* @author  GridYard Team
* @brief   纯 POSIX TCP 监听服务器实现（Boost.Asio 引擎）
*
* 监听描述符仍以原生系统调用创建（bind/listen 错误文案与 errno 语义保持
* 原样），随后交由 acceptor 接管并逐只挂接异步 accept：每交付一个接入
* 描述符即续挂下一只，接入节奏与旧 accept 循环一致。无人接管的连接立即
* 回收，杜绝无主连接悬挂。
*/

#include "core/tcp_server_core.h"

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/post.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <latch>
#include <utility>

namespace gy::net {

namespace {

// 把套接字地址格式化为文本（IPv4 映射地址归一化为点分十进制）
std::string formatAddress(const sockaddr *address, socklen_t length)
{
    if (!address || length < sizeof(sockaddr)) {
        return {};
    }
    char host[INET6_ADDRSTRLEN] = {0};
    if (address->sa_family == AF_INET) {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
        ::inet_ntop(AF_INET, &ipv4->sin_addr, host, sizeof(host));
    } else if (address->sa_family == AF_INET6) {
        const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(address);
        ::inet_ntop(AF_INET6, &ipv6->sin6_addr, host, sizeof(host));
    } else {
        return {};
    }
    std::string text = host;
    constexpr const char *kV4MappedPrefix = "::ffff:";
    if (text.starts_with(kV4MappedPrefix)) {
        const std::string dotted = text.substr(std::strlen(kV4MappedPrefix));
        if (dotted.find(':') == std::string::npos) {
            return dotted;
        }
    }
    return text;
}

}  // namespace

// 构造函数
TcpServerCore::TcpServerCore(NetReactor &reactor)
    : _reactor{reactor}
    , _acceptor{reactor.engineContext()}
    , _strand{reactor.engineContext().get_executor()}
    , _alive{std::make_shared<std::atomic_bool>(true)}
    , _peer{reactor.engineContext()}
{
}

// 析构函数：停止监听并回收描述符
TcpServerCore::~TcpServerCore()
{
    _alive->store(false);
    close();
}

// 绑定监听（双栈优先，失败回退 IPv4）
bool TcpServerCore::listen(uint16_t port, bool dualStack)
{
    close();
    _errorText.clear();

    if (dualStack) {
        if (createListener(AF_INET6, port)) {
            return armAccept();
        }
        // 内核或环境不支持 IPv6 双栈时回退 IPv4 仅栈
    }
    if (!createListener(AF_INET, port)) {
        return false;
    }
    return armAccept();
}

// 创建监听描述符
bool TcpServerCore::createListener(int family, uint16_t port)
{
    const int fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        _errorText = std::string("socket 创建失败: ") + std::strerror(errno);
        return false;
    }

    const int reuse = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (family == AF_INET6) {
        // 双栈监听必须显式关闭 V6ONLY，IPv4 客户端经映射地址接入
        const int v6only = 0;
        ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

    sockaddr_storage address{};
    socklen_t addressLength = 0;
    if (family == AF_INET6) {
        auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&address);
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_addr = in6addr_any;
        ipv6->sin6_port = htons(port);
        addressLength = sizeof(sockaddr_in6);
    } else {
        auto *ipv4 = reinterpret_cast<sockaddr_in *>(&address);
        ipv4->sin_family = AF_INET;
        ipv4->sin_addr.s_addr = htonl(INADDR_ANY);
        ipv4->sin_port = htons(port);
        addressLength = sizeof(sockaddr_in);
    }

    if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), addressLength) != 0) {
        _errorText = std::string("bind 失败: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }
    if (::listen(fd, 128) != 0) {
        _errorText = std::string("listen 失败: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }

    _fd = fd;
    _family = family;
    ++_generation;

    // 读取内核分配的实际端口（端口 0 场景）
    sockaddr_storage bound{};
    socklen_t boundLength = sizeof(bound);
    if (::getsockname(_fd, reinterpret_cast<sockaddr *>(&bound), &boundLength) == 0) {
        if (bound.ss_family == AF_INET) {
            _port = ntohs(reinterpret_cast<const sockaddr_in *>(&bound)->sin_port);
        } else if (bound.ss_family == AF_INET6) {
            _port = ntohs(reinterpret_cast<const sockaddr_in6 *>(&bound)->sin6_port);
        }
    }
    return true;
}

// 挂接引擎接入事件：外线程调用时经投递屏障执行
bool TcpServerCore::armAccept()
{
    if (_fd < 0) {
        return false;
    }
    if (_reactor.onReactorThread() || !_reactor.isRunning()) {
        return armAcceptOnEngine();
    }
    std::latch done(1);
    std::atomic_bool ok{false};
    boost::asio::post(_strand, [this, &done, &ok] {
        ok.store(armAcceptOnEngine());
        done.count_down();
    });
    done.wait();
    return ok.load();
}

// 引擎线程内的接入挂接主体
bool TcpServerCore::armAcceptOnEngine()
{
    boost::system::error_code ec;
    _acceptor.assign(_family == AF_INET6 ? boost::asio::ip::tcp::v6()
                                         : boost::asio::ip::tcp::v4(),
                     _fd, ec);
    if (ec) {
        _errorText = std::string("监听描述符接管失败: ") + ec.message();
        ::close(_fd);
        _fd = -1;
        _port = 0;
        return false;
    }
    armAcceptLoop();
    return true;
}

// 挂下一只异步 accept
void TcpServerCore::armAcceptLoop()
{
    if (_fd < 0 || _acceptArmed) {
        return;
    }
    _acceptArmed = true;
    auto alive = _alive;
    _acceptor.async_accept(_peer,
                           boost::asio::bind_executor(_strand, [this, alive](
                               const boost::system::error_code &ec) {
                               if (ec == boost::asio::error::operation_aborted || !alive->load()) {
                                   return;  // 取消路径零成员访问；迟到完成安全丢弃
                               }
                               _acceptArmed = false;
                               onAcceptCompleted(ec);
                           }));
}

// 异步 accept 完成入口
void TcpServerCore::onAcceptCompleted(const boost::system::error_code &ec)
{
    if (ec == boost::asio::error::operation_aborted) {
        return;  // 监听已关闭
    }
    if (!ec) {
        // 描述符所有权移交调用方：从引擎套接字剥离，保持 fd 交割契约
        const int client = static_cast<int>(_peer.release());
        sockaddr_storage peer{};
        socklen_t peerLength = sizeof(peer);
        const std::string peerAddress = ::getpeername(client, reinterpret_cast<sockaddr *>(&peer),
                                                      &peerLength) == 0
            ? formatAddress(reinterpret_cast<const sockaddr *>(&peer), peerLength)
            : std::string();
        ConnectionCallback onConnection = _onConnection;
        if (!onConnection) {
            ::close(client);  // 无人接管：立即回收，避免无主连接悬挂
        } else {
            onConnection(client, peerAddress);
        }
    } else if (_fd >= 0 && _errorText.empty()) {
        _errorText = std::string("accept 失败: ") + ec.message();
    }
    // 其余错误等下一轮就绪重试（与旧 accept 循环的容错一致）
    if (_fd >= 0) {
        armAcceptLoop();
    }
}

// 停止监听
void TcpServerCore::close()
{
    if (_fd < 0) {
        return;
    }
    if (_reactor.onReactorThread() || !_reactor.isRunning()) {
        closeDescriptor();
        return;
    }
    // 外线程调用：投递到 strand 关闭并等待生效，返回后不再有本监听的
    // 派发在途（对齐旧注销屏障语义；析构路径同构安全）
    std::latch done(1);
    boost::asio::post(_strand, [this, &done] {
        closeDescriptor();
        done.count_down();
    });
    done.wait();
}

// 实际监听端口
uint16_t TcpServerCore::serverPort() const
{
    return _port;
}

// 摘除监听并关闭描述符（仅引擎线程内执行）
void TcpServerCore::closeDescriptor()
{
    if (_fd < 0) {
        return;
    }
    _fd = -1;
    _port = 0;
    _acceptArmed = false;
    // acceptor 关闭即撤销引擎注册并以取消落地在途 accept
    boost::system::error_code ec;
    _acceptor.close(ec);
}

}  // namespace gy::net
