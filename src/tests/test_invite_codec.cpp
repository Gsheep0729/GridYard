/**
* @file    test_invite_codec.cpp
* @version 7.1.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   InviteCodec 单元测试
*
* 测试邀请文本的编解码功能。
*
* Change Log:
* [v7.1.0] GY   2026-07-21
* * Stage 7.1：新增邀请连接编解码测试
*/

#include "invite_codec.h"

#include <QString>
#include <QtTest>

class TestInviteCodec : public QObject {
    Q_OBJECT

private slots:
    // 正常编码解码
    void testEncodeDecode();
    // 缺字段
    void testMissingFields();
    // 端口非法
    void testInvalidPort();
    // IP 非法
    void testInvalidIp();
    // schema 不匹配
    void testInvalidSchema();
};

void TestInviteCodec::testEncodeDecode()
{
    // 正常情况
    {
        InviteCodec::Invite invite;
        invite.deviceId = "test-device-123";
        invite.deviceName = "测试设备";
        invite.ipAddress = "192.168.1.100";
        invite.tcpPort = 35100;
        invite.discoveryPort = 45678;

        QString encoded = InviteCodec::encode(invite);
        QVERIFY(!encoded.isEmpty());
        QVERIFY(encoded.startsWith("gridyard://invite?"));

        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::Invite decoded = InviteCodec::parse(encoded, &error);

        QVERIFY(error == InviteCodec::Error::None);
        QVERIFY(decoded.deviceId == invite.deviceId);
        QVERIFY(decoded.deviceName == invite.deviceName);
        QVERIFY(decoded.ipAddress == invite.ipAddress);
        QVERIFY(decoded.tcpPort == invite.tcpPort);
        QVERIFY(decoded.discoveryPort == invite.discoveryPort);
    }

    // IPv6 地址
    {
        InviteCodec::Invite invite;
        invite.deviceId = "ipv6-device";
        invite.deviceName = "IPv6 设备";
        invite.ipAddress = "::1";
        invite.tcpPort = 35100;
        invite.discoveryPort = 45678;

        QString encoded = InviteCodec::encode(invite);
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::Invite decoded = InviteCodec::parse(encoded, &error);

        QVERIFY(error == InviteCodec::Error::None);
        QVERIFY(decoded.ipAddress == "::1");
    }
}

void TestInviteCodec::testMissingFields()
{
    // 缺少 deviceId
    {
        QString text = "gridyard://invite?name=test&ip=192.168.1.1&tcpPort=35100";
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::MissingDeviceId);
    }
}

void TestInviteCodec::testInvalidPort()
{
    // 端口为 0
    {
        QString text = "gridyard://invite?deviceId=test&name=test&ip=192.168.1.1&tcpPort=0";
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::InvalidTcpPort);
    }

    // 端口超出范围
    {
        QString text = "gridyard://invite?deviceId=test&name=test&ip=192.168.1.1&tcpPort=70000";
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::InvalidTcpPort);
    }
}

void TestInviteCodec::testInvalidIp()
{
    // 非 IP 格式
    {
        QString text = "gridyard://invite?deviceId=test&name=test&ip=not.an.ip&tcpPort=35100";
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::InvalidIp);
    }

    // 空 IP
    {
        QString text = "gridyard://invite?deviceId=test&name=test&ip=&tcpPort=35100";
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::InvalidIp);
    }
}

void TestInviteCodec::testInvalidSchema()
{
    // 错误的 schema
    {
        QString text = "http://example.com/invite?deviceId=test&ip=192.168.1.1&tcpPort=35100";
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::InvalidSchema);
    }

    // 空文本
    {
        QString text;
        InviteCodec::Error error = InviteCodec::Error::None;
        InviteCodec::parse(text, &error);
        QVERIFY(error == InviteCodec::Error::InvalidSchema);
    }
}

QTEST_GUILESS_MAIN(TestInviteCodec)
#include "test_invite_codec.moc"