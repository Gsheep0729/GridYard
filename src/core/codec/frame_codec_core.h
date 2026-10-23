/**
* @file    frame_codec_core.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 TLV 帧编解码器——不依赖任何 Qt 类型
*
* 编解码逻辑与 src/shared/frame_codec.h 的 Qt 版本完全等价：8 字节帧头
* 为 uint32 Type + uint32 Length（大端序，与《V1.0 架构设计说明书》§7 一致），
* 本实现以 std::vector<uint8_t> 替代 QByteArray、以回调函数替代 Qt 信号槽。
* 帧格式常量与 shared/protocol.h 的对应值由 golden 测试中的 static_assert 锁定。
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gy::core {

// TLV 帧格式常量（与 src/shared/protocol.h 保持一致，golden 测试锁定一致性）
namespace frame {

constexpr uint32_t kHeaderSize = 8;                       // Type(4) + Length(4)
constexpr uint32_t kMaxControlPayload = 1 * 1024 * 1024;  // 控制帧上限 1MB
constexpr uint32_t kMaxDataPayload = 256 * 1024 * 1024;   // DataChunk 上限 256MB
constexpr uint32_t kTypeDataChunk = 0x0201;               // 数据块帧类型（唯一允许大载荷）

// 按帧类型返回载荷上限（与 protocol.h 的 maxPayloadForType 逻辑一致）
constexpr uint32_t maxPayloadForType(uint32_t type)
{
    return type == kTypeDataChunk ? kMaxDataPayload : kMaxControlPayload;
}

}  // namespace frame

// 帧解析结果
struct Frame {
    uint32_t              type = 0;    // 帧类型码
    std::vector<uint8_t>  payload;     // 载荷字节（不含 8 字节帧头）
};

// 有状态流式解码器：线程不安全，每个连接独立持有一个实例。
// 交付语义与 Qt 版一致：回调在 feed() 的调用线程中同步触发，
// payload 以副本交付（与 Qt 版 frameReady 的复制行为对齐）。
class FrameCodecCore {
public:
    // 帧就绪回调：完整帧解析成功时触发
    using FrameCallback = std::function<void(uint32_t type, std::vector<uint8_t> payload)>;
    // 错误回调：帧长超限等协议错误时触发，参数为可读错误描述
    using ErrorCallback = std::function<void(std::string message)>;

    // 构造：注入回调（必须非空，空回调将静默丢帧）
    explicit FrameCodecCore(FrameCallback onFrame, ErrorCallback onError);

    // 喂入字节流（任意大小分片），内部状态机处理粘包与半包
    void feed(const uint8_t *data, size_t length);
    void feed(const std::vector<uint8_t> &data);

    // 将 type + payload 序列化为完整 TLV 帧（含 8 字节大端帧头）。
    // 载荷超限时返回空 vector（与 Qt 版 encode 的失败语义一致）。
    static std::vector<uint8_t> build(uint32_t type, const uint8_t *payload, size_t length);
    static std::vector<uint8_t> build(uint32_t type, const std::vector<uint8_t> &payload);

private:
    // 解析状态机阶段
    enum class State {
        kReadHeader,   // 等待凑齐 8 字节帧头
        kReadPayload,  // 等待凑齐 Length 字节载荷
    };

    // 尝试从缓冲区解析下一帧；返回是否成功消费了一帧（错误时复位并返回 false）
    bool tryParse();
    // 从缓冲区指定偏移按大端序读取 uint32
    uint32_t readU32(size_t offset) const;
    // 回收已消费的缓冲区前缀，避免每帧 O(n) 移动（复刻 Qt 版压缩策略）
    void compact();

    State                _state = State::kReadHeader;  // 当前解析阶段
    std::vector<uint8_t> _buf;                         // 接收缓冲区
    size_t               _offset = 0;                  // 逻辑消费偏移
    uint32_t             _curType = 0;                 // 正在组装的帧类型
    uint32_t             _curLength = 0;               // 正在组装的载荷长度
    FrameCallback        _onFrame;                     // 帧就绪回调
    ErrorCallback        _onError;                     // 错误回调
};

}  // namespace gy::core
