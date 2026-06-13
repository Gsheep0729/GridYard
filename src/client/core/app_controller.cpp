/**
* @file    app_controller.cpp
* @version 4.10.0
* @date    2026-06-13
* @author  GY
* @brief   AppController 实现
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

AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    return new AppController{};
}

QString AppController::applicationName() const
{
    return QCoreApplication::applicationName();
}

QString AppController::applicationVersion() const
{
    return QCoreApplication::applicationVersion();
}

DiscoveryService *AppController::discovery() const
{
    return _discovery;
}

TransferSessionManager *AppController::transfer() const
{
    return _transfer;
}

void AppController::quit()
{
    qDebug() << "AppController::quit invoked from QML";
    QCoreApplication::quit();
}

void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
