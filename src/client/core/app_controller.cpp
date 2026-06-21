/**
* @file    app_controller.cpp
* @version 4.7.1
* @date    2026-06-13
* @author  GridYard Team
* @brief   应用全局控制器实现
*
* 构造时创建并组装 ConfigManager、DiscoveryService、P2pServer、
* TransferSessionManager，启动 P2P 服务器并初始化传输会话管理器。
*
* Change Log:
* [v4.7.1] GY   2026-06-05
* * 创建 TransferSessionManager 并初始化
* [v0.2.0] GY   2026-06-02
* * Stage 2：持有 ConfigManager 和 DiscoveryService
* [v0.1.0] GY   2026-05-24
* * Stage 0：实现 applicationName / applicationVersion / quit
*/

#include "app_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"
#include "transfer_session_manager.h"

#include <QCoreApplication>
#include <QDebug>

// 构造函数：创建并组装所有核心模块
AppController::AppController(QObject *parent)
    : QObject{parent}
    , _config{ConfigManager::create(nullptr, nullptr)}
    , _discovery{new DiscoveryService{_config, this}}
    , _p2pServer{new P2pServer{_config, this}}
    , _transfer{new TransferSessionManager{this}}
{
    // 启动 P2P 服务器
    _p2pServer->start();

    // 初始化传输会话管理器
    _transfer->init(_config, _discovery, _p2pServer);
}

// QML_SINGLETON 工厂方法，引擎调用
AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    return new AppController{};
}

// 获取应用名称
QString AppController::applicationName() const
{
    return QCoreApplication::applicationName();
}

// 获取应用版本号
QString AppController::applicationVersion() const
{
    return QCoreApplication::applicationVersion();
}

// 获取设备发现服务（供 QML 绑定设备列表）
DiscoveryService *AppController::discovery() const
{
    return _discovery;
}

// 获取传输会话管理器
TransferSessionManager *AppController::transfer() const
{
    return _transfer;
}

// 退出应用
void AppController::quit()
{
    qDebug() << "AppController::quit invoked from QML";
    QCoreApplication::quit();
}

// 测试 C++↔QML 通信
void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
