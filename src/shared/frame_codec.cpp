/**
* @file    frame_codec.cpp
* @version 6.6.2
* @date    2026-06-21
* @author  GY
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
    // 按 Type 分级检查载荷长度
    const quint32 maxPayload = gy::protocol::maxPayloadForType(type);
    if (static_cast<quint32>(payload.size()) > maxPayload) {
        qWarning() << "FrameCodec::encode: payload size" << payload.size()
                   << "exceeds maximum" << maxPayload << "for type" << Qt::hex << type;
        return {};
    }

    QByteArray frame;
    frame.reserve(gy::protocol::kHeaderBytes + payload.size());

    // 8 字节帧头：Type(4) + Length(4)，大端序
    QDataStream stream(&frame, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << type;
    stream << static_cast<quint32>(payload.size());

    // 追加载荷
    frame.append(payload);

    return frame;
}

// 将 socket 收到的新数据喂入解码器；遇到完整帧时 emit frameReady
void FrameCodec::feed(const QByteArray &data)
{
    _buffer.append(data);

    // 循环处理粘包：一次 feed 可能包含多个完整帧
    while (true) {
        if (_state == State::WaitingHeader) {
            // 等待帧头（8 字节）
            if (_buffer.size() < static_cast<int>(gy::protocol::kHeaderBytes)) {
                break;  // 数据不足，等待更多
            }

            // 解析帧头（大端序）
            QDataStream stream(_buffer.left(gy::protocol::kHeaderBytes));
            stream.setByteOrder(QDataStream::BigEndian);
            stream >> _pendingType;
            stream >> _pendingLength;

            // 按 Type 分级检查帧载荷长度
            const quint32 maxPayload = gy::protocol::maxPayloadForType(_pendingType);
            if (_pendingLength > maxPayload) {
                qWarning() << "FrameCodec::feed: payload length" << _pendingLength
                           << "exceeds maximum" << maxPayload << "for type" << Qt::hex << _pendingType;
                emit errorOccurred(gy::protocol::ErrorCode::FrameTooLarge,
                                   tr("帧载荷长度 %1 超出限制 %2（类型 0x%3）")
                                       .arg(_pendingLength)
                                       .arg(maxPayload)
                                       .arg(_pendingType, 0, 16));
                // 清空缓冲区，重置状态
                _buffer.clear();
                _state = State::WaitingHeader;
                _pendingType = 0;
                _pendingLength = 0;
                break;
            }

            // 移除已解析的帧头
            _buffer.remove(0, gy::protocol::kHeaderBytes);
            _state = State::WaitingPayload;
        }

        if (_state == State::WaitingPayload) {
            // 等待载荷
            if (_buffer.size() < static_cast<int>(_pendingLength)) {
                break;  // 数据不足，等待更多
            }

            // 提取载荷
            QByteArray payload = _buffer.left(_pendingLength);
            _buffer.remove(0, _pendingLength);

            // 发射信号，交付业务层
            emit frameReady(_pendingType, payload);

            // 重置状态，继续处理下一个帧
            _state = State::WaitingHeader;
            _pendingType = 0;
            _pendingLength = 0;
        }
    }
}
