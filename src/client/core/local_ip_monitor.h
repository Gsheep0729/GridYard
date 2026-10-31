/**
* @file    local_ip_monitor.h
* @version 8.2.1
* @date    2026-11-01
* @author  GridYard Team
* @brief   本机 IP 网络变化监听器
*
* 经 QNetworkInformation 监听链路可达性与传输介质变化，触发 ConfigManager
* 重新解析本机 IP，换网/IP 变化后邀请码与协调注册不再携带陈旧地址。
* 后端加载失败时静默降级为仅启动期取址（与既有行为一致），不产生额外依赖负担。
*/

#pragma once

#include <QObject>

class ConfigManager;
class QNetworkInformation;

class LocalIpMonitor : public QObject {
    Q_OBJECT

public:
    explicit LocalIpMonitor(ConfigManager *config, QObject *parent = nullptr);

private:
    // 链路状态或传输介质变化都可能伴随地址变化：统一重新解析本机 IP
    void refreshOnNetworkChanged();

    ConfigManager *_config = nullptr;      // 本机配置（持有 refreshLocalIp）
    QNetworkInformation *_info = nullptr;  // 网络状态信息（后端加载失败为空）
};
