/**
* @file    main.cpp
* @version   7.20.2
* @date 2026-10-05
* @author  GY
* @brief   GridYard 客户端程序入口
*
* main.cpp 只负责启动准备并显式创建 AppController。
* AppController 作为组合根初始化应用层对象和 UI 层。
* 所有 C++ 类型通过 QML_ELEMENT + qt_add_qml_module 路径自动注册，不走上下文属性。
* 自定义值类型（PeerInfo 等）在此统一
* qRegisterMetaType 注册，供跨线程 QueuedConnection 使用。
*
* 支持命令行参数（本机回环测试用）：
*   --port <port>       指定 TCP 端口（默认 35100）
*   --config <path>     指定配置文件路径
*   --name <name>       指定设备名称
*   --instance <N>      开发者模式实例号（1~9，本机多实例联调用）
*/

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQuickStyle>
#include <QStringList>

#include "app_controller.h"
#include "data_types.h"
#include "logger.h"

// 程序主函数入口，初始化应用并显式创建全局控制器
int main(int argc, char *argv[]) {
    QGuiApplication::setApplicationName("GridYard");
    QGuiApplication::setApplicationVersion("7.20.2");
    QGuiApplication::setOrganizationName("CQNU-SED");

    // 命令行参数须在 QApplication 构造前解析：X11 平台层会按 X 工具惯例
    // 吞掉 -name <值> 参数，导致后解析永远读不到设备名（多实例联调靠它区分实例）
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

    // --instance 参数（开发者模式：本机多实例联调，正常启动不带此参数）
    QCommandLineOption instanceOption("instance", "开发者实例号（1~9，隔离数据目录并偏移端口）", "N");
    parser.addOption(instanceOption);

    QStringList rawArguments;
    rawArguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        rawArguments << QString::fromLocal8Bit(argv[i]);
    }
    if (!parser.parse(rawArguments)) {
        qWarning("%s", qPrintable(parser.errorText()));
        return 1;
    }
    if (parser.isSet("help")) {
        parser.showHelp();
        return 0;
    }
    if (parser.isSet("version")) {
        parser.showVersion();
        return 0;
    }

    QApplication app(argc, argv);

    // 统一设置窗口图标，覆盖任务栏和窗口标题栏（QPixmap 依赖 QApplication，须后置）
    QGuiApplication::setWindowIcon(QIcon(":/qt/qml/cqnu/gridyard/client/icons/gridyard.png"));

    // 设置环境变量，供 ConfigManager 读取
    if (parser.isSet(portOption)) {
        qputenv("GRIDYARD_PORT", parser.value(portOption).toUtf8());
    }
    if (parser.isSet(configOption)) {
        qputenv("GRIDYARD_CONFIG", parser.value(configOption).toUtf8());
    }
    if (parser.isSet(nameOption)) {
        qputenv("GRIDYARD_NAME", parser.value(nameOption).toUtf8());
    }
    if (parser.isSet(instanceOption)) {
        qputenv("GRIDYARD_INSTANCE", parser.value(instanceOption).toUtf8());
    }

    QQuickStyle::setStyle("Material");

    // 初始化日志系统
    Logger::instance()->init();

    qRegisterMetaType<PeerInfo>("PeerInfo");

    AppController *controller = AppController::singleton();
    // QML 根对象创建失败时直接退出，避免进入无窗口事件循环。
    if (!controller->uiReady()) {
        return -1;
    }

    return app.exec();
}
