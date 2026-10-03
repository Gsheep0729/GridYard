/**
* @file    rendezvous_coordinator.h
* @version 7.15.16
* @date    2026-10-04
* @author  GridYard Team
* @brief   协调节点编排器
*
* 封装协调服务器的连接、注册、候选拉取与中继邀请接线：
* 按配置启停协调连接，监听 rendezvousEnabled/Host/Port/Token 配置变化即时生效，
* 不再需要重启应用。协调客户端对象本身由组合根持有，多个模块共享。
*
* Change Log:
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
* * 访问令牌纳入防抖比较与连接下发
* [v7.11.0] GY   2026-10-02
* * 自 AppController 拆出协调编排，修复修改协调配置需要重启才能生效的问题
*/

#pragma once

#include <QObject>
#include <QString>

class QTimer;
class ConfigManager;
class DiscoveryService;
class P2pServer;
class RendezvousClient;

class RendezvousCoordinator : public QObject {
    Q_OBJECT

public:
    explicit RendezvousCoordinator(ConfigManager *config, DiscoveryService *discovery,
                                   P2pServer *p2pServer, RendezvousClient *rendezvousClient,
                                   QObject *parent = nullptr);

    // 按当前配置启动或停止协调连接（构造后调用一次，此后随配置变化自动响应）
    void applyConfig();

private:
    // 连接并注册到指定协调服务器，同时启动候选拉取定时器
    void startRendezvous(const QString &host, quint16 port);
    // 断开协调服务器并停止候选拉取
    void stopRendezvous();
    // 连接成功后向协调服务器注册本机端点并查询一次在线设备
    void registerSelf();

    ConfigManager    *_config    = nullptr; // 协调配置来源
    DiscoveryService *_discovery = nullptr; // 接收候选端点并合并进设备列表
    P2pServer        *_p2pServer = nullptr; // 中继邀请到达后由它加入中继会话
    RendezvousClient *_rendezvous = nullptr; // 共享的协调客户端

    QTimer *_queryTimer = nullptr;  // 周期拉取在线设备
    bool _active = false;           // 协调连接是否处于启用状态
    QString _activeHost;            // 当前启用的服务器地址（防抖：目标未变不重连）
    quint16 _activePort = 0;        // 当前启用的服务器端口
    QString _activeToken;           // 当前启用的访问令牌
};
