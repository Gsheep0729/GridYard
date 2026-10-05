/**
* @file    discovery_service.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   局域网设备发现服务
*/

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>

#include "data_types.h"

class ConfigManager;
class TestChatManager;
class RendezvousClient;

class DiscoveryService : public QObject {
    Q_OBJECT

public:
    explicit DiscoveryService(ConfigManager *config, QObject *parent = nullptr);
    virtual ~DiscoveryService() override = default;

    DiscoveryService(const DiscoveryService &)            = delete;
    DiscoveryService &operator=(const DiscoveryService &) = delete;

    // 获取当前在线节点列表（供 QML 绑定）
    // 调整节点超时秒数（测试可调小；仅影响后续 prune 判定）
    void setNodeTimeoutSec(int seconds);

    QVariantList peers() const;

    // 查询可用于发送传输的对端快照；目标不存在或离线时返回空 map
    QVariantMap transferEndpoint(const QString &deviceId) const;

    // 返回协调节点缓存的对端备用地址（不含当前主端点）；tcpPort 非空时回传缓存端口
    QStringList rendezvousAlternateAddresses(const QString &deviceId, quint16 *tcpPort = nullptr) const;

    // 立即发送一次广播并清理离线节点
    void refresh();
    // 向指定地址发送定向 Hello 包（用于跨 AP 场景）
    void sendDirectedHello(const QHostAddress &address, quint16 discoveryPort);

    // 获取本实例实际绑定的 UDP 发现端口（邀请码端口照实携带用，未绑定时为 0）
    quint16 localDiscoveryPort() const;

    // 添加手动端点（来源标记为 manual）
    void addManualPeer(const PeerInfo &peer);

    // 以真实身份注入定向发现的在线条目（邀请导入探测成功后调用，来源标记为 directed）
    void addDirectedPeer(const PeerInfo &peer);

    // 从设备表移除指定设备（用户删除设备后调用）；再次广播/被协调发现时按全新设备重新入目录
    void removePeer(const QString &deviceId);

    // 处理协调节点返回的候选端点
    Q_INVOKABLE void onRendezvousPeersReceived(const QList<QVariantMap> &peers);

signals:
    // 节点列表变化通知
    void peersChanged();
    // 新节点发现
    void nodeDiscovered(const QString &deviceId);
    // 设备首次发现或元数据变化后的完整快照
    void peerUpdated(const PeerInfo &peer);
    // 节点离线
    void nodeExpired(const QString &deviceId);

private slots:
    // 发送 Hello 广播包
    void sendHelloPacket();
    // 接收 UDP 数据报
    void onDatagramReceived();
    // 清理离线节点
    void pruneOfflineNodes();

private:
    friend class TestChatManager;

    // 协调节点缓存的备用候选地址（设备表之外单独保管，供连接失败后轮询）
    struct RendezvousCandidates {
        QStringList addresses;
        quint16 tcpPort = 0;
    };

    // 构建 Hello 包 JSON 内容
    QByteArray buildHelloPayload() const;
    // 处理收到的 Hello 包
    void handleHelloPacket(const QJsonObject &json, const QHostAddress &sender);
    // 更新节点信息
    void updatePeer(const QString &deviceId, const PeerInfo &info);
    // 通知 QML 列表变化
    void notifyPeersChanged();

    ConfigManager *_config = nullptr;          // 本机身份和网络配置来源
    QUdpSocket    *_socket = nullptr;          // UDP 广播收发 socket
    QTimer        *_broadcastTimer = nullptr;  // 周期发送 Hello 广播
    QTimer        *_pruneTimer = nullptr;      // 周期清理过期节点

    // 节点表：deviceId -> PeerInfo
    QHash<QString, PeerInfo> _peers;
    int _nodeTimeoutSec = 15;          // 节点超时秒数，可注入短值供测试
    // 协调节点多地址缓存：deviceId -> 备用候选
    QHash<QString, RendezvousCandidates> _rendezvousCandidates;
};
