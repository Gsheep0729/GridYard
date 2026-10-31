/**
* @file    reachability_controller.cpp
* @version 7.22.0
* @date 2026-10-07
* @author  GridYard Team
* @brief   网络可达性控制器实现
*
* 聚合 EndpointProbe 和 DiscoveryService，提供统一的网络诊断能力。
* QML 通过此控制器发起 TCP 探测、定向 Hello 和邀请文本处理。
* 协调服务器启用时，支持向协调节点查询候选设备列表。
*/

#include "reachability_controller.h"
#include "config_manager.h"
#include "data_types.h"
#include "discovery_service.h"
#include "endpoint_probe.h"
#include "history_records.h"
#include "network/rendezvous_client.h"
#include "protocol.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <QJSEngine>

// 单例静态成员定义
QPointer<ReachabilityController> ReachabilityController::s_instance;

// 构造函数
ReachabilityController::ReachabilityController(QObject *parent)
    : QObject{parent}
    , _probe{new EndpointProbe{this}}
{
    connect(_probe, &EndpointProbe::probeFinished,
            this, &ReachabilityController::onProbeFinished);

    _localAddresses = collectLocalAddresses();
}

// QML 单例工厂函数
ReachabilityController *ReachabilityController::create(QJSEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine);
    Q_UNUSED(scriptEngine);
    return singleton();
}

// 获取单例实例
ReachabilityController *ReachabilityController::singleton()
{
    if (s_instance.isNull()) {
        s_instance = new ReachabilityController();
    }
    return s_instance;
}

// 获取本机所有 IPv4 地址列表
QStringList ReachabilityController::localAddresses() const
{
    return _localAddresses;
}

// 获取最近一次探测结果
QVariantMap ReachabilityController::lastProbeResult() const
{
    return _lastProbeResult;
}

// 当前是否正在探测
bool ReachabilityController::isProbing() const
{
    return _isProbing;
}

// 获取最近一次生成的邀请文本
QString ReachabilityController::lastInviteText() const
{
    return _lastInviteText;
}

// 获取最近一次邀请操作的错误信息
QString ReachabilityController::inviteError() const
{
    return _inviteError;
}

// 协调服务器是否启用
bool ReachabilityController::rendezvousEnabled() const
{
    return _config && _config->rendezvousEnabled();
}

// 探测指定端点
void ReachabilityController::probeEndpoint(const QString &ip, quint16 tcpPort, int timeoutMs)
{
    _isProbing = true;
    emit isProbingChanged();

    _probe->probeTcp(ip, tcpPort, timeoutMs);
}

// 发送定向 Hello
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

// 刷新本机地址列表
void ReachabilityController::refreshLocalAddresses()
{
    _localAddresses = collectLocalAddresses();
    emit localAddressesChanged();
}

// 设置 DiscoveryService 引用
void ReachabilityController::setDiscoveryService(DiscoveryService *discovery)
{
    _discovery = discovery;
}

// 设置 ConfigManager 引用
void ReachabilityController::setConfigManager(ConfigManager *config)
{
    _config = config;
    // 监听配置变化以更新 rendezvousEnabled 状态
    if (_config) {
        connect(_config, &ConfigManager::rendezvousEnabledChanged,
                this, &ReachabilityController::rendezvousEnabledChanged);
    }
}

// 设置 RendezvousClient 引用
void ReachabilityController::setRendezvousClient(RendezvousClient *client)
{
    _rendezvousClient = client;
    if (_rendezvousClient) {
        connect(_rendezvousClient, &RendezvousClient::peersReceived,
                this, [this](const QList<QVariantMap> &peers) {
                    emit rendezvousPeersReceived(peers);
                });
    }
}

// 向协调服务器查询在线设备列表
void ReachabilityController::queryRendezvousPeers()
{
    if (!_rendezvousClient) {
        qWarning() << "ReachabilityController: RendezvousClient 未设置";
        return;
    }
    if (!_rendezvousClient->isConnected()) {
        qWarning() << "ReachabilityController: 协调服务器未连接";
        return;
    }
    _rendezvousClient->listPeers(QStringLiteral("default"));
}

// 收集本机所有有效的 IPv4 地址
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

// 处理探测完成结果
void ReachabilityController::onProbeFinished(const EndpointProbe::ProbeResult &result)
{
    _isProbing = false;
    emit isProbingChanged();

    // 探测器与手动端点测试共用，只有目标与 pending 邀请上下文一致的探测才触发注入
    const QVariantMap pending = _lastProbeResult;
    if (pending.value("pending").toBool()
            && pending.value(gy::keys::kEndpointIpAddress).toString() == result.targetIp
            && pending.value(gy::keys::kEndpointTcpPort).toInt() == result.targetPort) {
        if (result.tcpConnected) {
            injectDirectedPeerFromInvite(pending);
            // 注入已提交设备目录落库，界面随后写好友标记即可命中该行
            emit invitePeerInjected(pending.value(QStringLiteral("deviceId")).toString());
        } else {
            // TCP 不可达时不注入，避免制造无法收发的"在线卡"，失败反馈走既有行内错误
            qDebug() << "ReachabilityController: 邀请目标探测失败，不注入设备条目"
                     << result.targetIp << ":" << result.targetPort;
        }
    }

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

// 邀请导入探测成功后，以邀请文本携带的真实身份注入设备表
void ReachabilityController::injectDirectedPeerFromInvite(const QVariantMap &invite)
{
    if (!_discovery) {
        return;
    }

    PeerInfo info;
    info.deviceId   = invite.value(QStringLiteral("deviceId")).toString();
    info.deviceName = invite.value(gy::keys::kEndpointDeviceName).toString();
    info.ipAddress  = invite.value(gy::keys::kEndpointIpAddress).toString();
    info.tcpPort    = static_cast<quint16>(invite.value(gy::keys::kEndpointTcpPort).toInt());

    // 空名兜底与广播 Hello 的处理口径一致，避免产生空名卡片
    if (info.deviceName.trimmed().isEmpty()) {
        info.deviceName = QStringLiteral("未知设备 (%1)").arg(info.ipAddress);
    }

    _discovery->addDirectedPeer(info);
}

// 生成当前设备的邀请文本
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

    // 邀请码端口照实携带：开发者实例的发现端口按实例号偏移绑定，取实际值；
    // 正常模式维持默认端口（bind 回退随机端口时的既有问题单独立项处理）
    invite.discoveryPort = gy::protocol::kDefaultDiscoveryPort;
    if (_config->instanceNumber() > 0 && _discovery) {
        invite.discoveryPort = _discovery->localDiscoveryPort();
    }

    _lastInviteText = InviteCodec::encode(invite);
    _inviteError.clear();
    emit lastInviteTextChanged();
    emit inviteErrorChanged();

    return _lastInviteText;
}

// 导入邀请文本
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
        {gy::keys::kEndpointDeviceName, invite.deviceName},
        {gy::keys::kEndpointIpAddress, invite.ipAddress},
        {gy::keys::kEndpointTcpPort, invite.tcpPort},
        {"discoveryPort", invite.discoveryPort},
        {"pending", true},
    };

    // 发送定向 Hello
    sendDirectedHello(invite.ipAddress, invite.discoveryPort > 0 ? invite.discoveryPort : 45678);

    emit inviteImported(true, invite.deviceId, QString());
}

// 手动添加端点（来源标记为 manual）
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

    // 直接添加到设备列表，标记为 manual 来源
    if (_discovery) {
        PeerInfo info;
        info.deviceId = gy::domain::manualPseudoDeviceId(ip);  // 真身合并前的占位 ID
        info.deviceName = QStringLiteral("手动端点 (%1)").arg(ip);
        info.ipAddress = ip;
        info.tcpPort = tcpPort;
        info.isOnline = true;
        info.lastSeen = QDateTime::currentDateTimeUtc();
        info.source = QStringLiteral("manual");
        _discovery->addManualPeer(info);
    }

    // 发送定向 Hello 尝试建立连接
    sendDirectedHello(ip, 45678);
}

// 测试手动端点
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

// 处理手动端点探测完成
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
