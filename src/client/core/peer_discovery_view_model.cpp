/**
* @file    peer_discovery_view_model.cpp
* @version 7.17.1
* @date    2026-10-04
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型实现
*
* Change Log:
* [v7.17.1] GY   2026-10-04
* * 合并链路消费 pinned/hidden：hidden 条目不进列表，pinned 数据可用
* * 新增置顶、隐藏、删除与自动恢复入口，删除链路同步清理内存列表
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
* * 选中状态收编：新增 selectedDeviceId 属性与 deviceById 查询，
*   QML 不再手工复制四元组并循环同步
* [v7.8.0] GY   2026-07-21
* * 按设备来源优先级排序：broadcast > directed > rendezvous > manual > history
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

// 设备来源优先级：数值越小优先级越高
int sourcePriority(const QVariant &peer)
{
    QString source = QStringLiteral("history");  // 默认最低优先级
    if (peer.canConvert<PeerInfo>()) {
        source = peer.value<PeerInfo>().source;
    } else {
        source = peer.toMap().value("source").toString();
    }
    // broadcast(0) > directed(1) > rendezvous(2) > manual(3) > history(4)
    if (source == QStringLiteral("broadcast")) return 0;
    if (source == QStringLiteral("directed")) return 1;
    if (source == QStringLiteral("rendezvous")) return 2;
    if (source == QStringLiteral("manual")) return 3;
    return 4;  // history 或空
}

// 比较函数用于排序
bool peerSortLessThan(const QVariant &a, const QVariant &b)
{
    const int priorityA = sourcePriority(a);
    const int priorityB = sourcePriority(b);
    if (priorityA != priorityB) {
        return priorityA < priorityB;
    }
    // 优先级相同按最后发现时间倒序
    QDateTime timeA, timeB;
    if (a.canConvert<PeerInfo>()) {
        timeA = a.value<PeerInfo>().lastSeen;
    }
    if (b.canConvert<PeerInfo>()) {
        timeB = b.value<PeerInfo>().lastSeen;
    }
    return timeA > timeB;  // 较新的排在前面
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

// 合并在线设备和历史设备，按来源优先级排序
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

    // 隐藏态设备不进列表（微信"不显示该聊天"语义），在线与历史条目一体过滤
    QList<QVariant> filteredPeers;
    filteredPeers.reserve(mergedPeers.size());
    for (const QVariant &peer : mergedPeers) {
        if (!_hiddenDeviceIds.contains(deviceIdFromVariant(peer))) {
            filteredPeers.append(peer);
        }
    }

    // 按来源优先级排序：broadcast > directed > rendezvous > manual > history
    std::sort(filteredPeers.begin(), filteredPeers.end(), peerSortLessThan);

    return filteredPeers;
}

// 按设备 ID 查询展示信息，未命中返回空表
QVariantMap PeerDiscoveryViewModel::deviceById(const QString &deviceId) const
{
    for (const QVariant &peer : peers()) {
        if (deviceIdFromVariant(peer) != deviceId) {
            continue;
        }
        if (peer.canConvert<PeerInfo>()) {
            const PeerInfo info = peer.value<PeerInfo>();
            return {{"deviceId", info.deviceId},
                    {"deviceName", info.deviceName},
                    {"ipAddress", info.ipAddress},
                    {"isOnline", info.isOnline},
                    {"pinned", _pinnedDeviceIds.contains(deviceId)}};
        }
        return peer.toMap();
    }
    return {};
}

// 获取当前选中设备 ID
QString PeerDiscoveryViewModel::selectedDeviceId() const
{
    return _selectedDeviceId;
}

// 更新选中设备并通知表现层
void PeerDiscoveryViewModel::setSelectedDeviceId(const QString &deviceId)
{
    if (_selectedDeviceId == deviceId) {
        return;
    }
    _selectedDeviceId = deviceId;
    emit selectedDeviceIdChanged();
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
                return;  // 数据库不可用时保持在线发现列表可用，管理状态沿用上次加载
            }

            QVariantList peers;
            peers.reserve(records.size());
            // 同步重建置顶/隐藏集合，过滤与展示均以此为准
            _hiddenDeviceIds.clear();
            _pinnedDeviceIds.clear();
            for (const PeerRecord &record : records) {
                peers.append(peerRecordToVariant(record));
                if (record.hidden) {
                    _hiddenDeviceIds.insert(record.deviceId);
                }
                if (record.pinned) {
                    _pinnedDeviceIds.insert(record.deviceId);
                }
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

// 置顶或取消置顶指定设备，落库成功后刷新内存状态
void PeerDiscoveryViewModel::setDevicePinned(const QString &deviceId, bool pinned)
{
    if (!_dataBroker || deviceId.isEmpty()) {
        return;
    }

    _dataBroker->setDevicePinned(this, deviceId, pinned,
                                 [this, deviceId, pinned](bool succeeded) {
        if (!succeeded) {
            return;  // 写库失败时保持现状，不打扰列表
        }
        if (pinned) {
            _pinnedDeviceIds.insert(deviceId);
        } else {
            _pinnedDeviceIds.remove(deviceId);
        }
        refreshHistory();  // 重新加载目录，条目的 pinned 字段随库更新
    });
}

// 隐藏或恢复显示指定设备，落库成功后刷新内存状态
void PeerDiscoveryViewModel::setDeviceHidden(const QString &deviceId, bool hidden)
{
    if (!_dataBroker || deviceId.isEmpty()) {
        return;
    }

    _dataBroker->setDeviceHidden(this, deviceId, hidden,
                                 [this, deviceId, hidden](bool succeeded) {
        if (!succeeded) {
            return;  // 写库失败时保持现状，不打扰列表
        }
        if (hidden) {
            _hiddenDeviceIds.insert(deviceId);
        } else {
            _hiddenDeviceIds.remove(deviceId);
        }
        refreshHistory();  // 重新加载目录，隐藏条目随过滤规则离开/回到列表
    });
}

// 删除设备及其聊天与传输历史（不删除已接收的本地文件），成功后同步清理内存列表
void PeerDiscoveryViewModel::deleteDeviceWithHistory(const QString &deviceId)
{
    if (!_dataBroker || deviceId.isEmpty()) {
        return;
    }

    _dataBroker->deleteDeviceWithHistory(this, deviceId, [this, deviceId](bool succeeded) {
        if (!succeeded) {
            return;  // 删除失败时保持数据库与列表现状
        }
        // 在线条目从发现服务移除；被删设备再次广播或被协调发现时
        // 按全新设备重新入目录（first_seen 语义），属拍板行为
        if (_discovery) {
            _discovery->removePeer(deviceId);
        }
        if (_selectedDeviceId == deviceId) {
            setSelectedDeviceId(QString());
        }
        refreshHistory();  // 重新加载目录，历史条目与置顶/隐藏集合随之收敛
    });
}

// 入站消息或传输请求到达时恢复隐藏设备的显示（微信语义）
void PeerDiscoveryViewModel::restoreHiddenDevice(const QString &deviceId)
{
    if (!_dataBroker || deviceId.isEmpty() || !_hiddenDeviceIds.contains(deviceId)) {
        return;  // 未处于隐藏态的设备无需复位
    }

    _dataBroker->setDeviceHidden(this, deviceId, false, [this, deviceId](bool succeeded) {
        if (!succeeded) {
            return;
        }
        _hiddenDeviceIds.remove(deviceId);  // 先解除过滤，列表刷新后恢复显示
        refreshHistory();
    });
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
        {"pinned", record.pinned},
        {"hidden", record.hidden},
    };
}
