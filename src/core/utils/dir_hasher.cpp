/**
* @file    dir_hasher.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 SHA-256 哈希工具实现
*
* 字节串哈希走 picosha2 一次性接口；文件哈希使用内置的流式 SHA-256
* 上下文（标准 FIPS 180-4 算法）按块喂入，避免大文件整载内存。
* 两路输出与 QCryptographicHash 完全一致，golden 测试对拍锁定。
*/

#include "core/utils/dir_hasher.h"

#include <picosha2.h>

#include <array>
#include <cstring>
#include <fstream>

namespace gy::core {
namespace {

// 流式 SHA-256 上下文（FIPS 180-4；picosha2 无增量接口，大文件需自带状态）
class Sha256Context {
public:
    Sha256Context() { reset(); }

    // 重置到初始状态，可复用
    void reset()
    {
        _state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        _bitLength = 0;
        _bufferLength = 0;
    }

    // 喂入任意长度数据
    void update(const uint8_t *data, size_t length)
    {
        _bitLength += static_cast<uint64_t>(length) * 8;
        while (length > 0) {
            const size_t take = std::min(length, sizeof(_buffer) - _bufferLength);
            std::memcpy(_buffer + _bufferLength, data, take);
            _bufferLength += take;
            data += take;
            length -= take;
            if (_bufferLength == sizeof(_buffer)) {
                transform(_buffer);
                _bufferLength = 0;
            }
        }
    }

    // 结束填充并输出 32 字节摘要
    std::array<uint8_t, 32> final()
    {
        const uint64_t bits = _bitLength;  // 先暂存消息真实长度，padding 不计入
        _buffer[_bufferLength++] = 0x80;
        if (_bufferLength > 56) {
            // 当前块装不下长度字段：补零压缩整块后另开一块
            std::memset(_buffer + _bufferLength, 0, sizeof(_buffer) - _bufferLength);
            transform(_buffer);
            _bufferLength = 0;
        }
        std::memset(_buffer + _bufferLength, 0, 56 - _bufferLength);
        for (int i = 0; i < 8; ++i) {
            _buffer[56 + i] = static_cast<uint8_t>(bits >> (56 - i * 8));  // 大端比特长度
        }
        transform(_buffer);
        _bufferLength = 0;

        std::array<uint8_t, 32> digest{};
        for (size_t i = 0; i < _state.size(); ++i) {
            digest[i * 4] = static_cast<uint8_t>(_state[i] >> 24);
            digest[i * 4 + 1] = static_cast<uint8_t>(_state[i] >> 16);
            digest[i * 4 + 2] = static_cast<uint8_t>(_state[i] >> 8);
            digest[i * 4 + 3] = static_cast<uint8_t>(_state[i]);
        }
        return digest;
    }

private:
    // 压缩函数（标准轮函数）
    void transform(const uint8_t *block)
    {
        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                   | (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3];
        uint32_t e = _state[4], f = _state[5], g = _state[6], h = _state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        _state[0] += a;
        _state[1] += b;
        _state[2] += c;
        _state[3] += d;
        _state[4] += e;
        _state[5] += f;
        _state[6] += g;
        _state[7] += h;
    }

    // 32 位循环右移
    static uint32_t rotr(uint32_t value, unsigned bits)
    {
        return (value >> bits) | (value << (32 - bits));
    }

    std::array<uint32_t, 8> _state;  // 中间哈希状态
    uint64_t _bitLength = 0;         // 已处理总比特数
    uint8_t  _buffer[64]{};          // 待压缩的 64 字节块缓冲
    size_t   _bufferLength = 0;      // 缓冲已填充字节数
};

// 摘要转 64 字符小写十六进制串
std::string toHexLower(const uint8_t *digest)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (int i = 0; i < 32; ++i) {
        hex.push_back(kHex[digest[i] >> 4]);
        hex.push_back(kHex[digest[i] & 0x0F]);
    }
    return hex;
}

}  // namespace

// 计算字节串的 SHA-256（picosha2 一次性接口）
std::string sha256Hex(const uint8_t *data, size_t length)
{
    std::string hex;
    picosha2::hash256_hex_string(data, data + length, hex);
    return hex;
}

// vector 重载转发到指针版本
std::string sha256Hex(const std::vector<uint8_t> &data)
{
    return sha256Hex(data.data(), data.size());
}

// 流式计算文件 SHA-256：64KB 分块喂入内置上下文，不整载内存
std::string sha256HexOfFile(const std::filesystem::path &path, size_t chunk_size)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};  // 打开失败返回空串（调用方以空串判定，与旧实现一致）
    }

    Sha256Context context;
    std::vector<char> buffer(chunk_size);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto readBytes = file.gcount();
        if (readBytes > 0) {
            context.update(reinterpret_cast<const uint8_t *>(buffer.data()),
                            static_cast<size_t>(readBytes));
        }
    }

    const std::array<uint8_t, 32> digest = context.final();
    return toHexLower(digest.data());
}

}  // namespace gy::core
