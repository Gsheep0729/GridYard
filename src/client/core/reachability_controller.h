/**
* @file    reachability_controller.h
* @version 7.0.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   网络可达性控制器（QML 单例）
*
* 聚合 EndpointProbe 和 DiscoveryService，向 QML 提供统一的网络诊断入口。
* 支持本机 IP 地址查询、TCP 探测和定向 Hello 发送。
*
* Change Log:
* [v7.0.0] GY   2026-07-21
* * Stage 7.0：新增网络可达性控制器，提供诊断入口
*/

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/QtQml>

#include "endpoint_probe.h"
#include "discovery_service.h"

class ReachabilityController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QStringList localAddresses READ localAddresses NOTIFY localAddressesChanged)
    Q_PROPERTY(QVariantMap lastProbeResult READ lastProbeResult NOTIFY lastProbeResultChanged)
    Q_PROPERTY(bool isProbing READ isProbing NOTIFY isProbingChanged)

public:
    static ReachabilityController *create(QJSEngine *engine, QJSEngine *scriptEngine);
    static ReachabilityController *singleton();

    explicit ReachabilityController(QObject *parent = nullptr);
    virtual ~ReachabilityController() override = default;

    ReachabilityController(const ReachabilityController &)            = delete;
    ReachabilityController &operator=(const ReachabilityController &) = delete;

    // 获取本机所有 IPv4 地址列表
    QStringList localAddresses() const;
    // 获取最近一次探测结果
    QVariantMap lastProbeResult() const;
    // 当前是否正在探测
    bool isProbing() const;

    Q_INVOKABLE void probeEndpoint(const QString &ip, quint16 tcpPort, int timeoutMs = 5000);
    Q_INVOKABLE void sendDirectedHello(const QString &ip, quint16 discoveryPort = 45678);
    Q_INVOKABLE void refreshLocalAddresses();

    // 设置 DiscoveryService 引用（由 AppController 在构造后调用）
    void setDiscoveryService(DiscoveryService *discovery);

signals:
    void localAddressesChanged();
    void lastProbeResultChanged();
    void isProbingChanged();

private:
    // 收集本机所有有效的 IPv4 地址
    QStringList collectLocalAddresses() const;
    // 处理探测完成结果
    void onProbeFinished(const EndpointProbe::ProbeResult &result);

    static QPointer<ReachabilityController> s_instance;  // 单例实例

    EndpointProbe *_probe = nullptr;    // TCP 探测器
    DiscoveryService *_discovery = nullptr; // 设备发现服务
    QStringList _localAddresses;       // 本机 IP 地址列表
    QVariantMap _lastProbeResult;       // 最近一次探测结果
    bool _isProbing = false;            // 是否正在探测
};