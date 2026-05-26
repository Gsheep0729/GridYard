/**
 * @file    app_controller.cpp
 * @date    2026-05-24
 * @author  GY
 * @brief   AppController 实现
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

#include "app_controller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

AppController::AppController(QObject *parent)
    : QObject{parent}
{
    // 引擎完成对象构造的下一帧再发 appReady，确保 QML 端的
    // Connections target: AppController 已经就绪
    QTimer::singleShot(0, this, &AppController::appReady);
}

AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    auto *instance = new AppController{};
    // 工厂返回的对象由引擎接管生命周期；不要 setParent(engine)
    Q_UNUSED(engine);
    return instance;
}

QString AppController::applicationName() const
{
    return QCoreApplication::applicationName();
}

QString AppController::applicationVersion() const
{
    return QCoreApplication::applicationVersion();
}

void AppController::quit()
{
    qDebug() << "AppController::quit invoked from QML";
    QCoreApplication::quit();
}
