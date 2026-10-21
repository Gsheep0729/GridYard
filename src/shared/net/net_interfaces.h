/**
* @file    net_interfaces.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   本机网卡与地址枚举
*
* 基于 getifaddrs 读取内核接口信息，按 IPv4 地址逐项返回。
* 广播地址直接取接口属性，不自行根据掩码推导，保持与系统配置一致。
*/

#pragma once

#include <QList>
#include <QString>

#include "ip_address.h"

namespace gy::net {

// 单个网卡的 IPv4 信息
struct NetInterface {
    QString name;               // 内核接口名
    bool isUp = false;          // 接口已启用
    bool isLoopback = false;    // 回环接口
    bool canBroadcast = false;   // 接口支持广播
    IpAddress address;          // 该接口的 IPv4 地址
    IpAddress broadcast;        // 接口广播地址，不支持时为空
};

// 枚举全部 IPv4 网卡信息
QList<NetInterface> listInterfaces();
// 枚举全部非回环 IPv4 地址
QList<IpAddress> listIpv4Addresses();

}  // namespace gy::net
