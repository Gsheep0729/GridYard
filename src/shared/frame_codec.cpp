/**
* @file    frame_codec.cpp
* @date    2026-05-24
* @author  GY
* @brief   FrameCodec 实现
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0 占位：空实现保证链接通过；真正逻辑 Stage 1 任务 1.3 落地
* [v0.2] GY   2026-06-02
* * Stage 1：实现 encode() + 粘包状态机 feed()
*/

#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>

FrameCodec::FrameCodec(QObject *parent)
    : QObject{parent}
{
}

QByteArray FrameCodec::encode(quint32 type, const QByteArray &payload)
{
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
