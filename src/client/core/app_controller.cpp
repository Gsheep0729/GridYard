/**
* @file    app_controller.cpp
* @version 6.1.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   应用全局控制器实现
*
* 构造时创建并组装 ConfigManager、DiscoveryService、P2pServer、
* TransferSessionManager，启动 P2P 服务器并初始化传输会话管理器。
*
* Change Log:
* [v6.1.0] GY   2026-06-25
* * 接入设备目录 Proxy，异步投递发现设备快照
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
#include "database_worker.h"
#include "chat_manager.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "p2p_server.h"
#include "transfer_session_manager.h"
#include "sqlite_database_proxy.h"
#include "sqlite_device_proxy.h"

#include <QCoreApplication>
#include <QDebug>
#include <QThread>

// 构造函数：创建并组装所有核心模块
AppController::AppController(QObject *parent)
    : QObject{parent}
    , _config{ConfigManager::create(nullptr, nullptr)}
    , _discovery{new DiscoveryService{_config, this}}
    , _p2pServer{new P2pServer{_config, this}}
    , _transfer{new TransferSessionManager{this}}
    , _chat{new ChatManager{this}}
    , _storage{std::make_unique<SqliteDatabaseProxy>()}
    , _deviceRepository{std::make_unique<SqliteDeviceProxy>(_storage.get())}
    , _storageThread{new QThread{this}}
    , _storageWorker{new DatabaseWorker{_storage.get()}}
{
    QString storageError;
    // 历史库失败时仅记录降级状态，不阻断已验收的 P2P 主链路
    if (!_storage->initialize(ApplicationPaths::databaseDir() + "/gridyard-history.sqlite", &storageError)) {
        qWarning() << "[Storage] 本地历史不可用:" << storageError;
    }

    // Worker 不设置 parent，移动后由线程结束信号安全回收
    _storageWorker->moveToThread(_storageThread);
    // 线程退出时在各自安全的事件上下文中释放 Worker 和线程对象
    connect(_storageThread, &QThread::finished, _storageWorker, &QObject::deleteLater);
    connect(_storageThread, &QThread::finished, _storageThread, &QObject::deleteLater);
    _storageThread->start();

    // 发现服务只发送值对象，应用层负责投递设备目录持久化任务
    connect(_discovery, &DiscoveryService::peerUpdated,
            this, [this](const PeerInfo &peer) {
                PeerRecord record;
                record.deviceId = peer.deviceId;
                record.deviceName = peer.deviceName;
                record.lastIpAddress = peer.ipAddress;
                record.lastTcpPort = peer.tcpPort;
                record.firstSeenAt = peer.lastSeen;
                record.lastSeenAt = peer.lastSeen;

                _storageWorker->submitSave(
                    [this, record](SqliteDatabaseProxy &, QString *errorMessage) {
                        return _deviceRepository->upsertPeer(record, errorMessage);
                    });
            });

    // 启动 P2P 服务器
    _p2pServer->start();

    // 初始化传输会话管理器
    _transfer->init(_config, _discovery, _p2pServer);

    // 初始化聊天管理器
    _chat->init(_config, _discovery, _p2pServer);
}

// 析构函数
AppController::~AppController()
{
    if (_storageThread && _storageThread->isRunning()) {
        // 先结束任务事件循环，再由 finished 信号删除 Worker
        _storageThread->quit();
        _storageThread->wait();
    }
}

// QML_SINGLETON 工厂方法，引擎调用
AppController *AppController::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    return new AppController{};
}

// 获取应用名称
QString AppController::applicationName() const
{
    return QCoreApplication::applicationName();
}

// 获取应用版本号
QString AppController::applicationVersion() const
{
    return QCoreApplication::applicationVersion();
}

// 获取设备发现服务（供 QML 绑定设备列表）
DiscoveryService *AppController::discovery() const
{
    return _discovery;
}

// 获取传输会话管理器
TransferSessionManager *AppController::transfer() const
{
    return _transfer;
}

// 获取在线聊天管理器
ChatManager *AppController::chat() const
{
    return _chat;
}

// 退出应用
void AppController::quit()
{
    qDebug() << "AppController::quit invoked from QML";
    QCoreApplication::quit();
}

// 测试 C++↔QML 通信
void AppController::test()
{
    qDebug() << "AppController::test() invoked from QML - C++↔QML 通信正常";
}
