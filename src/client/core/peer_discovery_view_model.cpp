/**
* @file    peer_discovery_view_model.cpp
* @version 6.7.0
* @date    2026-06-28
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型实现
*
* Change Log:
* [v6.7.0] GY   2026-06-28
* * 合并在线发现设备和本地历史设备目录
* [v6.6.2] GY   2026-06-27
* * 新增设备发现 UI API 门面
*/

#include "peer_discovery_view_model.h"

#include "data_types.h"
#include "discovery_service.h"
#include "local_data_broker.h"

#include <QDateTime>
#include <QSet>

namespace {
constexpr int kRecentPeerLimit = 100;  // 首屏恢复最近设备数量上限

// 将时间转换成 QML 侧可展示的 ISO 文本
QString timeToString(const QDateTime &time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString{};
}

// 从在线 PeerInfo 或历史 QVariantMap 中提取设备 ID
QString deviceIdFromVariant(const QVariant &peer)
{
    if (peer.canConvert<PeerInfo>()) {
        return peer.value<PeerInfo>().deviceId;
    }
    return peer.toMap().value("deviceId").toString();
}
}

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
    // 设备离线后重新加载设备目录，确保刚连接过的设备仍保留在列表中
    connect(_discovery, &DiscoveryService::nodeExpired,
            this, [this] {
                refreshHistory();
            });
}

// 合并在线设备和历史设备，在线设备优先，历史设备不重复追加
QVariantList PeerDiscoveryViewModel::peers() const
{
    QVariantList mergedPeers = _discovery ? _discovery->peers() : QVariantList{};
    // 先收集在线设备的 ID 集合，用于去重
    QSet<QString> onlineDeviceIds;
    for (const QVariant &peer : mergedPeers) {
        const QString deviceId = deviceIdFromVariant(peer);
        if (!deviceId.isEmpty()) {
            onlineDeviceIds.insert(deviceId);
        }
    }

    // 历史设备仅追加不在在线列表中的条目，避免设备在线时出现重复卡片
    for (const QVariant &peer : _historyPeers) {
        const QVariantMap map = peer.toMap();
        const QString deviceId = map.value("deviceId").toString();
        if (deviceId.isEmpty() || onlineDeviceIds.contains(deviceId)) {
            continue;
        }
        mergedPeers.append(map);
    }
    return mergedPeers;
}

// 请求立即刷新设备发现
void PeerDiscoveryViewModel::refresh()
{
    if (_discovery) {
        _discovery->refresh();
    }
    refreshHistory();
}

// 请求刷新本地历史设备目录，异步从数据库读取后合并到 peers 列表
void PeerDiscoveryViewModel::refreshHistory()
{
    if (!_dataBroker) {
        return;
    }

    _dataBroker->loadRecentPeers(
        this, kRecentPeerLimit,
        [this](const QList<PeerRecord> &records, bool succeeded) {
            if (!succeeded) {
                if (!_historyPeers.isEmpty()) {
                    _historyPeers.clear();
                    emit peersChanged();
                }
                return;  // 数据库不可用时保持在线发现列表可用
            }

            QVariantList peers;
            peers.reserve(records.size());
            for (const PeerRecord &record : records) {
                peers.append(peerRecordToVariant(record));
            }
            _historyPeers = peers;
            emit peersChanged();
        });
}

// 注入本地数据层入口
void PeerDiscoveryViewModel::initDataBroker(LocalDataBroker *dataBroker)
{
    _dataBroker = dataBroker;
    refreshHistory();
}

// 将设备目录记录转换成 QML 可绑定字段
QVariantMap PeerDiscoveryViewModel::peerRecordToVariant(const PeerRecord &record)
{
    return {
        {"deviceId", record.deviceId},
        {"deviceName", record.deviceName},
        {"ipAddress", record.lastIpAddress},
        {"isOnline", false},
        {"lastSeenAt", timeToString(record.lastSeenAt)},
        {"lastChatAt", timeToString(record.lastChatAt)},
        {"lastTransferAt", timeToString(record.lastTransferAt)},
    };
}
