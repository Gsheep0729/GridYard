/**
* @file    posix_tcp_socket.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   原生 TCP 套接字实现
*
* 所有系统调用均为非阻塞形式，可读可写事件由 QSocketNotifier 驱动。
* 发送统一带 MSG_NOSIGNAL，短写与 EAGAIN 留在用户态写队列，EINTR 原处重试。
*/

#include "posix_tcp_socket.h"

#include "posix_net_error.h"

#include <QMetaType>
#include <QSocketNotifier>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gy::net {

namespace {

constexpr qint64 kReceiveChunkBytes = 64 * 1024;          // 单次读取块大小
constexpr int kPollSliceMs = 100;                         // 阻塞等待切片，便于及时响应退出
constexpr qint64 kReadBufferCompactThreshold = 1024 * 1024;  // 读缓冲消费过半后的压缩门槛

// 给描述符设置非阻塞与关闭执行标志
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

// 开启 TCP_NODELAY，失败不中断连接
void applyTcpNoDelay(int fd)
{
    const int enabled = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
}

// 计算本轮 poll 的等待时长
int pollSliceMs(int remainingMs)
{
    if (remainingMs < 0) {
        return kPollSliceMs;
    }
    return std::min(remainingMs, kPollSliceMs);
}

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
        const int savedErrno = errno;
        ::close(descriptor);
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

// 移交前停用通知器
void PosixTcpSocket::prepareThreadMove()
{
    _movedPending.store(true, std::memory_order_release);
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
    }
    if (_writeNotifier) {
        _writeNotifier->setEnabled(false);
    }
}

// 在目标线程重新启用通知器
void PosixTcpSocket::finishThreadMove()
{
    _movedPending.store(false, std::memory_order_release);
    if (!_readNotifier && _fd >= 0) {
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
    _fd = ::socket(family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (_fd < 0) {
        const int savedErrno = errno;
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

    if (errno != EINPROGRESS) {
        const int savedErrno = errno;
        closeDescriptor();
        _state = SocketState::UnconnectedState;
        setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
        return;
    }

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
        int pending = 0;
        if (::ioctl(_fd, FIONREAD, &pending) == 0 && pending > 0) {
            total += pending;
        }
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

        pollfd descriptor{};
        descriptor.fd = _fd;
        descriptor.events = POLLOUT;
        const int ready = ::poll(&descriptor, 1, pollSliceMs(remaining));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int savedErrno = errno;
            setError(socketErrorFromErrno(savedErrno), errorMessageFromErrno(savedErrno));
            teardownNotifiers();
            closeDescriptor();
            _state = SocketState::UnconnectedState;
            return false;
        }
        if (ready == 0) {
            continue;
        }

        int socketError = 0;
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) != 0) {
            socketError = errno;
        }

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

    using Clock = std::chrono::steady_clock;
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(std::max(0, msecs));

    while (true) {
        const int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        if (remaining <= 0 && msecs >= 0) {
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = _fd;
        descriptor.events = POLLIN;
        const int ready = ::poll(&descriptor, 1, pollSliceMs(remaining));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ready == 0) {
            continue;
        }

        pumpReadBuffer(true);
        return bytesAvailable() > 0 || _state != SocketState::ConnectedState;
    }
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

        pollfd descriptor{};
        descriptor.fd = _fd;
        descriptor.events = POLLOUT;
        const int ready = ::poll(&descriptor, 1, pollSliceMs(remaining));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ready == 0) {
            continue;
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

        pollfd descriptor{};
        descriptor.fd = _fd;
        descriptor.events = POLLIN;
        const int ready = ::poll(&descriptor, 1, pollSliceMs(remaining));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (ready == 0) {
            continue;
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
    ::shutdown(_fd, SHUT_WR);

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
        return ::setsockopt(_fd, SOL_SOCKET, SO_SNDBUF, &intValue, sizeof(intValue)) == 0;
    case ReceiveBufferSizeOption:
        return ::setsockopt(_fd, SOL_SOCKET, SO_RCVBUF, &intValue, sizeof(intValue)) == 0;
    case LowDelayOption: {
        const int enabled = intValue != 0 ? 1 : 0;
        return ::setsockopt(_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) == 0;
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
        socklen_t errorLength = sizeof(socketError);
        if (::getsockopt(_fd, SOL_SOCKET, SO_ERROR, &socketError, &errorLength) != 0) {
            socketError = errno;
        }

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

// 创建事件通知器
void PosixTcpSocket::ensureNotifiers()
{
    if (_fd < 0) {
        return;
    }

    if (!_readNotifier) {
        _readNotifier = new QSocketNotifier(_fd, QSocketNotifier::Read, this);
        connect(_readNotifier, &QSocketNotifier::activated, this, [this]() {
            onReadActivated();
        });
    }
    if (!_writeNotifier) {
        _writeNotifier = new QSocketNotifier(_fd, QSocketNotifier::Write, this);
        _writeNotifier->setEnabled(false);
        connect(_writeNotifier, &QSocketNotifier::activated, this, [this]() {
            onWriteActivated();
        });
    }
}

// 销毁事件通知器
void PosixTcpSocket::teardownNotifiers()
{
    if (_readNotifier) {
        _readNotifier->setEnabled(false);
        delete _readNotifier;
        _readNotifier = nullptr;
    }
    if (_writeNotifier) {
        _writeNotifier->setEnabled(false);
        delete _writeNotifier;
        _writeNotifier = nullptr;
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
        ::close(_fd);
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
            const ssize_t amount = ::recv(_fd, chunk, sizeof(chunk), 0);
            if (amount > 0) {
                _rxBuf.append(chunk, static_cast<int>(amount));
                continue;
            }
            if (amount == 0) {
                _peerEofSeen = true;
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
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
        const ssize_t amount = ::recv(_fd, chunk, sizeof(chunk), 0);
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

        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        const int savedErrno = errno;
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
        const ssize_t amount = ::send(_fd, data, static_cast<size_t>(remaining), MSG_NOSIGNAL);
        if (amount > 0) {
            _txOffset += amount;
            written += amount;
            continue;
        }
        if (amount == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        const int savedErrno = errno;
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
            ::shutdown(_fd, SHUT_WR);
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

// 刷新读通知器开关
void PosixTcpSocket::syncReadNotifier()
{
    if (!_readNotifier || _movedPending.load(std::memory_order_acquire)) {
        return;
    }
    if (_readNotifier->thread() != QThread::currentThread()) {
        return;
    }

    const bool active = _fd >= 0 && (_state == SocketState::ConnectedState
        || _state == SocketState::ClosingState);
    const bool saturated = _readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit;
    _readNotifier->setEnabled(active && !saturated);
}

// 刷新写通知器开关
void PosixTcpSocket::syncWriteNotifier()
{
    if (!_writeNotifier || _movedPending.load(std::memory_order_acquire)) {
        return;
    }
    if (_writeNotifier->thread() != QThread::currentThread()) {
        return;
    }

    // 连接中的可写事件用于取得异步连接结果；连接建立后只在待写队列非空时开启
    const bool active = _fd >= 0
        && (_state == SocketState::ConnectingState
            || (bytesToWrite() > 0 && (_state == SocketState::ConnectedState
                                       || _state == SocketState::ClosingState)));
    _writeNotifier->setEnabled(active);
}

// 完成优雅关闭
void PosixTcpSocket::finishGracefulClose()
{
    if (_fd >= 0) {
        // 关闭前取走内核中已到达的对端数据，避免带未读数据关闭触发 RST
        pumpReadBuffer(false);
    }
    if (_fd >= 0) {
        ::shutdown(_fd, SHUT_WR);
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
