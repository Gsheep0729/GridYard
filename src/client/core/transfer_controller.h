/**
* @file    transfer_controller.h
* @version 7.17.4
* @date 2026-10-04
* @author  GridYard Team
* @brief   面向 QML 的文件传输控制器
*
* Change Log:
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 新增 cancelDeviceSessions，删除设备前取消其全部进行中会话
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 新增 activeSessionCount 只读属性透传，关闭确认弹窗据此提示退出将中断传输
* [v7.15.17] GY   2026-10-04
* * 新增 waitingConfirmReceiveSessions 调用入口，弹窗关闭后据此串行取下一个请求
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.15.1] GY   2026-10-03
* * 新增 sessionStale 信号透传，弹窗过期改由后端驱动
* [v7.15.0] GY   2026-10-03
* * relayModeRequested 更名 relayConfirmRequested，策略决策已下沉 Manager
* [v7.10.0] GY   2026-10-02
* * 新增 sessionModel 属性，QML 可绑定增量通知的会话模型
* [v7.9.0] GY   2026-07-26
* * 新增 retryViaRelay 入口，QML 确认后驱动中继重试
* [v7.8.0] GY   2026-07-21
* * 新增 relayModeRequested 信号，用于 P2P 直连失败后请求 Relay 中继
* [v6.6.2] GY   2026-06-27
* * 新增传输 UI API 门面，避免 QML 直接依赖内部 Manager
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
