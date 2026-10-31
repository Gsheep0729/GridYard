/**
* @file    local_ip_monitor.h
* @version 8.2.1
* @date    2026-11-01
* @author  GridYard Team
* @brief   本机 IP 网络变化监听器
*
* 经 netlink 路由套接字监听链路与 IPv4 地址变化，触发 ConfigManager 重新
* 解析本机 IP，换网/IP 变化后邀请码与协调注册不再携带陈旧地址。实现走
* Linux 原生 socket 加 QSocketNotifier，不引入 Qt 网络栈依赖；套接字不可用
* 时静默降级为仅启动期取址（与既有行为一致）。
*/

#pragma once

#include <QObject>

class ConfigManager;
class QSocketNotifier;

class LocalIpMonitor : public QObject {
    Q_OBJECT

public:
    explicit LocalIpMonitor(ConfigManager *config, QObject *parent = nullptr);
    ~LocalIpMonitor() override;

private:
    // 读空 netlink 队列，出现链路或 IPv4 地址事件时重新解析本机 IP
    void drainNetlink();

    ConfigManager *_config = nullptr;          // 本机配置（持有 refreshLocalIp）
    QSocketNotifier *_notifier = nullptr;      // netlink 可读通知（失败为空）
    int _fd = -1;                              // netlink 路由套接字
};
