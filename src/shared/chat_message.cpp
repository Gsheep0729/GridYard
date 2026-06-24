/**
* @file    chat_message.cpp
* @version 6.6.2
* @date    2026-06-23
* @author  GridYard Team
* @brief   在线聊天消息 JSON 编解码实现
*
* 负责将 ChatMessage 转换为紧凑 JSON，并在接收端统一校验 JSON 结构、
* UUID、时间和文本限制。该文件不处理 socket、连接状态或界面状态。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.3] GY   2026-06-23
* * 实现 Stage 5 在线聊天消息 JSON 编解码和字段校验
*/

#include "chat_message.h"
#include "protocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>

namespace {

// 读取必须为字符串的 JSON 字段
bool readStringField(const QJsonObject &json, const char *field, QString *value,
                     QString *errorMessage)
{
    const QJsonValue fieldValue = json.value(QLatin1StringView{field});
    if (!fieldValue.isString()) {
        *errorMessage = QObject::tr("聊天消息字段 %1 无效").arg(QLatin1StringView{field});
        return false;
    }

    *value = fieldValue.toString();
    return true;
}

}

namespace gy {

// 编码消息为紧凑 JSON Payload
bool ChatMessageCodec::encode(const ChatMessage &message, QByteArray *payload,
                              ChatMessageError *error, QString *errorMessage)
{
    if (!payload) {
        setError(ChatMessageError::InvalidInput, QObject::tr("消息载荷输出不能为空"),
                 error, errorMessage);
        return false;
    }

    if (!validate(message, error, errorMessage)) {
        return false;
    }

    QJsonObject json;
    json.insert(QLatin1StringView{protocol::kChatMessageIdField}, message.messageId);
    json.insert(QLatin1StringView{protocol::kChatFromDeviceIdField}, message.fromDeviceId);
    json.insert(QLatin1StringView{protocol::kChatFromNameField}, message.fromName);
    json.insert(QLatin1StringView{protocol::kChatContentField}, message.content);
    json.insert(QLatin1StringView{protocol::kChatSentAtField},
                message.sentAt.toUTC().toString(Qt::ISODateWithMs));

    const QByteArray encoded = QJsonDocument{json}.toJson(QJsonDocument::Compact);
    if (encoded.size() > protocol::kMaxChatPayloadBytes) {
        setError(ChatMessageError::InvalidInput, QObject::tr("聊天消息超过大小限制"),
                 error, errorMessage);
        return false;
    }

    *payload = encoded;
    setError(ChatMessageError::None, {}, error, errorMessage);
    return true;
}

// 从 JSON Payload 解码消息
bool ChatMessageCodec::decode(const QByteArray &payload, ChatMessage *message,
                              ChatMessageError *error, QString *errorMessage)
{
    if (!message) {
        setError(ChatMessageError::InvalidInput, QObject::tr("消息输出不能为空"),
                 error, errorMessage);
        return false;
    }

    if (payload.isEmpty() || payload.size() > protocol::kMaxChatPayloadBytes) {
        setError(ChatMessageError::InvalidPayload, QObject::tr("聊天消息载荷大小无效"),
                 error, errorMessage);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(ChatMessageError::InvalidPayload, QObject::tr("聊天消息不是有效 JSON 对象"),
                 error, errorMessage);
        return false;
    }

    ChatMessage decoded;
    QString fieldError;
    const QJsonObject json = document.object();
    if (!readStringField(json, protocol::kChatMessageIdField, &decoded.messageId, &fieldError)
        || !readStringField(json, protocol::kChatFromDeviceIdField, &decoded.fromDeviceId, &fieldError)
        || !readStringField(json, protocol::kChatFromNameField, &decoded.fromName, &fieldError)
        || !readStringField(json, protocol::kChatContentField, &decoded.content, &fieldError)) {
        setError(ChatMessageError::InvalidPayload, fieldError, error, errorMessage);
        return false;
    }

    QString sentAt;
    if (!readStringField(json, protocol::kChatSentAtField, &sentAt, &fieldError)) {
        setError(ChatMessageError::InvalidPayload, fieldError, error, errorMessage);
        return false;
    }
    decoded.sentAt = QDateTime::fromString(sentAt, Qt::ISODate);

    ChatMessageError validationError = ChatMessageError::None;
    QString validationErrorMessage;
    if (!validate(decoded, &validationError, &validationErrorMessage)) {
        setError(ChatMessageError::InvalidPayload, validationErrorMessage, error, errorMessage);
        return false;
    }

    *message = decoded;
    setError(ChatMessageError::None, {}, error, errorMessage);
    return true;
}

// 校验消息字段是否满足在线聊天协议
bool ChatMessageCodec::validate(const ChatMessage &message, ChatMessageError *error,
                                QString *errorMessage)
{
    if (QUuid{message.messageId}.isNull()) {
        setError(ChatMessageError::InvalidInput, QObject::tr("消息标识不是有效 UUID"),
                 error, errorMessage);
        return false;
    }

    if (message.fromDeviceId.trimmed().isEmpty()) {
        setError(ChatMessageError::InvalidInput, QObject::tr("发送方设备标识不能为空"),
                 error, errorMessage);
        return false;
    }

    if (message.fromName.trimmed().isEmpty()) {
        setError(ChatMessageError::InvalidInput, QObject::tr("发送方名称不能为空"),
                 error, errorMessage);
        return false;
    }

    if (message.content.trimmed().isEmpty()
        || message.content.size() > protocol::kMaxChatContentChars) {
        setError(ChatMessageError::InvalidInput, QObject::tr("聊天内容为空或超过长度限制"),
                 error, errorMessage);
        return false;
    }

    if (!message.sentAt.isValid()) {
        setError(ChatMessageError::InvalidInput, QObject::tr("发送时间无效"),
                 error, errorMessage);
        return false;
    }

    setError(ChatMessageError::None, {}, error, errorMessage);
    return true;
}

// 统一设置调用方可展示的失败结果
void ChatMessageCodec::setError(ChatMessageError error, const QString &errorMessage,
                                ChatMessageError *targetError, QString *targetErrorMessage)
{
    if (targetError) {
        *targetError = error;
    }
    if (targetErrorMessage) {
        *targetErrorMessage = errorMessage;
    }
}

}  // namespace gy
