/**
* @file    frame_codec.h
* @date    2026-05-24
* @author  GY
* @brief   TLV 帧编解码器（含粘包/半包状态机）
*
* encode() 把 type + payload 拼成 8 字节帧头 + 载荷字节流（大端序）。
* feed() 把 socket 收到的字节流喂进来，内部状态机识别完整帧后通过
* frameReady 信号交付业务层。状态机实现使粘包与半包都被正确处理。
* TCP 收发链路所有模块统一使用本类，禁止自行拼字节。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0 占位：仅类声明与空实现；状态机逻辑 Stage 1 任务 1.3 填充
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
    // Stage 1 任务 1.3 填充：状态机状态、接收缓冲区、待处理帧头字段
    QByteArray _buffer;
};
