/**
* @file    peer_discovery_view_model.h
* @version 7.24.0
* @date 2026-10-08
* @author  GridYard Team
* @brief   面向 QML 的设备发现视图模型
*
* 向表现层暴露在线设备和历史设备合并后的列表，隐藏 DiscoveryService
* 与本地设备目录的内部细节。
*/

#pragma once

#include "history_records.h"

#include <QHash>
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
    // "最近见过"段在列表中的可见条数上限，超出部分经搜索深检索数据库目录
    Q_PROPERTY(int recentVisibleLimit READ recentVisibleLimit NOTIFY peersChanged)
    // "最近见过"段是否展开全部条目（会话内记忆：心跳刷新不收起，重启复位）
    Q_PROPERTY(bool recentExpanded READ recentExpanded WRITE setRecentExpanded
               NOTIFY recentExpandedChanged)
    // "最近见过"段全量条数（不受截尾影响），段尾展开入口据此显示"共 N 台"
    Q_PROPERTY(int recentTotalCount READ recentTotalCount NOTIFY peersChanged)
    // 关键字检索的数据库历史命中条目，随 searchPeers 异步发布
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    // 数据库检索是否进行中（防抖提交后到回调返回之间），界面据此抑制空态闪现
    Q_PROPERTY(bool searchBusy READ searchBusy NOTIFY searchBusyChanged)
    Q_PROPERTY(QString selectedDeviceId READ selectedDeviceId WRITE setSelectedDeviceId NOTIFY selectedDeviceIdChanged)

public:
    explicit PeerDiscoveryViewModel(DiscoveryService *discovery, QObject *parent = nullptr);
    virtual ~PeerDiscoveryViewModel() override = default;

    PeerDiscoveryViewModel(const PeerDiscoveryViewModel &) = delete;
    PeerDiscoveryViewModel &operator=(const PeerDiscoveryViewModel &) = delete;

    // 获取 QML 可绑定的在线和历史设备合并列表（分段排序，最近见过段截尾）
    QVariantList peers() const;
    // 获取"最近见过"段的可见条数上限
    int recentVisibleLimit() const;
    // 获取"最近见过"段是否处于展开态
    bool recentExpanded() const;
    // 设置"最近见过"段展开态，展开/收起后列表立即重渲染
    void setRecentExpanded(bool expanded);
    // 获取"最近见过"段全量条数（不截尾，被限流隐藏的条目也计入）
    int recentTotalCount() const;
    // 获取隐藏态设备列表，设置页"已隐藏设备"区块据此展示恢复入口
    QVariantList hiddenPeers() const;
    // 获取关键字检索的数据库历史命中条目
    QVariantList searchResults() const;
    // 获取数据库检索进行中状态
    bool searchBusy() const;
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
    // 按关键字异步检索设备目录（设备名/备注/最近 IP），命中条目经
    // searchResults 发布，供列表把数据库历史命中追加到已加载条目之后
    Q_INVOKABLE void searchPeers(const QString &keyword);
    // 置顶或取消置顶指定设备（落库成功后刷新内存状态）
    Q_INVOKABLE void setDevicePinned(const QString &deviceId, bool pinned);
    // 收藏或取消收藏指定设备（落库成功后刷新内存状态）
    Q_INVOKABLE void setDeviceFavorite(const QString &deviceId, bool favorite);
    // 隐藏或恢复显示指定设备（落库成功后刷新内存状态）
    Q_INVOKABLE void setDeviceHidden(const QString &deviceId, bool hidden);
    // 设置或清除设备本地备注（空串表示清除，落库成功后刷新列表；
    // 显示优先级 alias > 广播名由展示层按条目的 alias 字段裁决）
    Q_INVOKABLE void setDeviceAlias(const QString &deviceId, const QString &alias);
    // 删除设备及其聊天与传输历史（不删除已接收的本地文件），成功后同步清理内存列表
    Q_INVOKABLE void deleteDeviceWithHistory(const QString &deviceId);
    // 设备关联合并：把 oldDeviceId 行的管理标记（可选连带聊天与传输历史）
    // 并入 newDeviceId 行并删除旧行；选中态指向旧 ID 时自动切换到新 ID
    Q_INVOKABLE void mergeDevice(const QString &newDeviceId, const QString &oldDeviceId,
                                 bool includeHistory);
    // 统计设备名下的聊天与传输条数（关联向导预览用），结果经 deviceHistoryCounted 发布
    Q_INVOKABLE void countDeviceHistory(const QString &deviceId);
    // 入站消息或传输请求到达时调用：设备处于隐藏态则复位并刷新列表（自动恢复显示）
    void restoreHiddenDevice(const QString &deviceId);
    // 注入本地数据层入口
    void initDataBroker(LocalDataBroker *dataBroker);

signals:
    void peersChanged();
    void recentExpandedChanged();
    void searchResultsChanged();
    void searchBusyChanged();
    void selectedDeviceIdChanged();
    void nodeDiscovered(const QString &deviceId);
    void nodeExpired(const QString &deviceId);
    void deviceHistoryCounted(QString deviceId, int messages, int transfers);
    void deviceMerged(bool success, QString newDeviceId, QString oldDeviceId);

private:
    // 设置检索进行中状态并通知 QML
    void setSearchBusy(bool value);
    // 合并在线与历史条目、过滤隐藏、标注分段并排序；不做最近见过截尾，
    // 供 deviceById 等需要全量目录的查询复用
    QVariantList buildMergedPeers() const;
    static QVariantMap peerRecordToVariant(const PeerRecord &record);
    // 将在线 PeerInfo 转成与历史条目同构的展示字段映射，统一过滤与排序规则
    QVariantMap peerInfoToVariant(const PeerInfo &info) const;

    DiscoveryService *_discovery = nullptr;  // 内部设备发现服务
    LocalDataBroker *_dataBroker = nullptr;  // 本地设备目录加载入口
    QVariantList _historyPeers;  // 已持久化的历史设备列表
    QVariantList _searchResults;  // 最近一次关键字检索的数据库命中条目
    QString _searchKeyword;  // 最近一次提交的检索关键字，用于丢弃过期回调结果
    bool _searchBusy = false;  // 数据库检索是否进行中
    QSet<QString> _hiddenDeviceIds;  // 数据库中隐藏态的设备（不进合并列表）
    QSet<QString> _pinnedDeviceIds;  // 数据库中置顶态的设备（位置语义，决定条目归属段）
    QSet<QString> _favoriteDeviceIds;  // 数据库中收藏态的设备（关系语义，段内排序优先）
    QHash<QString, QString> _aliasByDeviceId;  // 数据库中的设备备注，在线条目按 deviceId 回填
    QString _selectedDeviceId;  // 当前选中设备 ID
    bool _recentExpanded = false;  // "最近见过"段展开态，仅存于视图模型（重启复位）
};
