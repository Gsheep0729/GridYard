/**
* @file    rendezvous_coordinator.cpp
* @version 7.17.2
* @date    2026-10-04
* @author  GridYard Team
* @brief   协调节点编排器实现
*
* Change Log:
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.14.0] GY   2026-10-03
* * 访问令牌随连接下发并在配置变化时重连
* [v7.11.0] GY   2026-10-02
* * 自 AppController 拆出协调编排，修复修改协调配置需要重启才能生效的问题
*/

#include "rendezvous_coordinator.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"
#include "protocol.h"
#include "rendezvous_client.h"

#include <QDebug>
#include <QTimer>
#include <QVariantMap>

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

// 连接成功后向协调服务器注册本机端点并查询一次在线设备
void RendezvousCoordinator::registerSelf()
{
    QStringList addresses;
    if (!_config->localIp().isEmpty()) {
        addresses.append(_config->localIp());
    }
    _rendezvous->registerDevice(gy::protocol::kDefaultRendezvousRoom,
                                _config->deviceId(),
                                _config->deviceName(),
                                addresses,
                                _config->tcpPort(),
                                gy::protocol::kDefaultDiscoveryPort);
    _rendezvous->listPeers(gy::protocol::kDefaultRendezvousRoom);
}
