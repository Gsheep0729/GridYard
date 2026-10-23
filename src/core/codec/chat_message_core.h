/**
* @file    chat_message_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 聊天消息 JSON 编解码——不依赖任何 Qt 类型
*
* 编解码语义与 src/shared/chat_message.cpp 的 Qt 版本等价：字段名、
* 紧凑 JSON 输出（UTF-8 原文，与 QJsonDocument::toJson(Compact) 逐字节
* 一致，golden 已固化）、UUID 校验、正文长度上限均对齐旧实现。
* 时间在 Core 层以 Unix 毫秒表示，序列化边界维持 ISO-8601 毫秒格式。
*/

#pragma once

#include "core/types/core_types.h"

#include <string>

namespace gy::core {

// 聊天消息编解码错误分类（与 Qt 版 ChatMessageError 的可对拍子集）
enum class ChatCodecError {
    kNone = 0,
    kInvalidJson,       // JSON 解析失败、非对象或载荷大小超限
    kMissingField,      // 必填字段缺失、类型不符或字段内容无效
    kInvalidMessageId,  // message_id 不是有效 UUID
    kEmptyText,         // 消息正文为空或全空白
    kTooLong,           // 正文超过长度上限或编码输出超过载荷上限
};

// 序列化结果：error 为 kNone 时 json 有效
struct EncodeResult {
    std::string    json;                 // 紧凑 JSON 文本（UTF-8 原文）
    ChatCodecError error = ChatCodecError::kNone;
    std::string    error_detail;         // 可读错误描述
};

// 反序列化结果：error 为 kNone 时 msg 有效
struct DecodeResult {
    ChatMsg       msg;                   // 解码后的消息
    ChatCodecError error = ChatCodecError::kNone;
    std::string    error_detail;         // 可读错误描述
};

// 聊天消息 JSON 编解码工具（无状态，所有函数线程安全）
class ChatMessageCore {
public:
    // 将 ChatMsg 序列化为紧凑 JSON 文本
    static EncodeResult encode(const ChatMsg &msg);

    // 将 JSON 文本反序列化为 ChatMsg；校验规则与 Qt 版 validate 一致
    static DecodeResult decode(const std::string &json);

    // 校验 message_id 是否为有效 UUID：36 字符 8-4-4-4-12、连字符定位、
    // 十六进制（大小写均可）、且非全零——语义对齐 QUuid 构造 + isNull 判定，
    // 不强制 version/variant 位（QUuid 同样不检查版本位）
    static bool isValidMessageId(const std::string &id);
};

}  // namespace gy::core
