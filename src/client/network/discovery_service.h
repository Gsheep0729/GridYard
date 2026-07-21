/**
* @file    discovery_service.h
* @version 7.5.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   局域网设备发现服务
*
* Change Log:
* [v7.5.0] GY   2026-07-21
* * 新增 onRendezvousPeersReceived 处理协调节点返回的候选端点
* [v6.6.2] GY   2026-06-28
* * 移除 QML 暴露宏和 Q_INVOKABLE 标记，QML 通过 PeerDiscoveryViewModel 访问
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.1.0] GY   2026-06-25
* * 新增 peerUpdated 信号供应用层异步持久化设备目录
* [v5.1.0] FengChunlin   2026-06-24
* * 接入 ChatManager 处理入站聊天连接
* [v4.16.1] GY   2026-06-21
* * 提供 transferEndpoint() 对端快照查询，避免拆分读取节点字段
* [v4.7.1] FengChunlin   2026-06-05
* * 修复文件传输使用真实 IP 地址
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 refresh() 方法
* [v0.2.0] FengChunlin   2026-04-27
* * Stage 2：初始版本
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
    QVariantList peers() const;

    // 查询可用于发送传输的对端快照；目标不存在或离线时返回空 map
    QVariantMap transferEndpoint(const QString &deviceId) const;

    // 立即发送一次广播并清理离线节点
    void refresh();
    // 向指定地址发送定向 Hello 包（用于跨 AP 场景）
    void sendDirectedHello(const QHostAddress &address, quint16 discoveryPort);

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
};
