/**
* @file    tcp_socket_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 POSIX TCP 连接核心实现
*
* 所有系统调用均为非阻塞形式：recv 循环读入用户缓冲（受读上限约束，
* 饱和后停读形成 TCP 背压），send 带 MSG_NOSIGNAL 且 EAGAIN 时余量
* 留在用户态写队列由可写事件续传。回调契约：业务回调返回后对象才可
* 析构，所有成员访问在触发回调之前完成。
*/

#include "core/tcp_socket_core.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

namespace gy::net {

namespace {

constexpr size_t kReceiveChunkBytes = 64 * 1024;  // 单次读取块大小

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

// 把套接字地址格式化为文本：IPv4 映射的 IPv6 地址归一化为点分十进制
std::string formatPeerAddress(const sockaddr *address, socklen_t length)
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
    // 双栈监听接入的 IPv4 客户端呈现为 ::ffff:a.b.c.d，剥掉映射前缀
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
TcpSocketCore::TcpSocketCore(NetReactor &reactor)
    : _reactor{reactor}
{
}

// 析构函数：注销监听并关闭描述符
TcpSocketCore::~TcpSocketCore()
{
    if (_watchActive && _fd >= 0) {
        _reactor.unwatchNow(_fd, _generation);
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

// 接管已连接描述符
bool TcpSocketCore::adopt(int descriptor)
{
    if (descriptor < 0) {
        return false;
    }
    if (_watchActive && _fd >= 0) {
        _reactor.unwatchNow(_fd, _generation);
        _watchActive = false;
    }
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }

    if (!configureDescriptor(descriptor)) {
        ::close(descriptor);
        return false;
    }

    _fd = descriptor;
    applyTcpNoDelay(_fd);
    ++_generation;
    _rxBuf.clear();
    _rxOffset = 0;
    _txBuf.clear();
    _txOffset = 0;
    _gracefulClosePending = false;
    _readWanted = true;
    _writeWanted = false;

    const int watchedFd = _fd;
    const uint64_t generation = _generation;
    _reactor.watch(watchedFd, static_cast<uint32_t>(Event::kReadable), generation,
                   [this, watchedFd, generation](int fd, uint32_t events) {
                       if (fd != watchedFd) {
                           return;
                       }
                       handleEvents(events);
                   });
    _watchActive = true;
    return true;
}

// 设置读缓冲上限
void TcpSocketCore::setReadBufferLimit(int64_t bytes)
{
    _readBufferLimit = std::max<int64_t>(0, bytes);
    syncWatch();
}

// 可读字节数（用户缓冲与内核待读之和）
int64_t TcpSocketCore::bytesAvailable() const
{
    int64_t total = bufferedBytes();
    if (_fd >= 0) {
        int pending = 0;
        if (::ioctl(_fd, FIONREAD, &pending) == 0 && pending > 0) {
            total += pending;
        }
    }
    return total;
}

// 待写字节数
int64_t TcpSocketCore::bytesToWrite() const
{
    if (_txOffset >= _txBuf.size()) {
        return 0;
    }
    return static_cast<int64_t>(_txBuf.size() - _txOffset);
}

// 取走用户缓冲全部数据
std::vector<uint8_t> TcpSocketCore::readAll()
{
    if (bufferedBytes() <= 0) {
        _rxBuf.clear();
        _rxOffset = 0;
        syncWatch();
        return {};
    }
    std::vector<uint8_t> data(_rxBuf.begin() + static_cast<ptrdiff_t>(_rxOffset), _rxBuf.end());
    _rxBuf.clear();
    _rxOffset = 0;
    syncWatch();
    return data;
}

// 追加写数据（向量形式）
void TcpSocketCore::write(const std::vector<uint8_t> &data)
{
    if (data.empty()) {
        return;
    }
    write(data.data(), data.size());
}

// 追加写数据：先尝试直写内核，余量入队
void TcpSocketCore::write(const uint8_t *data, size_t length)
{
    if (!data || length == 0 || _fd < 0) {
        return;
    }
    _txBuf.insert(_txBuf.end(), data, data + length);
    pumpWrite();
}

// 优雅关闭：写队列排空后关闭
void TcpSocketCore::disconnectFromHost()
{
    if (_fd < 0) {
        return;
    }
    _gracefulClosePending = true;
    pumpWrite();
    if (_fd >= 0 && bytesToWrite() == 0) {
        finishClose(SocketCloseReason::kLocalClose);
    }
}

// 立即关闭
void TcpSocketCore::abort()
{
    if (_fd < 0) {
        return;
    }
    finishClose(SocketCloseReason::kLocalClose);
}

// 对端地址文本
std::string TcpSocketCore::peerAddressText() const
{
    if (_fd < 0) {
        return {};
    }
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getpeername(_fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        return {};
    }
    return formatPeerAddress(reinterpret_cast<const sockaddr *>(&address), length);
}

// Reactor 事件入口
void TcpSocketCore::handleEvents(uint32_t events)
{
    if (_fd < 0 || _inCallback) {
        return;
    }
    // 可读与挂断统一走读泵收尾（EOF 表现为可读后 recv 返回 0）
    if (events & (static_cast<uint32_t>(Event::kReadable)
                  | static_cast<uint32_t>(Event::kHangup))) {
        pumpRead();
    }
    if (_fd < 0) {
        return;  // 读泵已终结连接
    }
    if (events & (static_cast<uint32_t>(Event::kWritable)
                  | static_cast<uint32_t>(Event::kError))) {
        pumpWrite();
    }
}

// 从内核读入用户缓冲
void TcpSocketCore::pumpRead()
{
    if (_fd < 0) {
        return;
    }

    bool received = false;
    while (true) {
        if (_readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit) {
            break;  // 缓冲饱和：停读形成背压，消费后由 syncWatch 恢复
        }
        uint8_t chunk[kReceiveChunkBytes];
        const ssize_t amount = ::recv(_fd, chunk, sizeof(chunk), 0);
        if (amount > 0) {
            _rxBuf.insert(_rxBuf.end(), chunk, chunk + amount);
            received = true;
            continue;
        }
        if (amount == 0) {
            finishClose(SocketCloseReason::kPeerClosed);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        // 读错误与 EOF 分开归因，业务层据此区分日志与清理路径
        ErrorCallback onError = _onError;
        if (onError) {
            onError(std::string("recv 失败: ") + std::strerror(errno));
        }
        finishClose(SocketCloseReason::kError);
        return;
    }

    if (received) {
        ReadCallback onRead = _onRead;
        if (onRead) {
            _inCallback = true;
            onRead();
            _inCallback = false;
        }
    }
    if (_fd >= 0) {
        syncWatch();
    }
}

// 把用户态写队列送入内核
void TcpSocketCore::pumpWrite()
{
    if (_fd < 0 || _inCallback) {
        return;
    }

    int64_t written = 0;
    while (_txOffset < _txBuf.size()) {
        const uint8_t *data = _txBuf.data() + _txOffset;
        const size_t remaining = _txBuf.size() - _txOffset;
        const ssize_t amount = ::send(_fd, data, remaining, MSG_NOSIGNAL);
        if (amount > 0) {
            _txOffset += static_cast<size_t>(amount);
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
        ErrorCallback onError = _onError;
        if (onError) {
            onError(std::string("send 失败: ") + std::strerror(errno));
        }
        finishClose(SocketCloseReason::kError);
        return;
    }

    if (_txOffset >= _txBuf.size()) {
        _txBuf.clear();
        _txOffset = 0;
    }

    if (_fd >= 0) {
        syncWatch();
    }

    if (written > 0) {
        WrittenCallback onWritten = _onWritten;
        if (onWritten) {
            _inCallback = true;
            onWritten(written);
            _inCallback = false;
        }
    }

    // 写队列排空且挂起了优雅关闭：此刻完成关闭
    if (_fd >= 0 && _gracefulClosePending && bytesToWrite() == 0) {
        finishClose(SocketCloseReason::kLocalClose);
    }
}

// 刷新 Reactor 监听
void TcpSocketCore::syncWatch()
{
    if (_fd < 0 || !_watchActive) {
        return;
    }
    const bool saturated = _readBufferLimit > 0 && bufferedBytes() >= _readBufferLimit;
    _readWanted = !saturated;
    _writeWanted = bytesToWrite() > 0;

    uint32_t events = 0;
    if (_readWanted) {
        events |= static_cast<uint32_t>(Event::kReadable);
    }
    if (_writeWanted) {
        events |= static_cast<uint32_t>(Event::kWritable);
    }

    // 同一 fd 的再次 watch 即更新条目；Reactor 线程内即时生效
    const int watchedFd = _fd;
    const uint64_t generation = _generation;
    if (events != 0) {
        _reactor.watch(watchedFd, events, generation,
                       [this, watchedFd, generation](int fd, uint32_t revents) {
                           if (fd != watchedFd) {
                               return;
                           }
                           handleEvents(revents);
                       });
    } else {
        _reactor.unwatchNow(watchedFd, generation);
        _watchActive = false;
    }
}

// 统一收尾
void TcpSocketCore::finishClose(SocketCloseReason reason)
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
    _rxBuf.clear();
    _rxOffset = 0;
    _txBuf.clear();
    _txOffset = 0;
    _readWanted = false;
    _writeWanted = false;
    _gracefulClosePending = false;

    // 回调最后触发且回调后零成员访问：持有方允许在回调内立即析构本对象
    CloseCallback onClose = std::move(_onClose);
    if (onClose) {
        onClose(reason);
    }
}

// 用户缓冲未读字节数
int64_t TcpSocketCore::bufferedBytes() const
{
    if (_rxOffset >= _rxBuf.size()) {
        return 0;
    }
    return static_cast<int64_t>(_rxBuf.size() - _rxOffset);
}

}  // namespace gy::net
