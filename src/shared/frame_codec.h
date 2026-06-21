/**
* @file    frame_codec.h
* @version 4.16.1
* @date    2026-06-21
* @author  GridYard Team
* @brief   TLV 帧编解码器（含粘包/半包状态机）
*
* encode() 将 type + payload 编码为 8 字节帧头 + 载荷字节流（大端序）。
* feed() 将 socket 收到的字节流喂入内部状态机，识别完整帧后通过
* frameReady 信号交付业务层。粘包与半包均被正确处理。
* TCP 收发链路所有模块统一使用本类，禁止自行拼装字节。
*
* Change Log:
* [v4.16.1] GY   2026-06-21
* * 优化封装性，补充注释
* [v4.15.0] FengChunlin   2026-06-16
* * 按 Type 分级检查 Payload 大小（控制帧 1MB，DataChunk 256MB）
* * errorOccurred 信号添加 ErrorCode 参数
* [v4.13.0] GY   2026-06-14
* * 添加 errorOccurred 信号，帧长超限时发射
* [v0.1.0] GY   2026-04-20
* * Stage 1：实现 encode() + 粘包状态机 feed()
* [v0.0.1] GY   2026-04-06
* * Stage 0 占位：仅类声明与空实现
*/

#pragma once

#include "protocol.h"

#include <QByteArray>
#include <QObject>

class FrameCodec : public QObject {
    Q_OBJECT

public:
    explicit FrameCodec(QObject *parent = nullptr);
    virtual ~FrameCodec() override = default;

    FrameCodec(const FrameCodec &)            = delete;
    FrameCodec &operator=(const FrameCodec &) = delete;

    // 把 type + payload 编码为完整 TLV 帧字节流（含 8 字节帧头）
    static QByteArray encode(quint32 type, const QByteArray &payload);

    // 把 socket 收到的新数据喂入解码器；遇到完整帧时 emit frameReady
    void feed(const QByteArray &data);

signals:
    // 一个完整 TLV 帧就绪：type 是帧头 Type 字段，payload 是载荷
    void frameReady(quint32 type, const QByteArray &payload);
    // 协议错误（帧长超限、格式错误等）
    void errorOccurred(gy::protocol::ErrorCode errorCode, const QString &errorMsg);

private:
    // 状态机状态枚举
    enum class State {
        WaitingHeader,   // 等待帧头（8 字节）
        WaitingPayload   // 等待载荷（length 字节）
    };

    State _state = State::WaitingHeader;    // 当前状态机状态
    QByteArray _buffer;                     // 接收缓冲区，累积 socket 数据
    quint32 _pendingType = 0;              // 待处理帧的 Type 字段
    quint32 _pendingLength = 0;            // 待处理帧的载荷长度
};
