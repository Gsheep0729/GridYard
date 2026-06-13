/**
* @file    frame_codec.h
* @version 4.10.0
* @date    2026-06-13
* @author  GridYard Team
* @brief   TLV 帧编解码器（含粘包/半包状态机）
*
* encode() 把 type + payload 拼成 8 字节帧头 + 载荷字节流（大端序）。
* feed() 把 socket 收到的字节流喂进来，内部状态机识别完整帧后通过
* frameReady 信号交付业务层。状态机实现使粘包与半包都被正确处理。
* TCP 收发链路所有模块统一使用本类，禁止自行拼字节。
*
* Change Log:
* [v0.1.0] GY   2026-06-02
* * Stage 1：实现 encode() + 粘包状态机 feed()
* [v0.0.1] GY   2026-05-24
* * Stage 0 占位：仅类声明与空实现
*/

#pragma once

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

private:
    // 状态机状态
    enum class State {
        WaitingHeader,   // 等待帧头（8 字节）
        WaitingPayload   // 等待载荷（length 字节）
    };

    State _state = State::WaitingHeader;
    QByteArray _buffer;
    quint32 _pendingType = 0;
    quint32 _pendingLength = 0;
};
