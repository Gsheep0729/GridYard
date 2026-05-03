/**
* @file    app_controller.h
* @date    2026-05-24
* @author  GY
* @brief   GridYard 应用全局控制器（QML 单例）
*
* 按【代码规范 §3.5 四层架构】，AppController 是中介者单例。
* 持有 DiscoveryService 等下层模块，使 QML 通过
* AppController.discovery.peers 等路径触达。
* QML 端禁止使用 setContextProperty 暴露 C++ 对象。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：仅暴露 applicationName / applicationVersion / quit
* [v0.2] GY   2026-06-02
* * Stage 1：添加 test() 验证 C++↔QML 通信路径
* [v0.3] GY   2026-06-02
* * Stage 2：持有 DiscoveryService，暴露 discovery 属性给 QML
*/

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "discovery_service.h"

class QQmlEngine;
class QJSEngine;

class ConfigManager;
class P2pServer;

class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString applicationName    READ applicationName    CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(DiscoveryService* discovery READ discovery         CONSTANT)

public:
    // QML_SINGLETON 必需的工厂；引擎调用，外界不应直接 new
    static AppController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QString applicationName()    const;
    QString applicationVersion() const;

    // 获取设备发现服务（供 QML 绑定设备列表）
    DiscoveryService *discovery() const;

    Q_INVOKABLE void quit();
    Q_INVOKABLE void test();  // Stage 1：验证 C++↔QML 通信

signals:
    void appReady();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;

    ConfigManager    *_config    = nullptr;
    DiscoveryService *_discovery = nullptr;
    P2pServer        *_p2pServer = nullptr;
};
