/**
* @file    transfer_session_manager.h
* @date    2026-06-02
* @author  GY
* @brief   传输会话管理器（QML_SINGLETON）
*
* 管理所有进行中的传输会话，提供 Q_INVOKABLE 方法供 QML 调用。
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
*/

#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "file_receiver_worker.h"

class QQmlEngine;
class QJSEngine;
class ConfigManager;
class DiscoveryService;
class FileSenderWorker;
class P2pServer;

class TransferSessionManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)

public:
    static TransferSessionManager *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QVariantList sessions() const;

    // 初始化（由 AppController 调用）
    void init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer);

    // Q_INVOKABLE 方法供 QML 调用
    Q_INVOKABLE void createSendSession(const QString &deviceId, const QString &filePath);
    Q_INVOKABLE void acceptReceiveSession(const QString &sessionId);
    Q_INVOKABLE void rejectReceiveSession(const QString &sessionId);
    Q_INVOKABLE void cancelSession(const QString &sessionId);

signals:
    void sessionsChanged();
    // 新的接收请求（需要弹窗确认）
    void receiveRequestReceived(const QString &sessionId,
                                const QString &senderName,
                                const QString &fileName,
                                qint64 fileSize);

private slots:
    // 处理新的传输请求
    void onTransferRequestReceived(FileReceiverWorker *worker,
                                   const QString &senderName,
                                   const QString &fileName,
                                   qint64 fileSize);

public:
    explicit TransferSessionManager(QObject *parent = nullptr);
    TransferSessionManager(const TransferSessionManager &)            = delete;
    TransferSessionManager &operator=(const TransferSessionManager &) = delete;

    ConfigManager    *_config    = nullptr;
    DiscoveryService *_discovery = nullptr;
    P2pServer        *_p2pServer = nullptr;

    // 会话列表
    QList<QVariantMap> _sessions;
};
