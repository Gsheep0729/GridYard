/**
* @file    rendezvous_time.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点服务的 UTC 时间工具
*
* 纯 C++ 实现的毫秒时戳获取与 ISO-8601 格式化。协议输出的时间串
* 保持与旧 Qt 版 QDateTime(UTC).toString(Qt::ISODate) 逐字符一致：
* "YYYY-MM-DDTHH:mm:ssZ"（无毫秒段）。
*/

#pragma once

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace gy::rendezvous {

// 当前 UTC Unix 毫秒时戳
inline int64_t nowUtcMs()
{
    return static_cast<int64_t>(::time(nullptr)) * 1000;
}

// Unix 毫秒时戳 → 无毫秒段的 UTC ISO 串（不足一秒的尾数舍去）
inline std::string formatIsoSecondUtc(int64_t milliseconds)
{
    const time_t seconds = static_cast<time_t>(milliseconds / 1000);
    tm utc{};
    if (::gmtime_r(&seconds, &utc) == nullptr) {
        return {};
    }
    char text[128] = {0};  // 覆盖格式串各字段的整型宽度上界，杜绝截断告警
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                  utc.tm_hour, utc.tm_min, utc.tm_sec);
    return text;
}

}  // namespace gy::rendezvous
