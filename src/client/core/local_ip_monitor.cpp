/**
* @file    local_ip_monitor.cpp
* @version 8.2.1
* @date    2026-11-01
* @author  GridYard Team
* @brief   本机 IP 网络变化监听器实现
*/

#include "local_ip_monitor.h"

#include "config_manager.h"

#include <QDebug>
#include <QSocketNotifier>

#include <cstring>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

// 订阅链路状态与 IPv4 地址变化两组路由事件，经 QSocketNotifier 并入主线程事件循环
LocalIpMonitor::LocalIpMonitor(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
{
    _fd = ::socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (_fd < 0) {
        qDebug() << "LocalIpMonitor: netlink 套接字创建失败，本机 IP 仅启动期解析";
        return;
    }

    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;
    if (::bind(_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        qDebug() << "LocalIpMonitor: netlink 绑定失败，本机 IP 仅启动期解析";
        ::close(_fd);
        _fd = -1;
        return;
    }

    // 一次换网可能连发多条路由消息：通知只在读空队列后统一刷新一次，
    // refreshLocalIp 内部还会按地址实际变化合并，不产生重注册风暴
    _notifier = new QSocketNotifier{_fd, QSocketNotifier::Read, this};
    connect(_notifier, &QSocketNotifier::activated, this, [this]() {
        drainNetlink();
    });
    qDebug() << "LocalIpMonitor: netlink 路由监听已建立";
}

LocalIpMonitor::~LocalIpMonitor()
{
    if (_fd >= 0) {
        ::close(_fd);
    }
}

void LocalIpMonitor::drainNetlink()
{
    if (!_config) {
        return;
    }

    bool routeChanged = false;
    char buffer[4096];
    for (;;) {
        // NLMSG_NEXT 会前移长度游标，received 须可写且每轮重置
        ssize_t received = ::recv(_fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (received <= 0) {
            break;  // 队列已空（EAGAIN）或读失败，等下一次通知
        }
        // 逐条查看路由消息类型，只对链路启停与 IPv4 地址增删作出反应
        for (auto *header = reinterpret_cast<nlmsghdr *>(buffer);
             NLMSG_OK(header, static_cast<unsigned>(received));
             header = NLMSG_NEXT(header, received)) {
            if (header->nlmsg_type == RTM_NEWLINK || header->nlmsg_type == RTM_DELLINK
                || header->nlmsg_type == RTM_NEWADDR || header->nlmsg_type == RTM_DELADDR) {
                routeChanged = true;
            }
        }
    }

    if (routeChanged) {
        _config->refreshLocalIp();
    }
}
