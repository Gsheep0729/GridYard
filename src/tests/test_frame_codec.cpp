/**
* @file    test_frame_codec.cpp
* @date    2026-06-02
* @author  GY
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

QTEST_MAIN(TestFrameCodec)
#include "test_frame_codec.moc"
