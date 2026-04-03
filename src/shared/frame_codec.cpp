/**
* @file    frame_codec.cpp
* @date    2026-05-24
* @author  GY
* @brief   FrameCodec 实现
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0 占位：空实现保证链接通过；真正逻辑 Stage 1 任务 1.3 落地
*/

#include "frame_codec.h"

FrameCodec::FrameCodec(QObject *parent)
    : QObject{parent}
{
}

QByteArray FrameCodec::encode(quint32 type, const QByteArray &payload)
{
    // Stage 1 任务 1.3：拼 8 字节大端序帧头 + payload
    Q_UNUSED(type);
    Q_UNUSED(payload);
    return {};
}

void FrameCodec::feed(const QByteArray &data)
{
    // Stage 1 任务 1.3：两状态接收状态机
    // 1) WaitingHeader：累计到 8 字节后解析出 type/length
    // 2) WaitingPayload：累计到 length 字节后 emit frameReady，循环处理粘包
    _buffer.append(data);
}
