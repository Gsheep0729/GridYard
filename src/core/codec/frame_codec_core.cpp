/**
* @file    frame_codec_core.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 TLV 帧编解码器实现
*
* 状态机、缓冲压缩与超限复位行为逐项复刻 src/shared/frame_codec.cpp
* （粘包循环、_bufferOffset 过半压缩、超限清空缓冲重置），由 golden
* 测试与 Qt 版输出逐字节对拍保证等价。
*/

#include "core/codec/frame_codec_core.h"

#include <format>
#include <utility>

namespace gy::core {

// 构造函数：注入帧就绪与错误回调
FrameCodecCore::FrameCodecCore(FrameCallback onFrame, ErrorCallback onError)
    : _onFrame(std::move(onFrame))
    , _onError(std::move(onError))
{
}

// 将 type + payload 序列化为完整 TLV 帧
std::vector<uint8_t> FrameCodecCore::build(uint32_t type, const uint8_t *payload, size_t length)
{
    if (length > frame::maxPayloadForType(type)) {
        return {};  // 超限编码失败返回空，调用方以 empty() 判定（与 Qt 版一致）
    }

    std::vector<uint8_t> out;
    out.reserve(frame::kHeaderSize + length);
    // 8 字节大端帧头：Type(4) + Length(4)，与协议规格书一致
    out.push_back(static_cast<uint8_t>(type >> 24));
    out.push_back(static_cast<uint8_t>(type >> 16));
    out.push_back(static_cast<uint8_t>(type >> 8));
    out.push_back(static_cast<uint8_t>(type));
    out.push_back(static_cast<uint8_t>(length >> 24));
    out.push_back(static_cast<uint8_t>(length >> 16));
    out.push_back(static_cast<uint8_t>(length >> 8));
    out.push_back(static_cast<uint8_t>(length));
    out.insert(out.end(), payload, payload + length);
    return out;
}

// vector 重载转发到指针版本
std::vector<uint8_t> FrameCodecCore::build(uint32_t type, const std::vector<uint8_t> &payload)
{
    return build(type, payload.data(), payload.size());
}

// 喂入字节流并循环解析粘包
void FrameCodecCore::feed(const uint8_t *data, size_t length)
{
    _buf.insert(_buf.end(), data, data + length);

    // 已读偏移超过缓冲区一半时压缩，避免内存随长连接膨胀（与 Qt 版同策略）
    if (_offset > _buf.size() / 2) {
        compact();
    }

    while (tryParse()) {
        // 一个 feed 可能包含多个完整帧，逐帧交付
    }
}

// vector 重载转发到指针版本
void FrameCodecCore::feed(const std::vector<uint8_t> &data)
{
    feed(data.data(), data.size());
}

// 尝试解析一帧：先凑帧头再凑载荷，任一不满足即等待后续数据
bool FrameCodecCore::tryParse()
{
    if (_state == State::kReadHeader) {
        if (_buf.size() - _offset < frame::kHeaderSize) {
            return false;
        }

        _curType = readU32(_offset);
        _curLength = readU32(_offset + 4);

        // 按类型分级校验载荷上限，防止恶意超长帧占用内存
        const uint32_t maxPayload = frame::maxPayloadForType(_curType);
        if (_curLength > maxPayload) {
            if (_onError) {
                _onError(std::format("帧载荷长度 {} 超出限制 {}（类型 0x{:x}）", _curLength,
                                     maxPayload, _curType));
            }
            // 超限帧无法恢复：清空缓冲区并复位状态机（与 Qt 版行为一致）
            _buf.clear();
            _offset = 0;
            _state = State::kReadHeader;
            _curType = 0;
            _curLength = 0;
            return false;
        }

        _offset += frame::kHeaderSize;
        _state = State::kReadPayload;
    }

    if (_state == State::kReadPayload) {
        if (_buf.size() - _offset < _curLength) {
            return false;
        }

        // 复制载荷交付（与 Qt 版 frameReady 的复制语义对齐），随后复位状态机
        std::vector<uint8_t> payload(_buf.begin() + static_cast<std::ptrdiff_t>(_offset),
                                     _buf.begin() + static_cast<std::ptrdiff_t>(_offset + _curLength));
        _offset += _curLength;
        const uint32_t type = _curType;
        _state = State::kReadHeader;
        _curType = 0;
        _curLength = 0;

        if (_onFrame) {
            _onFrame(type, std::move(payload));
        }
        return true;
    }

    return false;
}

// 按大端序读取 4 字节无符号整数
uint32_t FrameCodecCore::readU32(size_t offset) const
{
    const uint8_t *p = _buf.data() + offset;
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
           | (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

// 回收已消费前缀：把未消费数据移到缓冲区头部
void FrameCodecCore::compact()
{
    _buf.erase(_buf.begin(), _buf.begin() + static_cast<std::ptrdiff_t>(_offset));
    _offset = 0;
}

}  // namespace gy::core
