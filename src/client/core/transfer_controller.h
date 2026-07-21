/**
* @file    transfer_controller.h
* @version 7.8.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   面向 QML 的文件传输控制器
*
* Change Log:
* [v7.8.0] GY   2026-07-21
* * 新增 relayModeRequested 信号，用于 P2P 直连失败后请求 Relay 中继
* [v6.6.2] GY   2026-06-27
* * 新增传输 UI API 门面，避免 QML 直接依赖内部 Manager
*/

#pragma once

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class TransferSessionManager;

class TransferController : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)

public:
    explicit TransferController(TransferSessionManager *manager, QObject *parent = nullptr);
    virtual ~TransferController() override = default;

    TransferController(const TransferController &) = delete;
    TransferController &operator=(const TransferController &) = delete;

    // 获取 QML 可绑定的传输会话列表
    QVariantList sessions() const;

    // 创建发送会话
    Q_INVOKABLE void createSendSession(const QString &deviceId, const QString &filePath);
    // 接受接收会话
    Q_INVOKABLE void acceptReceiveSession(const QString &sessionId);
    // 拒绝接收会话
    Q_INVOKABLE void rejectReceiveSession(const QString &sessionId);
    // 取消传输会话
    Q_INVOKABLE void cancelSession(const QString &sessionId);
    // 移除已结束会话
    Q_INVOKABLE void removeSession(const QString &sessionId);
    // 移除已结束会话并删除已接收文件
    Q_INVOKABLE void removeSessionAndDeleteFile(const QString &sessionId);
    // 清空所有已结束会话
    Q_INVOKABLE void clearFinishedSessions(bool deleteReceivedFiles = false,
                                           const QString &deviceId = {});

signals:
    void sessionsChanged();
    // Relay 降级请求（所有直连候选失败后触发）
    void relayModeRequested(const QString &sessionId, const QString &deviceId);
    void receiveRequestReceived(const QString &sessionId,
                                const QString &senderDeviceId,
                                const QString &senderName,
                                const QString &fileName,
                                qint64 fileSize,
                                int totalFiles,
                                qint64 totalBytes,
                                bool isDirectory,
                                const QVariantList &fileList);
    void transferCompleted(const QString &sessionId,
                           const QString &fileName,
                           const QString &filePath);
    void errorOccurred(const QString &message);
    void messageOccurred(const QString &message);

private:
    TransferSessionManager *_manager = nullptr;  // 内部传输会话管理器
};
