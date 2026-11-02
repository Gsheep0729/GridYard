/**
* @file    net_interfaces.cpp
* @version 8.2.1
* @date 2026-11-02
* @author  GridYard Team
* @brief   本机网卡与地址枚举实现
*
* 枚举只保留 IPv4 条目，与该应用现有发现与广播范围一致。
* 每个地址独立成项，调用方无需再处理一个接口多地址的嵌套结构。
*/

#include "net_interfaces.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <vector>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

namespace gy::net {

// 枚举全部 IPv4 网卡信息
QList<NetInterface> listInterfaces()
{
#ifdef _WIN32
    // Windows 分支：getifaddrs 无对应物，经 GetAdaptersAddresses（iphlpapi，
    // 尖兵报告 N8）枚举 IPv4 单播地址，每个地址独立成项。Winsock 不提供
    // 广播地址属性，广播地址由地址与前缀长度推得；广播能力按介质类型给足
    // （回环与隧道/点对点在 Linux 侧无 IFF_BROADCAST，此处同样收窄）
    QList<NetInterface> result;

    ULONG size = 15 * 1024;  // MSDN 建议的初始缓冲量，不足时按回填值重试
    std::vector<char> buffer(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST
        | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;

    ULONG status = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt) {
        auto *adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());
        status = ::GetAdaptersAddresses(AF_INET, flags, nullptr, adapters, &size);
        if (status == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(size);
        }
    }
    if (status != NO_ERROR) {
        return result;  // 不可枚举（无数据/参数错）：与 getifaddrs 失败同样返回空表
    }

    const auto *adapterBase = reinterpret_cast<const IP_ADAPTER_ADDRESSES *>(buffer.data());
    for (const IP_ADAPTER_ADDRESSES *adapter = adapterBase; adapter; adapter = adapter->Next) {
        for (const IP_ADAPTER_UNICAST_ADDRESS *unicast = adapter->FirstUnicastAddress;
             unicast; unicast = unicast->Next) {
            const sockaddr *socketAddress = unicast->Address.lpSockaddr;
            if (!socketAddress || socketAddress->sa_family != AF_INET) {
                continue;
            }

            NetInterface info;
            info.name = QString::fromWCharArray(adapter->FriendlyName);
            info.isUp = adapter->OperStatus == IfOperStatusUp;
            info.isLoopback = adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK;
            info.canBroadcast = !info.isLoopback && adapter->IfType != IF_TYPE_TUNNEL
                && adapter->IfType != IF_TYPE_PPP;
            info.address = IpAddress::fromSockAddr(socketAddress);

            if (info.canBroadcast && info.address.isValid()) {
                const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(socketAddress);
                const quint32 prefixLength = unicast->OnLinkPrefixLength;
                // 前缀长度取值 0~32，移位在 64 位整型上完成避免未定义行为
                const quint32 mask = static_cast<quint32>(0xffffffffull << (32 - prefixLength));
                const quint32 broadcastHost = ntohl(ipv4->sin_addr.s_addr) | ~mask;
                in_addr broadcastAddress{};
                broadcastAddress.s_addr = htonl(broadcastHost);
                info.broadcast = IpAddress(broadcastAddress);
            }

            result.append(info);
        }
    }
    return result;
#else
    QList<NetInterface> result;

    ifaddrs *interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) {
        return result;
    }

    for (const ifaddrs *entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        NetInterface info;
        info.name = QString::fromUtf8(entry->ifa_name ? entry->ifa_name : "");
        info.isUp = (entry->ifa_flags & IFF_UP) != 0;
        info.isLoopback = (entry->ifa_flags & IFF_LOOPBACK) != 0;
        info.canBroadcast = (entry->ifa_flags & IFF_BROADCAST) != 0;
        info.address = IpAddress::fromSockAddr(entry->ifa_addr);

        if (info.canBroadcast && entry->ifa_broadaddr) {
            info.broadcast = IpAddress::fromSockAddr(entry->ifa_broadaddr);
        }

        result.append(info);
    }

    ::freeifaddrs(interfaces);
    return result;
#endif
}

// 枚举全部非回环 IPv4 地址
QList<IpAddress> listIpv4Addresses()
{
    QList<IpAddress> result;
    const QList<NetInterface> interfaces = listInterfaces();
    for (const NetInterface &interface : interfaces) {
        if (!interface.isLoopback && interface.address.isValid()) {
            result.append(interface.address);
        }
    }
    return result;
}

// 判断接口名是否命中虚拟网卡名单
bool isVirtualInterfaceName(const QString &name)
{
    // 名单保守取常见容器/虚拟化网口前缀（C-04）：docker 网桥与 veth 对、
    // libvirt 网桥、VMware/VirtualBox 宿主网卡。改名网口或小众平台宁漏勿误，
    // VPN TUN（Meta、tun*/wg*）刻意不收——那是多子网直连的真实路径
    static const QStringList kVirtualPrefixes = {
        QStringLiteral("docker"), QStringLiteral("br-"),    QStringLiteral("veth"),
        QStringLiteral("virbr"),  QStringLiteral("vmnet"), QStringLiteral("vboxnet"),
    };
    for (const QString &prefix : kVirtualPrefixes) {
        if (name.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

}  // namespace gy::net
