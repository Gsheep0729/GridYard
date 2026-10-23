/**
* @file    udp_socket_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 POSIX UDP 套接字核心实现
*
* recvfrom 循环收取完整数据报（UDP 无部分读，单报文一次取尽），
* sendto 尽力发送，内核缓冲满（ENOBUFS/EAGAIN）时返回 false 交由
* 调用方决定重试节奏。
*/

#include "core/udp_socket_core.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace gy::net {

namespace {

constexpr size_t kMaxDatagramBytes = 64 * 1024;  // 单个数据报的收取上限

// 把 IPv4/IPv6 地址文本解析为套接字地址，失败返回 false
bool resolveAddress(const std::string &address, uint16_t port, sockaddr_storage *out,
                    socklen_t *outLength)
{
    if (address.empty() || !out) {
        return false;
    }
    sockaddr_in ipv4{};
    if (::inet_pton(AF_INET, address.c_str(), &ipv4.sin_addr) == 1) {
        ipv4.sin_family = AF_INET;
        ipv4.sin_port = htons(port);
        std::memcpy(out, &ipv4, sizeof(ipv4));
        *outLength = sizeof(ipv4);
        return true;
    }
    sockaddr_in6 ipv6{};
    if (::inet_pton(AF_INET6, address.c_str(), &ipv6.sin6_addr) == 1) {
        ipv6.sin6_family = AF_INET6;
        ipv6.sin6_port = htons(port);
        std::memcpy(out, &ipv6, sizeof(ipv6));
        *outLength = sizeof(ipv6);
        return true;
    }
    return false;
}

// 套接字地址 → 文本（IPv4 映射地址归一化为点分十进制）
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
UdpSocketCore::UdpSocketCore(NetReactor &reactor)
    : _reactor{reactor}
{
}

// 析构函数：关闭套接字并停止监听
UdpSocketCore::~UdpSocketCore()
{
    close();
}

// 绑定端口
bool UdpSocketCore::bind(uint16_t port, bool reuseAddress)
{
    close();
    _errorText.clear();

    // 双栈 UDP：IPv6 套接字关 V6ONLY 可同时收 IPv4，内核不支持则回退
    const int family = AF_INET6;
    const int fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        _errorText = std::string("socket 创建失败: ") + std::strerror(errno);
        return false;
    }

    if (reuseAddress) {
        const int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }
    const int v6only = 0;
    const bool dualStackOk = ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) == 0;

    sockaddr_storage address{};
    socklen_t addressLength = 0;
    bool bound = false;
    if (dualStackOk) {
        auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&address);
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_addr = in6addr_any;
        ipv6->sin6_port = htons(port);
        addressLength = sizeof(sockaddr_in6);
        bound = ::bind(fd, reinterpret_cast<const sockaddr *>(&address), addressLength) == 0;
    }
    if (!bound) {
        // IPv6 绑定不可用时回退 IPv4 仅栈
        auto *ipv4 = reinterpret_cast<sockaddr_in *>(&address);
        ipv4->sin_family = AF_INET;
        ipv4->sin_addr.s_addr = htonl(INADDR_ANY);
        ipv4->sin_port = htons(port);
        addressLength = sizeof(sockaddr_in);
        bound = ::bind(fd, reinterpret_cast<const sockaddr *>(&address), addressLength) == 0;
    }
    if (!bound) {
        _errorText = std::string("bind 失败: ") + std::strerror(errno);
        ::close(fd);
        return false;
    }

    _fd = fd;
    ++_generation;
    _watchActive = true;

    sockaddr_storage boundTo{};
    socklen_t boundLength = sizeof(boundTo);
    if (::getsockname(_fd, reinterpret_cast<sockaddr *>(&boundTo), &boundLength) == 0) {
        if (boundTo.ss_family == AF_INET) {
            _port = ntohs(reinterpret_cast<const sockaddr_in *>(&boundTo)->sin_port);
        } else if (boundTo.ss_family == AF_INET6) {
            _port = ntohs(reinterpret_cast<const sockaddr_in6 *>(&boundTo)->sin6_port);
        }
    }

    const int watchedFd = _fd;
    const uint64_t generation = _generation;
    _reactor.watch(watchedFd, static_cast<uint32_t>(Event::kReadable), generation,
                   [this, watchedFd](int fd, uint32_t events) {
                       if (fd != watchedFd) {
                           return;
                       }
                       handleReadable();
                   });
    return true;
}

// 发送单播数据报
bool UdpSocketCore::sendTo(const std::string &address, uint16_t port, const uint8_t *data,
                           size_t length)
{
    if (_fd < 0 || !data || length == 0) {
        return false;
    }
    sockaddr_storage target{};
    socklen_t targetLength = 0;
    if (!resolveAddress(address, port, &target, &targetLength)) {
        _errorText = "无法解析目标地址: " + address;
        return false;
    }
    const ssize_t sent = ::sendto(_fd, data, length, MSG_NOSIGNAL,
                                  reinterpret_cast<const sockaddr *>(&target), targetLength);
    if (sent < 0 || static_cast<size_t>(sent) != length) {
        _errorText = std::string("sendto 失败: ") + std::strerror(errno);
        return false;
    }
    return true;
}

// 关闭套接字
void UdpSocketCore::close()
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

// 本地端口
uint16_t UdpSocketCore::localPort() const
{
    return _port;
}

// 收取数据报
void UdpSocketCore::handleReadable()
{
    if (_fd < 0) {
        return;
    }
    while (true) {
        std::vector<uint8_t> data(kMaxDatagramBytes);
        sockaddr_storage peer{};
        socklen_t peerLength = sizeof(peer);
        const ssize_t amount = ::recvfrom(_fd, data.data(), data.size(), 0,
                                          reinterpret_cast<sockaddr *>(&peer), &peerLength);
        if (amount < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;  // EAGAIN：本轮已取尽
        }
        if (amount == 0) {
            continue;  // 空数据报（合法）继续收取
        }
        data.resize(static_cast<size_t>(amount));

        uint16_t senderPort = 0;
        if (peer.ss_family == AF_INET) {
            senderPort = ntohs(reinterpret_cast<const sockaddr_in *>(&peer)->sin_port);
        } else if (peer.ss_family == AF_INET6) {
            senderPort = ntohs(reinterpret_cast<const sockaddr_in6 *>(&peer)->sin6_port);
        }
        ReadCallback onRead = _onRead;
        if (onRead) {
            onRead(data, formatAddress(reinterpret_cast<const sockaddr *>(&peer), peerLength),
                   senderPort);
        }
    }
}

}  // namespace gy::net
