/**
* @file    peer_discovery_view_model.cpp
* @version 7.20.2
* @date 2026-10-05
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型实现
*/

#include "peer_discovery_view_model.h"

#include "data_types.h"
#include "discovery_service.h"
#include "local_data_broker.h"

#include <QDateTime>
#include <QSet>

namespace {
constexpr int kRecentPeerLimit = 100;  // 首屏恢复最近设备数量上限
constexpr int kSearchPeerLimit = 50;  // 设备搜索单次回投上限：控制体量并保持结果可扫视
constexpr int kRecentVisibleLimit = 10;  // "最近见过"段可见条数上限，其余经搜索深检索

// 将时间转换成 QML 侧可展示的 ISO 文本
QString timeToString(const QDateTime &time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString{};
}

// 设备来源优先级：数值越小优先级越高
int sourcePriority(const QVariant &peer)
{
    // broadcast(0) > directed(1) > rendezvous(2) > manual(3) > history(4)
    const QString source = peer.toMap().value("source").toString();
    if (source == QStringLiteral("broadcast")) return 0;
    if (source == QStringLiteral("directed")) return 1;
    if (source == QStringLiteral("rendezvous")) return 2;
    if (source == QStringLiteral("manual")) return 3;
    return 4;  // history 或空
}

// 最近活跃时间（ISO 文本，同固定格式下字典序即时间序）
QString peerLastSeenAt(const QVariant &peer)
{
    return peer.toMap().value("lastSeenAt").toString();
}

// 分段序：置顶(0) < 在线(1) < 最近见过(2)；扫描语义下在线是主内容，
// 历史是记忆，置顶是用户钉住的常用目标（无论在线离线）
int segmentRank(const QVariant &peer)
{
    const QVariantMap map = peer.toMap();
    if (map.value("pinned").toBool()) {
        return 0;
    }
    if (map.value("isOnline").toBool()) {
        return 1;
    }
    return 2;
}

// 比较函数用于排序：段间按置顶/在线/最近见过；置顶与在线段内沿用
// 来源优先级加最近活跃，最近见过段内只按最近活跃倒序
bool peerSortLessThan(const QVariant &a, const QVariant &b)
{
    const int rankA = segmentRank(a);
    const int rankB = segmentRank(b);
    if (rankA != rankB) {
        return rankA < rankB;
    }
    if (rankA == 2) {
        return peerLastSeenAt(a) > peerLastSeenAt(b);  // 历史条目按最后见过时间倒序
    }
    const int priorityA = sourcePriority(a);
    const int priorityB = sourcePriority(b);
    if (priorityA != priorityB) {
        return priorityA < priorityB;
    }
    // 优先级相同按最后发现时间倒序
    return peerLastSeenAt(a) > peerLastSeenAt(b);  // 较新的排在前面
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

// 合并在线设备和历史设备，过滤隐藏后标注分段并按段内规则排序；
// 不做最近见过截尾，供需要全量目录的查询（deviceById）复用
QVariantList PeerDiscoveryViewModel::buildMergedPeers() const
{
    QVariantList mergedPeers;
    if (_discovery) {
        const QVariantList discoveredPeers = _discovery->peers();
        mergedPeers.reserve(discoveredPeers.size() + _historyPeers.size());
        // 在线条目统一转成展示字段映射，与历史条目共用一套过滤与排序规则
        for (const QVariant &peer : discoveredPeers) {
            if (peer.canConvert<PeerInfo>()) {
                mergedPeers.append(peerInfoToVariant(peer.value<PeerInfo>()));
            }
        }
    }

    // 先收集在线设备的 ID 集合，用于去重
    QSet<QString> onlineDeviceIds;
    for (const QVariant &peer : mergedPeers) {
        const QString deviceId = peer.toMap().value("deviceId").toString();
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

    // 隐藏态设备不进列表（"不显示该设备"语义），在线与历史条目一体过滤
    QVariantList filteredPeers;
    filteredPeers.reserve(mergedPeers.size());
    for (const QVariant &peer : mergedPeers) {
        if (!_hiddenDeviceIds.contains(peer.toMap().value("deviceId").toString())) {
            filteredPeers.append(peer);
        }
    }

    // 分段标注：置顶段（无论在线离线）/ 在线段 / 最近见过段，列表据此渲染段头
    for (QVariant &peer : filteredPeers) {
        QVariantMap map = peer.toMap();
        map.insert(QStringLiteral("segment"),
                   map.value("pinned").toBool() ? QStringLiteral("pinned")
                   : (map.value("isOnline").toBool() ? QStringLiteral("online")
                                                     : QStringLiteral("recent")));
        peer = map;
    }

    // 排序：段间置顶 < 在线 < 最近见过；置顶与在线段内来源优先级
    //       (broadcast > directed > rendezvous > manual) 加最近活跃，
    //       最近见过段内按最后见过时间倒序；stable_sort 保持同序位装载顺序
    std::stable_sort(filteredPeers.begin(), filteredPeers.end(), peerSortLessThan);

    return filteredPeers;
}

// 列表视图数据：在分段排序结果上对最近见过段截尾——扫描语义下在线是
// 主内容，历史最多露出最近 kRecentVisibleLimit 台，其余经搜索深检索
QVariantList PeerDiscoveryViewModel::peers() const
{
    const QVariantList merged = buildMergedPeers();
    QVariantList visible;
    visible.reserve(merged.size());
    int recentSeen = 0;
    for (const QVariant &peer : merged) {
        if (peer.toMap().value("segment").toString() == QStringLiteral("recent")
                && ++recentSeen > kRecentVisibleLimit) {
            continue;
        }
        visible.append(peer);
    }
    return visible;
}

// 获取"最近见过"段的可见条数上限
int PeerDiscoveryViewModel::recentVisibleLimit() const
{
    return kRecentVisibleLimit;
}

// 隐藏态设备列表：历史目录保序在前，在线表补充的隐藏条目追加在后，
// 供设置页"已隐藏设备"区块展示设备名/IP/最后活跃并恢复显示
QVariantList PeerDiscoveryViewModel::hiddenPeers() const
{
    QVariantList result;
    QSet<QString> collectedIds;
    for (const QVariant &peer : _historyPeers) {
        const QVariantMap map = peer.toMap();
        const QString deviceId = map.value("deviceId").toString();
        if (deviceId.isEmpty() || !_hiddenDeviceIds.contains(deviceId)) {
            continue;
        }
        result.append(map);
        collectedIds.insert(deviceId);
    }

    if (_discovery) {
        const QVariantList discoveredPeers = _discovery->peers();
        for (const QVariant &peer : discoveredPeers) {
            if (!peer.canConvert<PeerInfo>()) {
                continue;
            }
            const PeerInfo info = peer.value<PeerInfo>();
            if (_hiddenDeviceIds.contains(info.deviceId)
                    && !collectedIds.contains(info.deviceId)) {
                result.append(peerInfoToVariant(info));
            }
        }
    }
    return result;
}

// 获取关键字检索的数据库历史命中条目
QVariantList PeerDiscoveryViewModel::searchResults() const
{
    return _searchResults;
}

// 获取数据库检索进行中状态
bool PeerDiscoveryViewModel::searchBusy() const
{
    return _searchBusy;
}

// 设置检索进行中状态并通知 QML
void PeerDiscoveryViewModel::setSearchBusy(bool value)
{
    if (_searchBusy == value) {
        return;
    }
    _searchBusy = value;
    emit searchBusyChanged();
}

// 按设备 ID 查询展示信息，未命中返回空表；走全量合并结果，
// 被最近见过截尾挡在列表外的历史设备仍可解析（如已选中的会话设备）
QVariantMap PeerDiscoveryViewModel::deviceById(const QString &deviceId) const
{
    const QVariantList merged = buildMergedPeers();
    for (const QVariant &peer : merged) {
        const QVariantMap map = peer.toMap();
        if (map.value("deviceId").toString() == deviceId) {
            return map;
        }
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
            // 同步重建置顶/隐藏集合与备注映射，过滤与展示均以此为准
            _hiddenDeviceIds.clear();
            _pinnedDeviceIds.clear();
            _aliasByDeviceId.clear();
            for (const PeerRecord &record : records) {
                peers.append(peerRecordToVariant(record));
                if (record.hidden) {
                    _hiddenDeviceIds.insert(record.deviceId);
                }
                if (record.pinned) {
                    _pinnedDeviceIds.insert(record.deviceId);
                }
                _aliasByDeviceId.insert(record.deviceId, record.alias);
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

// 按关键字异步检索设备目录，命中条目经 searchResults 发布给表现层
void PeerDiscoveryViewModel::searchPeers(const QString &keyword)
{
    const QString trimmed = keyword.trimmed();
    _searchKeyword = trimmed;

    // 空关键字立即清空结果并复位状态，避免旧检索残留到列表
    if (trimmed.isEmpty()) {
        setSearchBusy(false);
        if (!_searchResults.isEmpty()) {
            _searchResults.clear();
            emit searchResultsChanged();
        }
        return;
    }

    if (!_dataBroker) {
        return;  // 数据层未注入时保持现状，已加载列表的过滤仍可用
    }

    setSearchBusy(true);
    _dataBroker->searchPeers(
        this, trimmed, kSearchPeerLimit,
        [this, trimmed](const QList<PeerRecord> &records, bool succeeded) {
            // 回调到达时关键字已变化则丢弃过期结果，等待新关键字的回调收尾
            if (trimmed != _searchKeyword) {
                return;
            }
            setSearchBusy(false);
            if (!succeeded) {
                return;  // 检索失败保留上一次结果，已加载列表的过滤不受影响
            }
            _searchResults.clear();
            _searchResults.reserve(records.size());
            for (const PeerRecord &record : records) {
                _searchResults.append(peerRecordToVariant(record));
            }
            emit searchResultsChanged();
        });
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

// 设置或清除设备本地备注（空串表示清除），落库成功后刷新内存状态
void PeerDiscoveryViewModel::setDeviceAlias(const QString &deviceId, const QString &alias)
{
    if (!_dataBroker || deviceId.isEmpty()) {
        return;
    }

    _dataBroker->setDeviceAlias(this, deviceId, alias, [this, deviceId, alias](bool succeeded) {
        if (!succeeded) {
            return;  // 写库失败时保持现状，不打扰列表
        }
        if (alias.isEmpty()) {
            _aliasByDeviceId.remove(deviceId);
        } else {
            _aliasByDeviceId.insert(deviceId, alias);
        }
        refreshHistory();  // 重新加载目录，条目的 alias 字段随库更新
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
        {"alias", record.alias},
        {"pinned", record.pinned},
        {"hidden", record.hidden},
    };
}

// 将在线 PeerInfo 转成与历史条目同构的展示字段映射：
// 置顶/隐藏状态以内存集合为准，备注从设备目录按 deviceId 回填
// （在线 PeerInfo 不携带本地备注），聊天与传输活跃时间在线条目不携带
QVariantMap PeerDiscoveryViewModel::peerInfoToVariant(const PeerInfo &info) const
{
    return {
        {"deviceId", info.deviceId},
        {"deviceName", info.deviceName},
        {"ipAddress", info.ipAddress},
        {"isOnline", info.isOnline},
        {"source", info.source},
        {"lastSeenAt", timeToString(info.lastSeen)},
        {"lastChatAt", QString{}},
        {"lastTransferAt", QString{}},
        {"alias", _aliasByDeviceId.value(info.deviceId)},
        {"pinned", _pinnedDeviceIds.contains(info.deviceId)},
        {"hidden", _hiddenDeviceIds.contains(info.deviceId)},
    };
}
