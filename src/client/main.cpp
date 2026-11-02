/**
 * @file    main.cpp
 * @version 8.3.0
 * @date    2026-11-02
 * @author  GridYard Team
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
#include <QMessageBox>
#include <QQuickStyle>
#include <QSocketNotifier>
#include <QStringList>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <csignal>

#include "app_controller.h"
#include "data_types.h"
#include "logger.h"
#include "single_instance_guard.h"

#ifndef _WIN32
namespace {

// SIGTERM/SIGINT 自管对写端：信号处理器只做一次字节写入（async-signal-safe），
// 排空动作全部延迟到主线程事件循环中执行
int g_signalPipe[2] = {-1, -1};

extern "C" void handleTerminationSignal(int)
{
    const char token = 1;
    ssize_t written = 0;
    do {
        written = ::write(g_signalPipe[1], &token, 1);
    } while (written < 0 && errno == EINTR);
}

}  // namespace
#endif  // !_WIN32

// 程序主函数入口，初始化应用并显式创建全局控制器
int main(int argc, char *argv[]) {
#ifdef _WIN32
    // Windows 分支：进程入口一次性 WSAStartup，WSACleanup 由局部对象在所有
    // 退出路径对偶释放——发现/监听/传输各套接字面的 Winsock 前置条件由此
    // 不依赖 NetReactor 惰性初始化的先后次序（尖兵报告 B3/P10 面）
    struct WinsockLifetime {
        WinsockLifetime()
        {
            WSADATA data{};
            if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
                qCritical("GridYard: WSAStartup 失败（%d），网络功能不可用",
                          ::WSAGetLastError());
            }
        }
        ~WinsockLifetime() { ::WSACleanup(); }
    } winsockLifetime;
#endif

#ifndef _WIN32
    // 对端关闭后继续写入时忽略 SIGPIPE，套接字层另有 MSG_NOSIGNAL 逐次兜底
    std::signal(SIGPIPE, SIG_IGN);

    // SIGTERM/SIGINT 自管对：处理器仅写一字节，读端经 QSocketNotifier 在主线程
    // 事件循环触发既有排空链（与托盘退出同链路），不弹任何确认框
    if (::pipe2(g_signalPipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        g_signalPipe[0] = g_signalPipe[1] = -1;
    } else {
        struct sigaction action {};
        action.sa_handler = handleTerminationSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        ::sigaction(SIGTERM, &action, nullptr);
        ::sigaction(SIGINT, &action, nullptr);
    }
#endif  // !_WIN32

    QGuiApplication::setApplicationName("GridYard");
    QGuiApplication::setApplicationVersion("8.3.0");
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
        // 端口须为 1~65535 的数字：ConfigManager 侧按 quint16 接收，超出范围的
        // 值会被静默截断成其他端口（如 70000 变 4464），入口处直接拒绝
        const QString portValue = parser.value(portOption);
        bool portOk = false;
        const quint16 parsedPort = portValue.toUShort(&portOk);
        if (!portOk || parsedPort == 0) {
            qCritical("无效的 --port 参数：%s（应为 1~65535 之间的数字）", qPrintable(portValue));
            return 1;
        }
        qputenv("GRIDYARD_PORT", portValue.toUtf8());
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

    // 单实例守卫按实例配置目录互斥：第二实例投递唤回请求后立即退出，
    // 不再创建窗口与网络服务，避免同身份双进程争夺监听与发现端口
    SingleInstanceGuard instanceGuard;
    if (!instanceGuard.tryAcquire()) {
        instanceGuard.notifyExistingInstance();
        QMessageBox::information(nullptr, QStringLiteral("GridYard"),
                                 QStringLiteral("鸽邮已在运行"));
        return 0;
    }

    qRegisterMetaType<PeerInfo>("PeerInfo");

    AppController *controller = AppController::singleton();
    // QML 根对象创建失败时直接退出，避免进入无窗口事件循环。
    if (!controller->uiReady()) {
        return -1;
    }

    // 第二实例的唤回请求经组合根信号交给表现层，完成隐藏窗口的恢复与置顶
    QObject::connect(&instanceGuard, &SingleInstanceGuard::activationRequested,
                     controller, &AppController::windowActivationRequested);

    // 信号读端注册进主线程：读空管道后调用 AppController::quit() 既有排空链，
    // requestQuit 内部防重入、重复信号安全；普通关窗路径不受影响
#ifndef _WIN32
    if (g_signalPipe[0] >= 0) {
        auto *signalNotifier = new QSocketNotifier(g_signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(signalNotifier, &QSocketNotifier::activated, &app, [] {
            char drain[64];
            while (::read(g_signalPipe[0], drain, sizeof(drain)) > 0) {
            }
            AppController::singleton()->quit();
        });
    }
#endif  // !_WIN32

    return app.exec();
}
