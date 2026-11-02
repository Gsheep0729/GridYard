/**
* @file    posix_udp_socket.cpp
* @version 7.26.1
* @date 2026-11-02
* @author  GridYard Team
* @brief   原生 UDP 套接字实现
*
* 读取时先用 MSG_PEEK 与 MSG_TRUNC 取得完整报文长度，再按实际长度分配，
* 超长报文不会被静默截断。报文在用户态队列中保持到达顺序。
*/

#include "posix_udp_socket.h"

#include "core/net_reactor.h"

#include "posix_net_error.h"

#include <QMetaObject>

#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef MSG_NOSIGNAL
// Windows 分支：无 MSG_NOSIGNAL（SIGPIPE 概念不存在），发送按默认标志透传
#define MSG_NOSIGNAL 0
#endif
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gy::net {
#ifdef _WIN32
namespace {

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

}  // namespace
#endif  // _WIN32

// 构造函数
PosixUdpSocket::PosixUdpSocket(QObject *parent)
    : QObject{parent}
{
}

// 析构函数
PosixUdpSocket::~PosixUdpSocket()
{
    close();
}

// 绑定本地地址与端口
bool PosixUdpSocket::bind(const IpAddress &address, quint16 port, BindFlags flags)
{
    close();

    IpAddress local = address.isValid() ? address : IpAddress::any();
    const int family = local.family();
#ifdef _WIN32
    // Windows 分支：socket() 无 SOCK_NONBLOCK/SOCK_CLOEXEC 创建位——非阻塞以
    // FIONBIO 补齐，执行时关闭无对应概念可省（尖兵报告 N5/N11）
    const SOCKET rawFd = ::socket(family, SOCK_DGRAM, 0);
    if (rawFd == INVALID_SOCKET) {
        _errorText = errorMessageFromErrno(netErrno());
        return false;
    }
    const u_long nonBlocking = 1;
    ::ioctlsocket(rawFd, FIONBIO, &nonBlocking);
    _fd = static_cast<int>(rawFd);
#else
    _fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (_fd < 0) {
        _errorText = errorMessageFromErrno(errno);
        return false;
    }
#endif

    if (flags.testFlag(ShareAddress) || flags.testFlag(ReuseAddressHint)) {
        const int reuse = 1;
#ifdef _WIN32
        // Windows 分支：选项指针为 const char*、长度为 int（尖兵报告 N13）
        ::setsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), static_cast<int>(sizeof(reuse)));
#else
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    }
#ifdef _WIN32
    // Windows 分支：无 SO_REUSEPORT，端口共享重绑由 SO_REUSEADDR 的 Winsock
    // 语义（允许多套接字绑定同一地址）承担（尖兵报告 N13）
#else
    if (flags.testFlag(ReuseAddressHint)) {
        const int reusePort = 1;
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &reusePort, sizeof(reusePort));
    }
#endif

    const int broadcast = 1;
#ifdef _WIN32
    // Windows 分支：SO_BROADCAST 开法一致，仅选项指针为 const char*、长度为 int
    ::setsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char *>(&broadcast),
                 static_cast<int>(sizeof(broadcast)));
#else
    ::setsockopt(_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
#endif

    sockaddr_storage bound{};
    socklen_t boundLength = 0;
    if (family == AF_INET6) {
        auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&bound);
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_addr = reinterpret_cast<const sockaddr_in6 *>(local.sockAddr())->sin6_addr;
        ipv6->sin6_port = htons(port);
        boundLength = sizeof(sockaddr_in6);
    } else {
        auto *ipv4 = reinterpret_cast<sockaddr_in *>(&bound);
        ipv4->sin_family = AF_INET;
        ipv4->sin_addr = local.toInAddr();
        ipv4->sin_port = htons(port);
        boundLength = sizeof(sockaddr_in);
    }

    if (::bind(_fd, reinterpret_cast<const sockaddr *>(&bound), boundLength) != 0) {
#ifdef _WIN32
        _errorText = errorMessageFromErrno(netErrno());
#else
        _errorText = errorMessageFromErrno(errno);
#endif
        closeDescriptor();
        return false;
    }

    ++_watchGeneration;
    _watchActive = true;
    const int watchedFd = _fd;
    const uint64_t generation = _watchGeneration;
    // 回调在 Reactor 线程触发，投递回归属线程再执行报文泵（与旧通知器语义一致）
    sharedReactor().watch(watchedFd, static_cast<uint32_t>(Event::kReadable), generation,
                          [this, watchedFd, generation](int fd, uint32_t) {
                              QMetaObject::invokeMethod(
                                  this, [this, fd, generation]() {
                                      if (_fd != fd || _watchGeneration != generation) {
                                          return;  // 已注销或换代，丢弃陈旧事件
                                      }
                                      onReadActivated();
                                  },
                                  Qt::QueuedConnection);
                          });
    _errorText.clear();
    return true;
}

// 停止接收并关闭描述符
void PosixUdpSocket::close()
{
    if (_watchActive && _fd >= 0) {
        sharedReactor().unwatchSync(_fd, _watchGeneration);
    }
    _watchActive = false;
    _datagrams.clear();
    closeDescriptor();
}

// 是否已绑定
bool PosixUdpSocket::isBound() const
{
    return _fd >= 0;
}

// 本地端口
quint16 PosixUdpSocket::localPort() const
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

// 最近一次错误说明
QString PosixUdpSocket::errorString() const
{
    return _errorText;
}

// 发送数据报
qint64 PosixUdpSocket::writeDatagram(const QByteArray &datagram, const IpAddress &address,
                                     quint16 port)
{
    if (_fd < 0 || !address.isValid()) {
        return -1;
    }

    sockaddr_storage target{};
    socklen_t targetLength = 0;
    if (address.family() == AF_INET6) {
        auto *ipv6 = reinterpret_cast<sockaddr_in6 *>(&target);
        ipv6->sin6_family = AF_INET6;
        ipv6->sin6_addr = reinterpret_cast<const sockaddr_in6 *>(address.sockAddr())->sin6_addr;
        ipv6->sin6_port = htons(port);
        targetLength = sizeof(sockaddr_in6);
    } else {
        auto *ipv4 = reinterpret_cast<sockaddr_in *>(&target);
        ipv4->sin_family = AF_INET;
        ipv4->sin_addr = address.toInAddr();
        ipv4->sin_port = htons(port);
        targetLength = sizeof(sockaddr_in);
    }

    while (true) {
#ifdef _WIN32
        // Windows 分支：sendto 缓冲长度参数为 int；MSG_NOSIGNAL 无对应物按 0
        // 透传；EAGAIN 与出错路径同 POSIX 分支一致返回 -1
        const int sent = ::sendto(static_cast<SOCKET>(_fd), datagram.constData(),
                                  static_cast<int>(datagram.size()), MSG_NOSIGNAL,
                                  reinterpret_cast<const sockaddr *>(&target), targetLength);
        if (sent >= 0) {
            return sent;
        }
        const int err = netErrno();
        if (err == EINTR) {
            continue;
        }
        _errorText = errorMessageFromErrno(err);
        return -1;
#else
        const ssize_t sent = ::sendto(_fd, datagram.constData(),
                                      static_cast<size_t>(datagram.size()), MSG_NOSIGNAL,
                                      reinterpret_cast<const sockaddr *>(&target), targetLength);
        if (sent >= 0) {
            return sent;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -1;
        }
        _errorText = errorMessageFromErrno(errno);
        return -1;
#endif
    }
}

// 读取一个数据报
qint64 PosixUdpSocket::readDatagram(char *data, qint64 maxSize, IpAddress *address,
                                    quint16 *port)
{
    if (_datagrams.isEmpty()) {
        pumpDatagrams(false);
    }
    if (_datagrams.isEmpty() || !data || maxSize <= 0) {
        return -1;
    }

    const Datagram datagram = _datagrams.takeFirst();
    const qint64 copied = std::min<qint64>(maxSize, datagram.data.size());
    std::memcpy(data, datagram.data.constData(), static_cast<size_t>(copied));
    if (address) {
        *address = datagram.address;
    }
    if (port) {
        *port = datagram.port;
    }
    return copied;
}

// 是否有待读数据报
bool PosixUdpSocket::hasPendingDatagrams() const
{
    if (!_datagrams.isEmpty()) {
        return true;
    }
    if (_fd < 0) {
        return false;
    }
#ifdef _WIN32
    // Windows 分支：FIONREAD 经 ioctlsocket，出参为 u_long*（尖兵报告 N10）
    u_long pending = 0;
    return ::ioctlsocket(static_cast<SOCKET>(_fd), FIONREAD, &pending) == 0 && pending > 0;
#else
    int pending = 0;
    return ::ioctl(_fd, FIONREAD, &pending) == 0 && pending > 0;
#endif
}

// 下一个数据报长度
qint64 PosixUdpSocket::pendingDatagramSize() const
{
    if (_datagrams.isEmpty() && _fd >= 0) {
#ifdef _WIN32
        // Windows 分支：无 MSG_TRUNC，不能像 POSIX 分支那样零消耗探得报文
        // 全长；FIONREAD 给出可读字节数，作为下一报文长度的等价来源
        u_long size = 0;
        if (::ioctlsocket(static_cast<SOCKET>(_fd), FIONREAD, &size) == 0 && size > 0) {
            return static_cast<qint64>(size);
        }
#else
        char byte = 0;
        const ssize_t size = ::recvfrom(_fd, &byte, 0, MSG_PEEK | MSG_TRUNC, nullptr, nullptr);
        if (size >= 0) {
            return size;
        }
#endif
    }
    return _datagrams.isEmpty() ? 0 : _datagrams.first().data.size();
}

// 读事件处理
void PosixUdpSocket::onReadActivated()
{
    pumpDatagrams(true);
}

// 从内核取走全部待读报文
void PosixUdpSocket::pumpDatagrams(bool notify)
{
#ifdef _WIN32
    // Windows 分支：无 MSG_TRUNC，不能像 POSIX 分支那样先探长再按需分配；
    // 改按 UDP 报文长度上限（16 位长度字段）预分配缓冲一次 recvfrom 取整份
    // 报文，报文边界与到达顺序保持一致
    if (_fd < 0) {
        return;
    }

    bool received = false;
    QByteArray chunk;
    chunk.resize(65536);
    while (true) {
        sockaddr_storage source{};
        int sourceLength = sizeof(source);
        const int amount = ::recvfrom(static_cast<SOCKET>(_fd), chunk.data(),
                                      static_cast<int>(chunk.size()), 0,
                                      reinterpret_cast<sockaddr *>(&source), &sourceLength);
        if (amount < 0) {
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

        Datagram datagram;
        datagram.data = chunk.left(amount);
        datagram.address = IpAddress::fromSockAddr(reinterpret_cast<const sockaddr *>(&source));
        if (source.ss_family == AF_INET) {
            datagram.port = ntohs(reinterpret_cast<const sockaddr_in *>(&source)->sin_port);
        } else if (source.ss_family == AF_INET6) {
            datagram.port = ntohs(reinterpret_cast<const sockaddr_in6 *>(&source)->sin6_port);
        }
        _datagrams.append(datagram);
        received = true;
    }

    if (notify && received) {
        emit readyRead();
    }
#else
    if (_fd < 0) {
        return;
    }

    bool received = false;
    while (true) {
        sockaddr_storage source{};
        socklen_t sourceLength = sizeof(source);
        const ssize_t size = ::recvfrom(_fd, nullptr, 0, MSG_PEEK | MSG_TRUNC,
                                        reinterpret_cast<sockaddr *>(&source), &sourceLength);
        if (size < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            _errorText = errorMessageFromErrno(errno);
            break;
        }

        sourceLength = sizeof(source);
        QByteArray payload;
        payload.resize(static_cast<int>(size));
        const ssize_t amount = ::recvfrom(_fd, payload.data(), static_cast<size_t>(size), 0,
                                          reinterpret_cast<sockaddr *>(&source), &sourceLength);
        if (amount < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            _errorText = errorMessageFromErrno(errno);
            break;
        }

        Datagram datagram;
        datagram.data = payload.left(static_cast<int>(amount));
        datagram.address = IpAddress::fromSockAddr(reinterpret_cast<const sockaddr *>(&source));
        if (source.ss_family == AF_INET) {
            datagram.port = ntohs(reinterpret_cast<const sockaddr_in *>(&source)->sin_port);
        } else if (source.ss_family == AF_INET6) {
            datagram.port = ntohs(reinterpret_cast<const sockaddr_in6 *>(&source)->sin6_port);
        }
        _datagrams.append(datagram);
        received = true;
    }

    if (notify && received) {
        emit readyRead();
    }
#endif
}

// 关闭描述符
void PosixUdpSocket::closeDescriptor()
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

}  // namespace gy::net
