/**
* @file    test_rendezvous_client.cpp
* @version 7.14.2
* @date    2026-10-02
* @author  GY
* @brief   协调节点客户端测试
*
* 用真实 RendezvousServer（随机端口）验证客户端协议链路：连接注册、
* 候选设备列表解析、中继邀请受理、主动断开与连接失败报错，
* 以及访问令牌的携带与校验（配 token 正常往返、错 token 被拒）。
*
* Change Log:
* [v7.14.2] GY   2026-10-03
* * 新增断线重连成功路径用例
* [v7.14.0] GY   2026-10-03
* * 新增访问令牌往返与错误令牌被拒用例
*/

#include <QtTest/QtTest>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>

#include "rendezvous_client.h"
#include "rendezvous_server.h"

class TestRendezvousClient : public QObject {
    Q_OBJECT

private slots:
    void testRegisterReceivesAck();
    void testListPeersContainsOtherDevice();
    void testRelayInviteAckForOnlineTarget();
    void testDisconnectStopsSession();
    void testConnectRefusedReportsError();
    void testPeerEndpointToVariantMap();
    void testRegisterWithTokenSucceeds();
    void testRegisterWithWrongTokenRejected();
    void testReconnectAfterServerRestart();

private:
    // 连接协调服务器并完成注册（阻塞等待两个信号）
    void connectAndRegister(RendezvousClient &client, RendezvousServer *server,
                            const QString &deviceId);
};

// 连接协调服务器并完成注册（阻塞等待两个信号）
void TestRendezvousClient::connectAndRegister(RendezvousClient &client, RendezvousServer *server,
                                              const QString &deviceId)
{
    QSignalSpy connectedSpy(&client, &RendezvousClient::connected);
    client.connectToServer(QStringLiteral("127.0.0.1"), server->serverPort());
    QVERIFY2(connectedSpy.wait(3000), "客户端应在 3 秒内连上本机协调服务器");

    QSignalSpy ackSpy(&client, &RendezvousClient::registerAckReceived);
    client.registerDevice(QStringLiteral("room-gy"), deviceId,
                          QStringLiteral("设备-") + deviceId,
                          {QStringLiteral("127.0.0.1")}, 35100, 45678);
    QVERIFY2(ackSpy.wait(3000), "注册后应收到 register_ack");
}

// 注册后应收到带 TTL 的注册确认
void TestRendezvousClient::testRegisterReceivesAck()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());
    QVERIFY(server.serverPort() > 0);

    RendezvousClient client;
    QSignalSpy ackSpy(&client, &RendezvousClient::registerAckReceived);
    connectAndRegister(client, &server, QStringLiteral("dev-a"));

    QVERIFY(ackSpy.count() >= 1);
    QVERIFY(ackSpy.at(0).at(0).toInt() > 0);  // TTL 为正数
    QVERIFY(client.isConnected());
}

// 同房间的另一台设备注册后，listPeers 应能解析出对方
void TestRendezvousClient::testListPeersContainsOtherDevice()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());

    RendezvousClient clientA;
    RendezvousClient clientB;
    connectAndRegister(clientA, &server, QStringLiteral("dev-a"));
    connectAndRegister(clientB, &server, QStringLiteral("dev-b"));

    QSignalSpy peersSpy(&clientA, &RendezvousClient::peersReceived);
    clientA.listPeers(QStringLiteral("room-gy"));
    QVERIFY2(peersSpy.wait(3000), "listPeers 后应收到 peers 响应");

    const auto peers = peersSpy.at(0).at(0).value<QList<QVariantMap>>();
    QStringList deviceIds;
    for (const QVariantMap &peer : peers) {
        deviceIds.append(peer.value("deviceId").toString());
    }
    QVERIFY2(deviceIds.contains(QStringLiteral("dev-b")), "候选列表应包含 dev-b");
    QVERIFY2(deviceIds.contains(QStringLiteral("dev-a")), "候选列表应包含本机 dev-a");
}

// 目标设备在线时，中继邀请应被协调服务器受理并回 ack
void TestRendezvousClient::testRelayInviteAckForOnlineTarget()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());

    RendezvousClient clientA;
    RendezvousClient clientB;
    connectAndRegister(clientA, &server, QStringLiteral("dev-a"));
    connectAndRegister(clientB, &server, QStringLiteral("dev-b"));

    QSignalSpy ackSpy(&clientA, &RendezvousClient::relayInviteAckReceived);
    clientA.requestRelayInvite(QStringLiteral("relay-t1"), QStringLiteral("dev-b"),
                               QStringLiteral("demo.zip"), 2048);
    QVERIFY2(ackSpy.wait(3000), "目标在线时中继邀请应收到 ack");
    QCOMPARE(ackSpy.at(0).at(0).toString(), QStringLiteral("relay-t1"));
}

// 主动断开后连接状态复位
void TestRendezvousClient::testDisconnectStopsSession()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());

    RendezvousClient client;
    connectAndRegister(client, &server, QStringLiteral("dev-a"));
    QVERIFY(client.isConnected());

    QSignalSpy disconnectedSpy(&client, &RendezvousClient::disconnected);
    client.disconnectFromServer();
    QVERIFY2(!disconnectedSpy.isEmpty(), "主动断开应同步发出 disconnected");
    QVERIFY(!client.isConnected());
}

// 连接被拒绝时通过 errorOccurred 上报
void TestRendezvousClient::testConnectRefusedReportsError()
{
    // 借用一个只监听不 accept 的端口已断开的场景：直接连未监听端口
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
        probe.close();  // 关闭后该端口在短时间内表现为拒绝连接
    }

    RendezvousClient client;
    QSignalSpy errorSpy(&client, &RendezvousClient::errorOccurred);
    client.connectToServer(QStringLiteral("127.0.0.1"), closedPort);
    QVERIFY2(errorSpy.wait(3000), "连接被拒绝应上报错误");
    QVERIFY(!errorSpy.at(0).at(0).toString().isEmpty());

    client.disconnectFromServer();  // 停掉自动重连，避免影响后续用例
}

// PeerEndpoint 转 QVariantMap 的字段映射完整
void TestRendezvousClient::testPeerEndpointToVariantMap()
{
    RendezvousClient::PeerEndpoint peer;
    peer.deviceId = QStringLiteral("dev-x");
    peer.deviceName = QStringLiteral("设备X");
    peer.addresses = QStringList{QStringLiteral("10.0.0.5"), QStringLiteral("192.168.1.5")};
    peer.tcpPort = 35100;
    peer.discoveryPort = 45678;
    peer.lastSeen = QDateTime(QDate(2026, 10, 2), QTime(8, 0, 0), QTimeZone::UTC);

    const QVariantMap map = RendezvousClient::peerEndpointToVariantMap(peer);
    QCOMPARE(map.value("deviceId").toString(), QStringLiteral("dev-x"));
    QCOMPARE(map.value("deviceName").toString(), QStringLiteral("设备X"));
    QCOMPARE(map.value("addresses").toStringList().size(), 2);
    QCOMPARE(map.value("tcpPort").toUInt(), 35100u);
    QCOMPARE(map.value("discoveryPort").toUInt(), 45678u);
}

// 服务器配 token、客户端携带相同 token 时注册应正常往返
void TestRendezvousClient::testRegisterWithTokenSucceeds()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QStringLiteral("secret")};
    QVERIFY(server.start());

    RendezvousClient client;
    client.setToken(QStringLiteral("secret"));
    QSignalSpy ackSpy(&client, &RendezvousClient::registerAckReceived);
    connectAndRegister(client, &server, QStringLiteral("dev-token"));

    QVERIFY(ackSpy.count() >= 1);
    QVERIFY(client.isConnected());

    // 带令牌的会话还应能正常查询设备列表
    QSignalSpy peersSpy(&client, &RendezvousClient::peersReceived);
    client.listPeers(QStringLiteral("room-gy"));
    QVERIFY2(peersSpy.wait(3000), "带正确令牌的 list_peers 应正常应答");
}

// 客户端令牌与服务器不一致时所有控制消息被拒
void TestRendezvousClient::testRegisterWithWrongTokenRejected()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QStringLiteral("secret")};
    QVERIFY(server.start());

    RendezvousClient client;
    client.setToken(QStringLiteral("wrong"));
    QSignalSpy errorSpy(&client, &RendezvousClient::errorOccurred);
    QSignalSpy ackSpy(&client, &RendezvousClient::registerAckReceived);
    QSignalSpy connectedSpy(&client, &RendezvousClient::connected);
    client.connectToServer(QStringLiteral("127.0.0.1"), server.serverPort());
    QVERIFY2(connectedSpy.wait(3000), "客户端应能建立 TCP 连接");

    client.registerDevice(QStringLiteral("room-gy"), QStringLiteral("dev-wrong"),
                          QStringLiteral("设备-wrong"), {QStringLiteral("127.0.0.1")},
                          35100, 45678);
    QVERIFY2(errorSpy.wait(3000), "错令牌的注册应收到错误响应");
    QVERIFY2(ackSpy.count() == 0, "错令牌的注册不应收到 register_ack");
}

// 断线重连成功路径：服务器销毁导致断开后，客户端按 3 秒延迟自动重连恢复
void TestRendezvousClient::testReconnectAfterServerRestart()
{
    quint16 port = 0;
    RendezvousClient client;
    {
        RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
        QVERIFY(server.start());
        port = server.serverPort();

        client.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(port));
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
        // 作用域结束销毁服务器，会话随之断开，触发客户端的自动重连计划
    }

    QTRY_VERIFY_WITH_TIMEOUT(!client.isConnected(), 5000);

    // 新服务器占用同一端口后，重连延迟（约 3 秒）内应自动恢复连接
    RendezvousServer restarted{QStringLiteral("127.0.0.1"), port, QString()};
    QVERIFY(restarted.start());
    QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 8000);
}

QTEST_MAIN(TestRendezvousClient)
#include "test_rendezvous_client.moc"
