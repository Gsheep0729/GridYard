/**
* @file    posix_tcp_server.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   原生 TCP 监听器实现
*
* 监听失败时保留失败原因供调用方展示；已接入但尚未取走的连接
* 在退出时逐个关闭，避免描述符泄漏。
*/

#include "posix_tcp_server.h"

#include "posix_net_error.h"
#include "posix_tcp_socket.h"

#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gy::net {

namespace {

constexpr int kDefaultListenBacklog = 50;  // 与既有服务端的监听队列深度一致
constexpr int kMaxAcceptBatch = 64;        // 单轮事件最多接入数，避免连接风暴独占事件循环

}  // namespace

// 构造函数
PosixTcpServer::PosixTcpServer(QObject *parent)
    : QObject{parent}
{
}

// 析构函数
PosixTcpServer::~PosixTcpServer()
{
    close();
}

// 按地址文本绑定并监听
bool PosixTcpServer::listen(const QString &address, quint16 port, ListenMode mode)
{
    const QString trimmed = address.trimmed();
    IpAddress parsed;
    if (trimmed.isEmpty() || trimmed == QStringLiteral("0.0.0.0")
        || trimmed == QStringLiteral("::")) {
        parsed = mode == DualStack ? IpAddress::anyIpv6() : IpAddress::any();
    } else {
        parsed = IpAddress::fromString(trimmed);
    }

    return listen(parsed, port, mode);
}

// 按地址值绑定并监听
bool PosixTcpServer::listen(const IpAddress &address, quint16 port, ListenMode mode)
{
    close();

    if (!createListener(address, port, mode)) {
        return false;
    }

    _notifier = new QSocketNotifier(_fd, QSocketNotifier::Read, this);
    connect(_notifier, &QSocketNotifier::activated, this, [this]() {
        onAcceptActivated();
    });
    _port = queryPort();
    _errorText.clear();
    return true;
}

// 停止监听并关闭待取连接
void PosixTcpServer::close()
{
    if (_notifier) {
        _notifier->setEnabled(false);
        delete _notifier;
        _notifier = nullptr;
    }

    for (const PendingConnection &pending : _pending) {
        if (pending.descriptor >= 0) {
            ::close(pending.descriptor);
        }
    }
    _pending.clear();

    closeDescriptor();
    _port = 0;
}

// 是否正在监听
bool PosixTcpServer::isListening() const
{
    return _fd >= 0;
}

// 实际监听端口
quint16 PosixTcpServer::serverPort() const
{
    return _port;
}

// 最近一次监听失败原因
QString PosixTcpServer::errorString() const
{
    return _errorText;
}

// 是否有待取连接
bool PosixTcpServer::hasPendingConnections() const
{
    return !_pending.isEmpty();
}

// 取出下一个连接
PosixTcpSocket *PosixTcpServer::nextPendingConnection()
{
    if (_pending.isEmpty()) {
        return nullptr;
    }

    const PendingConnection pending = _pending.takeFirst();
    auto *socket = new PosixTcpSocket;
    if (!socket->adoptDescriptor(pending.descriptor)) {
        delete socket;
        return nullptr;
    }
    return socket;
}

// 接受事件处理
void PosixTcpServer::onAcceptActivated()
{
    if (_fd < 0) {
        return;
    }

    bool acceptedAny = false;
    int acceptedCount = 0;
    while (acceptedCount < kMaxAcceptBatch) {
        const int descriptor = ::accept4(_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (descriptor < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            _errorText = errorMessageFromErrno(errno);
            break;
        }

        const int enabled = 1;
        ::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        _pending.append(PendingConnection{descriptor});
        acceptedAny = true;
        ++acceptedCount;
    }

    if (acceptedAny) {
        emit newConnection();
    }
}

// 关闭监听描述符
void PosixTcpServer::closeDescriptor()
{
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

// 创建监听描述符
bool PosixTcpServer::createListener(const IpAddress &address, quint16 port, ListenMode mode)
{
    if (!address.isValid()) {
        _errorText = QStringLiteral("监听地址无效");
        return false;
    }

    const bool wantsDualStack = mode == DualStack;
    const bool addressIsIpv6 = address.family() == AF_INET6;
    if (!wantsDualStack && addressIsIpv6) {
        _errorText = QStringLiteral("仅 IPv4 模式无法监听 IPv6 地址");
        return false;
    }

    int family = wantsDualStack ? AF_INET6 : AF_INET;
    _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (_fd < 0 && wantsDualStack) {
        // 内核未启用 IPv6 时退回 IPv4，保持监听能力可用
        family = AF_INET;
        _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    }
    if (_fd < 0) {
        _errorText = errorMessageFromErrno(errno);
        return false;
    }

    const int reuse = 1;
    ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (family == AF_INET6) {
        const int onlyIpv6 = 0;
        if (::setsockopt(_fd, IPPROTO_IPV6, IPV6_V6ONLY, &onlyIpv6, sizeof(onlyIpv6)) != 0) {
            closeDescriptor();
            family = AF_INET;
            _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
            if (_fd < 0) {
                _errorText = errorMessageFromErrno(errno);
                return false;
            }
            ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        }
    }

    sockaddr_storage local{};
    socklen_t localLength = 0;
    if (family == AF_INET6) {
        auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&local);
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_port = htons(port);
        if (address.family() == AF_INET6) {
            ipv6->sin6_addr = reinterpret_cast<const sockaddr_in6 *>(address.sockAddr())->sin6_addr;
        } else if (address.toInAddr().s_addr == htonl(INADDR_ANY)) {
            ipv6->sin6_addr = in6addr_any;
        } else {
            ipv6->sin6_addr.s6_addr[10] = 0xff;
            ipv6->sin6_addr.s6_addr[11] = 0xff;
            const in_addr mapped = address.toInAddr();
            std::memcpy(&ipv6->sin6_addr.s6_addr[12], &mapped.s_addr, sizeof(in_addr));
        }
        localLength = sizeof(sockaddr_in6);
    } else {
        auto *ipv4 = reinterpret_cast<sockaddr_in *>(&local);
        ipv4->sin_family = AF_INET;
        ipv4->sin_addr = address.toInAddr();
        ipv4->sin_port = htons(port);
        localLength = sizeof(sockaddr_in);
    }

    if (::bind(_fd, reinterpret_cast<const sockaddr *>(&local), localLength) != 0) {
        _errorText = errorMessageFromErrno(errno);
        closeDescriptor();
        return false;
    }

    if (::listen(_fd, kDefaultListenBacklog) != 0) {
        _errorText = errorMessageFromErrno(errno);
        closeDescriptor();
        return false;
    }

    return true;
}

// 读取实际监听端口
quint16 PosixTcpServer::queryPort() const
{
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (_fd < 0 || ::getsockname(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return 0;
    }
    if (address.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in *>(&address)->sin_port);
    }
    if (address.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_port);
    }
    return 0;
}

}  // namespace gy::net
