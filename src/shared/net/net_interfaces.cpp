/**
* @file    net_interfaces.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   本机网卡与地址枚举实现
*
* 枚举只保留 IPv4 条目，与该应用现有发现与广播范围一致。
* 每个地址独立成项，调用方无需再处理一个接口多地址的嵌套结构。
*/

#include "net_interfaces.h"

#include <ifaddrs.h>
#include <net/if.h>

namespace gy::net {

// 枚举全部 IPv4 网卡信息
QList<NetInterface> listInterfaces()
{
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

}  // namespace gy::net
