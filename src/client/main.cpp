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
* 支持命令行参数（本机回环测试用）：
*   --port <port>       指定 TCP 端口（默认 35100）
*   --config <path>     指定配置文件路径
*   --name <name>       指定设备名称
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0：空白窗口能起来；注册 PeerInfo 元类型
* [v0.2] GY   2026-06-02
* * Stage 3：添加命令行参数支持（本机回环测试）
* [v0.3] GY   2026-06-03
* * Stage 3：版本号更新，连接体验优化
* [v0.4] GY   2026-06-03
* * Stage 3.10：版本号更新到 v0.3.1
* [v0.5] GY   2026-06-04
* * Stage 4.1：DirSerializer 实现，版本号更新到 v0.4.1
* [v0.6] GY   2026-06-04
* * Stage 4.2：FileSenderWorker 支持多文件/目录传输
* [v0.7] GY   2026-06-04
* * Stage 4.3：FileReceiverWorker 支持多文件接收，版本号更新到 v0.4.3
* [v0.8] GY   2026-06-04
* * Stage 4.3：SHA-256 校验实现，版本号更新到 v0.4.4
* [v0.9] GY   2026-06-04
* * Stage 4.3：UI 适配，信号签名添加 totalFiles/totalBytes，版本号更新到 v0.4.5
*/

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "data_types.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("GridYard");
    QGuiApplication::setApplicationVersion("0.4.5");
    QGuiApplication::setOrganizationName("CQNU-SED");

    // 命令行参数解析
    QCommandLineParser parser;
    parser.setApplicationDescription("GridYard - 局域网文件传输工具");
    parser.addHelpOption();
    parser.addVersionOption();

    // --port 参数
    QCommandLineOption portOption("port", "TCP 端口", "port", "35100");
    parser.addOption(portOption);

    // --config 参数
    QCommandLineOption configOption("config", "配置文件路径", "path");
    parser.addOption(configOption);

    // --name 参数
    QCommandLineOption nameOption("name", "设备名称", "name");
    parser.addOption(nameOption);

    parser.process(app);

    // 设置环境变量，供 ConfigManager 读取
    if (parser.isSet(portOption)) {
        qputenv("GRIDYARD_PORT", parser.value(portOption).toUtf8());
    }
    if (parser.isSet(configOption)) {
        qputenv("GRIDYARD_CONFIG", parser.value(configOption).toUtf8());
    } else if (parser.isSet(portOption)) {
        // 如果指定了端口但没有指定配置文件，自动使用不同的配置文件
        // 这样每个实例会有不同的 deviceId
        QString autoConfig = QDir::tempPath() + "/gridyard_config_" + parser.value(portOption) + ".ini";
        qputenv("GRIDYARD_CONFIG", autoConfig.toUtf8());
    }
    if (parser.isSet(nameOption)) {
        qputenv("GRIDYARD_NAME", parser.value(nameOption).toUtf8());
    }

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
