/**
* @file    app_controller.cpp
* @version 7.15.12
* @date    2026-10-03
* @author  GridYard Team
* @brief   应用全局控制器实现
*
* Change Log:
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.11.0] GY   2026-10-02
* * 构造函数瘦身为纯装配：协调编排、历史持久化装配、退出与缓存清理
*   分别移入 RendezvousCoordinator、HistoryWiring、ShutdownController
* [v7.9.0] GY   2026-07-26
* * 接入中继降级：收到中继邀请后驱动 P2pServer 加入中继会话
* [v7.6.0] GY   2026-07-21
* * 协调服务器启用时自动拉取在线设备列表，10 秒周期刷新
* [v7.0.0] GY   2026-07-21
* * 接入 ReachabilityController，提供网络可达性诊断入口
* [v6.7.0] GY   2026-06-28
* * 增加清除本地缓存入口，用于删除配置、历史数据库和日志
* * 启动时为设备列表加载本地历史设备目录
* [v6.6.2] GY   2026-06-28
* * AppController 只向 QML 暴露 Controller/ViewModel 门面
* * HistoryController 构造改为传入 LocalDataBroker
* [v6.6.2] GY   2026-06-25
* * 将 AppController 调整为系统组合根，负责应用层和 UI 层初始化
* * 消息持久化时同步更新设备最近聊天活动时间
* [v6.5.0] GY   2026-06-25
* * 为托盘退出增加存储排空超时兜底，避免后台进程无法关闭
* * 向表现层发布本地历史可用性与异步保存失败状态
* [v6.3.0] GY   2026-06-25
* * 接入传输历史持久化与启动恢复
* [v6.2.0] GY   2026-06-25
* * 接入聊天消息持久化，监听 messageToPersist 信号并异步提交存储
* [v6.1.0] GY   2026-06-25
* * 接入设备目录 Repository，异步投递发现设备快照
* [v6.0.0] GY   2026-06-25
* * 集中管理本地历史数据库与数据库任务线程
* [v5.1.0] FengChunlin   2026-06-24
* * 创建并初始化在线聊天管理器
* [v4.7.1] GY   2026-06-06
* * 创建 TransferSessionManager 并初始化
* [v0.2.0] FengChunlin   2026-04-27
* * Stage 2：持有 ConfigManager 和 DiscoveryService
* [v0.1.0] FengChunlin   2026-04-14
* * Stage 0：实现 applicationName / applicationVersion / quit
*/

#include "app_controller.h"
#include "chat_manager.h"
#include "chat_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "history_controller.h"
#include "history_wiring.h"
#include "local_data_broker.h"
#include "p2p_server.h"
#include "peer_discovery_view_model.h"
#include "reachability_controller.h"
#include "rendezvous_coordinator.h"
#include "shutdown_controller.h"
#include "transfer_controller.h"
#include "transfer_session_manager.h"
#include "network/rendezvous_client.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
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
    , _shutdownController{new ShutdownController{_dataBroker, this}}
{
    // 初始化 ReachabilityController 的引用
    _reachability->setDiscoveryService(_discovery);
    _reachability->setConfigManager(_config);
    _reachability->setRendezvousClient(_rendezvousClient);

    // 打开本地历史库并完成持久化装配；失败时记录降级状态，供 QML 判断是否展示历史入口
    _localHistoryAvailable = _historyWiring->initialize();
    connect(_historyWiring, &HistoryWiring::operationFailed,
            this,           &AppController::localHistoryOperationFailed);

    _p2pServer->start();

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

// 获取网络可达性控制器
ReachabilityController *AppController::reachabilityController() const
{
    return _reachability;
}

// 获取 UI 根对象是否创建成功
bool AppController::uiReady() const
{
    return _uiReady;
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

// 验证 QML 调用链路
void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
