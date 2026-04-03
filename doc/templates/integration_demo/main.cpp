/**
 * @file    main.cpp
 * @date    2026-05-24
 * @author  GY
 * @brief   integration_demo 程序入口
 *
 * 演示 GridYard 推荐的 QML 程序启动顺序：
 *   1. 启用高 DPI 缩放策略
 *   2. 注册跨线程通信用的自定义类型
 *   3. 通过 loadFromModule 而非 load(url) 加载根 QML
 * 全程不使用 setContextProperty，C++ 数据通过 QML_SINGLETON
 * 或 QML_ELEMENT 注册的方式暴露给 QML 层。
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "peer_info.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("GridYard Integration Demo");
    QGuiApplication::setApplicationVersion("1.0.0");
    QGuiApplication::setOrganizationName("CQNU-SED");

    QQuickStyle::setStyle("Material");

    // 自定义值类型用于跨线程 QueuedConnection signal 时必须注册
    // 本 demo 单线程，注册是预防性的，落到规范 §十.2
    qRegisterMetaType<PeerInfo>("PeerInfo");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app,    []{ QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    // loadFromModule(<URI>, <RootTypeName>) 是 Qt 6.5 推荐方式
    // URI 必须与 CMakeLists.txt 中 qt_add_qml_module 的 URI 一致
    engine.loadFromModule("cqnu.gridyard.demo", "Main");

    return app.exec();
}
