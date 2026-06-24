/**
* @file    transfer_session_manager.h
* @version 4.16.1
* @date    2026-06-21
* @author  GridYard Team
* @brief   传输会话管理器
*
* 管理所有进行中的传输会话（发送和接收），维护会话状态和进度。
* 提供 Q_INVOKABLE 方法供 QML 调用（创建、接受、拒绝、取消、移除会话）。
* 通过 AppController 暴露给 QML，不使用 QML_SINGLETON。
*
* Change Log:
* [v4.16.1] FengChunlin   2026-06-21
* * 接收请求和完成结果改为跨线程值传递，删除 Worker 状态读取函数
* [v4.14.0] GY   2026-06-15
* * 支持清理传输记录并删除已接收的本地文件
* [v4.13.2] DuRuoxian   2026-06-15
* * 接收确认信号增加文件夹标记和根目录预览
* [v4.11.0] GY   2026-06-13
* * 支持按配置自动接受并保存接收文件
* [v4.10.0] GY   2026-06-13
* * 新增 removeSession() 方法
* [v4.8.3] FengChunlin   2026-06-10
* * 文件传输使用发送方设备别名
* [v4.7.1] GY   2026-06-07
* * 移除 QML_SINGLETON，改为通过 AppController 暴露
* [v4.3.4] GY   2026-05-27
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.3.1] GY   2026-05-21
* * Stage 3.10：实现 cancelSession()；保存发送方 worker 引用
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 transferCompleted 信号；接收完成通知
* [v0.2.0] GY   2026-05-08
* * Stage 3：初始版本
*/

#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
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
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)

public:
    QVariantList sessions() const;

    // 初始化（由 AppController 调用）
    void init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer);

    // Q_INVOKABLE 方法供 QML 调用
    Q_INVOKABLE void createSendSession(const QString &deviceId, const QString &filePath);
    Q_INVOKABLE void acceptReceiveSession(const QString &sessionId);
    Q_INVOKABLE void rejectReceiveSession(const QString &sessionId);
    Q_INVOKABLE void cancelSession(const QString &sessionId);
    Q_INVOKABLE void removeSession(const QString &sessionId);
    // 删除本地文件只允许接收成功记录，避免误删发送源文件
    Q_INVOKABLE void removeSessionAndDeleteFile(const QString &sessionId);
    Q_INVOKABLE void clearFinishedSessions(bool deleteReceivedFiles = false);

signals:
    void sessionsChanged();
    // 新的接收请求（需要弹窗确认）
    void receiveRequestReceived(const QString &sessionId,
                                const QString &senderDeviceId,
                                const QString &senderName,
                                const QString &fileName,
                                qint64 fileSize,
                                int totalFiles,
                                qint64 totalBytes,
                                bool isDirectory,
                                const QVariantList &fileList);
    // 传输完成通知（接收方用于提示打开文件夹）
    void transferCompleted(const QString &sessionId,
                           const QString &fileName,
                           const QString &filePath);
    // 错误提示（显示给用户）
    void errorOccurred(const QString &message);
    // 成功提示（显示给用户）
    void messageOccurred(const QString &message);

private slots:
    // 处理新的传输请求
    void onTransferRequestReceived(FileReceiverWorker *worker, const QVariantMap &request);

public:
    explicit TransferSessionManager(QObject *parent = nullptr);
    TransferSessionManager(const TransferSessionManager &)            = delete;
    TransferSessionManager &operator=(const TransferSessionManager &) = delete;

private:
    // 删除失败时保留记录，便于用户重新处理
    bool deleteReceivedFile(const QVariantMap &session);

    ConfigManager    *_config    = nullptr; // 本机配置和接收路径来源
    DiscoveryService *_discovery = nullptr; // 在线设备与发送端点查询服务
    P2pServer        *_p2pServer = nullptr; // 入站传输请求来源

    // 会话列表
    QList<QVariantMap> _sessions;
    // 发送方 worker 映射（sessionId -> worker）
    QHash<QString, FileSenderWorker*> _sendWorkers;
};
