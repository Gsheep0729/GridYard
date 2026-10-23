/**
 * @file    rendezvous_protocol_keys_qt.h
 * @version 7.26.1
 * @date    2026-10-24
 * @author  GridYard Team
 * @brief   行协议键名常量的 Qt 侧透传
 *
 * 键值唯一定义源已下沉为纯 C++ 的 rendezvous_protocol_keys.h（服务端
 * 零 Qt 依赖），Qt 客户端的 QJsonObject/QString 调用点经本函数把
 * string_view 转为 QLatin1String 使用，键值字节保持一致。
 */

#pragma once

#include <QLatin1String>
#include <string_view>

#include "rendezvous_protocol_keys.h"

namespace gy::rendezvous {

// 协议键/类型串 → QLatin1String（零拷贝透传，供 Qt JSON 接口使用）
inline QLatin1String key(std::string_view value)
{
    return QLatin1String{value.data(), static_cast<int>(value.size())};
}

}
