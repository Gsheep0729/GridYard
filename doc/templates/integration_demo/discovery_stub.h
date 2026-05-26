/**
 * @file    discovery_stub.h
 * @date    2026-05-24
 * @author  GY
 * @brief   伪造的局域网设备发现服务（演示用）
 *
 * 真实项目中的 DiscoveryService 会通过 UDP 广播完成节点发现。
 * 这里是一个剥离了网络的桩对象，只保留"对外接口形状"以便
 * 演示 QML ↔ C++ 数据流向：
 *   - QML 调用 addPeer() / removeAllPeers() 推动状态变化
 *   - 状态变化通过 peersChanged signal 通知 QML
 *   - QML 通过 peers property 绑定列表，自动刷新
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

#pragma once

#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "peer_info.h"

class DiscoveryStub : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList peers READ peers NOTIFY peersChanged)

public:
    explicit DiscoveryStub(QObject *parent = nullptr);

    QVariantList peers() const;

    Q_INVOKABLE void addPeer(const QString &deviceName, const QString &ipAddress);
    Q_INVOKABLE void removeAllPeers();
    Q_INVOKABLE void markOffline(const QString &deviceId);

signals:
    void peersChanged();
    void nodeDiscovered(const QString &deviceId);
    void nodeExpired(const QString &deviceId);

private:
    mutable QMutex _mutex;
    QList<PeerInfo> _peers;
};
