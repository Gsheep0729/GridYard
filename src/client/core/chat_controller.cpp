/**
* @file    chat_controller.cpp
* @version 8.2.1
* @date 2026-11-01
* @author  GridYard Team
* @brief   面向 QML 的聊天控制器实现
*/

#include "chat_controller.h"

#include "chat_manager.h"

#include <QClipboard>
#include <QGuiApplication>

// 构造函数
ChatController::ChatController(ChatManager *manager, QObject *parent)
    : QObject{parent}
    , _manager{manager}
{
    if (!_manager) {
        return;  // 管理器为空时跳过信号连接，防御性编程
    }

    // 将内部管理器的信号逐个转发给 QML 控制器，隐藏内部实现细节
    connect(_manager, &ChatManager::messagesChanged,
            this, &ChatController::messagesChanged);
    connect(_manager, &ChatManager::sendFailed,
            this, &ChatController::sendFailed);
    connect(_manager, &ChatManager::connectionError,
            this, &ChatController::connectionError);
    // 收到新消息时通知表现层弹出系统通知
    connect(_manager, &ChatManager::incomingMessageReceived,
            this, &ChatController::incomingMessageReceived);
}

// 获取聊天内容长度上限
int ChatController::maxChatContentLength() const
{
    return static_cast<int>(gy::protocol::kMaxChatContentChars);
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

// 向在线设备发送文本消息，返回消息是否被接受（QML 据此决定是否清空输入框）
bool ChatController::sendText(const QString &deviceId, const QString &content)
{
    return _manager && _manager->sendText(deviceId, content);
}

// 剪贴板回显长度：Toast 只露内容前 20 字符，口令等敏感内容不整段示人
namespace {
constexpr int kClipboardEchoChars = 20;
}

// 读取系统剪贴板纯文本；图片/文件型剪贴板无文本形态返回空串
QString ChatController::clipboardText() const
{
    return QGuiApplication::clipboard()->text();
}

// 剪贴板外发评估：托盘发送剪贴板没有输入框兜底校验，空白与超长文本在
// 此处一并拦下，避免静默发出空消息或被协议层截断的半截内容
QVariantMap ChatController::evaluateClipboardSend(const QString &text) const
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("reason"), tr("剪贴板中没有可发送的文本")},
                {QStringLiteral("echo"), QString{}}};
    }
    if (trimmed.length() > maxChatContentLength()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("reason"),
                 tr("剪贴板内容超过单条消息上限（%1 字），未发送").arg(maxChatContentLength())},
                {QStringLiteral("echo"), QString{}}};
    }
    // 回显压平换行为空格后取前 20 字符：多行内容在 Toast 中单行展示可读，
    // 且只露开头片段
    QString flat = trimmed;
    flat.replace(QLatin1Char('\n'), QLatin1Char(' '))
            .replace(QLatin1Char('\r'), QLatin1Char(' '));
    const QString echo = flat.left(kClipboardEchoChars)
                         + (flat.length() > kClipboardEchoChars
                                ? QStringLiteral("…") : QString{});
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("reason"), QString{}},
            {QStringLiteral("echo"), echo}};
}

// 清理指定设备或全部设备的运行期消息
void ChatController::clearMessages(const QString &deviceId)
{
    if (_manager) {
        _manager->clearMessages(deviceId);
    }
}
