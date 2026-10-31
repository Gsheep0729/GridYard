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
#include <QNetworkInformation>

// 监听链路变化触发本机 IP 重解析：后端不可用时保持启动期单次取址的既有行为
LocalIpMonitor::LocalIpMonitor(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
{
    if (!QNetworkInformation::loadDefaultBackend()) {
        qDebug() << "LocalIpMonitor: 无可用网络状态后端，本机 IP 仅启动期解析";
        return;
    }
    _info = QNetworkInformation::instance();
    if (!_info) {
        return;
    }
    // 可达性变化覆盖断网/重连与跨网段切换，介质变化覆盖 WiFi/有线互切；
    // refreshLocalIp 内部仅在地址实际变化时发 localIpChanged，同刻多次回调天然合并
    connect(_info, &QNetworkInformation::reachabilityChanged,
            this,  &LocalIpMonitor::refreshOnNetworkChanged);
    connect(_info, &QNetworkInformation::transportMediumChanged,
            this,  &LocalIpMonitor::refreshOnNetworkChanged);
}

void LocalIpMonitor::refreshOnNetworkChanged()
{
    if (_config) {
        _config->refreshLocalIp();
    }
}
