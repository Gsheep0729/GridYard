/**
* @file    frame_codec.cpp
* @version 6.6.2
* @date    2026-06-21
* @author  GridYard Team
* @brief   TLV 帧编解码器实现
*
* 实现 encode() 编码和 feed() 解码状态机。编码时按 Type 分级检查
* 载荷大小，解码时处理粘包/半包，完整帧通过 frameReady 信号交付。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.1] GY   2026-06-21
* * 优化封装性，补充注释
* [v4.15.0] FengChunlin   2026-06-16
* * 按 Type 分级检查 Payload 大小（控制帧 1MB，DataChunk 256MB）
* * errorOccurred 信号添加 ErrorCode 参数
* [v4.13.0] GY   2026-06-14
* * 添加帧载荷长度检查，超限时发射 errorOccurred 信号
* [v0.1.0] GY   2026-04-20
* * Stage 1：实现 encode() + 粘包状态机 feed()
* [v0.0.1] GY   2026-04-06
* * Stage 0 占位：空实现保证链接通过
*/

#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QDebug>

// 构造函数
FrameCodec::FrameCodec(QObject *parent)
    : QObject{parent}
{
}

// 将 type + payload 编码为完整 TLV 帧字节流（含 8 字节帧头）
QByteArray FrameCodec::encode(quint32 type, const QByteArray &payload)
{
    // 按 Type 分级检查载荷长度，DataChunk 允许 256MB，其他帧限制 1MB
    const quint32 maxPayload = gy::protocol::maxPayloadForType(type);
    if (static_cast<quint32>(payload.size()) > maxPayload) {
        qWarning() << "FrameCodec::encode: payload size" << payload.size()
                   << "exceeds maximum" << maxPayload << "for type" << Qt::hex << type;
        return {};
    }

    QByteArray frame;
    // 预分配 header + payload 空间，避免多次 realloc
    frame.reserve(gy::protocol::kHeaderBytes + payload.size());

    // 8 字节帧头：Type(4) + Length(4)，大端序，与协议规格书一致
    QDataStream stream(&frame, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << type;
    stream << static_cast<quint32>(payload.size());

    frame.append(payload);

    return frame;
}

// 将 socket 收到的新数据喂入解码器；遇到完整帧时 emit frameReady
void FrameCodec::feed(const QByteArray &data)
{
    _buffer.append(data);

    // 当已读偏移超过缓冲区一半时，执行一次压缩操作避免内存膨胀
    if (_bufferOffset > _buffer.size() / 2) {
        _buffer.remove(0, static_cast<int>(_bufferOffset));
        _bufferOffset = 0;
    }

    // 循环处理粘包：一次 feed 可能包含多个完整帧（TCP 粘包特性）
    while (true) {
        if (_state == State::WaitingHeader) {
            // 帧头 8 字节未凑齐，等下一次 readyRead
            if (_buffer.size() - _bufferOffset < static_cast<int>(gy::protocol::kHeaderBytes)) {
                break;
            }

            // 解析帧头（大端序），直接用指针算术避免复制
            const char *ptr = _buffer.data() + _bufferOffset;
            QDataStream stream(QByteArray::fromRawData(ptr, gy::protocol::kHeaderBytes));
            stream.setByteOrder(QDataStream::BigEndian);
            stream >> _pendingType;
            stream >> _pendingLength;

            // 按 Type 分级检查帧载荷长度，防止恶意帧占用大量内存
            const quint32 maxPayload = gy::protocol::maxPayloadForType(_pendingType);
            if (_pendingLength > maxPayload) {
                qWarning() << "FrameCodec::feed: payload length" << _pendingLength
                           << "exceeds maximum" << maxPayload << "for type" << Qt::hex << _pendingType;
                emit errorOccurred(gy::protocol::ErrorCode::FrameTooLarge,
                                   tr("帧载荷长度 %1 超出限制 %2（类型 0x%3）")
                                       .arg(_pendingLength)
                                       .arg(maxPayload)
                                       .arg(_pendingType, 0, 16));
                // 超限帧无法恢复，清空缓冲区重置到初始状态
                _buffer.clear();
                _bufferOffset = 0;
                _state = State::WaitingHeader;
                _pendingType = 0;
                _pendingLength = 0;
                break;
            }

            _bufferOffset += gy::protocol::kHeaderBytes;
            _state = State::WaitingPayload;
        }

        if (_state == State::WaitingPayload) {
            // 载荷字节未凑齐，等下一次 readyRead
            if (_buffer.size() - _bufferOffset < static_cast<int>(_pendingLength)) {
                break;
            }

            // 提取载荷并通过信号交付，缓冲区仅移动偏移量不做复制
            QByteArray payload(_buffer.data() + _bufferOffset, static_cast<int>(_pendingLength));
            _bufferOffset += _pendingLength;

            // 完整帧就绪，通知业务层处理
            emit frameReady(_pendingType, payload);

            // 重置状态机，继续尝试解析缓冲区中可能剩余的下一个帧
            _state = State::WaitingHeader;
            _pendingType = 0;
            _pendingLength = 0;
        }
    }
}
