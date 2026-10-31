/**
* @file    app_controller.cpp
* @version 8.2.1
* @date 2026-11-01
* @author  GridYard Team
* @brief   应用全局控制器实现
*/

#include "app_controller.h"

#include "backup_controller.h"
#include "chat_manager.h"
#include "chat_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "history_controller.h"
#include "history_wiring.h"
#include "local_data_broker.h"
#include "local_ip_monitor.h"
#include "p2p_server.h"
#include "peer_discovery_view_model.h"
#include "reachability_controller.h"
#include "rendezvous_coordinator.h"
#include "shutdown_controller.h"
#include "transfer_controller.h"
#include "transfer_session_manager.h"
#include "network/file_receiver_worker.h"
#include "network/rendezvous_client.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QPointer>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlEngine>

namespace {

QPointer<AppController> s_appController;

}

// 构造并装配应用运行期依赖
AppController::AppController(QObject *parent)
    : QObject{parent}
    , _config{ConfigManager::create(nullptr, nullptr)}
    , _discovery{new DiscoveryService{_config, this}}
    , _p2pServer{new P2pServer{_config, this}}
    , _transfer{new TransferSessionManager{this}}
    , _chat{new ChatManager{this}}
    , _peerDiscoveryViewModel{new PeerDiscoveryViewModel{_discovery, this}}
    , _transferController{new TransferController{_transfer, this}}
    , _chatController{new ChatController{_chat, this}}
    , _dataBroker{new LocalDataBroker{this}}
    , _history{new HistoryController{_chat, _transfer, _config, _dataBroker, this}}
    , _reachability{new ReachabilityController{this}}
    , _rendezvousClient{new RendezvousClient{this}}
    , _rendezvousCoordinator{new RendezvousCoordinator{_config, _discovery, _p2pServer,
                                                       _rendezvousClient, this}}
    , _historyWiring{new HistoryWiring{_discovery, _chat, _transfer, _peerDiscoveryViewModel,
                                       _history, _dataBroker, this}}
    , _backupController{new BackupController{_dataBroker, _config, this}}
    , _shutdownController{new ShutdownController{_dataBroker, this}}
    , _localIpMonitor{new LocalIpMonitor{_config, this}}
{
    // 接收目录残留清理：崩溃/断电留下的陈旧 .part 中间名在启动时统一清走，
    // 也避免残留占住中间名挤占后续接收的落盘名。必须先于 P2P 监听启动，
    // 此时尚无入站接收任务，不会与活动接收的 .part 写入并发扫描
    FileReceiverWorker::cleanupResidualPartFiles(_config->receivePath());

    // 初始化 ReachabilityController 的引用
    _reachability->setDiscoveryService(_discovery);
    _reachability->setConfigManager(_config);
    _reachability->setRendezvousClient(_rendezvousClient);

    // 手动添加的伪条目探测不可达：经删除设备链路连库清理，最近见过不留死数据
    connect(_reachability, &ReachabilityController::manualEntryProbeFailed,
            _peerDiscoveryViewModel, &PeerDiscoveryViewModel::deleteDeviceWithHistory);

    // 打开本地历史库并完成持久化装配；失败时记录降级状态，供 QML 判断是否展示历史入口
    _localHistoryAvailable = _historyWiring->initialize();
    // 库损坏重建标志同样在启动期定型，随属性发布给设置页提示
    _historyDatabaseRebuilt = _historyWiring->historyDatabaseRebuilt();
    _rebuiltBackupPath = _historyWiring->rebuiltBackupPath();
    connect(_historyWiring, &HistoryWiring::operationFailed,
            this,           &AppController::localHistoryOperationFailed);

    // 监听端口是收发能力的底线：失败不再静默，标记经属性发布给表现层
    // 常驻警示，应用以不可收发状态运行时用户必须有明确感知
    _networkListenFailed = !_p2pServer->start();

    // 协调服务器的错误令牌拒绝与连接失败原先进了日志就到头，首配用户无从得知；
    // 错误经组合根属性发布给设置页内联提示，注册确认到达即视为服务端认可并清空
    connect(_rendezvousClient, &RendezvousClient::errorOccurred,
            this, [this](const QString &message) { setRendezvousError(message); });
    connect(_rendezvousClient, &RendezvousClient::registerAckReceived,
            this, [this](int) { setRendezvousError(QString()); });

    // 传入协调客户端，直连失败后可发起中继降级
    _transfer->init(_config, _discovery, _p2pServer, _rendezvousClient);

    _chat->init(_config, _discovery, _p2pServer);

    // 按当前配置启动协调连接；此后配置变化由编排器即时响应
    _rendezvousCoordinator->applyConfig();
}

// 析构函数
AppController::~AppController()
{
}

// 初始化 QML UI 层
void AppController::initializeUi()
{
    if (_uiInitialized) {
        return;
    }
    _uiInitialized = true;

    // 唤回置顶策略启动期定型并落日志：Wayland 会话降级为 show+raise+activate
    // +托盘提醒，其余平台保持临时置顶；供会话类型差异的实机排查定位
    qDebug() << "AppController: 平台" << QGuiApplication::platformName()
             << "窗口临时置顶支持" << staysOnTopSupported();

    _uiEngine = new QQmlApplicationEngine{this};
    connect(
        _uiEngine, &QQmlApplicationEngine::objectCreationFailed,
        qApp, [] { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    _uiEngine->loadFromModule("cqnu.gridyard.client", "Main");
    // QML 根对象创建失败时直接退出，避免进入无窗口事件循环。
    if (_uiEngine->rootObjects().isEmpty()) {
        _uiReady = false;
        return;
    }
    _uiReady = true;
}

// 获取应用全局控制器实例
AppController *AppController::singleton()
{
    if (!s_appController) {
        s_appController = new AppController{qApp};
        QQmlEngine::setObjectOwnership(s_appController, QQmlEngine::CppOwnership);
        s_appController->initializeUi();
    }
    return s_appController;
}

// 创建 QML 单例实例
AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    AppController *controller = singleton();
    QQmlEngine::setObjectOwnership(controller, QQmlEngine::CppOwnership);
    return controller;
}

// 获取应用名称
QString AppController::applicationName() const
{
    return QCoreApplication::applicationName();
}

// 获取应用版本
QString AppController::applicationVersion() const
{
    return QCoreApplication::applicationVersion();
}

// 获取开发者模式窗口标题后缀（如 " #2"），正常实例为空串
QString AppController::instanceTitleSuffix() const
{
    const int instance = _config->instanceNumber();
    return instance > 0 ? QStringLiteral(" #%1").arg(instance) : QString();
}

// 判定平台是否支持窗口临时置顶：Wayland 合成器不支持应用自定置顶（置顶
// 提示符被忽略且无层级控制协议），仅该平台唤回须降级；空平台名按支持
// 处理避免误降级，offscreen 等测试平台不受影响
bool AppController::staysOnTopSupportedOnPlatform(const QString &platformName)
{
    return !platformName.startsWith(QStringLiteral("wayland"));
}

// 获取当前平台是否支持临时置顶
bool AppController::staysOnTopSupported() const
{
    return staysOnTopSupportedOnPlatform(QGuiApplication::platformName());
}

// 获取设备发现视图模型
PeerDiscoveryViewModel *AppController::peerDiscoveryViewModel() const
{
    return _peerDiscoveryViewModel;
}

// 获取传输 UI 控制器
TransferController *AppController::transferController() const
{
    return _transferController;
}

// 获取聊天 UI 控制器
ChatController *AppController::chatController() const
{
    return _chatController;
}

// 获取本地历史 UI 控制器
HistoryController *AppController::historyController() const
{
    return _history;
}

// 获取本地历史可用性
bool AppController::localHistoryAvailable() const
{
    return _localHistoryAvailable;
}

// 获取本次启动是否重建过本地历史库
bool AppController::historyDatabaseRebuilt() const
{
    return _historyDatabaseRebuilt;
}

// 获取重建前损坏库的备份路径
QString AppController::rebuiltBackupPath() const
{
    return _rebuiltBackupPath;
}

// 获取网络可达性控制器
ReachabilityController *AppController::reachabilityController() const
{
    return _reachability;
}

// 获取备份与迁移控制器
BackupController *AppController::backupController() const
{
    return _backupController;
}

// 获取 UI 根对象是否创建成功
bool AppController::uiReady() const
{
    return _uiReady;
}

// 获取 P2P 监听是否启动失败
bool AppController::networkListenFailed() const
{
    return _networkListenFailed;
}

// 获取协调服务器最近一次交互错误
QString AppController::rendezvousError() const
{
    return _rendezvousError;
}

// 记录协调服务器交互错误并通知表现层
void AppController::setRendezvousError(const QString &message)
{
    if (_rendezvousError == message) {
        return;
    }
    _rendezvousError = message;
    emit rendezvousErrorChanged();
}

// 请求退出应用（排空与兜底策略由 ShutdownController 承担）
void AppController::quit()
{
    _shutdownController->requestQuit();
}

// 清除配置、历史数据库和日志后退出应用
void AppController::clearLocalCache()
{
    _shutdownController->requestClearCacheAndQuit();
}

// 打开接收目录（托盘菜单「打开接收目录」入口）：打开当前配置的接收路径。
// 完成提示卡仅存续数秒，错过提示后托盘是直达接收目录的唯一入口；路径复用
// ConfigManager::openFolder 既有语义——目录不存在时静默不动作，不误开任意位置
void AppController::openReceiveFolder()
{
    _config->openFolder(_config->receivePath());
}

// 关窗动作决策：配置与活动传输数的读取都留在 C++ 侧，表现层只按结果分发
QString AppController::resolveWindowCloseAction() const
{
    return ConfigManager::resolveWindowCloseAction(_config->closeWindowAction(),
                                                   _transfer->activeSessionCount());
}

// 托盘退出决策：托盘菜单"退出"是显式退出意图，不读关窗行为记忆（记住隐藏或
// 每次询问只约束关窗动作），只复用关窗退出分支的同一活动传输防护——有活动
// 传输时与关窗路径同判 confirm 弹警示确认，无活动传输时直接退出
QString AppController::resolveTrayQuitAction() const
{
    return ConfigManager::resolveWindowCloseAction(CloseWindowAction::Exit,
                                                   _transfer->activeSessionCount());
}

// 记录关窗行为：把关窗确认弹窗"记住我的选择"上报的动作写入配置，
// 取值与 resolveWindowCloseAction 的决策词表一致，非法取值忽略
void AppController::setCloseWindowAction(const QString &action)
{
    if (action == QStringLiteral("ask")) {
        _config->setCloseWindowAction(CloseWindowAction::Ask);
    } else if (action == QStringLiteral("hide")) {
        _config->setCloseWindowAction(CloseWindowAction::Hide);
    } else if (action == QStringLiteral("exit")) {
        _config->setCloseWindowAction(CloseWindowAction::Exit);
    }
}

// 设置页"启动新实例"入口：经 startDetached 异步拉起下一开发者实例（不阻塞 UI），
// 返回空串表示已发起启动，非空为需要行内展示的失败原因；
// 实例隔离（目录后缀/端口偏移/标题标识）是启动期属性，子实例走与命令行
// 完全相同的 --instance 解析链路，开关本身不改变当前实例的任何运行形态
QString AppController::launchDeveloperInstance()
{
    const int target = ConfigManager::nextLaunchInstance(_config->instanceNumber());
    if (target < 0) {
        return QStringLiteral("已达本机开发者实例上限（9 个），无法继续新增实例");
    }

    // 子实例的实例号只来自自身命令行，须剥离父进程残留的 GRIDYARD_* 环境变量，
    // 避免本实例的端口/配置/设备名覆盖子实例的启动语义
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("GRIDYARD_PORT"));
    env.remove(QStringLiteral("GRIDYARD_CONFIG"));
    env.remove(QStringLiteral("GRIDYARD_NAME"));
    env.remove(QStringLiteral("GRIDYARD_INSTANCE"));

    QProcess launcher;
    launcher.setProgram(QCoreApplication::applicationFilePath());
    launcher.setArguments({QStringLiteral("--instance=%1").arg(target)});
    launcher.setProcessEnvironment(env);

    // startDetached 成功后子进程独立于父进程运行，栈上对象即可安全析构
    if (!launcher.startDetached()) {
        qWarning() << "AppController: 拉起开发者实例" << target << "失败";
        return QStringLiteral("启动新实例失败，请确认程序文件存在且可执行");
    }

    qDebug() << "AppController: 已拉起开发者实例" << target;
    return QString();
}

// 验证 QML 调用链路
void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
