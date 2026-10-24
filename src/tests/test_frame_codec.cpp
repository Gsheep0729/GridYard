/**
* @file    test_frame_codec.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GY
* @brief   FrameCodec 单元测试
*
* 测试用例：单帧 / 粘包 / 半包 / 空 payload / 超大 payload / 协议版本 / 分级 Payload 上限
* 被测对象为纯 C++ 双生实现 FrameCodecCore，经 codec_bridge 包装构建与喂入，
* 断言期望值与旧 Qt 版时期保持一致。
*/

#include <QtTest/QtTest>
#include "codec_bridge.h"
#include "protocol.h"

#include <QDataStream>

#include <utility>
#include <vector>

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

// 测试单帧编码解码往返
void TestFrameCodec::testSingleFrame()
{
    std::vector<std::pair<quint32, QByteArray>> decoded;
    int errorCount = 0;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [&errorCount](std::string) { ++errorCount; });

    // 编码一个帧
    quint32 type = gy::protocol::kTypeHello;
    QByteArray payload = "{\"device_id\":\"test\"}";
    QByteArray frame = gy::bridge::encodeFrame(type, payload);

    // 喂入解码器
    gy::bridge::feedFrame(codec, frame);

    // 验证
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(decoded[0].first, type);
    QCOMPARE(decoded[0].second, payload);
    QCOMPARE(errorCount, 0);
}

// 测试粘包场景：多个帧一次性喂入
void TestFrameCodec::testMultipleFrames()
{
    std::vector<std::pair<quint32, QByteArray>> decoded;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [](std::string) {});

    // 编码多个帧
    QByteArray data;
    for (int i = 0; i < 3; ++i) {
        quint32 type = gy::protocol::kTypeHello;
        QByteArray payload = QString("{\"index\":%1}").arg(i).toUtf8();
        data.append(gy::bridge::encodeFrame(type, payload));
    }

    // 一次性喂入（模拟粘包）
    gy::bridge::feedFrame(codec, data);

    // 验证收到 3 个帧
    QCOMPARE(decoded.size(), size_t(3));
}

// 测试半包场景：帧分两次喂入
void TestFrameCodec::testPartialFrame()
{
    std::vector<std::pair<quint32, QByteArray>> decoded;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [](std::string) {});

    // 编码一个帧
    quint32 type = gy::protocol::kTypeTransferReq;
    QByteArray payload = "{\"files\":[]}";
    QByteArray frame = gy::bridge::encodeFrame(type, payload);

    // 分两次喂入（模拟半包）
    gy::bridge::feedFrame(codec, frame.left(4));  // 只喂一半帧头
    QCOMPARE(decoded.size(), size_t(0));           // 还没收到完整帧

    gy::bridge::feedFrame(codec, frame.mid(4));   // 喂剩余部分
    QCOMPARE(decoded.size(), size_t(1));           // 现在收到完整帧

    QCOMPARE(decoded[0].first, type);
    QCOMPARE(decoded[0].second, payload);
}

// 测试空 payload 帧的编解码
void TestFrameCodec::testEmptyPayload()
{
    std::vector<std::pair<quint32, QByteArray>> decoded;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [](std::string) {});

    // 编码空 payload 帧
    quint32 type = gy::protocol::kTypeTransferDone;
    QByteArray payload;
    QByteArray frame = gy::bridge::encodeFrame(type, payload);

    // 喂入解码器
    gy::bridge::feedFrame(codec, frame);

    // 验证
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(decoded[0].first, type);
    QCOMPARE(decoded[0].second.isEmpty(), true);
}

// 测试大 payload（1MB）的编解码
void TestFrameCodec::testLargePayload()
{
    std::vector<std::pair<quint32, QByteArray>> decoded;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [](std::string) {});

    // 编码大 payload 帧（1MB）
    quint32 type = gy::protocol::kTypeDataChunk;
    QByteArray payload(1024 * 1024, 'A');  // 1MB
    QByteArray frame = gy::bridge::encodeFrame(type, payload);

    // 喂入解码器
    gy::bridge::feedFrame(codec, frame);

    // 验证
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(decoded[0].first, type);
    QCOMPARE(decoded[0].second, payload);
}

// 测试超过 Payload 上限时编码失败
void TestFrameCodec::testOversizedPayload()
{
    // encodeFrame 应拒绝超大 payload，返回空 QByteArray
    quint32 type = gy::protocol::kTypeDataChunk;
    QByteArray oversizedPayload(gy::protocol::kMaxPayloadBytes + 1, 'X');
    QByteArray frame = gy::bridge::encodeFrame(type, oversizedPayload);

    // 验证返回空
    QVERIFY(frame.isEmpty());
}

// 测试收到超大帧时发射错误信号
void TestFrameCodec::testOversizedFrame()
{
    // feed 应拒绝超长帧，触发错误回调
    std::vector<std::pair<quint32, QByteArray>> decoded;
    int errorCount = 0;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [&errorCount](std::string) { ++errorCount; });

    // 手动构造一个声称载荷超长的帧头
    QByteArray fakeFrame;
    fakeFrame.resize(gy::protocol::kHeaderBytes);
    QDataStream stream(&fakeFrame, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(gy::protocol::kTypeDataChunk);
    stream << static_cast<quint32>(gy::protocol::kMaxPayloadBytes + 1);

    // 喂入解码器
    gy::bridge::feedFrame(codec, fakeFrame);

    // 验证触发了一次错误回调，没有交付任何帧
    QCOMPARE(errorCount, 1);
    QCOMPARE(decoded.size(), size_t(0));
}

// 测试 Hello 帧携带协议版本字段
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

// 测试控制帧 Payload 上限（1MB）
void TestFrameCodec::testControlFrameLimit()
{
    // 控制帧（kTypeTransferReq）超 1MB 应被 feed 拒绝
    std::vector<std::pair<quint32, QByteArray>> decoded;
    int errorCount = 0;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [&errorCount](std::string) { ++errorCount; });

    const quint32 oneMB = 1024 * 1024;
    const quint32 overLimit = oneMB + 1;

    // 手动构造声称载荷为 1MB+1 的 TransferReq 帧头
    QByteArray fakeFrame;
    fakeFrame.resize(gy::protocol::kHeaderBytes);
    QDataStream stream(&fakeFrame, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint32>(gy::protocol::kTypeTransferReq);
    stream << overLimit;

    gy::bridge::feedFrame(codec, fakeFrame);

    QCOMPARE(errorCount, 1);
    QCOMPARE(decoded.size(), size_t(0));

    // 验证 encodeFrame 也拒绝超大控制帧
    QByteArray oversizePayload(overLimit, 'X');
    QByteArray encoded = gy::bridge::encodeFrame(gy::protocol::kTypeTransferReq, oversizePayload);
    QVERIFY(encoded.isEmpty());
}

// 测试 DataChunk 帧 Payload 上限（256MB）
void TestFrameCodec::testDataChunkLimit()
{
    // DataChunk 超 1MB 但不超 256MB 应被允许
    std::vector<std::pair<quint32, QByteArray>> decoded;
    int errorCount = 0;
    gy::core::FrameCodecCore codec(
        [&decoded](uint32_t type, std::vector<uint8_t> payload) {
            decoded.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [&errorCount](std::string) { ++errorCount; });

    const quint32 twoMB = 2 * 1024 * 1024;

    // encode 2MB DataChunk 应成功
    QByteArray payload(twoMB, 'D');
    QByteArray frame = gy::bridge::encodeFrame(gy::protocol::kTypeDataChunk, payload);
    QVERIFY(!frame.isEmpty());

    // feed 2MB DataChunk 应正常
    gy::bridge::feedFrame(codec, frame);
    QCOMPARE(decoded.size(), size_t(1));
    QCOMPARE(errorCount, 0);
    QCOMPARE(decoded[0].first, static_cast<quint32>(gy::protocol::kTypeDataChunk));
    QCOMPARE(decoded[0].second.size(), static_cast<qsizetype>(twoMB));

    // 超 256MB 的 DataChunk 应被 feed 拒绝
    std::vector<std::pair<quint32, QByteArray>> decoded2;
    int errorCount2 = 0;
    gy::core::FrameCodecCore codec2(
        [&decoded2](uint32_t type, std::vector<uint8_t> payload) {
            decoded2.emplace_back(type, gy::bridge::fromBytes(payload));
        },
        [&errorCount2](std::string) { ++errorCount2; });

    const quint32 over256MB = gy::protocol::kMaxDataPayloadBytes + 1;
    QByteArray bigFrame;
    bigFrame.resize(gy::protocol::kHeaderBytes);
    QDataStream stream2(&bigFrame, QDataStream::WriteOnly);
    stream2.setByteOrder(QDataStream::BigEndian);
    stream2 << static_cast<quint32>(gy::protocol::kTypeDataChunk);
    stream2 << over256MB;

    gy::bridge::feedFrame(codec2, bigFrame);
    QCOMPARE(errorCount2, 1);
    QCOMPARE(decoded2.size(), size_t(0));
}

// 测试聊天帧 Payload 上限（64KB）
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
    QVERIFY(gy::bridge::encodeFrame(gy::protocol::kTypeChatText, oversizePayload).isEmpty());
    QVERIFY(gy::bridge::encodeFrame(gy::protocol::kTypeChatAck, oversizePayload).isEmpty());
}

QTEST_MAIN(TestFrameCodec)
#include "test_frame_codec.moc"
