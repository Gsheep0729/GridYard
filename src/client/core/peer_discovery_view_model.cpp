/**
* @file    peer_discovery_view_model.cpp
* @version 6.6.2
* @date    2026-06-27
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型实现
*
* Change Log:
* [v6.6.2] GY   2026-06-27
* * 新增设备发现 UI API 门面
*/

#include "peer_discovery_view_model.h"

#include "discovery_service.h"

// 构造函数
PeerDiscoveryViewModel::PeerDiscoveryViewModel(DiscoveryService *discovery, QObject *parent)
    : QObject{parent}
    , _discovery{discovery}
{
    if (!_discovery) {
        return;  // 发现服务为空时跳过信号连接，防御性编程
    }

    // 将内部发现服务的信号转发给 QML 视图模型
    connect(_discovery, &DiscoveryService::peersChanged,
            this, &PeerDiscoveryViewModel::peersChanged);
    // 新设备上线时通知表现层播放发现动画
    connect(_discovery, &DiscoveryService::nodeDiscovered,
            this, &PeerDiscoveryViewModel::nodeDiscovered);
    // 设备离线时通知表现层更新在线状态指示
    connect(_discovery, &DiscoveryService::nodeExpired,
            this, &PeerDiscoveryViewModel::nodeExpired);
}

// 获取 QML 可绑定的在线设备列表
QVariantList PeerDiscoveryViewModel::peers() const
{
    return _discovery ? _discovery->peers() : QVariantList{};
}

// 请求立即刷新设备发现
void PeerDiscoveryViewModel::refresh()
{
    if (_discovery) {
        _discovery->refresh();
    }
}
