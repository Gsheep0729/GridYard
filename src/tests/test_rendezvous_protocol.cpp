/**
* @file    test_rendezvous_protocol.cpp
* @version 7.17.1
* @date    2026-10-04
* @author  GY
* @brief   协调节点协议编解码测试
*
* 测试用例：请求类型解析（含未知类型报错）、注册确认与候选列表构建、
* PeerInfo / RelayInvite 提取与构建的往返一致、token 校验规则、TTL 夹紧。
*
* Change Log:
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.13.4] GY   2026-10-03
* * 新增 extractPeerInfo 的 TTL 夹紧用例
*/

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "rendezvous_protocol.h"

class TestRendezvousProtocol : public QObject {
    Q_OBJECT

private slots:
    void testParseRequestKnownTypes();
    void testParseRequestUnknownType();
    void testBuildRegisterAck();
    void testPeerInfoRoundTrip();
    void testPeersResponseMultipleItems();
    void testRelayInviteRoundTrip();
    void testBuildRelayInvitesAndError();
    void testExtractStringFields();
    void testValidateToken();
    void testExtractPeerInfoClampsTtl();
};

// 四种请求类型都能被正确解析
void TestRendezvousProtocol::testParseRequestKnownTypes()
{
    QString error;
    QCOMPARE(RendezvousProtocol::parseRequest(QJsonObject{{"type", "register"}}, &error),
             RendezvousProtocol::MessageType::Register);
    QCOMPARE(RendezvousProtocol::parseRequest(QJsonObject{{"type", "list_peers"}}, &error),
             RendezvousProtocol::MessageType::ListPeers);
    QCOMPARE(RendezvousProtocol::parseRequest(QJsonObject{{"type", "relay_invite"}}, &error),
             RendezvousProtocol::MessageType::RelayInvite);
    QCOMPARE(RendezvousProtocol::parseRequest(QJsonObject{{"type", "relay_poll"}}, &error),
             RendezvousProtocol::MessageType::RelayPoll);
    QVERIFY(error.isEmpty());
}

// 未知类型与空对象返回 Error 并给出原因
void TestRendezvousProtocol::testParseRequestUnknownType()
{
    QString error;
    QCOMPARE(RendezvousProtocol::parseRequest(QJsonObject{{"type", "shutdown"}}, &error),
             RendezvousProtocol::MessageType::Error);
    QVERIFY2(error.contains("未知消息类型"), "错误信息应说明类型未知");

    error.clear();
    QCOMPARE(RendezvousProtocol::parseRequest(QJsonObject{}, &error),
             RendezvousProtocol::MessageType::Error);
    QVERIFY(!error.isEmpty());
}

// 注册确认携带 TTL 与可解析的服务器时间
void TestRendezvousProtocol::testBuildRegisterAck()
{
    const QJsonObject ack = RendezvousProtocol::buildRegisterAck(45);
    QCOMPARE(ack["type"].toString(), QStringLiteral("register_ack"));
    QCOMPARE(ack["ttl_seconds"].toInt(), 45);

    const QDateTime serverTime = QDateTime::fromString(ack["server_time"].toString(), Qt::ISODate);
    QVERIFY2(serverTime.isValid(), "server_time 应是可解析的 ISO 时间");
}

// PeerInfo 提取后再构建响应，字段应完整往返
void TestRendezvousProtocol::testPeerInfoRoundTrip()
{
    QJsonObject request;
    request["device_id"] = QStringLiteral("dev-1");
    request["device_name"] = QStringLiteral("设备一");
    request["addresses"] = QJsonArray{QStringLiteral("10.0.0.1"), QStringLiteral("192.168.1.1")};
    request["tcp_port"] = 35100;
    request["discovery_port"] = 45678;
    request["ttl_seconds"] = 60;

    const OnlineRegistry::PeerInfo peer = RendezvousProtocol::extractPeerInfo(request);
    QCOMPARE(peer.deviceId, QStringLiteral("dev-1"));
    QCOMPARE(peer.deviceName, QStringLiteral("设备一"));
    QCOMPARE(peer.addresses.size(), 2);
    QCOMPARE(int(peer.tcpPort), 35100);
    QCOMPARE(int(peer.discoveryPort), 45678);
    QCOMPARE(peer.ttlSeconds, 60);
    QVERIFY(peer.registeredAt.isValid());

    const QJsonObject response = RendezvousProtocol::buildPeersResponse({peer});
    QCOMPARE(response["type"].toString(), QStringLiteral("peers"));
    const QJsonArray items = response["items"].toArray();
    QCOMPARE(items.size(), 1);

    const QJsonObject item = items.first().toObject();
    QCOMPARE(item["device_id"].toString(), QStringLiteral("dev-1"));
    QCOMPARE(item["device_name"].toString(), QStringLiteral("设备一"));
    QCOMPARE(item["addresses"].toArray().size(), 2);
    QCOMPARE(item["tcp_port"].toInt(), 35100);
    QCOMPARE(item["discovery_port"].toInt(), 45678);
    QVERIFY(!QDateTime::fromString(item["updated_at"].toString(), Qt::ISODate).isNull());
}

// 候选列表构建保留条目顺序与数量
void TestRendezvousProtocol::testPeersResponseMultipleItems()
{
    OnlineRegistry::PeerInfo a;
    a.deviceId = QStringLiteral("dev-a");
    a.tcpPort = 35100;
    a.discoveryPort = 45678;
    a.registeredAt = QDateTime::currentDateTimeUtc();

    OnlineRegistry::PeerInfo b;
    b.deviceId = QStringLiteral("dev-b");
    b.tcpPort = 35200;
    b.discoveryPort = 45678;
    b.registeredAt = QDateTime::currentDateTimeUtc();

    const QJsonArray items = RendezvousProtocol::buildPeersResponse({a, b})["items"].toArray();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toObject()["device_id"].toString(), QStringLiteral("dev-a"));
    QCOMPARE(items.at(1).toObject()["device_id"].toString(), QStringLiteral("dev-b"));
}

// 中继邀请字段提取与受理响应往返一致
void TestRendezvousProtocol::testRelayInviteRoundTrip()
{
    QJsonObject request;
    request["relay_id"] = QStringLiteral("relay-9");
    request["sender_device_id"] = QStringLiteral("dev-s");
    request["target_device_id"] = QStringLiteral("dev-t");
    request["file_name"] = QStringLiteral("demo.zip");
    request["total_bytes"] = qint64(123456789);

    const OnlineRegistry::RelayInvite invite = RendezvousProtocol::extractRelayInvite(request);
    QCOMPARE(invite.relayId, QStringLiteral("relay-9"));
    QCOMPARE(invite.senderDeviceId, QStringLiteral("dev-s"));
    QCOMPARE(invite.targetDeviceId, QStringLiteral("dev-t"));
    QCOMPARE(invite.fileName, QStringLiteral("demo.zip"));
    QCOMPARE(invite.totalBytes, qint64(123456789));

    const QJsonObject ack = RendezvousProtocol::buildRelayInviteAck(invite.relayId);
    QCOMPARE(ack["type"].toString(), QStringLiteral("relay_invite_ack"));
    QCOMPARE(ack["relay_id"].toString(), QStringLiteral("relay-9"));
}

// 邀请列表响应与错误响应的结构
void TestRendezvousProtocol::testBuildRelayInvitesAndError()
{
    OnlineRegistry::RelayInvite invite;
    invite.relayId = QStringLiteral("relay-1");
    invite.senderDeviceId = QStringLiteral("dev-s");
    invite.targetDeviceId = QStringLiteral("dev-t");
    invite.fileName = QStringLiteral("a.txt");
    invite.totalBytes = 42;

    const QJsonObject response = RendezvousProtocol::buildRelayInvites({invite});
    QCOMPARE(response["type"].toString(), QStringLiteral("relay_invites"));
    const QJsonObject item = response["items"].toArray().first().toObject();
    QCOMPARE(item["relay_id"].toString(), QStringLiteral("relay-1"));
    QCOMPARE(item["sender_device_id"].toString(), QStringLiteral("dev-s"));
    QCOMPARE(item["total_bytes"].toInteger(), qint64(42));

    const QJsonObject error = RendezvousProtocol::buildError(QStringLiteral("注册投毒"));
    QCOMPARE(error["type"].toString(), QStringLiteral("error"));
    QCOMPARE(error["message"].toString(), QStringLiteral("注册投毒"));
}

// room / token / device_id 字段提取，缺失时返回空串
void TestRendezvousProtocol::testExtractStringFields()
{
    QJsonObject request;
    request["room"] = QStringLiteral("room:with:colon");
    request["token"] = QStringLiteral("secret");
    request["device_id"] = QStringLiteral("dev-1");

    QCOMPARE(RendezvousProtocol::extractRoom(request), QStringLiteral("room:with:colon"));
    QCOMPARE(RendezvousProtocol::extractToken(request), QStringLiteral("secret"));
    QCOMPARE(RendezvousProtocol::extractDeviceId(request), QStringLiteral("dev-1"));
    QVERIFY(RendezvousProtocol::extractRoom(QJsonObject{}).isEmpty());
    QVERIFY(RendezvousProtocol::extractToken(QJsonObject{}).isEmpty());
    QVERIFY(RendezvousProtocol::extractDeviceId(QJsonObject{}).isEmpty());
}

// token 校验：服务端未配置时放行，配置后要求精确匹配
void TestRendezvousProtocol::testValidateToken()
{
    QVERIFY(RendezvousProtocol::validateToken(QStringLiteral("anything"), QString()));
    QVERIFY(RendezvousProtocol::validateToken(QString(), QString()));
    QVERIFY(RendezvousProtocol::validateToken(QStringLiteral("secret"), QStringLiteral("secret")));
    QVERIFY(!RendezvousProtocol::validateToken(QStringLiteral("wrong"), QStringLiteral("secret")));
    QVERIFY(!RendezvousProtocol::validateToken(QString(), QStringLiteral("secret")));
}

// 客户端自报 TTL 被服务端夹紧到 [1, 300]，缺失时回退默认 30
void TestRendezvousProtocol::testExtractPeerInfoClampsTtl()
{
    QJsonObject request;
    request["device_id"] = QStringLiteral("dev-ttl");

    QJsonObject huge = request;
    huge["ttl_seconds"] = 999999;
    QCOMPARE(RendezvousProtocol::extractPeerInfo(huge).ttlSeconds, 300);

    QJsonObject negative = request;
    negative["ttl_seconds"] = -5;
    QCOMPARE(RendezvousProtocol::extractPeerInfo(negative).ttlSeconds, 1);

    QJsonObject zero = request;
    zero["ttl_seconds"] = 0;
    QCOMPARE(RendezvousProtocol::extractPeerInfo(zero).ttlSeconds, 1);

    QCOMPARE(RendezvousProtocol::extractPeerInfo(request).ttlSeconds, 30);
}

QTEST_MAIN(TestRendezvousProtocol)
#include "test_rendezvous_protocol.moc"
