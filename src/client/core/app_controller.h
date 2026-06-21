/**
* @file    app_controller.h
* @version 4.16.5
* @date    2026-06-24
* @author  GridYard Team
* @brief   应用全局控制器（QML 单例）
*
* 按四层架构要求，AppController 是中介者单例，负责组装和持有
* DiscoveryService、TransferSessionManager、P2pServer 等下层模块。
* QML 通过 AppController.discovery.peers 等路径触达业务对象，
* 禁止使用 setContextProperty 暴露 C++ 对象。
*
* Change Log:
* [v4.16.5] GY   2026-06-24
* * 完成在线聊天管理器组装
* [v4.16.4] GY   2026-06-24
* * 组装在线聊天管理器并向 QML 暴露受控入口
* [v4.8.2] GY   2026-06-13
* * 修复 KDE 原生文件选择器
* [v4.7.1] GY   2026-06-05
* * 添加日志系统、修复传输功能
* [v0.2.0] GY   2026-06-02
* * Stage 2：持有 DiscoveryService，暴露 discovery 属性给 QML
* [v0.1.0] GY   2026-05-24
* * Stage 0：仅暴露 applicationName / applicationVersion / quit
*/

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "chat_manager.h"
#include "discovery_service.h"
#include "transfer_session_manager.h"

class QQmlEngine;
class QJSEngine;

class ConfigManager;
class P2pServer;

class AppController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString applicationName    READ applicationName    CONSTANT)
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(DiscoveryService* discovery READ discovery         CONSTANT)
    Q_PROPERTY(TransferSessionManager* transfer READ transfer     CONSTANT)
    Q_PROPERTY(ChatManager* chat READ chat                         CONSTANT)

public:
    // QML_SINGLETON 必需的工厂；引擎调用，外界不应直接 new
    static AppController *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QString applicationName()    const;
    QString applicationVersion() const;

    // 获取设备发现服务（供 QML 绑定设备列表）
    DiscoveryService *discovery() const;
    // 获取传输会话管理器
    TransferSessionManager *transfer() const;
    // 获取在线聊天管理器
    ChatManager *chat() const;

    Q_INVOKABLE void quit();
    Q_INVOKABLE void test();  // Stage 1：验证 C++↔QML 通信

signals:
    void appReady();

private:
    explicit AppController(QObject *parent = nullptr);
    AppController(const AppController &)            = delete;
    AppController &operator=(const AppController &) = delete;

    ConfigManager           *_config    = nullptr;  // 配置管理器（设备名、端口、接收路径）
    DiscoveryService        *_discovery = nullptr;  // UDP 设备发现服务
    P2pServer               *_p2pServer = nullptr;  // TCP P2P 文件传输服务器
    TransferSessionManager  *_transfer  = nullptr;  // 传输会话管理器
    ChatManager             *_chat      = nullptr;  // 在线聊天连接和内存会话管理器
};
