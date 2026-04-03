/**
* @file    main.cpp
* @date    2026-05-24
* @author  GY
* @brief   GridYard 客户端程序入口
*
* 启动 QQmlApplicationEngine，通过 loadFromModule 加载
* cqnu.gridyard.client 模块的 Main 根 QML。所有 C++ 类型通过
* QML_ELEMENT + qt_add_qml_module 路径自动注册，全程不使用
* setContextProperty。自定义值类型（PeerInfo 等）在此统一
* qRegisterMetaType 注册，供 Stage 2+ 跨线程 QueuedConnection 使用。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：空白窗口能起来；注册 PeerInfo 元类型
*/

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "data_types.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("GridYard");
    QGuiApplication::setApplicationVersion("0.1.0");
    QGuiApplication::setOrganizationName("CQNU-SED");

    QQuickStyle::setStyle("Material");

    qRegisterMetaType<PeerInfo>("PeerInfo");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app,    []{ QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.loadFromModule("cqnu.gridyard.client", "Main");

    return app.exec();
}
