/**
* @file    transfer_controller.cpp
* @version 7.25.0
* @date 2026-10-08
* @author  GridYard Team
* @brief   面向 QML 的文件传输控制器实现
*/

#include "transfer_controller.h"

#include "transfer_session_manager.h"

// 构造函数
TransferController::TransferController(TransferSessionManager *manager, QObject *parent)
    : QObject{parent}
    , _manager{manager}
{
    if (!_manager) {
        return;  // 管理器为空时跳过信号连接，防御性编程
    }

    // 将内部管理器的信号逐个转发给 QML 控制器，隐藏传输管理器的内部实现
    connect(_manager, &TransferSessionManager::sessionsChanged,
            this, &TransferController::sessionsChanged);
    // 活动会话计数透传，退出前警示据此刷新
    connect(_manager, &TransferSessionManager::activeSessionCountChanged,
            this, &TransferController::activeSessionCountChanged);
    // 中继确认请求透传（策略分流在 Manager 内完成，QML 只负责弹窗）
    connect(_manager, &TransferSessionManager::relayConfirmRequested,
            this, &TransferController::relayConfirmRequested);
    // 接收请求信号直接透传，由表现层弹窗确认
    connect(_manager, &TransferSessionManager::receiveRequestReceived,
            this, &TransferController::receiveRequestReceived);
    connect(_manager, &TransferSessionManager::transferCompleted,
            this, &TransferController::transferCompleted);
    // 会话过期透传，接收弹窗据此自动关闭
    connect(_manager, &TransferSessionManager::sessionStale,
            this, &TransferController::sessionStale);
    connect(_manager, &TransferSessionManager::errorOccurred,
            this, &TransferController::errorOccurred);
    connect(_manager, &TransferSessionManager::messageOccurred,
            this, &TransferController::messageOccurred);
}

// 获取 QML 可绑定的传输会话列表
QVariantList TransferController::sessions() const
{
    return _manager ? _manager->sessions() : QVariantList{};
}

// 返回当前全部等待确认的接收会话快照（按到达序），供确认弹窗关闭后串行取下一个
QVariantList TransferController::waitingConfirmReceiveSessions() const
{
    return _manager ? _manager->waitingConfirmReceiveSessions() : QVariantList{};
}

// 返回当前活动（未终态）会话数量
int TransferController::activeSessionCount() const
{
    return _manager ? _manager->activeSessionCount() : 0;
}

// 获取承载会话行的增量通知模型
QAbstractItemModel *TransferController::sessionModel() const
{
    return _manager ? _manager->sessionModel() : nullptr;
}

// 创建发送会话
void TransferController::createSendSession(const QString &deviceId, const QString &filePath)
{
    if (_manager) {
        _manager->createSendSession(deviceId, filePath);
    }
}

// 多选群发：委托会话管理器逐台建立独立发送会话
void TransferController::createMultiSendSessions(const QStringList &deviceIds,
                                                 const QString &filePath)
{
    if (_manager) {
        _manager->createMultiSendSessions(deviceIds, filePath);
    }
}

// 上次多选群发勾选的目标集合
QStringList TransferController::lastMultiTargets() const
{
    return _manager ? _manager->lastMultiTargets() : QStringList{};
}

// 记录本次多选群发的目标集合
void TransferController::saveMultiTargets(const QStringList &deviceIds)
{
    if (_manager) {
        _manager->saveMultiTargets(deviceIds);
    }
}

// 上次群发的内容路径
QString TransferController::lastMultiPath() const
{
    return _manager ? _manager->lastMultiPath() : QString();
}

// 记录本次群发的内容路径
void TransferController::saveMultiPath(const QString &filePath)
{
    if (_manager) {
        _manager->saveMultiPath(filePath);
    }
}

// 接受接收会话
void TransferController::acceptReceiveSession(const QString &sessionId)
{
    if (_manager) {
        _manager->acceptReceiveSession(sessionId);
    }
}

// 拒绝接收会话
void TransferController::rejectReceiveSession(const QString &sessionId)
{
    if (_manager) {
        _manager->rejectReceiveSession(sessionId);
    }
}

// 取消传输会话
void TransferController::cancelSession(const QString &sessionId)
{
    if (_manager) {
        _manager->cancelSession(sessionId);
    }
}

// 取消指定设备的全部进行中会话：删除设备前的收尾动作，终态会话由
// cancelSession 的既有守卫幂等跳过，无需在此重复状态判断
void TransferController::cancelDeviceSessions(const QString &deviceId)
{
    if (!_manager || deviceId.isEmpty()) {
        return;
    }
    const QVariantList all = _manager->sessions();
    for (const QVariant &entry : all) {
        const QVariantMap session = entry.toMap();
        if (session.value(gy::session::kDeviceId).toString() != deviceId) {
            continue;
        }
        cancelSession(session.value(gy::session::kSessionId).toString());
    }
}

// 移除已结束会话
void TransferController::removeSession(const QString &sessionId)
{
    if (_manager) {
        _manager->removeSession(sessionId);
    }
}

// 移除已结束会话并删除已接收文件
void TransferController::removeSessionAndDeleteFile(const QString &sessionId)
{
    if (_manager) {
        _manager->removeSessionAndDeleteFile(sessionId);
    }
}

// 清空所有已结束会话
void TransferController::clearFinishedSessions(bool deleteReceivedFiles, const QString &deviceId)
{
    if (_manager) {
        _manager->clearFinishedSessions(deleteReceivedFiles, deviceId);
    }
}

// 直连失败后经中继通道重新发送
void TransferController::retryViaRelay(const QString &sessionId)
{
    if (_manager) {
        _manager->retryViaRelay(sessionId);
    }
}
