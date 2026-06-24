/**
* @file    app_controller.h
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   应用全局控制器（QML 单例）
*
* 按四层架构要求，AppController 是中介者单例，负责组装和持有
* DiscoveryService、TransferSessionManager、P2pServer 等下层模块，
* 并负责初始化 QML UI 层。
* QML 通过 AppController.peerDiscoveryViewModel / transferController /
* chatController / historyController 等 UI API 门面触达应用能力，
* 不直接暴露内部 Manager、Service 或上下文属性。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 将 AppController 调整为系统组合根，接管 UI 引擎初始化
* * 同步文件头版本与当前主版本
* [v6.5.0] GY   2026-06-25
* * 向表现层发布本地历史可用性与异步保存失败状态
* [v6.3.0] GY   2026-06-25
* * 接入传输历史持久化与启动恢复
* [v6.2.0] GY   2026-06-25
* * 接入聊天消息持久化，监听 messageToPersist 并异步提交存储
* [v6.1.0] GY   2026-06-25
* * 接入设备目录 Repository，异步投递发现设备快照
* [v6.0.0] GY   2026-06-25
* * 集中管理本地历史数据库与数据库任务线程
* [v5.1.0] FengChunlin   2026-06-24
* * 组装在线聊天管理器并向 QML 暴露受控入口
* [v4.8.2] GY   2026-06-09
* * 修复 KDE 原生文件选择器
* [v4.7.1] GY   2026-06-06
* * 添加日志系统、修复传输功能
* [v0.2.0] FengChunlin   2026-04-27
* * Stage 2：持有 DiscoveryService，暴露 discovery 属性给 QML
* [v0.1.0] FengChunlin   2026-04-14
* * Stage 0：仅暴露 applicationName / applicationVersion / quit
*/

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "chat_controller.h"
#include "history_controller.h"
#include "peer_discovery_view_model.h"
#include "transfer_controller.h"

class QQmlEngine;
class QJSEngine;
class QQmlApplicationEngine;

class ConfigManager;
class ChatManager;
class DiscoveryService;
class LocalDataBroker;
class P2pServer;
class QTimer;
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
    Q_PROPERTY(bool localHistoryAvailable READ localHistoryAvailable CONSTANT)

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
    // 获取 UI 根对象是否创建成功
    bool uiReady() const;

    // 退出应用
    Q_INVOKABLE void quit();
    // 验证 QML 调用链路
    Q_INVOKABLE void test();

signals:
    void appReady();
    void localHistoryOperationFailed();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;

    // 异步恢复最近设备的聊天记录
    void loadRecentChatHistories();
    // 异步恢复最近传输历史
    void loadRecentTransferHistories();
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
    QTimer *_retentionTimer = nullptr;  // 周期性过期历史清理定时器
    bool _localHistoryAvailable = false;  // SQLite 历史功能是否可用
    bool _quitRequested = false;  // 防止托盘退出动作重复请求排空同一任务队列
    bool _uiInitialized = false;  // 防止 QML 单例回调期间重复加载界面
    bool _uiReady = false;  // QML 根对象是否已成功创建
};
