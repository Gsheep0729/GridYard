/**
* @file    ip_address.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   网络地址值类型实现
*
* 解析与格式化全部走 inet_pton / inet_ntop，不依赖 Qt 网络模块。
* 接收侧遇到 IPv4 映射地址时改存 AF_INET，业务层看到的地址字符串始终稳定。
*/

#include "ip_address.h"

#include <QByteArray>

#include <arpa/inet.h>
#include <cstring>

namespace gy::net {

namespace {

// 将 IPv6 中的 IPv4 映射地址搬入 IPv4 结构
bool extractMappedIpv4(const in6_addr &source, in_addr *target)
{
    if (!IN6_IS_ADDR_V4MAPPED(&source)) {
        return false;
    }
    std::memcpy(target, &source.s6_addr[12], sizeof(in_addr));
    return true;
}

}  // namespace

// 构造空地址
IpAddress::IpAddress(in_addr address)
{
    auto *storage = reinterpret_cast<sockaddr_in *>(&_storage);
    storage->sin_family = AF_INET;
    storage->sin_addr = address;
    _valid = true;
}

// 由 IPv6 网络字节序地址构造
IpAddress::IpAddress(in6_addr address)
{
    in_addr mapped{};
    if (extractMappedIpv4(address, &mapped)) {
        auto *storage = reinterpret_cast<sockaddr_in *>(&_storage);
        storage->sin_family = AF_INET;
        storage->sin_addr = mapped;
        _valid = true;
        return;
    }

    auto *storage = reinterpret_cast<sockaddr_in6 *>(&_storage);
    storage->sin6_family = AF_INET6;
    storage->sin6_addr = address;
    _valid = true;
}

// 解析文本地址
IpAddress IpAddress::fromString(const QString &text)
{
    const QByteArray raw = text.trimmed().toUtf8();
    if (raw.isEmpty()) {
        return {};
    }

    in_addr ipv4{};
    if (::inet_pton(AF_INET, raw.constData(), &ipv4) == 1) {
        return IpAddress(ipv4);
    }

    in6_addr ipv6{};
    if (::inet_pton(AF_INET6, raw.constData(), &ipv6) == 1) {
        return IpAddress(ipv6);
    }

    return {};
}

// 由系统地址结构构造
IpAddress IpAddress::fromSockAddr(const sockaddr *address)
{
    if (!address) {
        return {};
    }

    if (address->sa_family == AF_INET) {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(address);
        return IpAddress(ipv4->sin_addr);
    }

    if (address->sa_family == AF_INET6) {
        const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(address);
        return IpAddress(ipv6->sin6_addr);
    }

    return {};
}

// 任意 IPv4 地址
IpAddress IpAddress::any()
{
    in_addr address{};
    address.s_addr = htonl(INADDR_ANY);
    return IpAddress(address);
}

// IPv6 任意地址
IpAddress IpAddress::anyIpv6()
{
    in6_addr address{};
    address = in6addr_any;
    return IpAddress(address);
}

// IPv4 环回地址
IpAddress IpAddress::localHost()
{
    in_addr address{};
    address.s_addr = htonl(INADDR_LOOPBACK);
    return IpAddress(address);
}

// 地址是否有效
bool IpAddress::isValid() const
{
    return _valid;
}

// 地址是否为空
bool IpAddress::isNull() const
{
    return !_valid;
}

// 是否为环回地址
bool IpAddress::isLoopback() const
{
    if (!_valid) {
        return false;
    }

    if (_storage.ss_family == AF_INET) {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(&_storage);
        return (ntohl(ipv4->sin_addr.s_addr) >> 24) == 127;
    }

    if (_storage.ss_family == AF_INET6) {
        const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(&_storage);
        return IN6_IS_ADDR_LOOPBACK(&ipv6->sin6_addr);
    }

    return false;
}

// 地址族
int IpAddress::family() const
{
    return _valid ? _storage.ss_family : AF_UNSPEC;
}

// 文本形式
QString IpAddress::toString() const
{
    if (!_valid) {
        return {};
    }

    char text[INET6_ADDRSTRLEN] = {};
    const void *source = nullptr;
    if (_storage.ss_family == AF_INET) {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(&_storage);
        source = &ipv4->sin_addr;
        if (!::inet_ntop(AF_INET, source, text, sizeof(text))) {
            return {};
        }
    } else if (_storage.ss_family == AF_INET6) {
        const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(&_storage);
        source = &ipv6->sin6_addr;
        if (!::inet_ntop(AF_INET6, source, text, sizeof(text))) {
            return {};
        }
    } else {
        return {};
    }

    return QString::fromLatin1(text);
}

// 取出 IPv4 网络字节序地址
in_addr IpAddress::toInAddr() const
{
    in_addr address{};
    if (_valid && _storage.ss_family == AF_INET) {
        const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(&_storage);
        address = ipv4->sin_addr;
    }
    return address;
}

// 取系统地址结构指针
const sockaddr *IpAddress::sockAddr() const
{
    return _valid ? reinterpret_cast<const sockaddr *>(&_storage) : nullptr;
}

// 系统地址结构长度
socklen_t IpAddress::sockAddrLength() const
{
    if (!_valid) {
        return 0;
    }
    return _storage.ss_family == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
}

}  // namespace gy::net
