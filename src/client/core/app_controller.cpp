/**
* @file    app_controller.cpp
* @version 7.0.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   应用全局控制器实现
*
* 构造时创建并组装 ConfigManager、DiscoveryService、P2pServer、
* TransferSessionManager，启动 P2P 服务器并初始化传输会话管理器。
* UI 引擎由 singleton() 在控制器实例缓存后再初始化，避免 QML 单例回调递归创建。
*
* Change Log:
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
#include "application_paths.h"
#include "chat_manager.h"
#include "chat_controller.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "history_records.h"
#include "history_controller.h"
#include "local_data_broker.h"
#include "logger.h"
#include "p2p_server.h"
#include "peer_discovery_view_model.h"
#include "reachability_controller.h"
#include "transfer_controller.h"
#include "transfer_session_manager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QTimer>

namespace {

QPointer<AppController> s_appController;

QString activeConfigPath()
{
    const QString envPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    return envPath.isEmpty() ? ApplicationPaths::configDir() + "/gridyard.ini" : envPath;
}

void removeFileIfExists(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return;
    }
    if (!QFile::remove(path)) {
        qWarning() << "[Cache] 删除文件失败:" << path;
    }
}

void removeDirectoryIfExists(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }

    QDir dir{path};
    if (!dir.exists()) {
        return;
    }
    if (!dir.removeRecursively()) {
        qWarning() << "[Cache] 删除目录失败:" << path;
    }
}

}

// 构造并组装应用运行期依赖
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
    , _retentionTimer{new QTimer{this}}
{
    // 初始化 ReachabilityController 的 DiscoveryService 引用
    _reachability->setDiscoveryService(_discovery);

    QString storageError;
    if (!_dataBroker->initialize(ApplicationPaths::databaseDir() + "/gridyard-history.sqlite",
                                 &storageError)) {
        // 历史库不可用时保留在线收发能力，避免本地磁盘问题影响 P2P 主链路。
        qWarning() << "[Storage] 本地历史不可用:" << storageError;
    }
    _localHistoryAvailable = _dataBroker->isAvailable();  // 记录降级状态，供 QML 判断是否展示历史入口
    _peerDiscoveryViewModel->initDataBroker(_dataBroker);  // 启动时加载本地历史设备目录

    // 存储失败只记录降级状态，不影响已完成的网络收发。
    connect(_dataBroker, &LocalDataBroker::operationFailed,
            this, [this] {
                qWarning() << "[Storage] 存储任务失败，当前操作未写入本地历史";
                emit localHistoryOperationFailed();
            });

    // 发现结果异步写入设备目录，避免 UDP 心跳阻塞主线程。
    connect(_discovery, &DiscoveryService::peerUpdated,
            this, [this](const PeerInfo &peer) {
                _dataBroker->persistDiscoveredPeer(peer);  // 设备快照投递到 Worker 线程异步写入
            });

    _p2pServer->start();

    _transfer->init(_config, _discovery, _p2pServer);

    _chat->init(_config, _discovery, _p2pServer);

    // 消息收发成功后按顺序确保设备目录和聊天记录均已落库。
    connect(_chat, &ChatManager::messageToPersist,
            this, [this](const MessageRecord &record) {
                // 先查询对端最新端点信息，设备目录记录可能比消息记录更早写入。
                const QVariantMap endpoint = _discovery->transferEndpoint(record.peerDeviceId);
                _dataBroker->persistChatMessage(record, endpoint);
            });

    loadRecentChatHistories();

    connect(_transfer, &TransferSessionManager::transferToPersist,
            this, [this](const TransferRecord &record) {
                // 传输结束时同步写入设备目录，保证外键引用完整。
                const QVariantMap endpoint = _discovery->transferEndpoint(record.peerDeviceId);
                _dataBroker->persistTransferRecord(record, endpoint);
            });

    connect(_transfer, &TransferSessionManager::transferHistoryDeleteRequested,
            this, [this](const QStringList &recordIds) {
                if (recordIds.isEmpty()) {
                    return;  // 空列表无需提交 Worker 任务
                }

                _dataBroker->deleteTransfers(recordIds);  // 批量删除投递到 Worker 线程
            });

    loadRecentTransferHistories();

    _history->cleanupExpiredRecords();  // 启动时立即清理一次过期历史
    _retentionTimer->setInterval(60 * 60 * 1000);  // 历史保留清理间隔：1 小时
    connect(_retentionTimer, &QTimer::timeout,
            _history, &HistoryController::cleanupExpiredRecords);
    _retentionTimer->start();
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

// 请求退出应用
void AppController::quit()
{
    if (_quitRequested) {
        return;
    }
    _quitRequested = true;
    qDebug() << "AppController::quit invoked from QML";

    if (!_dataBroker) {
        QCoreApplication::exit(0);
        return;
    }

    // 单次触发的排空信号 + QueuedConnection，确保退出发生在事件循环空闲时。
    connect(_dataBroker, &LocalDataBroker::drained,
            this, [] { QCoreApplication::exit(0); },
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    _dataBroker->beginShutdown();  // 通知 Worker 线程排空剩余任务后停止

    // 极端情况下 Worker 线程没有及时响应，也不能让托盘进程永久留在后台。
    QTimer::singleShot(3000, this, [] { QCoreApplication::exit(0); });  // 3 秒兜底强制退出
}

// 清除配置、历史数据库和日志后退出应用
void AppController::clearLocalCache()
{
    if (_cacheClearRequested) {
        return;
    }
    _cacheClearRequested = true;
    _quitRequested = true;
    qInfo() << "[Cache] 开始清除本地缓存";

    auto finishClear = [this] {
        if (_cacheClearFinished) {
            return;
        }
        _cacheClearFinished = true;
        if (_dataBroker) {
            _dataBroker->closeStorage();
        }
        removeLocalCacheFiles();
        QCoreApplication::exit(0);
    };

    if (!_dataBroker) {
        finishClear();
        return;
    }

    connect(_dataBroker, &LocalDataBroker::drained,
            this, finishClear,
            static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::SingleShotConnection));
    _dataBroker->beginShutdown();

    // 存储线程异常无响应时仍执行清理，避免用户无法退出清除流程。
    QTimer::singleShot(3000, this, finishClear);
}

// 验证 QML 调用链路
void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}

// 删除本地持久化文件和目录
void AppController::removeLocalCacheFiles()
{
    Logger::instance()->shutdown();  // 释放当前日志文件句柄后再删除 logs 目录
    removeFileIfExists(activeConfigPath());
    removeDirectoryIfExists(ApplicationPaths::configDir());
    removeDirectoryIfExists(ApplicationPaths::databaseDir());
    removeDirectoryIfExists(ApplicationPaths::logDir());
}

// 在存储线程读取最近历史并回投到主线程恢复模型
void AppController::loadRecentChatHistories()
{
    if (!_dataBroker || !_dataBroker->isAvailable()) {
        return;
    }

    _dataBroker->loadRecentChatHistories(
        this, [this](const QHash<QString, QList<MessageRecord>> &histories) {
            for (auto it = histories.cbegin(); it != histories.cend(); ++it) {
                _chat->restoreMessages(it.key(), it.value());
            }
        });
}

// 在存储线程读取最近传输历史并回投到主线程恢复模型
void AppController::loadRecentTransferHistories()
{
    if (!_dataBroker || !_dataBroker->isAvailable()) {
        return;
    }

    _dataBroker->loadRecentTransferHistories(
        this, [this](const QList<TransferRecord> &records) {
            _transfer->restoreFinishedTransfers(records);
        });
}
