/**
* @file    codec_bridge.h
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   Qt 聊天 DTO 与纯 C++ 编解码核心之间的类型桥接
*
* 客户端编解码链路接入 gy::core 双生实现的唯一转换点：QString 与
* UTF-8 std::string、QDateTime 与 Unix 毫秒、QByteArray 与核心字节
* 容器的互转，Qt 聊天 DTO 与 ChatMsg 的互转，以及保持旧 Qt 编解码器
* 调用签名的 encode/decode 与帧构建包装（错误分类还原 Qt 版契约）。
* 转换逻辑只允许收敛在本头文件，调用方禁止自行拼装互转。
* 编码边界行为已实测钉死：合法载荷（含内嵌 NUL）roundtrip 无损，
* 非法 UTF-8 输入按 U+FFFD 替换、非法 UTF-8 JSON 与 Qt 版一致拒绝。
*/

#pragma once

#include "chat_message.h"
#include "protocol.h"

#include "core/codec/chat_message_core.h"
#include "core/codec/frame_codec_core.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QTimeZone>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gy::bridge {

// QString 转 UTF-8 编码的 std::string
inline std::string toUtf8(const QString &text)
{
    return text.toStdString();
}

// UTF-8 编码的字节串转 QString（非法序列按 U+FFFD 替换，与 Qt 既有语义一致）
inline QString fromUtf8(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

// QDateTime（任意时区表示）转 Unix 毫秒时间戳
inline int64_t toUtcMillis(const QDateTime &time)
{
    return time.toMSecsSinceEpoch();
}

// Unix 毫秒时间戳还原为 UTC 时区的 QDateTime
inline QDateTime fromUtcMillis(int64_t millis)
{
    return QDateTime::fromMSecsSinceEpoch(millis, QTimeZone::UTC);
}

// QByteArray 转核心层字节向量（载荷与帧的过桥）
inline std::vector<uint8_t> toBytes(const QByteArray &data)
{
    return std::vector<uint8_t>(data.cbegin(), data.cend());
}

// 核心层字节向量转 QByteArray
inline QByteArray fromBytes(const std::vector<uint8_t> &data)
{
    return QByteArray(reinterpret_cast<const char *>(data.data()), static_cast<qsizetype>(data.size()));
}

// Qt 聊天 DTO 转核心消息结构（时间为 Unix 毫秒）
inline gy::core::ChatMsg toCoreChat(const gy::ChatMessage &message)
{
    gy::core::ChatMsg core;
    core.message_id = toUtf8(message.messageId);
    core.from_device_id = toUtf8(message.fromDeviceId);
    core.from_name = toUtf8(message.fromName);
    core.content = toUtf8(message.content);
    core.sent_at_ms = toUtcMillis(message.sentAt);
    return core;
}

// 核心消息结构转 Qt 聊天 DTO
inline gy::ChatMessage fromCoreChat(const gy::core::ChatMsg &msg)
{
    gy::ChatMessage message;
    message.messageId = fromUtf8(msg.message_id);
    message.fromDeviceId = fromUtf8(msg.from_device_id);
    message.fromName = fromUtf8(msg.from_name);
    message.content = fromUtf8(msg.content);
    message.sentAt = fromUtcMillis(msg.sent_at_ms);
    return message;
}

// 统一设置调用方可展示的失败结果（与旧 Qt 编解码器的出口契约一致）
inline void setChatError(gy::ChatMessageError error, const QString &errorMessage,
                         gy::ChatMessageError *targetError, QString *targetErrorMessage)
{
    if (targetError) {
        *targetError = error;
    }
    if (targetErrorMessage) {
        *targetErrorMessage = errorMessage;
    }
}

// 编码消息为紧凑 JSON Payload（签名与错误分类还原 gy::ChatMessageCodec::encode）
inline bool encodeChatMessage(const gy::ChatMessage &message, QByteArray *payload,
                              gy::ChatMessageError *error = nullptr, QString *errorMessage = nullptr)
{
    if (!payload) {
        setChatError(gy::ChatMessageError::InvalidInput, QStringLiteral("消息载荷输出不能为空"),
                     error, errorMessage);
        return false;
    }

    // 无效时间在旧实现的 validate 中被拒绝，桥接保持同一出口（毫秒转换会把无效时间折算成 0）
    if (!message.sentAt.isValid()) {
        setChatError(gy::ChatMessageError::InvalidInput, QStringLiteral("发送时间无效"),
                     error, errorMessage);
        return false;
    }

    const gy::core::EncodeResult result = gy::core::ChatMessageCore::encode(toCoreChat(message));
    if (result.error != gy::core::ChatCodecError::kNone) {
        // 核心侧的校验失败分类在 Qt 版 encode 中统一为 InvalidInput
        setChatError(gy::ChatMessageError::InvalidInput, fromUtf8(result.error_detail),
                     error, errorMessage);
        return false;
    }

    *payload = QByteArray::fromStdString(result.json);
    return true;
}

// 从 JSON Payload 解码消息（签名与错误分类还原 gy::ChatMessageCodec::decode）
inline bool decodeChatMessage(const QByteArray &payload, gy::ChatMessage *message,
                              gy::ChatMessageError *error = nullptr, QString *errorMessage = nullptr)
{
    if (!message) {
        setChatError(gy::ChatMessageError::InvalidInput, QStringLiteral("消息输出不能为空"),
                     error, errorMessage);
        return false;
    }

    const gy::core::DecodeResult result =
        gy::core::ChatMessageCore::decode(payload.toStdString());
    if (result.error != gy::core::ChatCodecError::kNone) {
        // 核心侧的解码失败分类在 Qt 版 decode 中统一为 InvalidPayload
        setChatError(gy::ChatMessageError::InvalidPayload, fromUtf8(result.error_detail),
                     error, errorMessage);
        return false;
    }

    *message = fromCoreChat(result.msg);
    return true;
}

// 构建 TLV 帧（FrameCodecCore::build 的 Qt 容器包装；载荷超限返回空 QByteArray）
inline QByteArray encodeFrame(uint32_t type, const QByteArray &payload)
{
    return fromBytes(gy::core::FrameCodecCore::build(type, toBytes(payload)));
}

}  // namespace gy::bridge
