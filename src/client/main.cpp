/**
* @file    main.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   GridYard 客户端程序入口
*
* 启动 QQmlApplicationEngine，通过 loadFromModule 加载
* cqnu.gridyard.client 模块的 Main 根 QML。所有 C++ 类型通过
* QML_ELEMENT + qt_add_qml_module 路径自动注册，不走上下文属性。
* 自定义值类型（PeerInfo 等）在此统一
* qRegisterMetaType 注册，供跨线程 QueuedConnection 使用。
*
* 支持命令行参数（本机回环测试用）：
*   --port <port>       指定 TCP 端口（默认 35100）
*   --config <path>     指定配置文件路径
*   --name <name>       指定设备名称
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 修复聊天输入框对齐、历史首屏恢复、设备列表搜索刷新和传输清空范围
* [v6.6.1] GY   2026-06-25
* * 修复托盘退出确认与聊天输入框占位提示显示问题
* [v6.6.0] GY   2026-06-25
* * 完成本地数据层异常验收和交付收口版本同步
* [v6.5.0] GY   2026-06-25
* * 接入系统托盘、非阻塞通知与历史降级状态提示
* [v6.3.0] GY   2026-06-25
* * 持久化结束态传输历史并支持启动恢复，版本同步到 v6.3.0
* [v6.2.0] GY   2026-06-25
* * 持久化在线聊天记录并支持历史加载，版本同步到 v6.2.0
* [v6.1.0] GY   2026-06-25
* * 应用版本号同步到 v6.1.0，Stage 6 阶段 B 验收
* [v5.4.0] GY   2026-06-24
* * 完成局域网在线聊天阶段验收
* [v5.3.0] DuRuoxian   2026-06-24
* * 完成在线聊天会话页集成阶段
* [v5.2.0] DuRuoxian   2026-06-24
* * 完成在线聊天消息模型和气泡视图阶段
* [v5.1.0] FengChunlin   2026-06-24
* * 完成在线聊天连接和内存会话阶段
* [v5.0.0] FengChunlin   2026-06-23
* * 版本号更新到 v4.16.4
* [v4.16.3] GY   2026-06-23
* * 版本号更新到 v4.16.3
* [v4.16.2] DuRuoxian   2026-06-21
* * 添加窗口图标设置，解决任务栏图标缺失问题
* [v4.16.1] GY   2026-06-20
* * 优化封装性并补充注释，版本同步到 v4.16.1
* [v4.16.0] DuRuoxian   2026-06-18
* * 版本号更新到 v4.16.0
* [v4.15.2] DuRuoxian   2026-06-17
* * 版本号更新到 v4.15.2
* [v4.14.2] DuRuoxian   2026-06-16
* * 版本号更新到 v4.14.2
* [v4.14.0] GY   2026-06-15
* * 版本号更新到 v4.14.0
* [v4.13.3] DuRuoxian   2026-06-15
* * 版本号更新到 v4.13.3
* [v4.13.2] DuRuoxian   2026-06-15
* * 版本号更新到 v4.13.2
* [v4.13.1] DuRuoxian   2026-06-15
* * 版本号更新到 v4.13.1
* [v4.13.0] GY   2026-06-15
* * 版本号更新到 v4.13.0
* [v4.12.1] GY   2026-06-14
* * 版本号更新到 v4.12.1
* [v4.12.0] FengChunlin   2026-06-14
* * 版本号更新到 v4.12.0
* [v4.11.0] GY   2026-06-13
* * 版本号更新到 v4.11.0
* [v4.10.0] GY   2026-06-13
* * 版本号更新到 v4.10.0
* [v4.8.2] GY   2026-06-09
* * 修复 KDE 原生文件选择器
* [v4.7.1] GY   2026-06-06
* * 初始化 Logger，添加命令行参数支持
* [v0.3.1] GY   2026-05-21
* * 版本号更新到 v0.3.1
* [v0.2.0] DuRuoxian   2026-05-14
* * Stage 3：添加命令行参数支持（本机回环测试）
* [v0.1.0] GY   2026-04-03
* * Stage 0：空白窗口能起来；注册 PeerInfo 元类型
*/

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "data_types.h"
#include "logger.h"

namespace {

// 配置 Linux 桌面环境下 Qt 平台主题，使 KDE 使用原生文件选择器
void configurePlatformTheme()
{
#ifdef Q_OS_LINUX
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP");
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")
        && desktop.contains("KDE", Qt::CaseInsensitive)) {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }
#endif
}

}

// 程序主函数入口，初始化应用、解析命令行参数并加载 QML 引擎
int main(int argc, char *argv[]) {
    configurePlatformTheme();

    QGuiApplication app(argc, argv);

    QGuiApplication::setApplicationName("GridYard");
    QGuiApplication::setApplicationVersion("6.6.2");
    QGuiApplication::setOrganizationName("CQNU-SED");
    QGuiApplication::setWindowIcon(QIcon(":/qt/qml/cqnu/gridyard/client/icons/gridyard.png"));

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

    // 初始化日志系统
    Logger::instance()->init();

    qRegisterMetaType<PeerInfo>("PeerInfo");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app,    []{ QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.loadFromModule("cqnu.gridyard.client", "Main");
    // QML 根对象创建失败时直接退出，避免进入无窗口事件循环
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
