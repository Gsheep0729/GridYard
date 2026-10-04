/**
* @file    peer_discovery_view_model.h
* @version 7.17.2
* @date    2026-10-04
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型
*
* 向表现层暴露在线设备和历史设备合并后的列表，隐藏 DiscoveryService
* 与本地设备目录的内部细节。
*
* Change Log:
* [v7.17.2] GY   2026-10-04
* * 合并列表新增 hiddenPeers 只读属性，供设置页展示隐藏设备
* [v7.17.1] GY   2026-10-04
* * 新增置顶、隐藏、删除与自动恢复入口，管理状态由数据库恢复
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
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
#include <QSet>
#include <QVariantMap>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class DiscoveryService;
class LocalDataBroker;
struct PeerInfo;

class PeerDiscoveryViewModel : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)
    // 隐藏态设备列表（设备名/IP/最后活跃），供设置页"已隐藏设备"区块恢复显示
    Q_PROPERTY(QVariantList hiddenPeers READ hiddenPeers NOTIFY peersChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId WRITE setSelectedDeviceId NOTIFY selectedDeviceIdChanged)

public:
    explicit PeerDiscoveryViewModel(DiscoveryService *discovery, QObject *parent = nullptr);
    virtual ~PeerDiscoveryViewModel() override = default;

    PeerDiscoveryViewModel(const PeerDiscoveryViewModel &) = delete;
    PeerDiscoveryViewModel &operator=(const PeerDiscoveryViewModel &) = delete;

    // 获取 QML 可绑定的在线和历史设备合并列表
    QVariantList peers() const;
    // 获取隐藏态设备列表，设置页"已隐藏设备"区块据此展示恢复入口
    QVariantList hiddenPeers() const;
    // 获取当前选中设备 ID（选择状态收编到视图模型，QML 不再手工复制）
    QString selectedDeviceId() const;
    // 更新选中设备
    void setSelectedDeviceId(const QString &deviceId);
    // 按设备 ID 查询展示信息（deviceName/ipAddress/isOnline/pinned），未命中返回空表
    Q_INVOKABLE QVariantMap deviceById(const QString &deviceId) const;
    // 请求立即刷新设备发现
    Q_INVOKABLE void refresh();
    // 请求刷新本地历史设备目录
    Q_INVOKABLE void refreshHistory();
    // 置顶或取消置顶指定设备（落库成功后刷新内存状态）
    Q_INVOKABLE void setDevicePinned(const QString &deviceId, bool pinned);
    // 隐藏或恢复显示指定设备（落库成功后刷新内存状态）
    Q_INVOKABLE void setDeviceHidden(const QString &deviceId, bool hidden);
    // 删除设备及其聊天与传输历史（不删除已接收的本地文件），成功后同步清理内存列表
    Q_INVOKABLE void deleteDeviceWithHistory(const QString &deviceId);
    // 入站消息或传输请求到达时调用：设备处于隐藏态则复位并刷新列表（自动恢复显示）
    void restoreHiddenDevice(const QString &deviceId);
    // 注入本地数据层入口
    void initDataBroker(LocalDataBroker *dataBroker);

signals:
    void peersChanged();
    void selectedDeviceIdChanged();
    void nodeDiscovered(const QString &deviceId);
    void nodeExpired(const QString &deviceId);

private:
    static QVariantMap peerRecordToVariant(const PeerRecord &record);
    // 将在线 PeerInfo 转成与历史条目同构的展示字段映射，统一过滤与排序规则
    QVariantMap peerInfoToVariant(const PeerInfo &info) const;

    DiscoveryService *_discovery = nullptr;  // 内部设备发现服务
    LocalDataBroker *_dataBroker = nullptr;  // 本地设备目录加载入口
    QVariantList _historyPeers;  // 已持久化的历史设备列表
    QSet<QString> _hiddenDeviceIds;  // 数据库中隐藏态的设备（不进合并列表）
    QSet<QString> _pinnedDeviceIds;  // 数据库中置顶态的设备（排序规则由后续任务接入）
    QString _selectedDeviceId;  // 当前选中设备 ID
};
