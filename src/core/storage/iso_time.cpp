/**
* @file    iso_time.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 UTC 时间格式化与解析实现
*
* 公历与天数互转采用标准算术（Howard Hinnant 的 civil 算法），
* 覆盖公历整个有效范围，负时间戳（1970 年前）向下取整正确。
*/

#include "core/storage/iso_time.h"

#include <chrono>
#include <cstdio>

namespace gy::storage {
namespace {

// 天数转换为公历年月日（civil_from_days，算法对负天数同样成立）
void civilFromDays(int64_t days, int &year, unsigned &month, unsigned &day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yearOfEra) + era * 400;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned mp = (5 * dayOfYear + 2) / 153;
    const unsigned d = dayOfYear - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(m <= 2 ? y + 1 : y);
    month = m;
    day = d;
}

// 公历年月日转换为自 1970-01-01 起的天数（days_from_civil）
int64_t daysFromCivil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned dayOfYear =
        (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

// 向下取整除法：负时间戳的日/时/分/秒分量必须落在非负区间
int64_t floorDiv(int64_t value, int64_t modulus)
{
    const int64_t quotient = value / modulus;
    if (value % modulus != 0 && ((value < 0) != (modulus < 0))) {
        return quotient - 1;
    }
    return quotient;
}

// 解析固定两位数字段，失败返回 -1
int parseTwoDigits(std::string_view text, size_t offset)
{
    if (offset + 1 >= text.size()) {
        return -1;
    }
    const char first = text[offset];
    const char second = text[offset + 1];
    if (first < '0' || first > '9' || second < '0' || second > '9') {
        return -1;
    }
    return (first - '0') * 10 + (second - '0');
}

}  // namespace

// Unix 毫秒时间戳格式化为 ISO 8601 文本
std::string toIso8601Ms(int64_t unixMillis)
{
    // 负时间戳向下取整：-1 毫秒属于 1969-12-31T23:59:59.999
    int64_t seconds = unixMillis / 1000;
    int millis = static_cast<int>(unixMillis % 1000);
    if (millis < 0) {
        millis += 1000;
        --seconds;
    }

    const int64_t daySeconds = seconds - floorDiv(seconds, 86400) * 86400;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(floorDiv(seconds, 86400), year, month, day);
    const unsigned hour = static_cast<unsigned>(daySeconds / 3600);
    const unsigned minute = static_cast<unsigned>(daySeconds / 60 % 60);
    const unsigned second = static_cast<unsigned>(daySeconds % 60);

    char buffer[96];  // 覆盖 int 年份的最坏展开宽度，杜绝截断告警
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02uT%02u:%02u:%02u.%03dZ",
                  year, month, day, hour, minute, second, millis);
    return std::string{buffer};
}

// 解析 ISO 8601 文本为 Unix 毫秒时间戳
std::optional<int64_t> parseIso8601Ms(std::string_view text)
{
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T'
        || text[13] != ':' || text[16] != ':') {
        return std::nullopt;
    }

    // 年（4 位）在 0、月（2 位）在 5、日（2 位）在 8
    static constexpr size_t kDateOffsets[3] = {0, 5, 8};
    static constexpr size_t kDateDigits[3] = {4, 2, 2};
    int fields[3] = {};  // 年、月、日
    for (size_t field = 0; field < 3; ++field) {
        const size_t offset = kDateOffsets[field];
        const size_t digits = kDateDigits[field];
        if (offset + digits > text.size()) {
            return std::nullopt;
        }
        int value = 0;
        for (size_t i = 0; i < digits; ++i) {
            const char c = text[offset + i];
            if (c < '0' || c > '9') {
                return std::nullopt;
            }
            value = value * 10 + (c - '0');
        }
        fields[field] = value;
    }

    const int year = fields[0];
    const int month = fields[1];
    const int day = fields[2];
    const int hour = parseTwoDigits(text, 11);
    const int minute = parseTwoDigits(text, 14);
    const int second = parseTwoDigits(text, 17);
    if (hour < 0 || minute < 0 || second < 0 || month < 1 || month > 12 || day < 1
        || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return std::nullopt;
    }

    // 可选小数秒：1~9 位，超出按毫秒截断
    int64_t millis = 0;
    size_t cursor = 19;
    if (cursor < text.size() && text[cursor] == '.') {
        ++cursor;
        const size_t fractionStart = cursor;
        while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9') {
            ++cursor;
        }
        const size_t digits = cursor - fractionStart;
        if (digits == 0 || digits > 9) {
            return std::nullopt;
        }
        int64_t fraction = 0;
        for (size_t i = 0; i < digits; ++i) {
            fraction = fraction * 10 + (text[fractionStart + i] - '0');
        }
        // 小数秒折算到毫秒：不足三位补零放大，超出三位截断
        if (digits <= 3) {
            int64_t scale = 1;
            for (size_t i = digits; i < 3; ++i) {
                scale *= 10;
            }
            millis = fraction * scale;
        } else {
            int64_t scale = 1;
            for (size_t i = 3; i < digits; ++i) {
                scale *= 10;
            }
            millis = fraction / scale;
        }
    }

    // 时区：仅接受 Z 或 +00:00 的零偏移（本应用只写入 UTC）
    if (cursor >= text.size()) {
        return std::nullopt;
    }
    if (text[cursor] == 'Z') {
        if (cursor + 1 != text.size()) {
            return std::nullopt;
        }
    } else if (text[cursor] == '+' && cursor + 6 == text.size()
               && text.compare(cursor, 6, "+00:00") == 0) {
        // 零偏移等价于 UTC
    } else {
        return std::nullopt;
    }

    const int64_t days = daysFromCivil(year, static_cast<unsigned>(month),
                                       static_cast<unsigned>(day));
    const int64_t seconds =
        days * 86400 + hour * 3600 + minute * 60 + second;
    return seconds * 1000 + millis;
}

// 获取当前系统 UTC 毫秒时间戳
int64_t nowUnixMillis()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

// Unix 毫秒时间戳格式化为紧凑时间戳
std::string toCompactStamp(int64_t unixMillis)
{
    int64_t seconds = unixMillis / 1000;
    int millis = static_cast<int>(unixMillis % 1000);
    if (millis < 0) {
        millis += 1000;
        --seconds;
    }

    const int64_t daySeconds = seconds - floorDiv(seconds, 86400) * 86400;
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    civilFromDays(floorDiv(seconds, 86400), year, month, day);
    const unsigned hour = static_cast<unsigned>(daySeconds / 3600);
    const unsigned minute = static_cast<unsigned>(daySeconds / 60 % 60);
    const unsigned second = static_cast<unsigned>(daySeconds % 60);

    char buffer[96];  // 覆盖 int 年份的最坏展开宽度，杜绝截断告警
    std::snprintf(buffer, sizeof(buffer), "%04d%02u%02u%02u%02u%02u%03d",
                  year, month, day, hour, minute, second, millis);
    return std::string{buffer};
}

}  // namespace gy::storage
