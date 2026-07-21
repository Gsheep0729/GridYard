/**
* @file    reachability_controller.cpp
* @version 7.1.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   网络可达性控制器实现
*
* 聚合 EndpointProbe 和 DiscoveryService，提供统一的网络诊断能力。
* QML 通过此控制器发起 TCP 探测、定向 Hello 和邀请文本处理。
*
* Change Log:
* [v7.1.0] GY   2026-07-21
* * Stage 7.1：新增邀请文本导入导出和手动添加设备功能
* [v7.0.0] GY   2026-07-21
* * Stage 7.0：新增网络可达性控制器，提供诊断入口
*/

#include "reachability_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "endpoint_probe.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QJSEngine>

QPointer<ReachabilityController> ReachabilityController::s_instance;

ReachabilityController::ReachabilityController(QObject *parent)
    : QObject{parent}
    , _probe{new EndpointProbe{this}}
{
    connect(_probe, &EndpointProbe::probeFinished,
            this, &ReachabilityController::onProbeFinished);

    _localAddresses = collectLocalAddresses();
}

ReachabilityController *ReachabilityController::create(QJSEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);
    return singleton();
}

ReachabilityController *ReachabilityController::singleton()
{
    if (s_instance.isNull()) {
        s_instance = new ReachabilityController();
    }
    return s_instance;
}

QStringList ReachabilityController::localAddresses() const
{
    return _localAddresses;
}

QVariantMap ReachabilityController::lastProbeResult() const
{
    return _lastProbeResult;
}

bool ReachabilityController::isProbing() const
{
    return _isProbing;
}

QString ReachabilityController::lastInviteText() const
{
    return _lastInviteText;
}

QString ReachabilityController::inviteError() const
{
    return _inviteError;
}

void ReachabilityController::probeEndpoint(const QString &ip, quint16 tcpPort, int timeoutMs)
{
    _isProbing = true;
    emit isProbingChanged();

    _probe->probeTcp(ip, tcpPort, timeoutMs);
}

void ReachabilityController::sendDirectedHello(const QString &ip, quint16 discoveryPort)
{
    if (!_discovery) {
        qWarning() << "ReachabilityController: DiscoveryService 未设置，无法发送定向 Hello";
        return;
    }

    QHostAddress address;
    if (ip.isEmpty() || !address.setAddress(ip)) {
        qWarning() << "ReachabilityController: 无效的 IP 地址:" << ip;
        return;
    }

    _discovery->sendDirectedHello(address, discoveryPort);
}

void ReachabilityController::refreshLocalAddresses()
{
    _localAddresses = collectLocalAddresses();
    emit localAddressesChanged();
}

void ReachabilityController::setDiscoveryService(DiscoveryService *discovery)
{
    _discovery = discovery;
}

void ReachabilityController::setConfigManager(ConfigManager *config)
{
    _config = config;
}

QStringList ReachabilityController::collectLocalAddresses() const
{
    QStringList addresses;
    const auto interfaces = QNetworkInterface::allInterfaces();

    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (iface.type() == QNetworkInterface::Loopback) continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            addresses.append(entry.ip().toString());
        }
    }

    return addresses;
}

void ReachabilityController::onProbeFinished(const EndpointProbe::ProbeResult &result)
{
    _isProbing = false;
    emit isProbingChanged();

    _lastProbeResult = {
        {"targetIp", result.targetIp},
        {"targetPort", result.targetPort},
        {"tcpConnected", result.tcpConnected},
        {"elapsedMs", result.elapsedMs},
        {"errorCode", result.errorCode},
        {"errorMessage", result.errorMessage},
    };
    emit lastProbeResultChanged();
}

QString ReachabilityController::generateInvite()
{
    if (!_config) {
        _inviteError = QStringLiteral("配置管理器未设置");
        emit inviteErrorChanged();
        return {};
    }

    InviteCodec::Invite invite;
    invite.deviceId = _config->deviceId();
    invite.deviceName = _config->deviceName();
    invite.ipAddress = _config->localIp();
    invite.tcpPort = _config->tcpPort();
    invite.discoveryPort = 45678;

    _lastInviteText = InviteCodec::encode(invite);
    _inviteError.clear();
    emit lastInviteTextChanged();
    emit inviteErrorChanged();

    return _lastInviteText;
}

void ReachabilityController::importInvite(const QString &text)
{
    InviteCodec::Error error = InviteCodec::Error::None;
    InviteCodec::Invite invite = InviteCodec::parse(text, &error);

    if (error != InviteCodec::Error::None) {
        _inviteError = InviteCodec::errorString(error);
        emit inviteErrorChanged();
        emit inviteImported(false, QString(), _inviteError);
        return;
    }

    // 立即尝试 TCP 探测和定向 Hello
    probeEndpoint(invite.ipAddress, invite.tcpPort, 5000);

    // 保存邀请信息用于后续定向 Hello
    _lastProbeResult = {
        {"deviceId", invite.deviceId},
        {"deviceName", invite.deviceName},
        {"ipAddress", invite.ipAddress},
        {"tcpPort", invite.tcpPort},
        {"discoveryPort", invite.discoveryPort},
        {"pending", true},
    };

    // 发送定向 Hello
    sendDirectedHello(invite.ipAddress, invite.discoveryPort > 0 ? invite.discoveryPort : 45678);

    emit inviteImported(true, invite.deviceId, QString());
}

void ReachabilityController::addManualEndpoint(const QString &ip, quint16 tcpPort)
{
    if (ip.isEmpty()) {
        _inviteError = QStringLiteral("IP 地址不能为空");
        emit inviteErrorChanged();
        return;
    }

    QHostAddress addr;
    if (!addr.setAddress(ip)) {
        _inviteError = QStringLiteral("无效的 IP 地址格式");
        emit inviteErrorChanged();
        return;
    }

    _inviteError.clear();
    emit inviteErrorChanged();

    // 尝试连接并发送定向 Hello
    sendDirectedHello(ip, 45678);
}

void ReachabilityController::testManualEndpoint(const QString &ip, quint16 tcpPort)
{
    if (ip.isEmpty()) {
        emit manualEndpointTestResult(false, QStringLiteral("IP 地址不能为空"));
        return;
    }

    QHostAddress addr;
    if (!addr.setAddress(ip)) {
        emit manualEndpointTestResult(false, QStringLiteral("无效的 IP 地址格式"));
        return;
    }

    _isProbing = true;
    emit isProbingChanged();

    _probe->probeTcp(ip, tcpPort, 3000);
}

void ReachabilityController::onManualEndpointProbeFinished(const EndpointProbe::ProbeResult &result,
                                                            const QString &deviceId,
                                                            const QString &deviceName,
                                                            quint16 discoveryPort)
{
    Q_UNUSED(deviceId);
    Q_UNUSED(deviceName);
    Q_UNUSED(discoveryPort);

    _isProbing = false;
    emit isProbingChanged();

    if (result.tcpConnected) {
        emit manualEndpointTestResult(true, QString());
    } else {
        emit manualEndpointTestResult(false, result.errorMessage);
    }
}