/**
* @file    transfer_controller.h
* @version 7.25.0
* @date 2026-10-08
* @author  GridYard Team
* @brief   面向 QML 的文件传输控制器
*/

#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "transfer_session_model.h"

class TransferSessionManager;

class TransferController : public QObject {
private:
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QAbstractItemModel* sessionModel READ sessionModel CONSTANT)
    Q_PROPERTY(int activeSessionCount READ activeSessionCount NOTIFY activeSessionCountChanged)

public:
    explicit TransferController(TransferSessionManager *manager, QObject *parent = nullptr);
    virtual ~TransferController() override = default;

    TransferController(const TransferController &) = delete;
    TransferController &operator=(const TransferController &) = delete;

    // 获取 QML 可绑定的传输会话列表
    QVariantList sessions() const;

    // 返回当前全部等待确认的接收会话快照（按到达序），供确认弹窗关闭后串行取下一个
    Q_INVOKABLE QVariantList waitingConfirmReceiveSessions() const;

    // 返回当前活动（未终态）会话数量
    int activeSessionCount() const;

    // 获取承载会话行的增量通知模型
    QAbstractItemModel *sessionModel() const;

    // 创建发送会话
    Q_INVOKABLE void createSendSession(const QString &deviceId, const QString &filePath);
    // 多选群发：逐台独立 1:1 会话，序列化与哈希一次复用
    Q_INVOKABLE void createMultiSendSessions(const QStringList &deviceIds, const QString &filePath);
    // 上次多选群发勾选的目标集合（多选弹窗默认勾选）
    Q_INVOKABLE QStringList lastMultiTargets() const;
    Q_INVOKABLE void saveMultiTargets(const QStringList &deviceIds);
    // 上次群发的内容路径（多选弹窗预填）
    Q_INVOKABLE QString lastMultiPath() const;
    Q_INVOKABLE void saveMultiPath(const QString &filePath);
    // 接受接收会话
    Q_INVOKABLE void acceptReceiveSession(const QString &sessionId);
    // 拒绝接收会话
    Q_INVOKABLE void rejectReceiveSession(const QString &sessionId);
    // 取消传输会话
    Q_INVOKABLE void cancelSession(const QString &sessionId);
    // 取消指定设备的全部进行中会话（删除设备前的收尾，复用 cancelSession 能力）
    Q_INVOKABLE void cancelDeviceSessions(const QString &deviceId);
    // 移除已结束会话
    Q_INVOKABLE void removeSession(const QString &sessionId);
    // 移除已结束会话并删除已接收文件
    Q_INVOKABLE void removeSessionAndDeleteFile(const QString &sessionId);
    // 清空所有已结束会话
    Q_INVOKABLE void clearFinishedSessions(bool deleteReceivedFiles = false,
                                           const QString &deviceId = {});
    // 直连失败后经中继通道重新发送（用户确认中继后调用）
    Q_INVOKABLE void retryViaRelay(const QString &sessionId);

signals:
    void sessionsChanged();
    // 活动会话数量变化（会话创建或迁移到终态）
    void activeSessionCountChanged();
    // 中继确认请求（AskBeforeRelay 档直连失败后触发，QML 只弹窗回传用户选择）
    void relayConfirmRequested(const QString &sessionId, const QString &deviceId);
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
    // 等待确认的接收会话被后端终结（对方取消/超时/断连），弹窗应关闭
    void sessionStale(const QString &sessionId);
    void errorOccurred(const QString &message);
    void messageOccurred(const QString &message);

private:
    TransferSessionManager *_manager = nullptr;  // 内部传输会话管理器
};
