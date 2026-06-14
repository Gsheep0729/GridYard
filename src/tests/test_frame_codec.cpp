/**
* @file    test_frame_codec.cpp
* @date    2026-06-02
* @author  GridYard Team
* @brief   FrameCodec 单元测试
*
* 测试用例：单帧 / 粘包 / 半包 / 空 payload / 超大 payload
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 1：初始版本
*/

#include <QtTest/QtTest>
#include "frame_codec.h"
#include "protocol.h"

class TestFrameCodec : public QObject {
    Q_OBJECT

private slots:
    void testSingleFrame();
    void testMultipleFrames();
    void testPartialFrame();
    void testEmptyPayload();
    void testLargePayload();
    void testOversizedPayload();
    void testOversizedFrame();
    void testProtocolVersion();
};

void TestFrameCodec::testSingleFrame()
{
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);

    // 编码一个帧
    quint32 type = gy::protocol::kTypeHello;
    QByteArray payload = "{\"device_id\":\"test\"}";
    QByteArray frame = FrameCodec::encode(type, payload);

    // 喂入解码器
    codec.feed(frame);

    // 验证
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args[0].toUInt(), type);
    QCOMPARE(args[1].toByteArray(), payload);
}

void TestFrameCodec::testMultipleFrames()
{
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);

    // 编码多个帧
    QByteArray data;
    for (int i = 0; i < 3; ++i) {
        quint32 type = gy::protocol::kTypeHello;
        QByteArray payload = QString("{\"index\":%1}").arg(i).toUtf8();
        data.append(FrameCodec::encode(type, payload));
    }

    // 一次性喂入（模拟粘包）
    codec.feed(data);

    // 验证收到 3 个帧
    QCOMPARE(spy.count(), 3);
}

void TestFrameCodec::testPartialFrame()
{
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);

    // 编码一个帧
    quint32 type = gy::protocol::kTypeTransferReq;
    QByteArray payload = "{\"files\":[]}";
    QByteArray frame = FrameCodec::encode(type, payload);

    // 分两次喂入（模拟半包）
    codec.feed(frame.left(4));  // 只喂一半帧头
    QCOMPARE(spy.count(), 0);   // 还没收到完整帧

    codec.feed(frame.mid(4));   // 喂剩余部分
    QCOMPARE(spy.count(), 1);   // 现在收到完整帧

    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args[0].toUInt(), type);
    QCOMPARE(args[1].toByteArray(), payload);
}

void TestFrameCodec::testEmptyPayload()
{
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);

    // 编码空 payload 帧
    quint32 type = gy::protocol::kTypeTransferDone;
    QByteArray payload;
    QByteArray frame = FrameCodec::encode(type, payload);

    // 喂入解码器
    codec.feed(frame);

    // 验证
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args[0].toUInt(), type);
    QCOMPARE(args[1].toByteArray().isEmpty(), true);
}

void TestFrameCodec::testLargePayload()
{
    FrameCodec codec;
    QSignalSpy spy(&codec, &FrameCodec::frameReady);

    // 编码大 payload 帧（1MB）
    quint32 type = gy::protocol::kTypeDataChunk;
    QByteArray payload(1024 * 1024, 'A');  // 1MB
    QByteArray frame = FrameCodec::encode(type, payload);

    // 喂入解码器
    codec.feed(frame);

    // 验证
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args[0].toUInt(), type);
    QCOMPARE(args[1].toByteArray(), payload);
}

void TestFrameCodec::testOversizedPayload()
{
    // encode() 应拒绝超大 payload，返回空 QByteArray
    quint32 type = gy::protocol::kTypeDataChunk;
    QByteArray oversizedPayload(gy::protocol::kMaxPayloadBytes + 1, 'X');
    QByteArray frame = FrameCodec::encode(type, oversizedPayload);

    // 验证返回空
    QVERIFY(frame.isEmpty());
}

void TestFrameCodec::testOversizedFrame()
{
    // feed() 应拒绝超长帧，发射 errorOccurred 信号
    FrameCodec codec;
    QSignalSpy errorSpy(&codec, &FrameCodec::errorOccurred);
    QSignalSpy frameSpy(&codec, &FrameCodec::frameReady);

    // 手动构造一个声称载荷超长的帧头
    QByteArray fakeFrame;
    fakeFrame.resize(gy::protocol::kHeaderBytes);
    QDataStream stream(&fakeFrame, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(gy::protocol::kTypeDataChunk);
    stream << static_cast<quint32>(gy::protocol::kMaxPayloadBytes + 1);

    // 喂入解码器
    codec.feed(fakeFrame);

    // 验证发射了错误信号，没有发射帧就绪信号
    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(frameSpy.count(), 0);
}

void TestFrameCodec::testProtocolVersion()
{
    // 验证协议版本常量
    QCOMPARE(gy::protocol::kProtocolVersion, quint16(1));

    // 验证最大帧载荷常量
    QCOMPARE(gy::protocol::kMaxPayloadBytes, quint32(256 * 1024 * 1024));

    // 验证错误码枚举值
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::Success), quint16(0));
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::ConnectionTimeout), quint16(1001));
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::FrameTooLarge), quint16(2002));
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::Sha256Mismatch), quint16(4003));
}

QTEST_MAIN(TestFrameCodec)
#include "test_frame_codec.moc"
