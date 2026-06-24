/**
* @file    chat_controller.cpp
* @version 6.6.2
* @date    2026-06-27
* @author  GridYard Team
* @brief   面向 QML 的聊天控制器实现
*
* Change Log:
* [v6.6.2] GY   2026-06-27
* * 新增聊天 UI API 门面
*/

#include "chat_controller.h"

#include "chat_manager.h"

// 构造函数
ChatController::ChatController(ChatManager *manager, QObject *parent)
    : QObject{parent}
    , _manager{manager}
{
    if (!_manager) {
        return;
    }

    connect(_manager, &ChatManager::messagesChanged,
            this, &ChatController::messagesChanged);
    connect(_manager, &ChatManager::sendFailed,
            this, &ChatController::sendFailed);
    connect(_manager, &ChatManager::connectionError,
            this, &ChatController::connectionError);
    connect(_manager, &ChatManager::incomingMessageReceived,
            this, &ChatController::incomingMessageReceived);
}

// 获取指定设备的运行期消息快照
QVariantList ChatController::messagesForDevice(const QString &deviceId) const
{
    return _manager ? _manager->messagesForDevice(deviceId) : QVariantList{};
}

// 获取指定设备的稳定消息模型
QObject *ChatController::messageModelForDevice(const QString &deviceId)
{
    return _manager ? _manager->messageModelForDevice(deviceId) : nullptr;
}

// 向在线设备发送文本消息
void ChatController::sendText(const QString &deviceId, const QString &content)
{
    if (_manager) {
        _manager->sendText(deviceId, content);
    }
}

// 清理指定设备或全部设备的运行期消息
void ChatController::clearMessages(const QString &deviceId)
{
    if (_manager) {
        _manager->clearMessages(deviceId);
    }
}
