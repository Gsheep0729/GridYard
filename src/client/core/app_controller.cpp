/**
* @file    app_controller.cpp
* @date    2026-05-24
* @author  GY
* @brief   AppController 实现
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：实现 applicationName / applicationVersion / quit
* [v0.2] GY   2026-06-02
* * Stage 1：添加 test() 验证 C++↔QML 通信路径
* [v0.3] GY   2026-06-02
* * Stage 2：持有 ConfigManager 和 DiscoveryService
*/

#include "app_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"

#include <QCoreApplication>
#include <QDebug>

AppController::AppController(QObject *parent)
    : QObject{parent}
    , _config{ConfigManager::create(nullptr, nullptr)}
    , _discovery{new DiscoveryService{_config, this}}
    , _p2pServer{new P2pServer{_config, this}}
{
    // 启动 P2P 服务器
    _p2pServer->start();
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

void AppController::quit()
{
    qDebug() << "AppController::quit invoked from QML";
    QCoreApplication::quit();
}

void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
