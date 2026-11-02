/**
* @file    posix_tcp_socket.cpp
* @version 8.3.0
* @date    2026-11-03
* @author  GridYard Team
* @brief   原生 TCP 套接字实现
*
* 所有系统调用均为非阻塞形式，可读可写事件由 NetReactor 事件泵驱动：
* Reactor 线程回调经 QMetaObject::invokeMethod(QueuedConnection) 投回
* socket 归属线程再执行泵逻辑，线程语义与旧 QSocketNotifier 完全一致。
* 发送统一带 MSG_NOSIGNAL，短写与 EAGAIN 留在用户态写队列，EINTR 原处重试。
* 阻塞等待族在局部引擎上挂异步就绪等待，不私有消费 fd 事件，
* 与事件泵对同一 fd 的监听互不抢占。
*/

#include "posix_tcp_socket.h"

#include "core/net_reactor.h"
#include "posix_net_error.h"

#include <QMetaObject>
#include <QMetaType>
#include <QThread>

#include <boost/asio/io_context.hpp>
#ifdef _WIN32
#include <boost/asio/ip/tcp.hpp>
#else
#include <boost/asio/posix/stream_descriptor.hpp>
#endif
#include <boost/asio/steady_timer.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef MSG_NOSIGNAL
// Windows 分支：无 MSG_NOSIGNAL（SIGPIPE 概念不存在），发送按默认标志透传
#define MSG_NOSIGNAL 0
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gy::net {

namespace {

constexpr qint64 kReceiveChunkBytes = 64 * 1024;          // 单次读取块大小
constexpr qint64 kReadBufferCompactThreshold = 1024 * 1024;  // 读缓冲消费过半后的压缩门槛
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
    u_long nonBlocking = 1;
    ::ioctlsocket(raw, FIONBIO, &nonBlocking);
    return static_cast<int>(raw);
}

#endif  // _WIN32

// 给描述符设置非阻塞与关闭执行标志
#ifdef _WIN32
bool configureDescriptor(int fd)
{
    // Windows 分支：无 fcntl，非阻塞以 FIONBIO 补齐（尖兵报告 N5/N11）；
    // 执行时关闭无对应概念可省
    u_long nonBlocking = 1;
    return ::ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nonBlocking) == 0;
}
#else
bool configureDescriptor(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }

    const int descriptorFlags = ::fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0 || ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
        return false;
    }
    return true;
}
#endif

// 开启 TCP_NODELAY，失败不中断连接
void applyTcpNoDelay(int fd)
{
    const int enabled = 1;
#ifdef _WIN32
    // Windows 分支：选项指针为 const char*、长度为 int（尖兵报告 N13）
    ::setsockopt(static_cast<SOCKET>(fd), IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char *>(&enabled), static_cast<int>(sizeof(enabled)));
#else
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
#endif
}

// 一次性就绪等待：在局部引擎上挂异步 wait，就绪或到期即返回。
// 事件泵对本 fd 的监听在各自独立的实例上，互不抢占事件，阻塞窗口内
// 两边的挂起操作都拿得到各自的通知。msecs 非 0 时到期返回 false。
#ifdef _WIN32
bool waitDescriptorReady(int fd, bool wantWrite, int msecs)
{
    // Windows 分支：posix::stream_descriptor 无 Windows 形态，改以 asio TCP
    // 套接字挂 async_wait（引擎、定时器与剥离所有权结构与 POSIX 分支一致）；
    // 原生套接字归调用方
    if (fd < 0) {
        return false;
    }
    boost::asio::io_context engine;
    boost::asio::ip::tcp::socket descriptor(engine);
    boost::system::error_code ec;
    descriptor.assign(boost::asio::ip::tcp::v4(), static_cast<SOCKET>(fd), ec);
    if (ec) {
        // IPv4 协议标注不被接受时按 IPv6 再试（双栈监听接入的描述符）
        ec.clear();
        descriptor.assign(boost::asio::ip::tcp::v6(), static_cast<SOCKET>(fd), ec);
    }
    if (ec) {
        return false;
    }
    boost::asio::steady_timer deadline(engine);
    bool ready = false;
    descriptor.async_wait(wantWrite ? boost::asio::ip::tcp::socket::wait_write
                                    : boost::asio::ip::tcp::socket::wait_read,
                          [&ready, &deadline](const boost::system::error_code &waitEc) {
                              if (waitEc == boost::asio::error::operation_aborted) {
                                  return;  // 到期取消路径
                              }
                              ready = true;
                              deadline.cancel();
                          });
    if (msecs >= 0) {
        deadline.expires_after(std::chrono::milliseconds(msecs));
        deadline.async_wait([&descriptor](const boost::system::error_code &timerEc) {
            if (!timerEc) {
                descriptor.cancel();  // 到期取消就绪等待，引擎随后排空退出
            }
        });
    }
    engine.run();
    descriptor.release();  // 剥离引擎所有权，原生套接字归调用方
    return ready;
}
#else
bool waitDescriptorReady(int fd, bool wantWrite, int msecs)
{
    if (fd < 0) {
        return false;
    }
    boost::asio::io_context engine;
    boost::asio::posix::stream_descriptor descriptor(engine);
    boost::system::error_code ec;
    descriptor.assign(fd, ec);
    if (ec) {
        return false;
    }
    boost::asio::steady_timer deadline(engine);
    bool ready = false;
    descriptor.async_wait(wantWrite ? boost::asio::posix::descriptor_base::wait_write
                                    : boost::asio::posix::descriptor_base::wait_read,
                          [&ready, &deadline](const boost::system::error_code &waitEc) {
                              if (waitEc == boost::asio::error::operation_aborted) {
                                  return;  // 到期取消路径
                              }
                              ready = true;
                              deadline.cancel();
                          });
    if (msecs >= 0) {
        deadline.expires_after(std::chrono::milliseconds(msecs));
        deadline.async_wait([&descriptor](const boost::system::error_code &timerEc) {
            if (!timerEc) {
                descriptor.cancel();  // 到期取消就绪等待，引擎随后排空退出
            }
        });
    }
    engine.run();
    descriptor.release();  // 剥离引擎所有权，原生描述符归调用方
    return ready;
}
#endif

}  // namespace

// 构造函数
PosixTcpSocket::PosixTcpSocket(QObject *parent)
    : QObject{parent}
{
    qRegisterMetaType<gy::net::SocketError>("gy::net::SocketError");
}

// 析构函数
PosixTcpSocket::~PosixTcpSocket()
{
    _movedPending.store(true, std::memory_order_release);
    teardownNotifiers();
    closeDescriptor();
}

// 接管已建立的连接描述符
bool PosixTcpSocket::adoptDescriptor(int descriptor)
{
    if (descriptor < 0) {
        return false;
    }

    teardownNotifiers();
    closeDescriptor();

    if (!configureDescriptor(descriptor)) {
#ifdef _WIN32
        const int savedErrno = netErrno();
        // Windows 分支：套接字关闭为 closesocket（尖兵报告 N7）
        ::closesocket(static_cast<SOCKET>(descriptor));
#else
        const int savedErrno = errno;
        ::close(descriptor);
#endif
        setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
        return false;
    }

    _fd = descriptor;
    applyTcpNoDelay(_fd);
    _state = SocketState::ConnectedState;
    _lastError = SocketError::NoError;
    _errorText.clear();
    _txBuf.clear();
    _txOffset = 0;
    _peerEofSeen = false;
    ensureNotifiers();
    syncReadNotifier();
    return true;
}

// 移交前停用监听
void PosixTcpSocket::prepareThreadMove()
{
    _movedPending.store(true, std::memory_order_release);
    if (_fd >= 0) {
        // 只摘监听不关 fd：对象仍存活，Qt 清事件机制兜底排队中的投递
        sharedReactor().unwatch(_fd, _watchGeneration);
        _readWatchWanted = false;
        _writeWatchWanted = false;
    }
}

// 在目标线程重新启用监听
void PosixTcpSocket::finishThreadMove()
{
    _movedPending.store(false, std::memory_order_release);
    if (_fd >= 0) {
        ensureNotifiers();
    }
    syncReadNotifier();
    syncWriteNotifier();
}

// 是否处于线程移交窗口
bool PosixTcpSocket::isThreadMovePending() const
{
    return _movedPending.load(std::memory_order_acquire);
}

// 按主机名或地址文本建立连接
void PosixTcpSocket::connectToHost(const QString &host, quint16 port)
{
    const IpAddress parsed = IpAddress::fromString(host);
    if (parsed.isValid()) {
        connectToHost(parsed, port);
        return;
    }

    abort();
    _lastError = SocketError::NoError;
    _errorText.clear();

    const QByteArray hostName = host.trimmed().toUtf8();
    if (hostName.isEmpty()) {
        setError(SocketError::HostNotFoundError, QStringLiteral("找不到主机"));
        return;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo *addresses = nullptr;
    const int lookupResult = ::getaddrinfo(hostName.constData(), nullptr, &hints, &addresses);
    if (lookupResult != 0 || !addresses) {
        setError(SocketError::HostNotFoundError, QStringLiteral("找不到主机"));
        return;
    }

    IpAddress target;
    for (const addrinfo *entry = addresses; entry; entry = entry->ai_next) {
        if (entry->ai_family == AF_INET || entry->ai_family == AF_INET6) {
            target = IpAddress::fromSockAddr(entry->ai_addr);
            if (target.isValid()) {
                break;
            }
        }
    }
    ::freeaddrinfo(addresses);

    if (!target.isValid()) {
        setError(SocketError::HostNotFoundError, QStringLiteral("找不到主机"));
        return;
    }

    connectToHost(target, port);
}

// 按地址值建立连接
void PosixTcpSocket::connectToHost(const IpAddress &address, quint16 port)
{
    abort();
    _lastError = SocketError::NoError;
    _errorText.clear();

    if (!address.isValid()) {
        setError(SocketError::HostNotFoundError, QStringLiteral("找不到主机"));
        return;
    }

    const int family = address.family();
#ifdef _WIN32
    _fd = createStreamSocket(family);
#else
    _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#endif
    if (_fd < 0) {
#ifdef _WIN32
        const int savedErrno = netErrno();
#else
        const int savedErrno = errno;
#endif
        _state = SocketState::UnconnectedState;
        setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
        return;
    }

    sockaddr_storage target{};
    socklen_t targetLength = 0;
    if (family == AF_INET6) {
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

    _peerEofSeen = false;
    _state = SocketState::ConnectingState;
    const int result = ::connect(_fd, reinterpret_cast<const sockaddr *>(&target), targetLength);
    if (result == 0) {
        finishConnect();
        return;
    }

#ifdef _WIN32
    // Windows 分支：未决连接以 WSAEWOULDBLOCK 呈现，折回 errno 空间后与
    // EINPROGRESS 同判（尖兵报告 N6）
    const int pendingErrno = netErrno();
    if (pendingErrno != EINPROGRESS && pendingErrno != EAGAIN) {
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        setError(socketErrorFromErrno(pendingErrno), errorMessageFromErrno(pendingErrno));
        return;
    }
#else
    if (errno != EINPROGRESS) {
        const int savedErrno = errno;
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
        return;
    }
#endif

    ensureNotifiers();
    syncReadNotifier();
    syncWriteNotifier();
}

// 当前连接状态
SocketState PosixTcpSocket::state() const
{
    return _state;
}

// 最近一次错误类型
SocketError PosixTcpSocket::error() const
{
    return _lastError;
}

// 最近一次错误说明
QString PosixTcpSocket::errorString() const
{
    return _errorText;
}

// 可读字节数
qint64 PosixTcpSocket::bytesAvailable() const
{
    qint64 total = bufferedBytes();
    if (_fd >= 0) {
#ifdef _WIN32
        // Windows 分支：FIONREAD 经 ioctlsocket，出参为 u_long*（尖兵报告 N10）
        u_long pending = 0;
        if (::ioctlsocket(static_cast<SOCKET>(_fd), FIONREAD, &pending) == 0 && pending > 0) {
            total += pending;
        }
#else
        int pending = 0;
        if (::ioctl(_fd, FIONREAD, &pending) == 0 && pending > 0) {
            total += pending;
        }
#endif
    }
    return total;
}

// 待写字节数
qint64 PosixTcpSocket::bytesToWrite() const
{
    return std::max<qint64>(0, _txBuf.size() - _txOffset);
}

// 读取指定长度到调用方缓冲
qint64 PosixTcpSocket::read(char *data, qint64 maxSize)
{
    if (!data || maxSize <= 0) {
        return 0;
    }

    pumpReadBuffer(false);
    const qint64 amount = std::min(maxSize, bufferedBytes());
    if (amount <= 0) {
        return 0;
    }

    std::memcpy(data, _rxBuf.constData() + _rxOffset, static_cast<size_t>(amount));
    _rxOffset += amount;
    compactReadBuffer();
    syncReadNotifier();
    return amount;
}

// 读取指定长度并返回字节数组
QByteArray PosixTcpSocket::read(qint64 maxSize)
{
    if (maxSize <= 0) {
        return {};
    }

    pumpReadBuffer(false);
    const qint64 amount = std::min(maxSize, bufferedBytes());
    if (amount <= 0) {
        return {};
    }

    const QByteArray result = _rxBuf.mid(_rxOffset, amount);
    _rxOffset += amount;
    compactReadBuffer();
    syncReadNotifier();
    return result;
}

// 读取全部可读内容
QByteArray PosixTcpSocket::readAll()
{
    pumpReadBuffer(false);
    if (bufferedBytes() <= 0) {
        return {};
    }

    const QByteArray result = _rxBuf.mid(_rxOffset);
    _rxOffset = _rxBuf.size();
    compactReadBuffer();
    syncReadNotifier();
    return result;
}

// 窥探指定长度但不消费
QByteArray PosixTcpSocket::peek(qint64 maxSize)
{
    if (maxSize <= 0) {
        return {};
    }

    pumpReadBuffer(false);
    const qint64 amount = std::min(maxSize, bufferedBytes());
    if (amount <= 0) {
        return {};
    }
    return _rxBuf.mid(_rxOffset, amount);
}

// 读取一行
QByteArray PosixTcpSocket::readLine(qint64 maxSize)
{
    pumpReadBuffer(false);
    const qint64 available = bufferedBytes();
    if (available <= 0) {
        return {};
    }

    const qint64 limit = maxSize > 0 ? std::min(maxSize, available) : available;
    const int newlineIndex = _rxBuf.indexOf('\n', static_cast<int>(_rxOffset));
    qint64 amount = limit;
    if (newlineIndex >= 0) {
        const qint64 lineLength = newlineIndex - _rxOffset + 1;
        amount = std::min(limit, lineLength);
    }

    const QByteArray result = _rxBuf.mid(_rxOffset, amount);
    _rxOffset += amount;
    compactReadBuffer();
    syncReadNotifier();
    return result;
}

// 写入数据
qint64 PosixTcpSocket::write(const QByteArray &data)
{
    return write(data.constData(), data.size());
}

// 写入原始数据
qint64 PosixTcpSocket::write(const char *data, qint64 size)
{
    if (!data || size <= 0) {
        return 0;
    }
    if (_fd < 0 || _state == SocketState::UnconnectedState) {
        return -1;
    }
    if (size > INT_MAX - _txBuf.size()) {
        return -1;
    }

    _txBuf.append(data, static_cast<int>(size));
    pumpWriteBuffer();
    return _state == SocketState::UnconnectedState ? -1 : size;
}

// 尽力把用户态写缓冲送入内核
void PosixTcpSocket::flush()
{
    pumpWriteBuffer();
}

// 阻塞等待连接建立
bool PosixTcpSocket::waitForConnected(int msecs)
{
    if (_state == SocketState::ConnectedState) {
        return true;
    }
    if (_fd < 0) {
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(std::max(0, msecs));

    while (true) {
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        if (remaining <= 0 && msecs >= 0) {
            setError(SocketError::SocketTimeoutError, QStringLiteral("连接超时"));
            teardownNotifiers();
            closeDescriptor();
            _state = SocketState::UnconnectedState;
            return false;
        }

        if (!waitDescriptorReady(_fd, true, msecs < 0 ? -1 : remaining)) {
            continue;  // 到期：外层超时检查收尾
        }

        int socketError = 0;
#ifdef _WIN32
        // Windows 分支：选项出参为 char*、长度为 int；SO_ERROR 返回 WSA 错误
        // 码，折回 errno 空间后再走既有错误映射（尖兵报告 N6）
        int errorLength = sizeof(socketError);
        if (::getsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char *>(&socketError), &errorLength) != 0) {
            socketError = netErrno();
        } else {
            socketError = winsockErrorToErrno(socketError);
        }
#else
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) != 0) {
            socketError = errno;
        }
#endif

        if (socketError == 0) {
            finishConnect();
            return _state == SocketState::ConnectedState;
        }

        setError(socketErrorFromErrno(socketError), errorMessageFromErrno(socketError));
        teardownNotifiers();
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        return false;
    }
}

// 阻塞等待可读数据
bool PosixTcpSocket::waitForReadyRead(int msecs)
{
    if (bytesAvailable() > 0) {
        pumpReadBuffer(true);
        return true;
    }
    if (_fd < 0) {
        return false;
    }

    if (!waitDescriptorReady(_fd, false, msecs)) {
        return false;  // 到期未就绪
    }

    pumpReadBuffer(true);
    return bytesAvailable() > 0 || _state != SocketState::ConnectedState;
}

// 阻塞等待写缓冲排空
bool PosixTcpSocket::waitForBytesWritten(int msecs)
{
    if (bytesToWrite() == 0) {
        return true;
    }
    if (_fd < 0) {
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(std::max(0, msecs));

    while (bytesToWrite() > 0) {
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        if (remaining <= 0 && msecs >= 0) {
            return false;
        }

        if (!waitDescriptorReady(_fd, true, msecs < 0 ? -1 : remaining)) {
            return false;  // 到期未排空
        }

        pumpWriteBuffer();
        if (_fd < 0) {
            return false;
        }
    }
    return true;
}

// 阻塞等待连接断开
bool PosixTcpSocket::waitForDisconnected(int msecs)
{
    if (_state == SocketState::UnconnectedState) {
        return true;
    }
    if (_fd < 0) {
        return false;
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(std::max(0, msecs));

    while (_state != SocketState::UnconnectedState) {
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        if (remaining <= 0 && msecs >= 0) {
            return false;
        }

        if (!waitDescriptorReady(_fd, false, msecs < 0 ? -1 : remaining)) {
            return false;  // 到期未断开
        }

        // 关闭等待期间只排空缓冲，不再向业务层派发可读事件
        pumpReadBuffer(false);
        if (_peerEofSeen) {
            return true;
        }
    }
    return true;
}

// 优雅关闭
void PosixTcpSocket::disconnectFromHost(bool waitForPeer, int waitMs)
{
    if (_state == SocketState::UnconnectedState) {
        return;
    }
    if (_state == SocketState::ConnectingState) {
        abort();
        return;
    }
    if (_fd < 0) {
        abort();
        return;
    }

    // 先在连接态把待写数据送出，避免写泵看到关闭态后提前收尾
    if (bytesToWrite() > 0) {
        pumpWriteBuffer();
    }
    if (bytesToWrite() > 0) {
        // 写缓冲尚未排空，记录等待语义，待写泵清空后由写事件续走关闭流程
        _gracefulWaitPending = waitForPeer;
        _gracefulWaitMs = waitMs;
        _state = SocketState::ClosingState;
        syncWriteNotifier();
        return;
    }
    if (_fd < 0) {
        return;
    }

    _state = SocketState::ClosingState;
    _gracefulWaitPending = false;
#ifdef _WIN32
    // Windows 分支：半关发送侧以 SD_SEND 表达（SHUT_WR 无对应物）
    ::shutdown(static_cast<SOCKET>(_fd), SD_SEND);
#else
    ::shutdown(_fd, SHUT_WR);
#endif

    // 取消帧等场景需要等对端读到并先关闭，持续排空到达数据可避免带未读数据关闭触发 RST
    if (waitForPeer) {
        waitForDisconnected(waitMs);
    }

    if (_fd >= 0) {
        finishGracefulClose();
    }
}

// 立即关闭
void PosixTcpSocket::abort()
{
    const bool wasActive = _state == SocketState::ConnectedState
        || _state == SocketState::ConnectingState
        || _state == SocketState::ClosingState;

    teardownNotifiers();
    closeDescriptor();
    _state = SocketState::UnconnectedState;
    _txBuf.clear();
    _txOffset = 0;
    _rxBuf.clear();
    _rxOffset = 0;
    _gracefulWaitPending = false;
    _gracefulWaitMs = 0;
    _peerEofSeen = false;

    if (wasActive) {
        emit disconnected();
    }
}

// 关闭连接
void PosixTcpSocket::close()
{
    // 立即关闭语义与优雅断连分开，避免待写数据让关闭无限延后
    abort();
}

// 设置套接字选项
bool PosixTcpSocket::setSocketOption(SocketOption option, const QVariant &value)
{
    if (_fd < 0) {
        return false;
    }

    const int intValue = value.toInt();
    switch (option) {
    case SendBufferSizeOption:
#ifdef _WIN32
        // Windows 分支：选项指针为 const char*、长度为 int（尖兵报告 N13）
        return ::setsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_SNDBUF,
                            reinterpret_cast<const char *>(&intValue),
                            static_cast<int>(sizeof(intValue))) == 0;
#else
        return ::setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &intValue, sizeof(intValue)) == 0;
#endif
    case ReceiveBufferSizeOption:
#ifdef _WIN32
        return ::setsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_RCVBUF,
                            reinterpret_cast<const char *>(&intValue),
                            static_cast<int>(sizeof(intValue))) == 0;
#else
        return ::setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &intValue, sizeof(intValue)) == 0;
#endif
    case LowDelayOption: {
        const int enabled = intValue != 0 ? 1 : 0;
#ifdef _WIN32
        return ::setsockopt(static_cast<SOCKET>(_fd), IPPROTO_TCP, TCP_NODELAY,
                            reinterpret_cast<const char *>(&enabled),
                            static_cast<int>(sizeof(enabled))) == 0;
#else
        return ::setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0;
#endif
    }
    }
    return false;
}

// 设置应用层读缓冲上限
void PosixTcpSocket::setReadBufferSize(qint64 size)
{
    // 只限制用户态缓冲，内核接收窗口保持系统自动调优
    _readBufferLimit = std::max<qint64>(0, size);
    syncReadNotifier();
}

// 本地端口
quint16 PosixTcpSocket::localPort() const
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

// 对端端口
quint16 PosixTcpSocket::peerPort() const
{
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (_fd < 0 || ::getpeername(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
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

// 本地地址
IpAddress PosixTcpSocket::localAddress() const
{
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (_fd < 0 || ::getsockname(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return {};
    }
    return IpAddress::fromSockAddr(reinterpret_cast<const sockaddr *>(&address));
}

// 对端地址
IpAddress PosixTcpSocket::peerAddress() const
{
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (_fd < 0 || ::getpeername(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return {};
    }
    return IpAddress::fromSockAddr(reinterpret_cast<const sockaddr *>(&address));
}

// 原生描述符
int PosixTcpSocket::socketDescriptor() const
{
    return _fd;
}

// 读事件处理
void PosixTcpSocket::onReadActivated()
{
    pumpReadBuffer(true);
}

// 写事件处理
void PosixTcpSocket::onWriteActivated()
{
    if (_movedPending.load(std::memory_order_acquire)) {
        return;
    }

    if (_state == SocketState::ConnectingState) {
        int socketError = 0;
#ifdef _WIN32
        // Windows 分支：选项出参为 char*、长度为 int；SO_ERROR 返回 WSA 错误
        // 码，折回 errno 空间后再走既有错误映射（尖兵报告 N6）
        int errorLength = sizeof(socketError);
        if (::getsockopt(static_cast<SOCKET>(_fd), SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char *>(&socketError), &errorLength) != 0) {
            socketError = netErrno();
        } else {
            socketError = winsockErrorToErrno(socketError);
        }
#else
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) != 0) {
            socketError = errno;
        }
#endif

        if (socketError == 0) {
            finishConnect();
        } else {
            teardownNotifiers();
            closeDescriptor();
            _state = SocketState::UnconnectedState;
            setError(socketErrorFromErrno(socketError), errorMessageFromErrno(socketError));
        }
        return;
    }

    pumpWriteBuffer();
    syncWriteNotifier();
}

// 建立 Reactor 事件监听（每次 fd 生命周期开始时代际递增）
void PosixTcpSocket::ensureNotifiers()
{
    if (_fd < 0 || _readWatchWanted) {
        return;
    }

    ++_watchGeneration;
    _readWatchWanted = true;
    _writeWatchWanted = false;
    applyWatch();
}

// 摘除 Reactor 事件监听：同步等待 Reactor 确认，保证返回后不再有
// 该 fd 的派发在途，close(fd) 与对象析构窗口内无陈旧回调竞态
void PosixTcpSocket::teardownNotifiers()
{
    if (_watchActive && _fd >= 0) {
        sharedReactor().unwatchSync(_fd, _watchGeneration);
    }
    _watchActive = false;
    _readWatchWanted = false;
    _writeWatchWanted = false;
}

// 按当前读写监听意愿应用 Reactor watch/unwatch
void PosixTcpSocket::applyWatch()
{
    if (_fd < 0) {
        return;
    }

    uint32_t events = 0;
    if (_readWatchWanted) {
        events |= static_cast<uint32_t>(Event::kReadable);
    }
    if (_writeWatchWanted) {
        events |= static_cast<uint32_t>(Event::kWritable);
    }

    if (events != 0) {
        // 回调在 Reactor 线程触发：只做投递，经 QueuedConnection 回到 socket
        // 归属线程执行泵逻辑（复刻 QSocketNotifier 在 owner 线程派发的语义）。
        // 读与写投递为两个独立事件——旧实现是读写两个独立通知器，连接刚建立
        // 即被对端关闭的场景下，state 必须先后经过 Connected 再 Unconnected。
        const int watchedFd = _fd;
        const uint64_t generation = _watchGeneration;
        sharedReactor().watch(watchedFd, events, generation,
                              [this, watchedFd, generation](int fd, uint32_t revents) {
                                  if (revents & (static_cast<uint32_t>(Event::kWritable)
                                                 | static_cast<uint32_t>(Event::kError))) {
                                      QMetaObject::invokeMethod(
                                          this, [this, fd, generation]() {
                                              if (_fd != fd || _watchGeneration != generation) {
                                                  return;  // 已注销或换代，丢弃陈旧事件
                                              }
                                              onWriteActivated();
                                          },
                                          Qt::QueuedConnection);
                                  }
                                  // 可读与挂断统一走读泵收尾（错误已归写回调，与旧通知器语义一致）
                                  if (revents & (static_cast<uint32_t>(Event::kReadable)
                                                 | static_cast<uint32_t>(Event::kHangup))) {
                                      QMetaObject::invokeMethod(
                                          this, [this, fd, generation]() {
                                              if (_fd != fd || _watchGeneration != generation) {
                                                  return;  // 已注销或换代，丢弃陈旧事件
                                              }
                                              onReadActivated();
                                          },
                                          Qt::QueuedConnection);
                                  }
                              });
        _watchActive = true;
    } else if (_watchActive) {
        sharedReactor().unwatch(_fd, _watchGeneration);
        _watchActive = false;
    }
}

// 记录错误并通知
void PosixTcpSocket::setError(SocketError error, const QString &message)
{
    _lastError = error;
    _errorText = message;
    emit errorOccurred(error);
}

// 关闭描述符
void PosixTcpSocket::closeDescriptor()
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

// 从内核读取数据到用户态缓冲
void PosixTcpSocket::pumpReadBuffer(bool notify)
{
    if (_fd < 0 || _movedPending.load(std::memory_order_acquire)) {
        return;
    }

    // 已在读泵内时只允许无通知的强制排空，不再递归派发可读/断开信号
    if (_pumpingRead) {
        if (notify) {
            return;
        }
        while (true) {
            if (_readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit) {
                break;
            }
            char chunk[kReceiveChunkBytes];
#ifdef _WIN32
            // Windows 分支：recv 缓冲长度参数为 int；错误经 netErrno 折回
            // errno 空间（尖兵报告 N6）
            const int amount = ::recv(static_cast<SOCKET>(_fd), chunk,
                                      static_cast<int>(sizeof(chunk)), 0);
#else
            const ssize_t amount = ::recv(_fd, chunk, sizeof(chunk), 0);
#endif
            if (amount > 0) {
                _rxBuf.append(chunk, static_cast<int>(amount));
                continue;
            }
            if (amount == 0) {
                _peerEofSeen = true;
                break;
            }
#ifdef _WIN32
            const int err = netErrno();
            if (err == EINTR) {
                continue;
            }
            if (err == EAGAIN) {
                break;
            }
#else
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
#endif
            _peerEofSeen = true;
            break;
        }
        return;
    }

    _pumpingRead = true;
    bool received = false;
    while (true) {
        if (_readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit) {
            break;
        }

        char chunk[kReceiveChunkBytes];
#ifdef _WIN32
        // Windows 分支：recv 缓冲长度参数为 int；错误经 netErrno 折回 errno
        // 空间（尖兵报告 N6）
        const int amount = ::recv(static_cast<SOCKET>(_fd), chunk,
                                  static_cast<int>(sizeof(chunk)), 0);
#else
        const ssize_t amount = ::recv(_fd, chunk, sizeof(chunk), 0);
#endif
        if (amount > 0) {
            _rxBuf.append(chunk, static_cast<int>(amount));
            received = true;
            continue;
        }
        if (amount == 0) {
            if (notify && received) {
                emit readyRead();
            }
            const bool wasActive = _state != SocketState::UnconnectedState;
            teardownNotifiers();
            closeDescriptor();
            _state = SocketState::UnconnectedState;
            _txBuf.clear();
            _txOffset = 0;
            _gracefulWaitPending = false;
            _peerEofSeen = false;
            if (wasActive) {
                emit disconnected();
            }
            _pumpingRead = false;
            return;
        }

#ifdef _WIN32
        const int err = netErrno();
        if (err == EINTR) {
            continue;
        }
        if (err == EAGAIN) {
            break;
        }
        const int savedErrno = err;
#else
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        const int savedErrno = errno;
#endif
        if (notify && received) {
            emit readyRead();
        }
        const bool wasActive = _state != SocketState::UnconnectedState;
        setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
        teardownNotifiers();
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        _txBuf.clear();
        _txOffset = 0;
        _gracefulWaitPending = false;
        _peerEofSeen = false;
        if (wasActive) {
            emit disconnected();
        }
        _pumpingRead = false;
        return;
    }

    if (notify && received) {
        emit readyRead();
    }
    syncReadNotifier();
    _pumpingRead = false;

    // 可读回调内调用过无通知排空并看到对端关闭时，由最外层读泵补发断开
    if (_peerEofSeen) {
        _peerEofSeen = false;
        const bool wasActive = _state != SocketState::UnconnectedState;
        teardownNotifiers();
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        _txBuf.clear();
        _txOffset = 0;
        _gracefulWaitPending = false;
        if (wasActive) {
            emit disconnected();
        }
    }
}

// 把用户态写缓冲送入内核
void PosixTcpSocket::pumpWriteBuffer()
{
    if (_pumpingWrite || _fd < 0 || _movedPending.load(std::memory_order_acquire)
        || _state == SocketState::ConnectingState || _state == SocketState::UnconnectedState) {
        return;
    }

    _pumpingWrite = true;
    qint64 written = 0;

    while (_txOffset < _txBuf.size()) {
        const char *data = _txBuf.constData() + _txOffset;
        const qint64 remaining = _txBuf.size() - _txOffset;
#ifdef _WIN32
        // Windows 分支：send 缓冲长度参数为 int；MSG_NOSIGNAL 无对应物按 0
        // 透传；错误经 netErrno 折回 errno 空间（尖兵报告 N6）
        const int amount = ::send(static_cast<SOCKET>(_fd), data,
                                  static_cast<int>(remaining), MSG_NOSIGNAL);
#else
        const ssize_t amount = ::send(_fd, data, static_cast<size_t>(remaining), MSG_NOSIGNAL);
#endif
        if (amount > 0) {
            _txOffset += amount;
            written += amount;
            continue;
        }
        if (amount == 0) {
            break;
        }
#ifdef _WIN32
        const int err = netErrno();
        if (err == EINTR) {
            continue;
        }
        if (err == EAGAIN) {
            break;
        }
        const int savedErrno = err;
#else
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        const int savedErrno = errno;
#endif
        _pumpingWrite = false;
        setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
        teardownNotifiers();
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        _txBuf.clear();
        _txOffset = 0;
        emit disconnected();
        return;
    }

    if (_txOffset >= _txBuf.size()) {
        _txBuf.clear();
        _txOffset = 0;
    }

    _pumpingWrite = false;
    syncWriteNotifier();

    if (written > 0) {
        emit bytesWritten(written);
    }

    if (_state == SocketState::ClosingState && _txBuf.isEmpty()) {
        const bool waitForPeer = _gracefulWaitPending;
        const int waitMs = _gracefulWaitMs;
        _gracefulWaitPending = false;
        _gracefulWaitMs = 0;

        if (waitForPeer && _fd >= 0) {
#ifdef _WIN32
            // Windows 分支：半关发送侧以 SD_SEND 表达（SHUT_WR 无对应物）
            ::shutdown(static_cast<SOCKET>(_fd), SD_SEND);
#else
            ::shutdown(_fd, SHUT_WR);
#endif
            waitForDisconnected(waitMs);
        }
        if (_fd >= 0) {
            finishGracefulClose();
        }
    }
}

// 完成连接建立
void PosixTcpSocket::finishConnect()
{
    _state = SocketState::ConnectedState;
    _lastError = SocketError::NoError;
    _errorText.clear();
    applyTcpNoDelay(_fd);
    ensureNotifiers();
    syncReadNotifier();
    emit connected();
    pumpWriteBuffer();
    syncWriteNotifier();
}

// 刷新读监听开关
void PosixTcpSocket::syncReadNotifier()
{
    if (_movedPending.load(std::memory_order_acquire)) {
        return;  // 线程移交窗口内不做开关同步，避免重新开启已停用的监听
    }
    if (_fd < 0 && !_watchActive) {
        return;  // 从未建立过监听（未连接）则无需同步
    }
    if (thread() != QThread::currentThread()) {
        return;  // 监听开关涉及无锁状态字段，仅归属线程允许同步
    }

    const bool active = _fd >= 0 && (_state == SocketState::ConnectedState
        || _state == SocketState::ClosingState);
    const bool saturated = _readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit;
    _readWatchWanted = active && !saturated;
    applyWatch();
}

// 刷新写监听开关
void PosixTcpSocket::syncWriteNotifier()
{
    if (_movedPending.load(std::memory_order_acquire)) {
        return;  // 线程移交窗口内不做开关同步，避免重新开启已停用的监听
    }
    if (_fd < 0 && !_watchActive) {
        return;  // 从未建立过监听（未连接）则无需同步
    }
    if (thread() != QThread::currentThread()) {
        return;  // 监听开关涉及无锁状态字段，仅归属线程允许同步
    }

    // 连接中的可写事件用于取得异步连接结果；连接建立后只在待写队列非空时开启
    _writeWatchWanted = _fd >= 0
        && (_state == SocketState::ConnectingState
            || (bytesToWrite() > 0 && (_state == SocketState::ConnectedState
                                       || _state == SocketState::ClosingState)));
    applyWatch();
}

// 完成优雅关闭
void PosixTcpSocket::finishGracefulClose()
{
    if (_fd >= 0) {
        // 关闭前取走内核中已到达的对端数据，避免带未读数据关闭触发 RST
        pumpReadBuffer(false);
    }
    if (_fd >= 0) {
#ifdef _WIN32
        // Windows 分支：半关发送侧以 SD_SEND 表达（SHUT_WR 无对应物）
        ::shutdown(static_cast<SOCKET>(_fd), SD_SEND);
#else
        ::shutdown(_fd, SHUT_WR);
#endif
    }
    teardownNotifiers();
    closeDescriptor();
    if (_state != SocketState::UnconnectedState) {
        _state = SocketState::UnconnectedState;
        // 本端主动关闭时把断开通知排到事件循环，让"发送后主动终结"的业务状态先落定
        QMetaObject::invokeMethod(this, [this]() {
            emit disconnected();
        }, Qt::QueuedConnection);
    }
}

// 用户态缓冲未读字节数
qint64 PosixTcpSocket::bufferedBytes() const
{
    return std::max<qint64>(0, _rxBuf.size() - _rxOffset);
}

// 压缩已消费的读缓冲
void PosixTcpSocket::compactReadBuffer()
{
    if (_rxOffset <= 0) {
        return;
    }
    if (_rxOffset == _rxBuf.size()) {
        _rxBuf.clear();
        _rxOffset = 0;
    } else if (_rxOffset >= kReadBufferCompactThreshold || _rxOffset * 2 >= _rxBuf.size()) {
        _rxBuf.remove(0, static_cast<int>(_rxOffset));
        _rxOffset = 0;
    }
}

}  // namespace gy::net
