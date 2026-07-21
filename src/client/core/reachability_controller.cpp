/**
* @file    reachability_controller.cpp
* @version 7.0.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   网络可达性控制器实现
*
* 聚合 EndpointProbe 和 DiscoveryService，提供统一的网络诊断能力。
* QML 通过此控制器发起 TCP 探测和定向 Hello 操作。
*
* Change Log:
* [v7.0.0] GY   2026-07-21
* * Stage 7.0：新增网络可达性控制器，提供诊断入口
*/

#include "reachability_controller.h"
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
    // 连接探测完成信号
    connect(_probe, &EndpointProbe::probeFinished,
            this, &ReachabilityController::onProbeFinished);

    // 初始化本机地址列表
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

QStringList ReachabilityController::collectLocalAddresses() const
{
    QStringList addresses;
    const auto interfaces = QNetworkInterface::allInterfaces();

    for (const QNetworkInterface &iface : interfaces) {
        // 只处理激活的、非回环的物理网卡
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack) continue;
        if (iface.type() == QNetworkInterface::Loopback) continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            // 只处理 IPv4 地址
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