/**
* @file    dir_hasher.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SHA-256 哈希工具——不依赖任何 Qt 类型
*
* 字节串一次性哈希使用 vendored picosha2；文件哈希为流式分块实现
* （picosha2 仅提供一次性接口，无法增量计算大文件）。输出与
* QCryptographicHash::Sha256 的十六进制结果逐字节一致，由 golden
* 测试的 NIST 向量与对照值锁定。
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gy::core {

// 计算字节串的 SHA-256，返回 64 字符小写十六进制字符串
// （空输入返回空串的 NIST 标准向量 e3b0c442...b855）
std::string sha256Hex(const uint8_t *data, size_t length);
std::string sha256Hex(const std::vector<uint8_t> &data);

// 计算文件的 SHA-256（流式分块读取，不全量加载到内存）。
// 返回 64 字符小写十六进制字符串；文件无法打开时返回空串。
// chunk_size 为每次读取的字节数，默认 64KB。
std::string sha256HexOfFile(const std::filesystem::path &path, size_t chunk_size = 65536);

}  // namespace gy::core
