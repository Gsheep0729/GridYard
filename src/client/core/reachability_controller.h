/**
* @file    reachability_controller.h
* @version 7.1.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   网络可达性控制器（QML 单例）
*
* 聚合 EndpointProbe 和 DiscoveryService，向 QML 提供统一的网络诊断入口。
* 支持本机 IP 地址查询、TCP 探测、定向 Hello 发送和邀请文本处理。
*
* Change Log:
* [v7.1.0] GY   2026-07-21
* * Stage 7.1：新增邀请文本导入导出和手动添加设备功能
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
#include "invite_codec.h"

class ConfigManager;

class ReachabilityController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QStringList localAddresses READ localAddresses NOTIFY localAddressesChanged)
    Q_PROPERTY(QVariantMap lastProbeResult READ lastProbeResult NOTIFY lastProbeResultChanged)
    Q_PROPERTY(bool isProbing READ isProbing NOTIFY isProbingChanged)
    Q_PROPERTY(QString lastInviteText READ lastInviteText NOTIFY lastInviteTextChanged)
    Q_PROPERTY(QString inviteError READ inviteError NOTIFY inviteErrorChanged)

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
    // 获取最近一次生成的邀请文本
    QString lastInviteText() const;
    // 获取最近一次邀请操作的错误信息
    QString inviteError() const;

    // 生成当前设备的邀请文本
    Q_INVOKABLE QString generateInvite();
    // 导入邀请文本，尝试连接对端设备
    Q_INVOKABLE void importInvite(const QString &text);
    // 手动添加设备（IP + TCP 端口）
    Q_INVOKABLE void addManualEndpoint(const QString &ip, quint16 tcpPort);
    // 测试手动端点
    Q_INVOKABLE void testManualEndpoint(const QString &ip, quint16 tcpPort);

    Q_INVOKABLE void probeEndpoint(const QString &ip, quint16 tcpPort, int timeoutMs = 5000);
    Q_INVOKABLE void sendDirectedHello(const QString &ip, quint16 discoveryPort = 45678);
    Q_INVOKABLE void refreshLocalAddresses();

    // 设置 DiscoveryService 引用（由 AppController 在构造后调用）
    void setDiscoveryService(DiscoveryService *discovery);
    // 设置 ConfigManager 引用（由 AppController 在构造后调用）
    void setConfigManager(ConfigManager *config);

signals:
    void localAddressesChanged();
    void lastProbeResultChanged();
    void isProbingChanged();
    void lastInviteTextChanged();
    void inviteErrorChanged();
    void manualEndpointTestResult(bool success, const QString &errorString);
    void inviteImported(bool success, const QString &deviceId, const QString &errorString);

private:
    // 收集本机所有有效的 IPv4 地址
    QStringList collectLocalAddresses() const;
    // 处理探测完成结果
    void onProbeFinished(const EndpointProbe::ProbeResult &result);
    // 处理手动端点探测完成
    void onManualEndpointProbeFinished(const EndpointProbe::ProbeResult &result, const QString &deviceId, const QString &deviceName, quint16 discoveryPort);

    static QPointer<ReachabilityController> s_instance;  // 单例实例

    EndpointProbe *_probe = nullptr;    // TCP 探测器
    DiscoveryService *_discovery = nullptr; // 设备发现服务
    ConfigManager *_config = nullptr;   // 本机配置
    QStringList _localAddresses;       // 本机 IP 地址列表
    QVariantMap _lastProbeResult;       // 最近一次探测结果
    bool _isProbing = false;            // 是否正在探测
    QString _lastInviteText;           // 最近生成的邀请文本
    QString _inviteError;              // 最近邀请操作错误
};