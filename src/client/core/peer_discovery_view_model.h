/**
* @file    peer_discovery_view_model.h
* @version 7.15.11
* @date    2026-10-03
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型
*
* 向表现层暴露在线设备和历史设备合并后的列表，隐藏 DiscoveryService
* 与本地设备目录的内部细节。
*
* Change Log:
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.15.3] GY   2026-10-03
* * 新增 selectedDeviceId 属性与 deviceById 查询，选中状态由视图模型持有
* [v6.7.0] GY   2026-06-28
* * 合并在线发现设备和本地历史设备目录
* [v6.6.2] GY   2026-06-27
* * 新增设备发现 UI API 门面，避免 QML 直接依赖内部 Service
*/

#pragma once

#include "history_records.h"

#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class DiscoveryService;
class LocalDataBroker;

class PeerDiscoveryViewModel : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId WRITE setSelectedDeviceId NOTIFY selectedDeviceIdChanged)

public:
    explicit PeerDiscoveryViewModel(DiscoveryService *discovery, QObject *parent = nullptr);
    virtual ~PeerDiscoveryViewModel() override = default;

    PeerDiscoveryViewModel(const PeerDiscoveryViewModel &) = delete;
    PeerDiscoveryViewModel &operator=(const PeerDiscoveryViewModel &) = delete;

    // 获取 QML 可绑定的在线和历史设备合并列表
    QVariantList peers() const;
    // 获取当前选中设备 ID（选择状态收编到视图模型，QML 不再手工复制）
    QString selectedDeviceId() const;
    // 更新选中设备
    void setSelectedDeviceId(const QString &deviceId);
    // 按设备 ID 查询展示信息（deviceName/ipAddress/isOnline），未命中返回空表
    Q_INVOKABLE QVariantMap deviceById(const QString &deviceId) const;
    // 请求立即刷新设备发现
    Q_INVOKABLE void refresh();
    // 请求刷新本地历史设备目录
    Q_INVOKABLE void refreshHistory();
    // 注入本地数据层入口
    void initDataBroker(LocalDataBroker *dataBroker);

signals:
    void peersChanged();
    void selectedDeviceIdChanged();
    void nodeDiscovered(const QString &deviceId);
    void nodeExpired(const QString &deviceId);

private:
    static QVariantMap peerRecordToVariant(const PeerRecord &record);

    DiscoveryService *_discovery = nullptr;  // 内部设备发现服务
    LocalDataBroker *_dataBroker = nullptr;  // 本地设备目录加载入口
    QVariantList _historyPeers;  // 已持久化的历史设备列表
    QString _selectedDeviceId;  // 当前选中设备 ID
};
