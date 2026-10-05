/**
* @file    app_controller.h
* @version 7.20.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   应用全局控制器（QML 单例）
*
* 作为组合根只负责创建子对象并装配依赖；协调编排、历史持久化装配、
* 退出与缓存清理策略分别由 RendezvousCoordinator、HistoryWiring、
* ShutdownController 承担。
*/

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "chat_controller.h"
#include "history_controller.h"
#include "peer_discovery_view_model.h"
#include "reachability_controller.h"
#include "transfer_controller.h"

#include "network/rendezvous_client.h"

class QQmlEngine;
class QJSEngine;
class QQmlApplicationEngine;

class ConfigManager;
class ChatManager;
class DiscoveryService;
class HistoryWiring;
class LocalDataBroker;
class P2pServer;
class ReachabilityController;
class RendezvousClient;
class RendezvousCoordinator;
class ShutdownController;
class TransferSessionManager;

class AppController : public QObject {
private:
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString applicationName    READ applicationName    CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(PeerDiscoveryViewModel* peerDiscoveryViewModel READ peerDiscoveryViewModel CONSTANT)
    Q_PROPERTY(TransferController* transferController READ transferController CONSTANT)
    Q_PROPERTY(ChatController* chatController READ chatController CONSTANT)
    Q_PROPERTY(HistoryController* historyController READ historyController CONSTANT)
    Q_PROPERTY(ReachabilityController* reachabilityController READ reachabilityController CONSTANT)
    Q_PROPERTY(bool localHistoryAvailable READ localHistoryAvailable CONSTANT)
    Q_PROPERTY(bool historyDatabaseRebuilt READ historyDatabaseRebuilt CONSTANT)
    Q_PROPERTY(QString rebuiltBackupPath READ rebuiltBackupPath CONSTANT)
    Q_PROPERTY(QString instanceTitleSuffix READ instanceTitleSuffix CONSTANT)

public:
    virtual ~AppController() override;

    // 获取应用全局控制器实例
    static AppController *singleton();
    // 创建 QML 单例实例
    static AppController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    // 获取应用名称
    QString applicationName()    const;
    // 获取应用版本
    QString applicationVersion() const;

    // 获取设备发现视图模型
    PeerDiscoveryViewModel *peerDiscoveryViewModel() const;
    // 获取传输 UI 控制器
    TransferController *transferController() const;
    // 获取聊天 UI 控制器
    ChatController *chatController() const;
    // 获取本地历史 UI 控制器
    HistoryController *historyController() const;
    // 获取本地历史可用性
    bool localHistoryAvailable() const;
    // 获取本次启动是否重建过本地历史库
    bool historyDatabaseRebuilt() const;
    // 获取重建前损坏库的备份路径
    QString rebuiltBackupPath() const;
    // 获取开发者模式窗口标题后缀（如 " #2"），正常实例为空串
    QString instanceTitleSuffix() const;
    // 获取网络可达性控制器
    ReachabilityController *reachabilityController() const;
    // 获取 UI 根对象是否创建成功
    bool uiReady() const;

    // 退出应用
    Q_INVOKABLE void quit();
    // 清除本地缓存并退出应用
    Q_INVOKABLE void clearLocalCache();
    // 关窗动作决策：读取关窗行为配置与活动传输数，返回 ask / hide / exit / confirm
    Q_INVOKABLE QString resolveWindowCloseAction() const;
    // 记录关窗行为：写入"记住我的选择"上报的动作，action 取 ask / hide / exit
    Q_INVOKABLE void setCloseWindowAction(const QString &action);
    // 设置页"启动新实例"入口：startDetached 异步拉起下一开发者实例，
    // 返回空串表示已发起启动，非空为需要行内展示的失败原因
    Q_INVOKABLE QString launchDeveloperInstance();
    // 验证 QML 调用链路
    Q_INVOKABLE void test();

signals:
    void appReady();
    void localHistoryOperationFailed();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;

    // 初始化 QML UI 层
    void initializeUi();

    ConfigManager           *_config    = nullptr;  // 本机身份与配置来源
    DiscoveryService        *_discovery = nullptr;  // 在线设备发现服务
    P2pServer               *_p2pServer = nullptr;  // P2P 入站服务器
    TransferSessionManager  *_transfer  = nullptr;  // 文件传输会话管理器
    ChatManager             *_chat      = nullptr;  // 在线聊天管理器
    PeerDiscoveryViewModel *_peerDiscoveryViewModel = nullptr;  // 设备发现 QML 视图模型
    TransferController *_transferController = nullptr;  // 传输 QML 控制器
    ChatController *_chatController = nullptr;  // 聊天 QML 控制器
    LocalDataBroker *_dataBroker = nullptr;  // 本地数据层代管者
    QQmlApplicationEngine *_uiEngine = nullptr;  // 由控制器持有的 QML UI 引擎
    HistoryController *_history = nullptr;  // 本地历史查询、清理与 QML 操作入口
    ReachabilityController *_reachability = nullptr;  // 网络可达性诊断控制器
    RendezvousClient *_rendezvousClient = nullptr;  // 协调节点客户端（与可达性、中继降级共享）
    RendezvousCoordinator *_rendezvousCoordinator = nullptr;  // 协调节点编排（动态启停）
    HistoryWiring *_historyWiring = nullptr;  // 本地历史持久化装配
    ShutdownController *_shutdownController = nullptr;  // 退出排空与缓存清理
    bool _localHistoryAvailable = false;  // SQLite 历史功能是否可用
    bool _historyDatabaseRebuilt = false;  // 启动时是否因库损坏重建本地历史库
    QString _rebuiltBackupPath;  // 重建前损坏库的备份文件路径
    bool _uiInitialized = false;  // 防止 QML 单例回调期间重复加载界面
    bool _uiReady = false;  // QML 根对象是否已成功创建
};
