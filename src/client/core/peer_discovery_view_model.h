/**
* @file    peer_discovery_view_model.h
* @version 6.6.2
* @date    2026-06-27
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型
*
* 仅向表现层暴露在线设备列表和刷新命令，隐藏 DiscoveryService 的
* 网络发现、端点查询和持久化事件等内部服务职责。
*
* Change Log:
* [v6.6.2] GY   2026-06-27
* * 新增设备发现 UI API 门面，避免 QML 直接依赖内部 Service
*/

#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class DiscoveryService;

class PeerDiscoveryViewModel : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)

public:
    explicit PeerDiscoveryViewModel(DiscoveryService *discovery, QObject *parent = nullptr);
    virtual ~PeerDiscoveryViewModel() override = default;

    PeerDiscoveryViewModel(const PeerDiscoveryViewModel &) = delete;
    PeerDiscoveryViewModel &operator=(const PeerDiscoveryViewModel &) = delete;

    // 获取 QML 可绑定的在线设备列表
    QVariantList peers() const;
    // 请求立即刷新设备发现
    Q_INVOKABLE void refresh();

signals:
    void peersChanged();
    void nodeDiscovered(const QString &deviceId);
    void nodeExpired(const QString &deviceId);

private:
    DiscoveryService *_discovery = nullptr;  // 内部设备发现服务
};
