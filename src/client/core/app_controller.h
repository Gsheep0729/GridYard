/**
* @file    app_controller.h
 * @version 6.7.0
 * @date    2026-06-27
 * @author  FCL
 * @brief   应用全局控制器（QML 单例）
 *
 * 按四层架构要求，AppController 是中介者单例，负责组装和持有
 * DiscoveryService、TransferSessionManager、P2pServer 等下层模块。
 * QML 通过 AppController.deviceList 等路径触达业务对象，
 * 不使用上下文属性直接暴露 C++ 对象。
 *
 * Change Log:
 * [v6.7.0] FCL   2026-06-27
 * * 新增 deviceList 属性合并在线发现设备和离线历史设备
 * [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.5.0] GY   2026-06-25
* * 向表现层发布本地历史可用性与异步保存失败状态
* [v6.3.0] GY   2026-06-25
* * 接入传输历史持久化与启动恢复
* [v6.2.0] GY   2026-06-25
* * 接入聊天消息持久化，监听 messageToPersist 并异步提交存储
* [v6.1.0] GY   2026-06-25
* * 接入设备目录 Proxy，异步投递发现设备快照
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
#include <memory>
#include <QtQml/qqmlregistration.h>

#include "chat_manager.h"
#include "discovery_service.h"
#include "history_controller.h"
#include "transfer_session_manager.h"

class QQmlEngine;
class QJSEngine;

class ConfigManager;
class P2pServer;
class SqliteDatabaseProxy;
class SqliteDeviceProxy;
class SqliteMessageProxy;
class SqliteTransferHistoryProxy;
class DatabaseWorker;
class QThread;
class QTimer;

class AppController : public QObject {
private:
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString applicationName    READ applicationName    CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(DiscoveryService* discovery READ discovery         CONSTANT)
    Q_PROPERTY(TransferSessionManager* transfer READ transfer     CONSTANT)
    Q_PROPERTY(ChatManager* chat READ chat                         CONSTANT)
    Q_PROPERTY(HistoryController* history READ history             CONSTANT)
    Q_PROPERTY(bool localHistoryAvailable READ localHistoryAvailable CONSTANT)
    Q_PROPERTY(QVariantList deviceList READ deviceList NOTIFY deviceListChanged)

public:
    virtual ~AppController() override;

    // 创建 QML 单例实例
    static AppController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    // 获取应用名称
    QString applicationName()    const;
    // 获取应用版本
    QString applicationVersion() const;

    // 获取设备发现服务
    DiscoveryService *discovery() const;
    // 获取传输会话管理器
    TransferSessionManager *transfer() const;
    // 获取在线聊天管理器
    ChatManager *chat() const;
    HistoryController *history() const;
    bool localHistoryAvailable() const;
    QVariantList deviceList() const;

    // 退出应用
    Q_INVOKABLE void quit();
    // 验证 QML 调用链路
    Q_INVOKABLE void test();

signals:
    void appReady();
    void localHistoryOperationFailed();
    void deviceListChanged();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;

    // 异步恢复最近设备的聊天记录
    void loadRecentChatHistories();
    // 异步恢复最近传输历史
    void loadRecentTransferHistories();
    // 从数据库加载离线设备目录缓存
    void loadOfflineDeviceCache();

    ConfigManager           *_config    = nullptr;  // 本机身份与配置来源
    DiscoveryService        *_discovery = nullptr;  // 在线设备发现服务
    P2pServer               *_p2pServer = nullptr;  // P2P 入站服务器
    TransferSessionManager  *_transfer  = nullptr;  // 文件传输会话管理器
    ChatManager             *_chat      = nullptr;  // 在线聊天管理器
    std::unique_ptr<SqliteDatabaseProxy> _storage;  // 本地历史数据库入口
    std::unique_ptr<SqliteDeviceProxy> _deviceRepository;  // 设备目录持久化端口
    std::unique_ptr<SqliteMessageProxy> _messageRepository;  // 消息持久化端口
    std::unique_ptr<SqliteTransferHistoryProxy> _transferRepository;  // 传输历史持久化端口
    QThread *_storageThread = nullptr;  // 存储任务专用线程
    DatabaseWorker *_storageWorker = nullptr;  // 串行执行存储任务的 Worker
    HistoryController *_history = nullptr;  // 本地历史查询、清理与 QML 操作入口
    QTimer *_retentionTimer = nullptr;  // 周期性过期历史清理定时器
    QVariantList _offlineDeviceCache;  // 离线设备目录缓存，按 deviceId 去重
    bool _localHistoryAvailable = false;  // SQLite 历史功能是否可用
    bool _quitRequested = false;  // 防止托盘退出动作重复请求排空同一任务队列
};
