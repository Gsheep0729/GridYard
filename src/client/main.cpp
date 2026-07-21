/**
* @file    main.cpp
* @version 7.0.0
* @date    2026-07-21
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
*
* Change Log:
* [v6.8.1] GY   2026-06-28
* * 补充源码行内注释，移除无效平台主题配置函数，版本同步到 v6.8.1
* [v6.8.0] GY   2026-06-28
* * 调整运行时数据目录策略
* [v6.7.0] GY   2026-06-28
* * 取消 --port 自动生成 /tmp 临时配置文件
* * 应用版本号更新到 6.7.0
* [v6.6.3] GY   2026-06-28
* * 提升 core 和 ui 分组源码行内注释密度，同步版本号
* [v6.6.2] GY   2026-06-25
* * 调整启动入口，由 AppController 负责系统和 UI 初始化
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

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QQuickStyle>

#include "app_controller.h"
#include "data_types.h"
#include "logger.h"

// 程序主函数入口，初始化应用并显式创建全局控制器
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QGuiApplication::setApplicationName("GridYard");
    QGuiApplication::setApplicationVersion("7.8.0");
    QGuiApplication::setOrganizationName("CQNU-SED");
    // 统一设置窗口图标，覆盖任务栏和窗口标题栏
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
    }
    if (parser.isSet(nameOption)) {
        qputenv("GRIDYARD_NAME", parser.value(nameOption).toUtf8());
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
