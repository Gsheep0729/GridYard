/**
* @file    test_rendezvous_protocol.cpp
* @version 8.1.0
* @date    2026-10-28
* @author  GY
* @brief   协调节点协议编解码测试
*
* 测试用例：请求类型解析（含未知类型报错）、注册确认与候选列表构建、
* PeerInfo / RelayInvite 提取与构建的往返一致、token 校验规则、TTL 夹紧。
* 协议层为纯 C++（nlohmann/json），断言语义与旧 QJson 版保持一致。
*/

#include <QtTest/QtTest>
#include <QDateTime>

#include <nlohmann/json.hpp>

#include "rendezvous_protocol.h"

class TestRendezvousProtocol : public QObject {
    Q_OBJECT

private slots:
    void testParseRequestKnownTypes();
    void testParseRequestUnknownType();
    void testParseRequestNullErrorString();
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
    std::string error;
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "register"}}, &error),
             RendezvousProtocol::MessageType::Register);
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "list_peers"}}, &error),
             RendezvousProtocol::MessageType::ListPeers);
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "relay_invite"}}, &error),
             RendezvousProtocol::MessageType::RelayInvite);
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "relay_poll"}}, &error),
             RendezvousProtocol::MessageType::RelayPoll);
    QVERIFY(error.empty());
}

// 未知类型与空对象返回 Error 并给出原因
void TestRendezvousProtocol::testParseRequestUnknownType()
{
    std::string error;
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "shutdown"}}, &error),
             RendezvousProtocol::MessageType::Error);
    QVERIFY2(error.find("未知消息类型") != std::string::npos, "错误信息应说明类型未知");

    error.clear();
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json::object(), &error),
             RendezvousProtocol::MessageType::Error);
    QVERIFY(!error.empty());
}

// 错误出参允许为空的契约面：未知类型不得解引用空指针
void TestRendezvousProtocol::testParseRequestNullErrorString()
{
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "shutdown"}}, nullptr),
             RendezvousProtocol::MessageType::Error);
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json::object(), nullptr),
             RendezvousProtocol::MessageType::Error);
    // 已知类型不受影响
    QCOMPARE(RendezvousProtocol::parseRequest(nlohmann::json{{"type", "register"}}, nullptr),
             RendezvousProtocol::MessageType::Register);
}

// 注册确认携带 TTL 与可解析的服务器时间
void TestRendezvousProtocol::testBuildRegisterAck()
{
    const nlohmann::json ack = RendezvousProtocol::buildRegisterAck(45);
    QCOMPARE(ack["type"].get<std::string>(), std::string("register_ack"));
    QCOMPARE(ack["ttl_seconds"].get<int>(), 45);

    const QDateTime serverTime = QDateTime::fromString(
        QString::fromStdString(ack["server_time"].get<std::string>()), Qt::ISODate);
    QVERIFY2(serverTime.isValid(), "server_time 应是可解析的 ISO 时间");
    QVERIFY2(serverTime.timeSpec() == Qt::UTC
                 || serverTime.offsetFromUtc() == 0,
             "server_time 应为 UTC 时间");
}

// PeerInfo 提取后再构建响应，字段应完整往返
void TestRendezvousProtocol::testPeerInfoRoundTrip()
{
    nlohmann::json request;
    request["device_id"] = "dev-1";
    request["device_name"] = "设备一";
    request["addresses"] = nlohmann::json::array({"10.0.0.1", "192.168.1.1"});
    request["tcp_port"] = 35100;
    request["discovery_port"] = 45678;
    request["ttl_seconds"] = 60;

    const OnlineRegistry::PeerInfo peer = RendezvousProtocol::extractPeerInfo(request);
    QCOMPARE(peer.deviceId, std::string("dev-1"));
    QCOMPARE(peer.deviceName, std::string("设备一"));
    QCOMPARE(peer.addresses.size(), size_t(2));
    QCOMPARE(int(peer.tcpPort), 35100);
    QCOMPARE(int(peer.discoveryPort), 45678);
    QCOMPARE(peer.ttlSeconds, 60);
    QVERIFY(peer.registeredAtMs > 0);

    const nlohmann::json response = RendezvousProtocol::buildPeersResponse({peer});
    QCOMPARE(response["type"].get<std::string>(), std::string("peers"));
    const nlohmann::json items = response["items"];
    QCOMPARE(items.size(), size_t(1));

    const nlohmann::json item = items.front();
    QCOMPARE(item["device_id"].get<std::string>(), std::string("dev-1"));
    QCOMPARE(item["device_name"].get<std::string>(), std::string("设备一"));
    QCOMPARE(item["addresses"].size(), size_t(2));
    QCOMPARE(item["tcp_port"].get<int>(), 35100);
    QCOMPARE(item["discovery_port"].get<int>(), 45678);
    const QDateTime updatedAt = QDateTime::fromString(
        QString::fromStdString(item["updated_at"].get<std::string>()), Qt::ISODate);
    QVERIFY(!updatedAt.isNull());
}

// 候选列表构建保留条目顺序与数量
void TestRendezvousProtocol::testPeersResponseMultipleItems()
{
    OnlineRegistry::PeerInfo a;
    a.deviceId = "dev-a";
    a.tcpPort = 35100;
    a.discoveryPort = 45678;
    a.registeredAtMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

    OnlineRegistry::PeerInfo b;
    b.deviceId = "dev-b";
    b.tcpPort = 35200;
    b.discoveryPort = 45678;
    b.registeredAtMs = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

    const nlohmann::json items = RendezvousProtocol::buildPeersResponse({a, b})["items"];
    QCOMPARE(items.size(), size_t(2));
    QCOMPARE(items.at(0)["device_id"].get<std::string>(), std::string("dev-a"));
    QCOMPARE(items.at(1)["device_id"].get<std::string>(), std::string("dev-b"));
}

// 中继邀请字段提取与受理响应往返一致
void TestRendezvousProtocol::testRelayInviteRoundTrip()
{
    nlohmann::json request;
    request["relay_id"] = "relay-9";
    request["sender_device_id"] = "dev-s";
    request["target_device_id"] = "dev-t";
    request["file_name"] = "demo.zip";
    request["total_bytes"] = qint64(123456789);

    const OnlineRegistry::RelayInvite invite = RendezvousProtocol::extractRelayInvite(request);
    QCOMPARE(invite.relayId, std::string("relay-9"));
    QCOMPARE(invite.senderDeviceId, std::string("dev-s"));
    QCOMPARE(invite.targetDeviceId, std::string("dev-t"));
    QCOMPARE(invite.fileName, std::string("demo.zip"));
    QCOMPARE(invite.totalBytes, qint64(123456789));

    const nlohmann::json ack = RendezvousProtocol::buildRelayInviteAck(invite.relayId);
    QCOMPARE(ack["type"].get<std::string>(), std::string("relay_invite_ack"));
    QCOMPARE(ack["relay_id"].get<std::string>(), std::string("relay-9"));
}

// 邀请列表响应与错误响应的结构
void TestRendezvousProtocol::testBuildRelayInvitesAndError()
{
    OnlineRegistry::RelayInvite invite;
    invite.relayId = "relay-1";
    invite.senderDeviceId = "dev-s";
    invite.targetDeviceId = "dev-t";
    invite.fileName = "a.txt";
    invite.totalBytes = 42;

    const nlohmann::json response = RendezvousProtocol::buildRelayInvites({invite});
    QCOMPARE(response["type"].get<std::string>(), std::string("relay_invites"));
    const nlohmann::json item = response["items"].front();
    QCOMPARE(item["relay_id"].get<std::string>(), std::string("relay-1"));
    QCOMPARE(item["sender_device_id"].get<std::string>(), std::string("dev-s"));
    QCOMPARE(item["total_bytes"].get<qint64>(), qint64(42));

    const nlohmann::json error = RendezvousProtocol::buildError("注册投毒");
    QCOMPARE(error["type"].get<std::string>(), std::string("error"));
    QCOMPARE(error["message"].get<std::string>(), std::string("注册投毒"));
}

// room / token / device_id 字段提取，缺失时返回空串
void TestRendezvousProtocol::testExtractStringFields()
{
    nlohmann::json request;
    request["room"] = "room:with:colon";
    request["token"] = "secret";
    request["device_id"] = "dev-1";

    QCOMPARE(RendezvousProtocol::extractRoom(request), std::string("room:with:colon"));
    QCOMPARE(RendezvousProtocol::extractToken(request), std::string("secret"));
    QCOMPARE(RendezvousProtocol::extractDeviceId(request), std::string("dev-1"));
    QVERIFY(RendezvousProtocol::extractRoom(nlohmann::json::object()).empty());
    QVERIFY(RendezvousProtocol::extractToken(nlohmann::json::object()).empty());
    QVERIFY(RendezvousProtocol::extractDeviceId(nlohmann::json::object()).empty());
}

// token 校验：服务端未配置时放行，配置后要求精确匹配
void TestRendezvousProtocol::testValidateToken()
{
    QVERIFY(RendezvousProtocol::validateToken("anything", ""));
    QVERIFY(RendezvousProtocol::validateToken("", ""));
    QVERIFY(RendezvousProtocol::validateToken("secret", "secret"));
    QVERIFY(!RendezvousProtocol::validateToken("wrong", "secret"));
    QVERIFY(!RendezvousProtocol::validateToken("", "secret"));
}

// 客户端自报 TTL 被服务端夹紧到 [1, 300]，缺失时回退默认 30
void TestRendezvousProtocol::testExtractPeerInfoClampsTtl()
{
    nlohmann::json request;
    request["device_id"] = "dev-ttl";

    nlohmann::json huge = request;
    huge["ttl_seconds"] = 999999;
    QCOMPARE(RendezvousProtocol::extractPeerInfo(huge).ttlSeconds, 300);

    nlohmann::json negative = request;
    negative["ttl_seconds"] = -5;
    QCOMPARE(RendezvousProtocol::extractPeerInfo(negative).ttlSeconds, 1);

    nlohmann::json zero = request;
    zero["ttl_seconds"] = 0;
    QCOMPARE(RendezvousProtocol::extractPeerInfo(zero).ttlSeconds, 1);

    QCOMPARE(RendezvousProtocol::extractPeerInfo(request).ttlSeconds, 30);
}

QTEST_MAIN(TestRendezvousProtocol)
#include "test_rendezvous_protocol.moc"
