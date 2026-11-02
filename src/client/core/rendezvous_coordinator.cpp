/**
* @file    rendezvous_coordinator.cpp
* @version 8.3.0
* @date 2026-11-03
* @author  GridYard Team
* @brief   协调节点编排器实现
*/

#include "rendezvous_coordinator.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "net_interfaces.h"
#include "p2p_server.h"
#include "protocol.h"
#include "rendezvous_client.h"

#include <QDebug>
#include <QTimer>
#include <QVariantMap>

#ifdef _WIN32
// Windows 头链把 interface 定义成宏，展开会击穿下方以 interface 命名的循环变量
#undef interface
#endif

// 候选设备列表的周期拉取间隔
static constexpr int kPeerQueryIntervalMs = 10000;

// 构造函数：接线配置变化、协调客户端信号与中继邀请
RendezvousCoordinator::RendezvousCoordinator(ConfigManager *config, DiscoveryService *discovery,
                                             P2pServer *p2pServer, RendezvousClient *rendezvousClient,
                                             QObject *parent)
    : QObject{parent}
    , _config{config}
    , _discovery{discovery}
    , _p2pServer{p2pServer}
    , _rendezvous{rendezvousClient}
    , _queryTimer{new QTimer{this}}
{
    _queryTimer->setInterval(kPeerQueryIntervalMs);
    connect(_queryTimer, &QTimer::timeout, this, [this]() {
        // 客户端断线时由其内部重连逻辑恢复，这里只在已连接时刷新列表
        if (_rendezvous->isConnected()) {
            _rendezvous->listPeers(gy::protocol::kDefaultRendezvousRoom);
        }
    });

    // 配置变化即时生效：启停或切换协调服务器，无需重启应用
    connect(_config, &ConfigManager::rendezvousEnabledChanged,
            this,    &RendezvousCoordinator::applyConfig);
    connect(_config, &ConfigManager::rendezvousHostChanged,
            this,    &RendezvousCoordinator::applyConfig);
    connect(_config, &ConfigManager::rendezvousPortChanged,
            this,    &RendezvousCoordinator::applyConfig);
    connect(_config, &ConfigManager::rendezvousTokenChanged,
            this,    &RendezvousCoordinator::applyConfig);

    // 连接成功后注册本机端点并查询一次在线设备
    connect(_rendezvous, &RendezvousClient::connected, this, [this]() {
        registerSelf();
    });

    // 换网/IP 变化自愈：本机地址实际变化即以新地址重注册，不等断线重连。
    // localIpChanged 只在 refreshLocalIp 解析出不同地址时发出，同刻多次网络
    // 回调已被合并，重注册天然节流；未连接时由重连后的注册带回新地址
    connect(_config, &ConfigManager::localIpChanged, this, [this]() {
        if (_active && _rendezvous->isConnected()) {
            registerSelf();
        }
    });

    // 协调节点返回的候选端点加入设备列表
    connect(_rendezvous, &RendezvousClient::peersReceived,
            _discovery, &DiscoveryService::onRendezvousPeersReceived);

    // 轮询到目标为本机的中继邀请：连接中继服务器并加入会话，
    // 后续字节就是标准 TLV 传输流，由 P2pServer 首帧路由接管
    connect(_rendezvous, &RendezvousClient::relayInvitesReceived,
            this, [this](const QList<QVariantMap> &invites) {
                for (const QVariantMap &invite : invites) {
                    const QString relayId = invite[QStringLiteral("relayId")].toString();
                    if (relayId.isEmpty()) {
                        continue;
                    }
                    _p2pServer->joinRelaySession(_config->rendezvousHost(),
                                                 static_cast<quint16>(_config->rendezvousPort()),
                                                 relayId);
                }
            });
}

// 按当前配置启动或停止协调连接
void RendezvousCoordinator::applyConfig()
{
    if (!_config->rendezvousEnabled()) {
        stopRendezvous();
        return;
    }

    const QString host = _config->rendezvousHost();
    const quint16 port = static_cast<quint16>(_config->rendezvousPort());
    const QString token = _config->rendezvousToken();
    if (_active && host == _activeHost && port == _activePort && token == _activeToken) {
        return;  // 目标未变，避免设置页连续修改触发重复重连
    }
    stopRendezvous();
    startRendezvous(host, port);
}

// 连接并注册到指定协调服务器
void RendezvousCoordinator::startRendezvous(const QString &host, quint16 port)
{
    qDebug() << "RendezvousCoordinator: 启动协调连接" << host << ":" << port;
    _active = true;
    _activeHost = host;
    _activePort = port;
    _activeToken = _config->rendezvousToken();
    _rendezvous->setToken(_activeToken);
    _rendezvous->connectToServer(host, static_cast<int>(port));
    if (!_queryTimer->isActive()) {
        _queryTimer->start();
    }
}

// 断开协调服务器并停止候选拉取
void RendezvousCoordinator::stopRendezvous()
{
    if (!_active && !_queryTimer->isActive()) {
        return;
    }
    qDebug() << "RendezvousCoordinator: 停止协调连接";
    _active = false;
    _activeHost.clear();
    _activePort = 0;
    _activeToken.clear();
    _queryTimer->stop();
    // 手动断开后客户端不再自动重连，重新启用由 applyConfig 驱动
    _rendezvous->disconnectFromServer();
}

// 收集注册要上报的本机 IPv4 候选（C-02）：默认路由地址 localIp 置首，保持
// 对端"首地址即直连端点"的既有语义；其余非回环地址全部紧随，VPN/多子网场景
// 下对端候选轮询真正生效。docker/网桥类虚拟网卡按名单剔除（C-04 名单随本项
// 同批落地），不向对端散播容器网段地址
static QStringList collectAnnouncedAddresses(const ConfigManager *config)
{
    QStringList addresses;
    if (!config->localIp().isEmpty()) {
        addresses.append(config->localIp());
    }

    const QList<gy::net::NetInterface> interfaces = gy::net::listInterfaces();
    for (const gy::net::NetInterface &interface : interfaces) {
        if (!interface.isUp || interface.isLoopback || !interface.address.isValid()) {
            continue;
        }
        if (gy::net::isVirtualInterfaceName(interface.name)) {
            continue;
        }
        const QString address = interface.address.toString();
        if (!addresses.contains(address)) {
            addresses.append(address);
        }
    }
    return addresses;
}

// 连接成功后向协调服务器注册本机端点并查询一次在线设备
void RendezvousCoordinator::registerSelf()
{
    _rendezvous->registerDevice(gy::protocol::kDefaultRendezvousRoom,
                                _config->deviceId(),
                                _config->deviceName(),
                                collectAnnouncedAddresses(_config),
                                _config->announcedTcpPort(),
                                gy::protocol::kDefaultDiscoveryPort);
    _rendezvous->listPeers(gy::protocol::kDefaultRendezvousRoom);
}
