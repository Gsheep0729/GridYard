/**
* @file    app_controller.h
* @date    2026-05-24
* @author  GY
* @brief   GridYard 应用全局控制器（QML 单例）
*
* 按【代码规范 §3.5 四层架构】，AppController 是中介者单例。
* Stage 0 仅承载应用元信息（名称、版本）与关闭意图；后续阶段
* 添加 Q_PROPERTY 持有 DiscoveryService、TransferSessionManager
* 等下层模块，使 QML 通过 AppController.discovery.peers 等路径
* 触达。QML 端禁止使用 setContextProperty 暴露 C++ 对象。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：仅暴露 applicationName / applicationVersion / quit
*/

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString applicationName    READ applicationName    CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)

public:
    // QML_SINGLETON 必需的工厂；引擎调用，外界不应直接 new
    static AppController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QString applicationName()    const;
    QString applicationVersion() const;

    Q_INVOKABLE void quit();

signals:
    void appReady();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;
};
