/**
* @file    transfer_controller.cpp
* @version 7.8.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   面向 QML 的文件传输控制器实现
*
* Change Log:
* [v7.8.0] GY   2026-07-21
* * 新增 relayModeRequested 信号透传
* [v6.6.2] GY   2026-06-27
* * 新增传输 UI API 门面
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
    // Relay 降级请求透传
    connect(_manager, &TransferSessionManager::relayModeRequested,
            this, &TransferController::relayModeRequested);
    // 接收请求信号直接透传，由表现层弹窗确认
    connect(_manager, &TransferSessionManager::receiveRequestReceived,
            this, &TransferController::receiveRequestReceived);
    connect(_manager, &TransferSessionManager::transferCompleted,
            this, &TransferController::transferCompleted);
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

// 创建发送会话
void TransferController::createSendSession(const QString &deviceId, const QString &filePath)
{
    if (_manager) {
        _manager->createSendSession(deviceId, filePath);
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
