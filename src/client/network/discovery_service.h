/**
* @file    discovery_service.h
* @version 4.10.0
* @date    2026-06-13
* @author  GridYard Team
* @brief   局域网设备发现服务
*
* 通过 UDP 广播实现局域网内设备自动发现。
* 每 5 秒发送 Hello 包，维护在线节点表，15 秒无心跳自动剔除。
*
* Change Log:
* [v4.7.1] FengChunlin   2026-06-05
* * 修复文件传输使用真实 IP 地址
* [v0.3.0] FengChunlin   2026-06-03
* * 添加 refresh() 方法
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 2：初始版本
*/

#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "data_types.h"

class ConfigManager;

class DiscoveryService : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)

public:
    explicit DiscoveryService(ConfigManager *config, QObject *parent = nullptr);
    virtual ~DiscoveryService() override = default;

    DiscoveryService(const DiscoveryService &)            = delete;
    DiscoveryService &operator=(const DiscoveryService &) = delete;

    // 获取当前在线节点列表（供 QML 绑定）
    QVariantList peers() const;

    // 根据 deviceId 获取设备信息
    PeerInfo peerInfo(const QString &deviceId) const;

    // 立即发送一次广播并清理离线节点
    Q_INVOKABLE void refresh();

signals:
    // 节点列表变化通知
    void peersChanged();
    // 新节点发现
    void nodeDiscovered(const QString &deviceId);
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
    // 构建 Hello 包 JSON 内容
    QByteArray buildHelloPayload() const;
    // 处理收到的 Hello 包
    void handleHelloPacket(const QJsonObject &json, const QHostAddress &sender);
    // 更新节点信息
    void updatePeer(const QString &deviceId, const PeerInfo &info);
    // 通知 QML 列表变化
    void notifyPeersChanged();

    ConfigManager *_config = nullptr;
    QUdpSocket    *_socket = nullptr;
    QTimer        *_broadcastTimer = nullptr;
    QTimer        *_pruneTimer = nullptr;

    // 节点表：deviceId -> PeerInfo
    QHash<QString, PeerInfo> _peers;
};
