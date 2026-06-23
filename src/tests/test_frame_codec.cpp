/**
* @file    test_frame_codec.cpp
* @version 4.16.2
* @date    2026-06-24
* @author  GridYard Team
* @brief   FrameCodec 单元测试
*
* 测试用例：单帧 / 粘包 / 半包 / 空 payload / 超大 payload / 协议版本 / 分级 Payload 上限
*
* Change Log:
* [v4.16.2] GY   2026-06-24
* * 新增聊天控制帧 Type 与 Payload 上限测试
* [v4.15.1] FengChunlin   2026-06-17
* * 新增 testControlFrameLimit：控制帧超 1MB 被 feed/encode 拒绝
* * 新增 testDataChunkLimit：DataChunk 允许超 1MB，超 256MB 被拒绝
* [v4.15.0] GY   2026-06-17
* * 适配协议版本常量变化（kProtocolVersion 改为 0x0100）
* * 新增版本号提取函数和分级 Payload 上限测试
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
    void testControlFrameLimit();
    void testDataChunkLimit();
    void testChatFrameLimit();
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
    // 验证协议版本常量（高8位主版本，低8位次版本）
    QCOMPARE(gy::protocol::kProtocolVersion, quint16(0x0100));
    QCOMPARE(gy::protocol::kProtocolMajorVersion, quint8(1));
    QCOMPARE(gy::protocol::kProtocolMinorVersion, quint8(0));

    // 验证版本号提取函数
    QCOMPARE(gy::protocol::majorVersion(quint16(0x0102)), quint8(1));
    QCOMPARE(gy::protocol::minorVersion(quint16(0x0102)), quint8(2));

    // 验证最大帧载荷常量
    QCOMPARE(gy::protocol::kMaxPayloadBytes, quint32(256 * 1024 * 1024));
    QCOMPARE(gy::protocol::kMaxControlPayloadBytes, quint32(1 * 1024 * 1024));
    QCOMPARE(gy::protocol::kMaxDataPayloadBytes, quint32(256 * 1024 * 1024));

    // 验证按 Type 分级的 Payload 上限
    QCOMPARE(gy::protocol::maxPayloadForType(gy::protocol::kTypeDataChunk), quint32(256 * 1024 * 1024));
    QCOMPARE(gy::protocol::maxPayloadForType(gy::protocol::kTypeTransferReq), quint32(1 * 1024 * 1024));

    // 验证错误码枚举值
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::Success), quint16(0));
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::ConnectionTimeout), quint16(1001));
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::FrameTooLarge), quint16(2002));
    QCOMPARE(static_cast<quint16>(gy::protocol::ErrorCode::Sha256Mismatch), quint16(4003));
}

void TestFrameCodec::testControlFrameLimit()
{
    // 控制帧（kTypeTransferReq）超 1MB 应被 feed() 拒绝
    FrameCodec codec;
    QSignalSpy errorSpy(&codec, &FrameCodec::errorOccurred);
    QSignalSpy frameSpy(&codec, &FrameCodec::frameReady);

    const quint32 oneMB = 1024 * 1024;
    const quint32 overLimit = oneMB + 1;

    // 手动构造声称载荷为 1MB+1 的 TransferReq 帧头
    QByteArray fakeFrame;
    fakeFrame.resize(gy::protocol::kHeaderBytes);
    QDataStream stream(&fakeFrame, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(gy::protocol::kTypeTransferReq);
    stream << overLimit;

    codec.feed(fakeFrame);

    QCOMPARE(errorSpy.count(), 1);
    QCOMPARE(frameSpy.count(), 0);

    // 验证 encode() 也拒绝超大控制帧
    QByteArray oversizePayload(overLimit, 'X');
    QByteArray encoded = FrameCodec::encode(gy::protocol::kTypeTransferReq, oversizePayload);
    QVERIFY(encoded.isEmpty());
}

void TestFrameCodec::testDataChunkLimit()
{
    // DataChunk 超 1MB 但不超 256MB 应被允许
    FrameCodec codec;
    QSignalSpy frameSpy(&codec, &FrameCodec::frameReady);
    QSignalSpy errorSpy(&codec, &FrameCodec::errorOccurred);

    const quint32 twoMB = 2 * 1024 * 1024;

    // encode 2MB DataChunk 应成功
    QByteArray payload(twoMB, 'D');
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeDataChunk, payload);
    QVERIFY(!frame.isEmpty());

    // feed 2MB DataChunk 应正常
    codec.feed(frame);
    QCOMPARE(frameSpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(frameSpy.first().at(0).toUInt(), static_cast<quint32>(gy::protocol::kTypeDataChunk));
    QCOMPARE(frameSpy.first().at(1).toByteArray().size(), static_cast<int>(twoMB));

    // 超 256MB 的 DataChunk 应被 feed() 拒绝
    FrameCodec codec2;
    QSignalSpy errorSpy2(&codec2, &FrameCodec::errorOccurred);
    QSignalSpy frameSpy2(&codec2, &FrameCodec::frameReady);

    const quint32 over256MB = gy::protocol::kMaxDataPayloadBytes + 1;
    QByteArray bigFrame;
    bigFrame.resize(gy::protocol::kHeaderBytes);
    QDataStream stream2(&bigFrame, QDataStream::WriteOnly);
    stream2.setByteOrder(QDataStream::BigEndian);
    stream2 << static_cast<quint32>(gy::protocol::kTypeDataChunk);
    stream2 << over256MB;

    codec2.feed(bigFrame);
    QCOMPARE(errorSpy2.count(), 1);
    QCOMPARE(frameSpy2.count(), 0);
}

void TestFrameCodec::testChatFrameLimit()
{
    QCOMPARE(gy::protocol::kTypeChatText, quint32(0x0501));
    QCOMPARE(gy::protocol::kTypeChatAck, quint32(0x0502));
    QCOMPARE(gy::protocol::maxPayloadForType(gy::protocol::kTypeChatText),
             gy::protocol::kMaxControlPayloadBytes);
    QCOMPARE(gy::protocol::maxPayloadForType(gy::protocol::kTypeChatAck),
             gy::protocol::kMaxControlPayloadBytes);

    const quint32 overLimit = gy::protocol::kMaxControlPayloadBytes + 1;
    QByteArray oversizePayload(overLimit, 'C');
    QVERIFY(FrameCodec::encode(gy::protocol::kTypeChatText, oversizePayload).isEmpty());
    QVERIFY(FrameCodec::encode(gy::protocol::kTypeChatAck, oversizePayload).isEmpty());
}

QTEST_MAIN(TestFrameCodec)
#include "test_frame_codec.moc"
