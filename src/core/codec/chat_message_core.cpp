/**
* @file    chat_message_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 聊天消息 JSON 编解码实现
*
* 使用 vendored nlohmann/json 替代 QJsonDocument。序列化调用 dump()
* 默认参数（ensure_ascii=false，非 ASCII 以 UTF-8 原文输出），与
* QJsonDocument::toJson(Compact) 的输出逐字节一致——该等价性由 Phase 0
* golden 测试固化验证。时间工具为 ISO-8601 毫秒格式的手写解析与格式化。
*/

#include "core/codec/chat_message_core.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <format>
#include <string_view>

namespace gy::core {
namespace {

// JSON 字段名（与 src/shared/protocol.h 的 kChatXxxField 同值，golden 测试锁定）
constexpr char kFieldMessageId[] = "message_id";
constexpr char kFieldFromDeviceId[] = "from_device_id";
constexpr char kFieldFromName[] = "from_name";
constexpr char kFieldContent[] = "content";
constexpr char kFieldSentAt[] = "sent_at";

// 聊天载荷上限与正文长度上限（与 protocol.h 的 kMaxChat* 常量一致）
constexpr size_t kMaxPayloadBytes = 64 * 1024;
constexpr int64_t kMaxContentChars = 4000;

// 判断是否为 Qt::trimmed 认定的常用空白字符（0x09~0x0D 与 0x20）
bool isSpaceChar(char c)
{
    const unsigned char u = static_cast<unsigned char>(c);
    return (u >= 0x09 && u <= 0x0D) || u == 0x20;
}

// 去除首尾空白（对齐 QString::trimmed 的常用字符集）
std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && isSpaceChar(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpaceChar(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

// UTF-8 码点计数：统计非 continuation 字节（0b10xxxxxx）个数。
// 与 Qt 按 UTF-16 码元计数在 BMP 字符上一致，非 BMP 字符（emoji）有
// 1:2 差异，属 golden 已接受的边界差异。
int64_t utf8CodePointCount(std::string_view text)
{
    int64_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

// 公历日期转自 1970-01-01 起的天数（Howard Hinnant days_from_civil 算法）
int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const int64_t y = year;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

// 自 1970-01-01 起的天数转公历日期（civil_from_days 算法）
void civilFromDays(int64_t days, int *year, unsigned *month, unsigned *day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    const unsigned mp = (5u * doy + 2u) / 153u;
    const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
    const unsigned m = mp + (mp < 10u ? 3u : static_cast<unsigned>(-9));
    *year = static_cast<int>(y + (m <= 2 ? 1 : 0));
    *month = m;
    *day = d;
}

// 解析两位数字字段，失败返回 -1
int parseTwoDigits(std::string_view text, size_t offset)
{
    if (offset + 1 >= text.size()) {
        return -1;
    }
    const char a = text[offset];
    const char b = text[offset + 1];
    if (!std::isdigit(static_cast<unsigned char>(a)) || !std::isdigit(static_cast<unsigned char>(b))) {
        return -1;
    }
    return (a - '0') * 10 + (b - '0');
}

// 解析 ISO-8601 时间为 Unix 毫秒："YYYY-MM-DDTHH:MM:SS[.zzz][Z|±HH:MM]"。
// 与 Qt::ISODate 宽松语义对齐：毫秒段与时区后缀均可省略；解析失败返回 -1。
int64_t parseIsoToMs(std::string_view text)
{
    if (text.size() < 19 || (text[4] != '-' || text[7] != '-' || text[10] != 'T')
        || (text[13] != ':' || text[16] != ':')) {
        return -1;
    }
    int year = 0;
    for (int i = 0; i < 4; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[static_cast<size_t>(i)]))) {
            return -1;
        }
        year = year * 10 + (text[static_cast<size_t>(i)] - '0');
    }
    const int month = parseTwoDigits(text, 5);
    const int day = parseTwoDigits(text, 8);
    const int hour = parseTwoDigits(text, 11);
    const int minute = parseTwoDigits(text, 14);
    const int second = parseTwoDigits(text, 17);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23
        || minute < 0 || minute > 59 || second < 0 || second > 60) {
        return -1;
    }

    size_t pos = 19;
    int64_t milliseconds = 0;
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        int64_t fraction = 0;
        int digits = 0;
        while (pos < text.size() && digits < 3 && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            fraction = fraction * 10 + (text[pos] - '0');
            ++pos;
            ++digits;
        }
        if (digits == 0) {
            return -1;
        }
        // 毫秒段不足三位按 Qt 语义右侧补零（"2026-01-01T00:00:00.5Z" = 500ms）
        for (int i = digits; i < 3; ++i) {
            fraction *= 10;
        }
        milliseconds = fraction;
        // 跳过多余的小数位（Qt 截断到毫秒）
        while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }

    int64_t offsetMinutes = 0;
    if (pos < text.size()) {
        if (text[pos] == 'Z') {
            ++pos;  // UTC，无偏移
        } else if (text[pos] == '+' || text[pos] == '-') {
            const bool negative = text[pos] == '-';
            ++pos;
            const int offsetHour = parseTwoDigits(text, pos);
            if (offsetHour < 0) {
                return -1;
            }
            pos += 2;
            if (pos >= text.size() || text[pos] != ':') {
                return -1;
            }
            const int offsetMinute = parseTwoDigits(text, pos + 1);
            if (offsetMinute < 0) {
                return -1;
            }
            pos += 3;
            offsetMinutes = offsetHour * 60 + offsetMinute;
            if (negative) {
                offsetMinutes = -offsetMinutes;
            }
        } else {
            return -1;  // 非法时区后缀
        }
    }
    if (pos != text.size()) {
        return -1;  // 存在尾随垃圾
    }

    const int64_t days = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    int64_t totalMs = days * 86400000 + hour * 3600000 + minute * 60000 + second * 1000 + milliseconds;
    totalMs -= offsetMinutes * 60000;
    return totalMs;
}

// 将 Unix 毫秒格式化为 UTC ISO-8601："YYYY-MM-DDTHH:MM:SS.zzzZ"。
// 输出与 QDateTime::toUTC().toString(Qt::ISODateWithMs) 逐字符一致。
std::string formatMsToIso(int64_t milliseconds)
{
    const int64_t days = milliseconds / 86400000;
    int64_t remainder = milliseconds - days * 86400000;
    if (remainder < 0) {
        remainder += 86400000;
    }
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(days, &year, &month, &day);
    const int hour = static_cast<int>(remainder / 3600000);
    const int minute = static_cast<int>((remainder / 60000) % 60);
    const int second = static_cast<int>((remainder / 1000) % 60);
    const int ms = static_cast<int>(remainder % 1000);
    return std::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}.{:03d}Z", year, month, day,
                       hour, minute, second, ms);
}

}  // namespace

// 校验 message_id 是否为有效 UUID（对齐 QUuid 构造 + isNull 语义）
bool ChatMessageCore::isValidMessageId(const std::string &id)
{
    if (id.size() != 36) {
        return false;
    }
    // 连字符固定位于 8/13/18/23
    if (id[8] != '-' || id[13] != '-' || id[18] != '-' || id[23] != '-') {
        return false;
    }
    bool allZero = true;
    for (size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            continue;
        }
        const bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHex) {
            return false;
        }
        if (c != '0') {
            allZero = false;  // QUuid 语义：全零 UUID 视为 NULL（无效）
        }
    }
    return !allZero;
}

// 将 ChatMsg 序列化为紧凑 JSON 文本
EncodeResult ChatMessageCore::encode(const ChatMsg &msg)
{
    EncodeResult result;
    if (!isValidMessageId(msg.message_id)) {
        result.error = ChatCodecError::kInvalidMessageId;
        result.error_detail = "消息标识不是有效 UUID";
        return result;
    }
    if (trimmed(msg.from_device_id).empty()) {
        result.error = ChatCodecError::kMissingField;
        result.error_detail = "发送方设备标识不能为空";
        return result;
    }
    if (trimmed(msg.from_name).empty()) {
        result.error = ChatCodecError::kMissingField;
        result.error_detail = "发送方名称不能为空";
        return result;
    }
    if (trimmed(msg.content).empty()) {
        result.error = ChatCodecError::kEmptyText;
        result.error_detail = "聊天内容为空";
        return result;
    }
    if (utf8CodePointCount(msg.content) > kMaxContentChars) {
        result.error = ChatCodecError::kTooLong;
        result.error_detail = "聊天内容超过长度限制";
        return result;
    }
    if (msg.sent_at_ms < 0) {
        result.error = ChatCodecError::kMissingField;
        result.error_detail = "发送时间无效";
        return result;
    }

    nlohmann::json json;
    json[kFieldMessageId] = msg.message_id;
    json[kFieldFromDeviceId] = msg.from_device_id;
    json[kFieldFromName] = msg.from_name;
    json[kFieldContent] = msg.content;
    json[kFieldSentAt] = formatMsToIso(msg.sent_at_ms);

    // dump() 默认 ensure_ascii=false：非 ASCII 以 UTF-8 原文输出，
    // 与 QJsonDocument::toJson(Compact) 逐字节一致（golden 固化）
    std::string encoded = json.dump();
    if (encoded.size() > kMaxPayloadBytes) {
        result.error = ChatCodecError::kTooLong;
        result.error_detail = "聊天消息超过大小限制";
        return result;
    }
    result.json = std::move(encoded);
    return result;
}

// 将 JSON 文本反序列化为 ChatMsg
DecodeResult ChatMessageCore::decode(const std::string &json)
{
    DecodeResult result;
    if (json.empty() || json.size() > kMaxPayloadBytes) {
        result.error = ChatCodecError::kInvalidJson;
        result.error_detail = "聊天消息载荷大小无效";
        return result;
    }

    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        result.error = ChatCodecError::kInvalidJson;
        result.error_detail = "聊天消息不是有效 JSON 对象";
        return result;
    }

    // 逐字段提取：必须存在且为字符串（对齐 Qt 版 readStringField）
    const auto readString = [&parsed](const char *field, std::string *value) {
        const auto it = parsed.find(field);
        if (it == parsed.end() || !it->is_string()) {
            return false;
        }
        *value = it->get_ref<const std::string &>();
        return true;
    };

    ChatMsg msg;
    std::string sentAt;
    if (!readString(kFieldMessageId, &msg.message_id) || !readString(kFieldFromDeviceId, &msg.from_device_id)
        || !readString(kFieldFromName, &msg.from_name) || !readString(kFieldContent, &msg.content)
        || !readString(kFieldSentAt, &sentAt)) {
        result.error = ChatCodecError::kMissingField;
        result.error_detail = "聊天消息字段缺失或类型无效";
        return result;
    }

    // 时间宽松解析（对齐 Qt::ISODate：毫秒与时区后缀可省略）
    const int64_t sentAtMs = parseIsoToMs(sentAt);
    if (sentAtMs < 0) {
        result.error = ChatCodecError::kMissingField;
        result.error_detail = "发送时间无效";
        return result;
    }
    msg.sent_at_ms = sentAtMs;

    // 复用 encode 的校验规则保证往返一致性
    const EncodeResult check = encode(msg);
    if (check.error != ChatCodecError::kNone) {
        result.error = check.error;
        result.error_detail = check.error_detail;
        return result;
    }
    result.msg = std::move(msg);
    return result;
}

}  // namespace gy::core
