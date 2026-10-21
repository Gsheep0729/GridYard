/**
* @file    ip_address.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   网络地址值类型
*
* 封装 IPv4 与 IPv6 地址，内部保存可直接传给系统调用的 sockaddr 结构。
* IPv4 映射的 IPv6 地址在解析和接收阶段统一换算为纯 IPv4，避免同一设备
* 因地址表示不同而重复入表。
*/

#pragma once

#include <QString>

#include <netinet/in.h>
#include <sys/socket.h>

namespace gy::net {

class IpAddress {
public:
    // 构造空地址
    IpAddress() = default;
    // 由 IPv4 网络字节序地址构造
    explicit IpAddress(in_addr address);
    // 由 IPv6 网络字节序地址构造
    explicit IpAddress(in6_addr address);

    // 解析点分十进制或 IPv6 文本，失败返回空地址
    static IpAddress fromString(const QString &text);
    // 由系统地址结构构造，自动识别地址族并归一化 IPv4 映射地址
    static IpAddress fromSockAddr(const sockaddr *address);
    // 任意 IPv4 地址
    static IpAddress any();
    // IPv6 任意地址
    static IpAddress anyIpv6();
    // IPv4 环回地址
    static IpAddress localHost();

    // 地址是否有效
    bool isValid() const;
    // 地址是否为空（与有效判定相反）
    bool isNull() const;
    // 是否为环回地址
    bool isLoopback() const;
    // 地址族，空地址返回 AF_UNSPEC
    int family() const;
    // 文本形式，IPv4 输出点分十进制，IPv6 输出压缩形式
    QString toString() const;

    // 取出 IPv4 网络字节序地址，非 IPv4 返回全零
    in_addr toInAddr() const;
    // 取系统地址结构指针，空地址返回 nullptr
    const sockaddr *sockAddr() const;
    // 系统地址结构长度，空地址返回 0
    socklen_t sockAddrLength() const;

private:
    sockaddr_storage _storage{};  // 系统地址结构
    bool _valid = false;          // 是否已成功赋值
};

}  // namespace gy::net
