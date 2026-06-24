/**
* @file    test_chat_manager.cpp
* @version 4.16.5
* @date    2026-06-24
* @author  GridYard Team
* @brief   在线聊天连接与内存会话测试
*
* 覆盖在线发送、离线拒绝、入站去重和连接断开后的按需重连。测试直接
* 使用本地 TCP 服务验证 ChatManager，不依赖 QML 页面或持久化存储。
*
* Change Log:
* [v4.16.5] GY   2026-06-24
* * 新增 Stage 5 聊天连接与内存会话测试
*/

#include <QtTest/QtTest>

#include <QDateTime>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "chat_manager.h"
#include "chat_message.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "frame_codec.h"
#include "p2p_server.h"
#include "protocol.h"

class TestChatManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testOfflinePeerRejected();
    void testOnlineSendAndReconnect();
    void testIncomingMessageDeduplicated();
    void testInvalidFollowUpFrameRejected();

private:
    void addOnlinePeer(const QString &deviceId, quint16 port);
    gy::ChatMessage createPeerMessage(const QString &messageId, const QString &content) const;
    QByteArray encodeMessageFrame(const gy::ChatMessage &message) const;
    quint16 reservePort();

    QTemporaryDir *_tempDir = nullptr;
    ConfigManager *_config = nullptr;
    DiscoveryService *_discovery = nullptr;
    P2pServer *_p2pServer = nullptr;
    ChatManager *_manager = nullptr;
    quint16 _p2pPort = 0;
};

// 测试套件初始化
void TestChatManager::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "ChatTestDevice");
    _config = ConfigManager::create(nullptr, nullptr);
    _p2pPort = reservePort();
    QVERIFY(_p2pPort != 0);
    _config->setTcpPort(_p2pPort);

    _discovery = new DiscoveryService{_config, this};
    _p2pServer = new P2pServer{_config, this};
    QVERIFY(_p2pServer->start());

    _manager = new ChatManager{this};
    _manager->init(_config, _discovery, _p2pServer);
}

// 测试套件清理
void TestChatManager::cleanupTestCase()
{
    delete _manager;
    delete _p2pServer;
    delete _discovery;
    delete _config;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 验证离线设备不会创建待投递消息
void TestChatManager::testOfflinePeerRejected()
{
    QSignalSpy failedSpy(_manager, &ChatManager::sendFailed);
    _manager->sendText("offline-device", "不会发送");

    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(1).value<gy::ChatMessageError>(),
             gy::ChatMessageError::PeerOffline);
    QVERIFY(_manager->messagesForDevice("offline-device").isEmpty());
}

// 验证在线设备可发送并在断线后按需重连
void TestChatManager::testOnlineSendAndReconnect()
{
    QTcpServer peerServer;
    QVERIFY(peerServer.listen(QHostAddress::LocalHost, 0));
    const QString deviceId = "online-peer";
    addOnlinePeer(deviceId, peerServer.serverPort());

    _manager->sendText(deviceId, "第一条消息");
    QTRY_VERIFY_WITH_TIMEOUT(peerServer.hasPendingConnections(), 3000);
    QTcpSocket *firstSocket = peerServer.nextPendingConnection();
    QVERIFY(firstSocket != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(firstSocket->bytesAvailable() > 0, 3000);

    FrameCodec codec;
    QSignalSpy firstFrameSpy(&codec, &FrameCodec::frameReady);
    codec.feed(firstSocket->readAll());
    QCOMPARE(firstFrameSpy.count(), 1);
    QCOMPARE(firstFrameSpy.first().at(0).toUInt(), gy::protocol::kTypeChatText);

    QTRY_COMPARE_WITH_TIMEOUT(_manager->messagesForDevice(deviceId).size(), 1, 3000);
    QCOMPARE(_manager->messagesForDevice(deviceId).first().toMap().value("status").toInt(), 1);

    gy::ChatMessage reply;
    reply.messageId = "0eb7ad80-56bf-4ce7-8a1d-00d10e3b25a9";
    reply.fromDeviceId = deviceId;
    reply.fromName = "OnlinePeer";
    reply.content = "收到第一条消息";
    reply.sentAt = QDateTime::currentDateTimeUtc();
    const QByteArray replyFrame = encodeMessageFrame(reply);
    QVERIFY(firstSocket->write(replyFrame) == replyFrame.size());
    QVERIFY(firstSocket->waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(_manager->messagesForDevice(deviceId).size(), 2, 3000);
    QVERIFY(!_manager->messagesForDevice(deviceId).last().toMap().value("isOutgoing").toBool());

    firstSocket->disconnectFromHost();
    firstSocket->deleteLater();
    QTest::qWait(100);

    _manager->sendText(deviceId, "第二条消息");
    QTRY_VERIFY_WITH_TIMEOUT(peerServer.hasPendingConnections(), 3000);
    QTcpSocket *secondSocket = peerServer.nextPendingConnection();
    QVERIFY(secondSocket != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(secondSocket->bytesAvailable() > 0, 3000);

    FrameCodec secondCodec;
    QSignalSpy secondFrameSpy(&secondCodec, &FrameCodec::frameReady);
    secondCodec.feed(secondSocket->readAll());
    QCOMPARE(secondFrameSpy.count(), 1);
    QCOMPARE(secondFrameSpy.first().at(1).toByteArray().contains("第二条消息"), true);
    QTRY_COMPARE_WITH_TIMEOUT(_manager->messagesForDevice(deviceId).size(), 3, 3000);
    QCOMPARE(_manager->messagesForDevice(deviceId).last().toMap().value("status").toInt(), 1);

    secondSocket->disconnectFromHost();
    secondSocket->deleteLater();
}

// 验证同一 messageId 的入站帧只归档一次
void TestChatManager::testIncomingMessageDeduplicated()
{
    const gy::ChatMessage message = createPeerMessage(
        "c8f3b2a1-4d5e-6f7a-8b9c-0d1e2f3a4b5c", "重复消息测试");
    _manager->clearMessages(message.fromDeviceId);
    const QByteArray frame = encodeMessageFrame(message);

    QTcpSocket peer;
    peer.connectToHost(QHostAddress::LocalHost, _p2pPort);
    QVERIFY(peer.waitForConnected(3000));
    QVERIFY(peer.write(frame) == frame.size());
    QVERIFY(peer.waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(_manager->messagesForDevice(message.fromDeviceId).size(), 1, 3000);

    QVERIFY(peer.write(frame) == frame.size());
    QVERIFY(peer.waitForBytesWritten(1000));
    QTest::qWait(100);
    QCOMPARE(_manager->messagesForDevice(message.fromDeviceId).size(), 1);

    peer.disconnectFromHost();
    peer.waitForDisconnected(1000);
}

// 验证合法首帧后的非法帧不会进入内存会话
void TestChatManager::testInvalidFollowUpFrameRejected()
{
    const gy::ChatMessage message = createPeerMessage(
        "cd0f6d2c-9f2a-4146-8fc9-ef211aa5f841", "非法后续帧测试");
    _manager->clearMessages(message.fromDeviceId);
    const QByteArray validFrame = encodeMessageFrame(message);
    const QByteArray invalidFrame = FrameCodec::encode(gy::protocol::kTypeChatText, "{}");

    QTcpSocket peer;
    QSignalSpy disconnectedSpy(&peer, &QTcpSocket::disconnected);
    peer.connectToHost(QHostAddress::LocalHost, _p2pPort);
    QVERIFY(peer.waitForConnected(3000));
    QVERIFY(peer.write(validFrame) == validFrame.size());
    QVERIFY(peer.waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(_manager->messagesForDevice(message.fromDeviceId).size(), 1, 3000);

    QVERIFY(peer.write(invalidFrame) == invalidFrame.size());
    QVERIFY(peer.waitForBytesWritten(1000));
    QVERIFY(disconnectedSpy.wait(3000));
    QCOMPARE(_manager->messagesForDevice(message.fromDeviceId).size(), 1);
}

// 将测试用对端放入发现服务的在线端点表
void TestChatManager::addOnlinePeer(const QString &deviceId, quint16 port)
{
    PeerInfo peer;
    peer.deviceId = deviceId;
    peer.deviceName = "OnlinePeer";
    peer.ipAddress = QHostAddress{QHostAddress::LocalHost}.toString();
    peer.tcpPort = port;
    peer.isOnline = true;
    peer.lastSeen = QDateTime::currentDateTimeUtc();
    _discovery->updatePeer(deviceId, peer);
}

// 构造对端发来的合法聊天消息
gy::ChatMessage TestChatManager::createPeerMessage(const QString &messageId,
                                                    const QString &content) const
{
    gy::ChatMessage message;
    message.messageId = messageId;
    message.fromDeviceId = "incoming-peer";
    message.fromName = "IncomingPeer";
    message.content = content;
    message.sentAt = QDateTime::currentDateTimeUtc();
    return message;
}

// 编码测试用聊天帧
QByteArray TestChatManager::encodeMessageFrame(const gy::ChatMessage &message) const
{
    QByteArray payload;
    if (!gy::ChatMessageCodec::encode(message, &payload)) {
        return {};
    }
    return FrameCodec::encode(gy::protocol::kTypeChatText, payload);
}

// 请求系统分配一个可用 TCP 端口
quint16 TestChatManager::reservePort()
{
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    return server.serverPort();
}

QTEST_MAIN(TestChatManager)
#include "test_chat_manager.moc"
