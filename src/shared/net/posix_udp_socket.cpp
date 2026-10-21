/**
* @file    posix_udp_socket.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   原生 UDP 套接字实现
*
* 读取时先用 MSG_PEEK 与 MSG_TRUNC 取得完整报文长度，再按实际长度分配，
* 超长报文不会被静默截断。报文在用户态队列中保持到达顺序。
*/

#include "posix_udp_socket.h"

#include "posix_net_error.h"

#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gy::net {

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
    _fd = ::socket(family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (_fd < 0) {
        _errorText = errorMessageFromErrno(errno);
        return false;
    }

    if (flags.testFlag(ShareAddress) || flags.testFlag(ReuseAddressHint)) {
        const int reuse = 1;
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }
    if (flags.testFlag(ReuseAddressHint)) {
        const int reusePort = 1;
        ::setsockopt(_fd, SOL_SOCKET, SO_REUSEPORT, &reusePort, sizeof(reusePort));
    }

    const int broadcast = 1;
    ::setsockopt(_fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

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
        _errorText = errorMessageFromErrno(errno);
        closeDescriptor();
        return false;
    }

    _notifier = new QSocketNotifier(_fd, QSocketNotifier::Read, this);
    connect(_notifier, &QSocketNotifier::activated, this, [this]() {
        onReadActivated();
    });
    _errorText.clear();
    return true;
}

// 停止接收并关闭描述符
void PosixUdpSocket::close()
{
    if (_notifier) {
        _notifier->setEnabled(false);
        delete _notifier;
        _notifier = nullptr;
    }
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
    int pending = 0;
    return ::ioctl(_fd, FIONREAD, &pending) == 0 && pending > 0;
}

// 下一个数据报长度
qint64 PosixUdpSocket::pendingDatagramSize() const
{
    if (_datagrams.isEmpty() && _fd >= 0) {
        char byte = 0;
        const ssize_t size = ::recvfrom(_fd, &byte, 0, MSG_PEEK | MSG_TRUNC, nullptr, nullptr);
        if (size >= 0) {
            return size;
        }
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
}

// 关闭描述符
void PosixUdpSocket::closeDescriptor()
{
    if (_fd >= 0) {
        ::close(_fd);
        _fd = -1;
    }
}

}  // namespace gy::net
