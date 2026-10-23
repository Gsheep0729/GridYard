/**
* @file    iso_time.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 UTC 时间格式化与解析——不依赖任何 Qt 类型
*
* 输出与 QDateTime::toUTC().toString(Qt::ISODateWithMs) 逐字节一致
*（"yyyy-MM-ddTHH:mm:ss.zzzZ"），这是新旧两套存储实现对同一数据库
* 文本列保持排序兼容的前提，由存储测试的格式对拍用例锁定。
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gy::storage {

// Unix 毫秒时间戳格式化为 ISO 8601 文本（UTC，毫秒三位，Z 结尾）
std::string toIso8601Ms(int64_t unixMillis);

// 解析 ISO 8601 文本为 Unix 毫秒时间戳；格式不符返回空
// （接受 1~9 位小数秒，按毫秒截断；仅接受 Z 或 +00:00 的零偏移）
std::optional<int64_t> parseIso8601Ms(std::string_view text);

// 当前系统 UTC 时间的 Unix 毫秒时间戳
int64_t nowUnixMillis();

// Unix 毫秒时间戳格式化为紧凑时间戳（yyyyMMddHHmmsszzz，用于备份命名）
std::string toCompactStamp(int64_t unixMillis);

}  // namespace gy::storage
