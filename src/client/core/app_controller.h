/**
* @file    app_controller.h
* @version 8.2.1
* @date 2026-11-01
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

#include "backup_controller.h"
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
class BackupController;
class HistoryWiring;
class LocalDataBroker;
class LocalIpMonitor;
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
    Q_PROPERTY(BackupController* backupController READ backupController CONSTANT)
    Q_PROPERTY(bool localHistoryAvailable READ localHistoryAvailable CONSTANT)
    Q_PROPERTY(bool historyDatabaseRebuilt READ historyDatabaseRebuilt CONSTANT)
    Q_PROPERTY(QString rebuiltBackupPath READ rebuiltBackupPath CONSTANT)
    Q_PROPERTY(QString instanceTitleSuffix READ instanceTitleSuffix CONSTANT)
    // 当前平台是否支持窗口临时置顶：仅 Wayland 合成器无应用自定置顶接口，
    // 该会话唤回由表现层降级为 show+raise+activate+任务栏提醒
    Q_PROPERTY(bool staysOnTopSupported READ staysOnTopSupported CONSTANT)
    Q_PROPERTY(bool networkListenFailed READ networkListenFailed CONSTANT)
    Q_PROPERTY(QString rendezvousError READ rendezvousError NOTIFY rendezvousErrorChanged)

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
    // 判定平台是否支持窗口临时置顶：Wayland 合成器不提供应用自定置顶接口
    // （WindowStaysOnTopHint 被忽略、无层级控制协议），xcb/Windows/offscreen
    // 等均支持；平台名未能确定时按支持处理，避免误降级
    static bool staysOnTopSupportedOnPlatform(const QString &platformName);
    // 获取当前平台是否支持临时置顶（QML 据此分支唤回置顶策略）
    bool staysOnTopSupported() const;
    // 获取网络可达性控制器
    ReachabilityController *reachabilityController() const;
    BackupController *backupController() const;
    // 获取 UI 根对象是否创建成功
    bool uiReady() const;

    // 获取 P2P 监听是否启动失败（启动期定型，供表现层常驻警示）
    bool networkListenFailed() const;

    // 获取协调服务器最近一次交互错误（空串表示无错误，供设置页内联提示）
    QString rendezvousError() const;

    // 退出应用
    Q_INVOKABLE void quit();
    // 清除本地缓存并退出应用
    Q_INVOKABLE void clearLocalCache();
    // 打开接收目录（托盘菜单入口）：打开当前配置的接收路径，复用
    // ConfigManager::openFolder 语义（目录不存在时静默不动作）
    Q_INVOKABLE void openReceiveFolder();
    // 关窗动作决策：读取关窗行为配置与活动传输数，返回 ask / hide / exit / confirm
    Q_INVOKABLE QString resolveWindowCloseAction() const;
    // 托盘退出决策：托盘菜单"退出"是显式退出意图，不读关窗行为记忆（hide/ask
    // 只约束关窗动作），仅复用关窗退出分支的同一活动传输防护，返回 exit / confirm
    Q_INVOKABLE QString resolveTrayQuitAction() const;
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
    // 协调服务器最近一次交互错误变化（错误令牌、连接失败等）
    void rendezvousErrorChanged();
    // 第二实例请求唤回窗口时发出（单实例守卫转交），窗口恢复由表现层完成
    void windowActivationRequested();

private:
    friend class TestAppController;  // 组合根启动失败路径的回归测试直接构造
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;

    // 初始化 QML UI 层
    void initializeUi();

    // 记录协调服务器交互错误并通知表现层（注册确认到达时清空）
    void setRendezvousError(const QString &message);

    // 停机契约：这些 QObject 子对象按创建序析构。发现、P2P 与传输等先于
    // 存储成员创建的网络成员，退出时先于存储 Worker 删除而停止；协调客户端
    // 与协调器晚于存储成员创建，其停止亦晚于存储 Worker 删除。整体次序为
    // 先停网络监听与广播、再停存储（DatabaseWorker 在归属线程删除）、最后
    // 销毁其余 Qt 对象；调整成员顺序前先复核架构说明的停机契约节。
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
    BackupController *_backupController = nullptr;  // 备份与迁移控制器
    ShutdownController *_shutdownController = nullptr;  // 退出排空与缓存清理
    // 本机 IP 网络变化监听（只读无套接字，最后创建最先析构，析构时配置仍存活）
    LocalIpMonitor *_localIpMonitor = nullptr;
    bool _localHistoryAvailable = false;  // SQLite 历史功能是否可用
    bool _historyDatabaseRebuilt = false;  // 启动时是否因库损坏重建本地历史库
    QString _rebuiltBackupPath;  // 重建前损坏库的备份文件路径
    bool _uiInitialized = false;  // 防止 QML 单例回调期间重复加载界面
    bool _uiReady = false;  // QML 根对象是否已成功创建
    bool _networkListenFailed = false;  // P2P 监听是否启动失败（启动期定型）
    QString _rendezvousError;  // 协调服务器最近一次交互错误（空串表示无错误）
};
