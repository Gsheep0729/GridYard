/**
* @file    chat_message.h
* @version 4.16.3
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天消息值类型与 JSON 编解码接口
*
* 集中定义 P2P 在线文本消息的内存表示、错误分类和 JSON 编解码入口。
* 网络层与后续聊天管理器只通过该接口构造和校验消息，避免协议字段分散。
*
* Change Log:
* [v4.16.3] GY   2026-06-24
* * 新增 Stage 5 在线聊天消息值类型和校验接口
*/

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace gy {

// 在线聊天消息的内存表示
struct ChatMessage {
    QString messageId;     // 消息唯一标识
    QString fromDeviceId;  // 发送方设备标识
    QString fromName;      // 发送方显示名称
    QString content;       // UTF-8 文本内容
    QDateTime sentAt;      // 发送时间
};

// 聊天消息处理失败分类
enum class ChatMessageError : quint8 {
    None,
    InvalidInput,
    PeerOffline,
    ConnectionFailed,
    WriteFailed,
    ConnectionLost,
    InvalidPayload,
};

// 聊天消息 JSON 编解码器
class ChatMessageCodec final {
public:
    // 编码消息为紧凑 JSON Payload
    static bool encode(const ChatMessage &message, QByteArray *payload,
                       ChatMessageError *error = nullptr, QString *errorMessage = nullptr);
    // 从 JSON Payload 解码消息
    static bool decode(const QByteArray &payload, ChatMessage *message,
                       ChatMessageError *error = nullptr, QString *errorMessage = nullptr);

private:
    // 校验消息字段是否满足在线聊天协议
    static bool validate(const ChatMessage &message, ChatMessageError *error, QString *errorMessage);
    // 统一设置调用方可展示的失败结果
    static void setError(ChatMessageError error, const QString &errorMessage,
                         ChatMessageError *targetError, QString *targetErrorMessage);
};

}  // namespace gy

Q_DECLARE_METATYPE(gy::ChatMessage)
Q_DECLARE_METATYPE(gy::ChatMessageError)
