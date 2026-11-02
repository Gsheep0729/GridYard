/**
* @file    posix_tcp_server.cpp
* @version 7.26.1
* @date 2026-11-02
* @author  GridYard Team
* @brief   原生 TCP 监听器实现
*
* 监听失败时保留失败原因供调用方展示；已接入但尚未取走的连接
* 在退出时逐个关闭，避免描述符泄漏。
*/

#include "posix_tcp_server.h"

#include "core/net_reactor.h"

#include "posix_net_error.h"
#include "posix_tcp_socket.h"

#include <QMetaObject>

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gy::net {

namespace {

constexpr int kDefaultListenBacklog = 50;  // 与既有服务端的监听队列深度一致
constexpr int kMaxAcceptBatch = 64;        // 单轮事件最多接入数，避免连接风暴独占事件循环
#ifdef _WIN32

// Windows 分支：WSA 错误码折回 errno 空间——Winsock 失败不写 errno（尖兵
// 报告 N6），错误捕获点先经此折算再走 posix_net_error 既有映射表；无 errno
// 对应条目的 WSA 专有码原值透传，落 UnknownSocketError 兜底文案
int winsockErrorToErrno(int wsaError)
{
    switch (wsaError) {
    case WSAEINTR:           return EINTR;
    case WSAEACCES:          return EACCES;
    case WSAEFAULT:          return EFAULT;
    case WSAEINVAL:          return EINVAL;
    case WSAEMFILE:          return EMFILE;
    case WSAEWOULDBLOCK:     return EAGAIN;
    case WSAEINPROGRESS:     return EINPROGRESS;
    case WSAEALREADY:        return EALREADY;
    case WSAENOTSOCK:        return ENOTSOCK;
    case WSAEDESTADDRREQ:    return EDESTADDRREQ;
    case WSAEMSGSIZE:        return EMSGSIZE;
    case WSAEPROTOTYPE:      return EPROTOTYPE;
    case WSAENOPROTOOPT:     return ENOPROTOOPT;
    case WSAEPROTONOSUPPORT: return EPROTONOSUPPORT;
    case WSAEOPNOTSUPP:      return EOPNOTSUPP;
    case WSAEAFNOSUPPORT:    return EAFNOSUPPORT;
    case WSAEADDRINUSE:      return EADDRINUSE;
    case WSAEADDRNOTAVAIL:   return EADDRNOTAVAIL;
    case WSAENETDOWN:        return ENETDOWN;
    case WSAENETUNREACH:     return ENETUNREACH;
    case WSAENETRESET:       return ENETRESET;
    case WSAECONNABORTED:    return ECONNABORTED;
    case WSAECONNRESET:      return ECONNRESET;
    case WSAENOBUFS:         return ENOBUFS;
    case WSAEISCONN:         return EISCONN;
    case WSAENOTCONN:        return ENOTCONN;
    case WSAETIMEDOUT:       return ETIMEDOUT;
    case WSAECONNREFUSED:    return ECONNREFUSED;
    case WSAEHOSTUNREACH:    return EHOSTUNREACH;
    default:                 return wsaError;
    }
}

// Windows 分支：套接字调用后的"errno"等价物（WSAGetLastError 折回 errno 空间）
int netErrno()
{
    return winsockErrorToErrno(::WSAGetLastError());
}

// Windows 分支：socket() 无 SOCK_NONBLOCK/SOCK_CLOEXEC 创建位——创建后以
// FIONBIO 补非阻塞，执行时关闭无对应概念可省（尖兵报告 N5/N11）
int createStreamSocket(int family)
{
    const SOCKET raw = ::socket(family, SOCK_STREAM, 0);
    if (raw == INVALID_SOCKET) {
        return -1;
    }
    const u_long nonBlocking = 1;
    ::ioctlsocket(raw, FIONBIO, &nonBlocking);
    return static_cast<int>(raw);
}

#endif  // _WIN32

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

    ++_watchGeneration;
    _watchActive = true;
    const int watchedFd = _fd;
    const uint64_t generation = _watchGeneration;
    // 回调在 Reactor 线程触发，投递回归属线程再执行接入泵（与旧通知器语义一致）
    sharedReactor().watch(watchedFd, static_cast<uint32_t>(Event::kReadable), generation,
                          [this, watchedFd, generation](int fd, uint32_t) {
                              QMetaObject::invokeMethod(
                                  this, [this, fd, generation]() {
                                      if (_fd != fd || _watchGeneration != generation) {
                                          return;  // 已注销或换代，丢弃陈旧事件
                                      }
                                      onAcceptActivated();
                                  },
                                  Qt::QueuedConnection);
                          });
    _port = queryPort();
    _errorText.clear();
    return true;
}

// 停止监听并关闭待取连接
void PosixTcpServer::close()
{
    if (_watchActive && _fd >= 0) {
        sharedReactor().unwatchSync(_fd, _watchGeneration);
    }
    _watchActive = false;

    for (const PendingConnection &pending : _pending) {
        if (pending.descriptor >= 0) {
#ifdef _WIN32
            // Windows 分支：套接字关闭为 closesocket（尖兵报告 N7）
            ::closesocket(static_cast<SOCKET>(pending.descriptor));
#else
            ::close(pending.descriptor);
#endif
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
#ifdef _WIN32
        // Windows 分支：无 accept4——accept 后补 FIONBIO 非阻塞（尖兵报告
        // N5/N11），执行时关闭无对应概念可省
        const SOCKET rawDescriptor = ::accept(static_cast<SOCKET>(_fd), nullptr, nullptr);
        if (rawDescriptor == INVALID_SOCKET) {
            const int err = netErrno();
            if (err == EINTR) {
                continue;
            }
            if (err == EAGAIN) {
                break;
            }
            _errorText = errorMessageFromErrno(err);
            break;
        }
        const u_long nonBlocking = 1;
        ::ioctlsocket(rawDescriptor, FIONBIO, &nonBlocking);
        const int descriptor = static_cast<int>(rawDescriptor);
#else
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
#endif

        const int enabled = 1;
#ifdef _WIN32
        // Windows 分支：选项指针为 const char*、长度为 int（尖兵报告 N13）
        ::setsockopt(static_cast<SOCKET>(descriptor), IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char *>(&enabled), static_cast<int>(sizeof(enabled)));
#else
        ::setsockopt(descriptor, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
#endif
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
#ifdef _WIN32
        // Windows 分支：套接字关闭为 closesocket（尖兵报告 N7）
        ::closesocket(static_cast<SOCKET>(_fd));
#else
        ::close(_fd);
#endif
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
#ifdef _WIN32
    _fd = createStreamSocket(family);
#else
    _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#endif
    if (_fd < 0 && wantsDualStack) {
        // 内核未启用 IPv6 时退回 IPv4，保持监听能力可用
        family = AF_INET;
#ifdef _WIN32
        _fd = createStreamSocket(family);
#else
        _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#endif
    }
    if (_fd < 0) {
#ifdef _WIN32
        _errorText = errorMessageFromErrno(netErrno());
#else
        _errorText = errorMessageFromErrno(errno);
#endif
        return false;
    }

    const int reuse = 1;
#ifdef _WIN32
    // Windows 分支：选项指针为 const char*、长度为 int；SO_REUSEADDR 在
    // Windows 语义更宽（后绑者可抢占共享端口），监听端口场景下可接受
    //（尖兵报告 N13）
    ::setsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char *>(&reuse), static_cast<int>(sizeof(reuse)));
#else
    ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    if (family == AF_INET6) {
        const int onlyIpv6 = 0;
#ifdef _WIN32
        // Windows 分支：V6ONLY 默认值为 1，显式置 0 后与 Linux 行为一致；
        // 选项指针为 const char*、长度为 int（尖兵报告 N13/N16）
        const int v6onlyResult = ::setsockopt(static_cast<SOCKET>(_fd), IPPROTO_IPV6,
                                              IPV6_V6ONLY,
                                              reinterpret_cast<const char *>(&onlyIpv6),
                                              static_cast<int>(sizeof(onlyIpv6)));
        if (v6onlyResult != 0) {
#else
        if (::setsockopt(_fd, IPPROTO_IPV6, IPV6_V6ONLY, &onlyIpv6, sizeof(onlyIpv6)) != 0) {
#endif
            closeDescriptor();
            family = AF_INET;
#ifdef _WIN32
            _fd = createStreamSocket(family);
#else
            _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#endif
            if (_fd < 0) {
#ifdef _WIN32
                _errorText = errorMessageFromErrno(netErrno());
#else
                _errorText = errorMessageFromErrno(errno);
#endif
                return false;
            }
#ifdef _WIN32
            ::setsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_REUSEADDR,
                         reinterpret_cast<const char *>(&reuse),
                         static_cast<int>(sizeof(reuse)));
#else
            ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
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
#ifdef _WIN32
        _errorText = errorMessageFromErrno(netErrno());
#else
        _errorText = errorMessageFromErrno(errno);
#endif
        closeDescriptor();
        return false;
    }

    if (::listen(_fd, kDefaultListenBacklog) != 0) {
#ifdef _WIN32
        _errorText = errorMessageFromErrno(netErrno());
#else
        _errorText = errorMessageFromErrno(errno);
#endif
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
