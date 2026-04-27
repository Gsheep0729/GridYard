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
*/

#include "app_controller.h"

#include <QCoreApplication>
#include <QDebug>

AppController::AppController(QObject *parent)
    : QObject{parent}
{
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

void AppController::quit()
{
    qDebug() << "AppController::quit invoked from QML";
    QCoreApplication::quit();
}

void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
