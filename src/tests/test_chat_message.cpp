/**
* @file    test_chat_message.cpp
* @version 4.16.3
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天消息编解码测试
*
* 覆盖聊天消息的 JSON 往返、字段校验、文本长度限制和业务 Payload 上限，
* 确保非法入站数据不会进入后续聊天连接与消息模型。
*
* Change Log:
* [v4.16.3] GY   2026-06-24
* * 新增 Stage 5 在线聊天消息协议测试
*/

#include <QtTest/QtTest>

#include "chat_message.h"
#include "protocol.h"

#include <QJsonDocument>
#include <QJsonObject>

class TestChatMessage : public QObject {
    Q_OBJECT

private slots:
    void testRoundTrip();
    void testInvalidInput();
    void testInvalidPayload_data();
    void testInvalidPayload();
    void testOversizedPayload();
    void testErrorCategories();
};

// 验证中文和多行消息可以无损往返
void TestChatMessage::testRoundTrip()
{
    gy::ChatMessage source;
    source.messageId = "c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c";
    source.fromDeviceId = "2c7c0f0b-4ca9-44f2-9f9d-6441e9f7ef77";
    source.fromName = "开发机";
    source.content = "第一行中文\nsecond line";
    source.sentAt = QDateTime::fromString("2026-06-24T10:30:15.123Z", Qt::ISODate);

    QByteArray payload;
    gy::ChatMessageError error = gy::ChatMessageError::InvalidInput;
    QString errorMessage;
    QVERIFY2(gy::ChatMessageCodec::encode(source, &payload, &error, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(error, gy::ChatMessageError::None);

    gy::ChatMessage decoded;
    QVERIFY2(gy::ChatMessageCodec::decode(payload, &decoded, &error, &errorMessage),
             qPrintable(errorMessage));
    QCOMPARE(error, gy::ChatMessageError::None);
    QCOMPARE(decoded.messageId, source.messageId);
    QCOMPARE(decoded.fromDeviceId, source.fromDeviceId);
    QCOMPARE(decoded.fromName, source.fromName);
    QCOMPARE(decoded.content, source.content);
    QCOMPARE(decoded.sentAt, source.sentAt);
}

// 验证发送前会拒绝不符合协议的输入
void TestChatMessage::testInvalidInput()
{
    gy::ChatMessage message;
    message.messageId = "not-a-uuid";
    message.fromDeviceId = "device";
    message.fromName = "设备";
    message.content = "文本";
    message.sentAt = QDateTime::currentDateTimeUtc();

    QByteArray payload;
    gy::ChatMessageError error = gy::ChatMessageError::None;
    QVERIFY(!gy::ChatMessageCodec::encode(message, &payload, &error));
    QCOMPARE(error, gy::ChatMessageError::InvalidInput);

    message.messageId = "c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c";
    message.content = "   ";
    QVERIFY(!gy::ChatMessageCodec::encode(message, &payload, &error));
    QCOMPARE(error, gy::ChatMessageError::InvalidInput);

    message.content = QString(gy::protocol::kMaxChatContentChars + 1, u'x');
    QVERIFY(!gy::ChatMessageCodec::encode(message, &payload, &error));
    QCOMPARE(error, gy::ChatMessageError::InvalidInput);
}

// 准备缺字段、类型错误和无效值的入站 Payload
void TestChatMessage::testInvalidPayload_data()
{
    QTest::addColumn<QByteArray>("payload");

    QTest::newRow("missing-content")
        << QByteArray{"{\"message_id\":\"c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c\",\"from_device_id\":\"device\",\"from_name\":\"name\",\"sent_at\":\"2026-06-24T10:30:15Z\"}"};
    QTest::newRow("content-not-string")
        << QByteArray{"{\"message_id\":\"c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c\",\"from_device_id\":\"device\",\"from_name\":\"name\",\"content\":1,\"sent_at\":\"2026-06-24T10:30:15Z\"}"};
    QTest::newRow("blank-content")
        << QByteArray{"{\"message_id\":\"c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c\",\"from_device_id\":\"device\",\"from_name\":\"name\",\"content\":\"  \",\"sent_at\":\"2026-06-24T10:30:15Z\"}"};
    QTest::newRow("invalid-uuid")
        << QByteArray{"{\"message_id\":\"invalid\",\"from_device_id\":\"device\",\"from_name\":\"name\",\"content\":\"text\",\"sent_at\":\"2026-06-24T10:30:15Z\"}"};
    QTest::newRow("invalid-time")
        << QByteArray{"{\"message_id\":\"c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c\",\"from_device_id\":\"device\",\"from_name\":\"name\",\"content\":\"text\",\"sent_at\":\"tomorrow\"}"};
}

// 验证无效入站 Payload 不会生成消息
void TestChatMessage::testInvalidPayload()
{
    QFETCH(QByteArray, payload);

    gy::ChatMessage message;
    gy::ChatMessageError error = gy::ChatMessageError::None;
    QVERIFY(!gy::ChatMessageCodec::decode(payload, &message, &error));
    QCOMPARE(error, gy::ChatMessageError::InvalidPayload);
}

// 验证业务 Payload 上限先于 JSON 解析生效
void TestChatMessage::testOversizedPayload()
{
    QByteArray payload(gy::protocol::kMaxChatPayloadBytes + 1, 'X');

    gy::ChatMessage message;
    gy::ChatMessageError error = gy::ChatMessageError::None;
    QVERIFY(!gy::ChatMessageCodec::decode(payload, &message, &error));
    QCOMPARE(error, gy::ChatMessageError::InvalidPayload);
}

// 验证后续连接层可使用的失败分类保持稳定
void TestChatMessage::testErrorCategories()
{
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::None), quint8(0));
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::InvalidInput), quint8(1));
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::PeerOffline), quint8(2));
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::ConnectionFailed), quint8(3));
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::WriteFailed), quint8(4));
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::ConnectionLost), quint8(5));
    QCOMPARE(static_cast<quint8>(gy::ChatMessageError::InvalidPayload), quint8(6));
}

QTEST_MAIN(TestChatMessage)
#include "test_chat_message.moc"
