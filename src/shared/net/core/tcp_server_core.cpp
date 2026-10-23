/**
* @file    tcp_server_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 POSIX TCP 监听服务器实现
*
* accept 循环用 accept4 直接产出非阻塞描述符；未设置接入回调时立即
* 关闭新描述符，杜绝无主连接悬挂。双栈优先 IPv6（关 V6ONLY），失败
* 回退 IPv4，与旧 Qt 桥接层监听行为一致。
*/

#include "core/tcp_server_core.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
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
{
}

// 析构函数：停止监听并回收描述符
TcpServerCore::~TcpServerCore()
{
    close();
}

// 绑定监听（双栈优先，失败回退 IPv4）
bool TcpServerCore::listen(uint16_t port, bool dualStack)
{
    close();
    _errorText.clear();

    if (dualStack) {
        if (createListener(AF_INET6, port)) {
            return true;
        }
        // 内核或环境不支持 IPv6 双栈时回退 IPv4 仅栈
    }
    if (!createListener(AF_INET, port)) {
        return false;
    }
    return true;
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
    ++_generation;
    _watchActive = true;

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

    const int watchedFd = _fd;
    const uint64_t generation = _generation;
    _reactor.watch(watchedFd, static_cast<uint32_t>(Event::kReadable), generation,
                   [this, watchedFd](int fd, uint32_t events) {
                       if (fd != watchedFd) {
                           return;
                       }
                       handleAcceptable();
                   });
    return true;
}

// 停止监听
void TcpServerCore::close()
{
    closeDescriptor();
}

// 实际监听端口
uint16_t TcpServerCore::serverPort() const
{
    return _port;
}

// 处理接入：循环 accept 直到 EAGAIN
void TcpServerCore::handleAcceptable()
{
    while (_fd >= 0) {
        sockaddr_storage peer{};
        socklen_t peerLength = sizeof(peer);
        const int client = ::accept4(_fd, reinterpret_cast<sockaddr *>(&peer), &peerLength,
                                     SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;  // EAGAIN：本轮接入已取尽；其余错误等下轮可读重试
        }

        const std::string peerAddress = formatAddress(
            reinterpret_cast<const sockaddr *>(&peer), peerLength);
        ConnectionCallback onConnection = _onConnection;
        if (!onConnection) {
            ::close(client);  // 无人接管：立即回收，避免无主连接悬挂
            continue;
        }
        onConnection(client, peerAddress);
    }
}

// 摘除监听并关闭描述符
void TcpServerCore::closeDescriptor()
{
    if (_fd < 0) {
        return;
    }
    if (_watchActive) {
        _reactor.unwatchNow(_fd, _generation);
        _watchActive = false;
    }
    ::close(_fd);
    _fd = -1;
    _port = 0;
}

}  // namespace gy::net
