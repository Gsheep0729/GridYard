/**
* @file    test_relay_chain.cpp
* @version 7.15.8
* @date    2026-10-03
* @author  GY
* @brief   Relay 降级链路测试
*
* 测试用例：中继邀请登记与领取、协调端口复用的中继管道、
* 端到端经中继的文件传输（真实 RendezvousClient + RelayServer + P2pServer + FileSenderWorker）、
* 服务端加固（房间隔离、超长行断开、握手/空闲超时、会话数上限、注册表上限、
* TTL 夹紧、relay_id 复用竞态、响应写积压断开、会话等待超时）。
*
* Change Log:
* [v7.15.8] GY   2026-10-03
* * 适配 RendezvousServer 构造函数删除 host 死参数
* [v7.15.7] GY   2026-10-03
* * JSON 行读写与轮询等待改用 tests/test_utils 公共工具
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.14.0] GY   2026-10-03
* * 新增协调管道与独立模式的令牌用例
* [v7.14.2] GY   2026-10-03
* * 新增独立监听模式无令牌基本转发用例
* [v7.14.1] GY   2026-10-03
* * 新增阻塞期取消可达用例：假中继不回 ready，取消请求即时生效
* [v7.13.4] GY   2026-10-03
* * 新增 Phase1-D 加固用例：注册表上限、TTL 夹紧、id 复用竞态、写积压断开、等待超时
* [v7.12.0] GY   2026-10-02
* * 新增服务端加固用例：房间隔离、超长行、超时、会话上限
*/

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "config_manager.h"
#include "file_receiver_worker.h"
#include "file_sender_worker.h"
#include "protocol.h"
#include "online_registry.h"
#include "p2p_server.h"
#include "relay_server.h"
#include "rendezvous_client.h"
#include "rendezvous_server.h"

#include "test_utils/json_line.h"
#include "test_utils/wait.h"

// JSON 行读写与轮询等待复用测试公共工具
using gy::test::readJsonLine;
using gy::test::waitBytes;
using gy::test::waitConnected;
using gy::test::writeJsonLine;

class TestRelayChain : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInvitePollFlow();
    void testRelayPipeOnRendezvousPort();
    void testEndToEndFileTransferThroughRelay();
    void testRoomIsolation();
    void testOversizeLineDrops();
    void testSessionTimeouts();
    void testSessionCap();
    void testRegistryLimits();
    void testRegisterRejectsWhenRoomFull();
    void testRegisterAckClampsTtl();
    void testRelaySessionWaitTimeout();
    void testRelayIdReuseStaleCloseIgnored();
    void testResponseBackpressureDisconnects();
    void testRelayPipeWithToken();
    void testStandaloneRelayWithToken();
    void testStandaloneRelayForwarding();
    void testCancelDuringRelayWait();

private:
    QTemporaryDir *_tempDir = nullptr;
    ConfigManager *_config = nullptr;
};

void TestRelayChain::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "RelayTestDevice");

    _config = ConfigManager::create(nullptr, nullptr);
}

void TestRelayChain::cleanupTestCase()
{
    delete _config;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 邀请登记后由目标设备一次性领取，重复轮询为空，目标离线直接报错
void TestRelayChain::testInvitePollFlow()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    const quint16 port = server.serverPort();

    // 发送端注册并发出邀请
    QTcpSocket sender;
    sender.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(waitConnected(&sender));
    QJsonObject registerRequest;
    registerRequest[QStringLiteral("type")] = QStringLiteral("register");
    registerRequest[QStringLiteral("room")] = QStringLiteral("default");
    registerRequest[QStringLiteral("device_id")] = QStringLiteral("device-a");
    registerRequest[QStringLiteral("device_name")] = QStringLiteral("A");
    writeJsonLine(&sender, registerRequest);
    QCOMPARE(readJsonLine(&sender)[QStringLiteral("type")].toString(), QStringLiteral("register_ack"));

    // 接收端先注册，邀请才允许登记（服务端校验目标在线）
    QTcpSocket receiver;
    receiver.connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(waitConnected(&receiver));
    QJsonObject receiverRegister = registerRequest;
    receiverRegister[QStringLiteral("device_id")] = QStringLiteral("device-b");
    receiverRegister[QStringLiteral("device_name")] = QStringLiteral("B");
    writeJsonLine(&receiver, receiverRegister);
    QCOMPARE(readJsonLine(&receiver)[QStringLiteral("type")].toString(), QStringLiteral("register_ack"));

    QJsonObject inviteRequest;
    inviteRequest[QStringLiteral("type")] = QStringLiteral("relay_invite");
    inviteRequest[QStringLiteral("room")] = QStringLiteral("default");
    inviteRequest[QStringLiteral("relay_id")] = QStringLiteral("relay-1");
    inviteRequest[QStringLiteral("target_device_id")] = QStringLiteral("device-b");
    writeJsonLine(&sender, inviteRequest);
    QCOMPARE(readJsonLine(&sender)[QStringLiteral("type")].toString(), QStringLiteral("relay_invite_ack"));

    // 目标未注册时邀请直接被拒绝
    QJsonObject inviteUnknown;
    inviteUnknown[QStringLiteral("type")] = QStringLiteral("relay_invite");
    inviteUnknown[QStringLiteral("room")] = QStringLiteral("default");
    inviteUnknown[QStringLiteral("relay_id")] = QStringLiteral("relay-2");
    inviteUnknown[QStringLiteral("target_device_id")] = QStringLiteral("device-ghost");
    writeJsonLine(&sender, inviteUnknown);
    QCOMPARE(readJsonLine(&sender)[QStringLiteral("type")].toString(), QStringLiteral("error"));

    QJsonObject pollRequest;
    pollRequest[QStringLiteral("type")] = QStringLiteral("relay_poll");
    pollRequest[QStringLiteral("room")] = QStringLiteral("default");
    pollRequest[QStringLiteral("device_id")] = QStringLiteral("device-b");
    writeJsonLine(&receiver, pollRequest);

    QJsonObject pollResponse = readJsonLine(&receiver);
    QCOMPARE(pollResponse[QStringLiteral("type")].toString(), QStringLiteral("relay_invites"));
    QCOMPARE(pollResponse[QStringLiteral("items")].toArray().size(), 1);
    QCOMPARE(pollResponse[QStringLiteral("items")].toArray().first().toObject()
                 [QStringLiteral("relay_id")].toString(), QStringLiteral("relay-1"));

    // 邀请一次性消费，再次轮询应为空
    writeJsonLine(&receiver, pollRequest);
    pollResponse = readJsonLine(&receiver);
    QCOMPARE(pollResponse[QStringLiteral("items")].toArray().size(), 0);
}

// relay_create / relay_join 走协调端口：发送端收到 relay_ready 后字节双向转发
void TestRelayChain::testRelayPipeOnRendezvousPort()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    const quint16 port = server.serverPort();

    // 协调节点模式同端口内置中继（与 rendezvous/main.cpp 的装配一致）
    RelayServer relay{port, QString(), &server};
    QObject::connect(&server, &RendezvousServer::relayPipeRequested,
                     &relay, &RelayServer::adoptConnection);

    // 发送端握手建会话；中继会话会接管 socket 的归属，必须堆分配
    auto *sender = new QTcpSocket;
    sender->connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(waitConnected(sender));
    QJsonObject createHello;
    createHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    createHello[QStringLiteral("relay_id")] = QStringLiteral("relay-pipe");
    writeJsonLine(sender, createHello);

    // 接收端加入后会话齐备
    auto *receiver = new QTcpSocket;
    receiver->connectToHost(QHostAddress::LocalHost, port);
    QVERIFY(waitConnected(receiver));
    QJsonObject joinHello;
    joinHello[QStringLiteral("type")] = QStringLiteral("relay_join");
    joinHello[QStringLiteral("relay_id")] = QStringLiteral("relay-pipe");
    writeJsonLine(receiver, joinHello);

    // 只有发送端收到 relay_ready，接收端字节流保持纯净（TLV 直通）
    const QJsonObject ready = readJsonLine(sender);
    QCOMPARE(ready[QStringLiteral("type")].toString(), QStringLiteral("relay_ready"));

    // 双向字节转发
    sender->write("PING");
    sender->flush();
    QCOMPARE(waitBytes(receiver, 4), QByteArray("PING"));

    receiver->write("PONG");
    receiver->flush();
    QCOMPARE(waitBytes(sender, 4), QByteArray("PONG"));

    // 会话与 socket 由中继服务器接管回收，断开触发清理
    sender->disconnectFromHost();
    receiver->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(relay.sessionCount() == 0, 3000);
}

// 端到端：邀请信令 + 中继管道 + 真 TLV 传输，文件经中继完整落地
void TestRelayChain::testEndToEndFileTransferThroughRelay()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    const quint16 port = server.serverPort();

    // 协调节点模式同端口内置中继（与 rendezvous/main.cpp 的装配一致）
    RelayServer relay{port, QString(), &server};
    QObject::connect(&server, &RendezvousServer::relayPipeRequested,
                     &relay, &RelayServer::adoptConnection);

    // 接收端：P2pServer 监听随机端口，中继邀请到达后加入会话
    _config->setTcpPort(0);
    P2pServer p2p{_config, this};
    QVERIFY(p2p.start());

    RendezvousClient receiverClient;
    receiverClient.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(port));
    QTRY_VERIFY_WITH_TIMEOUT(receiverClient.isConnected(), 3000);
    receiverClient.registerDevice(QStringLiteral("default"), QStringLiteral("device-b"),
                                  QStringLiteral("B"), {}, 0, 0);

    QObject::connect(&receiverClient, &RendezvousClient::relayInvitesReceived,
                     this, [this, &p2p, port](const QList<QVariantMap> &invites) {
        for (const QVariantMap &invite : invites) {
            p2p.joinRelaySession(QStringLiteral("127.0.0.1"), port,
                                 invite[QStringLiteral("relayId")].toString());
        }
    });

    // 发送端注册并请求邀请
    RendezvousClient senderClient;
    senderClient.connectToServer(QStringLiteral("127.0.0.1"), static_cast<int>(port));
    QTRY_VERIFY_WITH_TIMEOUT(senderClient.isConnected(), 3000);
    senderClient.registerDevice(QStringLiteral("default"), QStringLiteral("device-a"),
                                QStringLiteral("A"), {}, 0, 0);

    const QString sourcePath = _tempDir->path() + "/relay-source.bin";
    const QByteArray payload = "GRIDYARD-RELAY-CHAIN-TEST";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), qint64(payload.size()));
    source.close();

    const QString relayId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSignalSpy ackSpy(&senderClient, &RendezvousClient::relayInviteAckReceived);
    senderClient.requestRelayInvite(relayId, QStringLiteral("device-b"),
                                    QStringLiteral("relay-source.bin"), payload.size());
    QTRY_COMPARE_WITH_TIMEOUT(ackSpy.count(), 1, 3000);

    // 接收端重注册触发立即轮询（正常场景由 5 秒心跳覆盖）
    receiverClient.registerDevice(QStringLiteral("default"), QStringLiteral("device-b"),
                                  QStringLiteral("B"), {}, 0, 0);

    QSignalSpy requestSpy(&p2p, &P2pServer::transferRequestReceived);

    // 发送 worker 在后台线程经中继通道传输
    FileSenderWorker senderWorker;
    QThread workerThread;
    senderWorker.moveToThread(&workerThread);
    const QString path = sourcePath;
    QObject::connect(&workerThread, &QThread::started, &senderWorker, [&senderWorker, port, path, relayId]() {
        senderWorker.startTransfer(QList<QPair<QString, quint16>>{
                                       {QStringLiteral("127.0.0.1"), port}},
                                   path, QStringLiteral("device-a"), QStringLiteral("A"), relayId);
    });
    QSignalSpy sendFinishedSpy(&senderWorker, &FileSenderWorker::transferFinished);
    workerThread.start();

    // 传输请求经中继到达接收端
    QTRY_COMPARE_WITH_TIMEOUT(requestSpy.count(), 1, 10000);
    auto *receiverWorker = requestSpy.first().first().value<FileReceiverWorker*>();
    QVERIFY(receiverWorker);

    // 接受并等待落盘
    const QString receiveDir = _tempDir->path() + "/relay-recv";
    QDir{}.mkpath(receiveDir);
    QMetaObject::invokeMethod(receiverWorker, [receiverWorker, receiveDir]() {
        receiverWorker->setReceivePath(receiveDir);
        receiverWorker->acceptTransfer();
    }, Qt::QueuedConnection);

    QSignalSpy recvFinishedSpy(receiverWorker, &FileReceiverWorker::transferFinished);

    // 发送端成功意味着最后一块校验已通过，此时再校验接收端结果
    QTRY_COMPARE_WITH_TIMEOUT(sendFinishedSpy.count(), 1, 10000);
    QVERIFY(sendFinishedSpy.first().at(0).toBool());  // 发送成功

    QTRY_COMPARE_WITH_TIMEOUT(recvFinishedSpy.count(), 1, 10000);
    QVERIFY(recvFinishedSpy.first().at(0).toBool());  // 接收成功

    const QString savedPath = recvFinishedSpy.first().at(3).toString();
    QFile saved{savedPath};
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), payload);
    saved.close();

    workerThread.quit();
    QVERIFY(workerThread.wait(3000));
}


// 房间隔离：room 含 ":" 时不与其他房间互通（二级哈希存储，无前缀扫描绕过）
void TestRelayChain::testRoomIsolation()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    const quint16 port = server.serverPort();

    // 在房间 a 与房间 a:b 各注册一台设备（非 void lambda，不能使用 QTest 断言宏）
    auto registerInRoom = [&port](const QString &room, const QString &deviceId) -> QTcpSocket *{
        auto *socket = new QTcpSocket;
        socket->connectToHost(QHostAddress::LocalHost, port);
        QElapsedTimer elapsed;
        elapsed.start();
        while (socket->state() != QAbstractSocket::ConnectedState && !elapsed.hasExpired(3000)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }
        if (socket->state() != QAbstractSocket::ConnectedState) {
            qWarning() << "房间测试连接超时";
            return socket;  // 后续 writeJsonLine 是无害的空操作，用例会在断言处失败
        }
        QJsonObject request;
        request[QStringLiteral("type")] = QStringLiteral("register");
        request[QStringLiteral("room")] = room;
        request[QStringLiteral("device_id")] = deviceId;
        request[QStringLiteral("device_name")] = deviceId;
        writeJsonLine(socket, request);
        const QString responseType = readJsonLine(socket)[QStringLiteral("type")].toString();
        if (responseType != QStringLiteral("register_ack")) {
            qWarning() << "房间测试注册失败:" << responseType;
        }
        return socket;
    };
    auto *roomA = registerInRoom(QStringLiteral("a"), QStringLiteral("device-a"));
    auto *roomAB = registerInRoom(QStringLiteral("a:b"), QStringLiteral("device-ab"));

    // 在房间 a 查询：只能看到房间 a 的设备，绝不能看到 a:b 的
    auto *querier = registerInRoom(QStringLiteral("a"), QStringLiteral("device-querier"));
    QJsonObject listRequest;
    listRequest[QStringLiteral("type")] = QStringLiteral("list_peers");
    listRequest[QStringLiteral("room")] = QStringLiteral("a");
    listRequest[QStringLiteral("device_id")] = QStringLiteral("device-querier");
    writeJsonLine(querier, listRequest);

    const QJsonObject response = readJsonLine(querier);
    QCOMPARE(response[QStringLiteral("type")].toString(), QStringLiteral("peers"));
    const QJsonArray items = response[QStringLiteral("items")].toArray();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toObject()[QStringLiteral("device_id")].toString(),
             QStringLiteral("device-a"));

    roomA->close();
    roomAB->close();
    querier->close();
    roomA->deleteLater();
    roomAB->deleteLater();
    querier->deleteLater();
}

// 超长控制行：服务端主动断开，不做无界缓冲
void TestRelayChain::testOversizeLineDrops()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());

    auto *socket = new QTcpSocket;
    socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::ConnectedState, 3000);

    // 发送超过 64KB 上限且不含换行的数据
    socket->write(QByteArray(128 * 1024, 'x'));
    socket->flush();

    QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::UnconnectedState, 5000);
    socket->deleteLater();
}

// 握手与空闲超时：不发首行或注册后长期沉默的连接被回收
void TestRelayChain::testSessionTimeouts()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    server.setSessionTimeouts(400, 500);  // 缩短超时便于测试

    // 握手超时：连接后不发送任何数据
    auto *silent = new QTcpSocket;
    silent->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(silent->state() == QAbstractSocket::ConnectedState, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(silent->state() == QAbstractSocket::UnconnectedState, 3000);
    silent->deleteLater();

    // 空闲超时：注册成功后不再发送任何行
    auto *idle = new QTcpSocket;
    idle->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(idle->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject request;
    request[QStringLiteral("type")] = QStringLiteral("register");
    request[QStringLiteral("room")] = QStringLiteral("default");
    request[QStringLiteral("device_id")] = QStringLiteral("idle-device");
    writeJsonLine(idle, request);
    QCOMPARE(readJsonLine(idle)[QStringLiteral("type")].toString(), QStringLiteral("register_ack"));

    QTRY_VERIFY_WITH_TIMEOUT(idle->state() == QAbstractSocket::UnconnectedState, 5000);
    idle->deleteLater();
}

// 会话数上限：超出上限的新连接被直接拒绝
void TestRelayChain::testSessionCap()
{
    RendezvousServer server{0, QString(), 2};
    QVERIFY(server.start());

    QList<QTcpSocket *> sockets;
    const auto connectClient = [&server, &sockets](const QString &deviceId) {
        auto *socket = new QTcpSocket;
        socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::ConnectedState, 3000);
        QJsonObject request;
        request[QStringLiteral("type")] = QStringLiteral("register");
        request[QStringLiteral("room")] = QStringLiteral("default");
        request[QStringLiteral("device_id")] = deviceId;
        writeJsonLine(socket, request);
        QCOMPARE(readJsonLine(socket)[QStringLiteral("type")].toString(), QStringLiteral("register_ack"));
        sockets.append(socket);
    };

    // 前两个会话正常建立
    connectClient(QStringLiteral("cap-a"));
    connectClient(QStringLiteral("cap-b"));

    // 第三个连接被服务端拒绝（连接被关闭，无法收到注册确认）
    auto *rejected = new QTcpSocket;
    rejected->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(rejected->state() == QAbstractSocket::ConnectedState, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(rejected->state() == QAbstractSocket::UnconnectedState, 3000);
    rejected->deleteLater();

    qDeleteAll(sockets);
}

// 注册表上限：房间数与每房设备数达到上限后拒绝新条目，已有设备刷新不受限
void TestRelayChain::testRegistryLimits()
{
    OnlineRegistry registry;

    registry.setRegistryLimits(1, 1);
    OnlineRegistry::PeerInfo first;
    first.deviceId = QStringLiteral("dev-1");
    QVERIFY(registry.upsertPeer(QStringLiteral("room-a"), first));

    OnlineRegistry::PeerInfo second;
    second.deviceId = QStringLiteral("dev-2");
    QVERIFY(!registry.upsertPeer(QStringLiteral("room-a"), second));

    // 已注册设备重复注册（心跳刷新）不拒绝
    QVERIFY(registry.upsertPeer(QStringLiteral("room-a"), first));

    // 房间数已满，新房间整体拒绝
    QVERIFY(!registry.upsertPeer(QStringLiteral("room-b"), second));
    QCOMPARE(registry.roomCount(QStringLiteral("room-a")), 1);
}

// 服务端注册超限：第二台设备收到 error 响应而不是注册确认
void TestRelayChain::testRegisterRejectsWhenRoomFull()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    server.setRegistryLimits(1, 1);

    // 注册并断言响应类型；void lambda 内可安全使用 QTest 断言宏
    const auto registerAndExpect = [&server](const QString &deviceId, const QString &expectedType) {
        auto *socket = new QTcpSocket;
        socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::ConnectedState, 3000);
        QJsonObject request;
        request[QStringLiteral("type")] = QStringLiteral("register");
        request[QStringLiteral("room")] = QStringLiteral("default");
        request[QStringLiteral("device_id")] = deviceId;
        writeJsonLine(socket, request);
        QCOMPARE(readJsonLine(socket)[QStringLiteral("type")].toString(), expectedType);
        socket->deleteLater();
    };

    registerAndExpect(QStringLiteral("limit-a"), QStringLiteral("register_ack"));
    registerAndExpect(QStringLiteral("limit-b"), QStringLiteral("error"));
    // 已注册设备重新注册是心跳刷新，不受上限影响
    registerAndExpect(QStringLiteral("limit-a"), QStringLiteral("register_ack"));
}

// 自报超大 TTL 经线路被夹紧，注册确认回写的是夹紧后的值
void TestRelayChain::testRegisterAckClampsTtl()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());

    auto *socket = new QTcpSocket;
    socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::ConnectedState, 3000);

    QJsonObject request;
    request[QStringLiteral("type")] = QStringLiteral("register");
    request[QStringLiteral("room")] = QStringLiteral("default");
    request[QStringLiteral("device_id")] = QStringLiteral("ttl-device");
    request[QStringLiteral("ttl_seconds")] = 999999;
    writeJsonLine(socket, request);

    const QJsonObject ack = readJsonLine(socket);
    QCOMPARE(ack[QStringLiteral("type")].toString(), QStringLiteral("register_ack"));
    QCOMPARE(ack[QStringLiteral("ttl_seconds")].toInt(), 300);
    socket->deleteLater();
}

// 会话等待超时：只有一端加入的会话在超时后被回收
void TestRelayChain::testRelaySessionWaitTimeout()
{
    RelayServer relay{0, QString()};
    QVERIFY(relay.start());
    relay.setSessionWaitMs(200);

    auto *sender = new QTcpSocket;
    sender->connectToHost(QHostAddress::LocalHost, relay.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(sender->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject createHello;
    createHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    createHello[QStringLiteral("relay_id")] = QStringLiteral("wait-timeout");
    writeJsonLine(sender, createHello);
    QTRY_COMPARE_WITH_TIMEOUT(relay.sessionCount(), 1, 3000);

    // 对端迟迟不来，超时后会话被回收并主动断开发送端
    QTRY_VERIFY_WITH_TIMEOUT(relay.sessionCount() == 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(sender->state() == QAbstractSocket::UnconnectedState, 3000);
    sender->deleteLater();
}

// relay_id 复用竞态：旧会话的迟到二次关闭不得误删同 id 新会话的注册表项
void TestRelayChain::testRelayIdReuseStaleCloseIgnored()
{
    RelayServer relay{0, QString()};
    QVERIFY(relay.start());

    // 本地馈送服务器造可控 socket 对：client 端交给中继，peer 端握在测试手里
    QTcpServer feeder;
    QVERIFY(feeder.listen(QHostAddress::LocalHost, 0));
    const auto makePair = [&feeder](QTcpSocket **client, QTcpSocket **peer) {
        auto *local = new QTcpSocket;
        local->connectToHost(QHostAddress::LocalHost, feeder.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(local->state() == QAbstractSocket::ConnectedState, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(feeder.hasPendingConnections(), 3000);
        *client = local;
        *peer = feeder.nextPendingConnection();
    };

    QTcpSocket *sockS = nullptr;
    QTcpSocket *peerS = nullptr;
    makePair(&sockS, &peerS);
    QJsonObject createHello;
    createHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    createHello[QStringLiteral("relay_id")] = QStringLiteral("reuse-id");
    relay.adoptConnection(sockS, createHello, {});
    QCOMPARE(relay.sessionCount(), 1);

    // 后续复用所需的 socket 对提前建好，避免在关闭处理器里泵事件打乱时序
    QTcpSocket *sockS2 = nullptr;
    QTcpSocket *peerS2 = nullptr;
    QTcpSocket *sockR2 = nullptr;
    QTcpSocket *peerR2 = nullptr;
    makePair(&sockS2, &peerS2);
    makePair(&sockR2, &peerR2);

    QJsonObject create2;
    create2[QStringLiteral("type")] = QStringLiteral("relay_create");
    create2[QStringLiteral("relay_id")] = QStringLiteral("reuse-id");

    // 复用窗口模拟：第一次关闭的处理器里旧会话尚未析构，此刻同 id 新会话入表，
    // 并让旧会话对象再发一次迟到的 sessionClosed
    QSignalSpy closedSpy(&relay, &RelayServer::sessionClosed);
    bool adopted = false;
    QObject::connect(&relay, &RelayServer::sessionClosed, &relay, [&](const QString &) {
        if (adopted) {
            return;
        }
        adopted = true;
        relay.adoptConnection(sockS2, create2, {});
        // relay 的子会话按创建顺序排列：first 是旧会话 S1，second 是刚入表的 S2
        const auto sessions = relay.findChildren<RelaySession *>();
        QCOMPARE(sessions.size(), 2);
        emit sessions.first()->sessionClosed();
    });

    // S1 的发送端断开触发第一次关闭；窗口内的迟到关闭应被指针判等挡下
    peerS->abort();
    QTRY_VERIFY_WITH_TIMEOUT(adopted, 3000);
    QTest::qWait(50);

    // 只有真正的第一次关闭对外发信号；迟到的关闭被挡下，S2 保持在表
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(relay.sessionCount(), 1);

    // S2 仍然可用：接收端加入后其发送端收到 relay_ready
    QJsonObject join2;
    join2[QStringLiteral("type")] = QStringLiteral("relay_join");
    join2[QStringLiteral("relay_id")] = QStringLiteral("reuse-id");
    relay.adoptConnection(sockR2, join2, {});
    QVERIFY(waitBytes(peerS2, 10).contains("relay_ready"));
    QCOMPARE(relay.sessionCount(), 1);
}

// 响应写积压超限：消费过慢的客户端被服务端主动断开
void TestRelayChain::testResponseBackpressureDisconnects()
{
    RendezvousServer server{0, QString()};
    QVERIFY(server.start());
    server.setMaxResponseQueueBytes(16 * 1024);

    // 客户端读缓冲封顶后停止消费，服务端写队列才会持续积压
    auto *socket = new QTcpSocket;
    socket->setReadBufferSize(1024);
    socket->connectToHost(QHostAddress::LocalHost, server.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::ConnectedState, 3000);

    QJsonObject bogus;
    bogus[QStringLiteral("type")] = QStringLiteral("bogus");
    QElapsedTimer elapsed;
    elapsed.start();
    while (socket->state() == QAbstractSocket::ConnectedState && elapsed.elapsed() < 20000) {
        writeJsonLine(socket, bogus);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QVERIFY2(elapsed.elapsed() < 20000, "服务端应在超时前因写积压断开慢客户端");
    QTRY_VERIFY_WITH_TIMEOUT(socket->state() == QAbstractSocket::UnconnectedState, 5000);
    socket->deleteLater();
}

// 协调节点配 token：带令牌的中继握手放行，错令牌回 relay_error
void TestRelayChain::testRelayPipeWithToken()
{
    RendezvousServer server{0, QStringLiteral("secret")};
    QVERIFY(server.start());
    const quint16 port = server.serverPort();

    RelayServer relay{port, QStringLiteral("secret"), &server};
    QObject::connect(&server, &RendezvousServer::relayPipeRequested,
                     &relay, &RelayServer::adoptConnection);

    // 错令牌在协调会话的分流之前就被控制面拒绝，收到通用 error 响应
    auto *bad = new QTcpSocket;
    bad->connectToHost(QHostAddress::LocalHost, port);
    QTRY_VERIFY_WITH_TIMEOUT(bad->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject badHello;
    badHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    badHello[QStringLiteral("relay_id")] = QStringLiteral("relay-token-bad");
    badHello[QStringLiteral("token")] = QStringLiteral("wrong");
    writeJsonLine(bad, badHello);
    const QJsonObject badReply = readJsonLine(bad);
    QCOMPARE(badReply[QStringLiteral("type")].toString(), QStringLiteral("error"));
    bad->deleteLater();

    // 带正确令牌的握手正常建会话并转发
    auto *sender = new QTcpSocket;
    sender->connectToHost(QHostAddress::LocalHost, port);
    QTRY_VERIFY_WITH_TIMEOUT(sender->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject createHello;
    createHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    createHello[QStringLiteral("relay_id")] = QStringLiteral("relay-token-ok");
    createHello[QStringLiteral("token")] = QStringLiteral("secret");
    writeJsonLine(sender, createHello);

    auto *receiver = new QTcpSocket;
    receiver->connectToHost(QHostAddress::LocalHost, port);
    QTRY_VERIFY_WITH_TIMEOUT(receiver->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject joinHello;
    joinHello[QStringLiteral("type")] = QStringLiteral("relay_join");
    joinHello[QStringLiteral("relay_id")] = QStringLiteral("relay-token-ok");
    joinHello[QStringLiteral("token")] = QStringLiteral("secret");
    writeJsonLine(receiver, joinHello);

    QCOMPARE(readJsonLine(sender)[QStringLiteral("type")].toString(), QStringLiteral("relay_ready"));
    sender->write("PING");
    sender->flush();
    QCOMPARE(waitBytes(receiver, 4), QByteArray("PING"));

    sender->disconnectFromHost();
    receiver->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(relay.sessionCount() == 0, 3000);
}

// 独立监听模式配 token：握手行走 LineSession 路径同样校验
void TestRelayChain::testStandaloneRelayWithToken()
{
    RelayServer relay{0, QStringLiteral("secret")};
    QVERIFY(relay.start());

    // 错令牌被拒
    auto *bad = new QTcpSocket;
    bad->connectToHost(QHostAddress::LocalHost, relay.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(bad->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject badHello;
    badHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    badHello[QStringLiteral("relay_id")] = QStringLiteral("standalone-bad");
    writeJsonLine(bad, badHello);
    const QJsonObject badReply = readJsonLine(bad);
    QCOMPARE(badReply[QStringLiteral("type")].toString(), QStringLiteral("relay_error"));
    QTRY_VERIFY_WITH_TIMEOUT(bad->state() == QAbstractSocket::UnconnectedState, 3000);
    bad->deleteLater();

    // 正确令牌建会话
    auto *sender = new QTcpSocket;
    sender->connectToHost(QHostAddress::LocalHost, relay.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(sender->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject createHello;
    createHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    createHello[QStringLiteral("relay_id")] = QStringLiteral("standalone-ok");
    createHello[QStringLiteral("token")] = QStringLiteral("secret");
    writeJsonLine(sender, createHello);
    QTRY_COMPARE_WITH_TIMEOUT(relay.sessionCount(), 1, 3000);

    sender->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(relay.sessionCount() == 0, 3000);
}

// 阻塞期取消可达：假中继只接受连接不回 relay_ready，
// 发送 worker 卡在中继等待期时跨线程置位取消标志应立即终结会话
void TestRelayChain::testCancelDuringRelayWait()
{
    // 只监听不受理：内核完成握手后连接进入积压队列，发送端永远等不到 relay_ready
    QTcpServer fakeRelay;
    QVERIFY(fakeRelay.listen(QHostAddress::LocalHost, 0));

    const QString sourcePath = _tempDir->path() + "/cancel-source.bin";
    QFile source{sourcePath};
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray(64, 'x')), qint64(64));
    source.close();

    FileSenderWorker senderWorker;
    QThread workerThread;
    senderWorker.moveToThread(&workerThread);
    const quint16 port = fakeRelay.serverPort();
    QObject::connect(&workerThread, &QThread::started, &senderWorker, [&senderWorker, port, sourcePath]() {
        senderWorker.startTransfer({{QStringLiteral("127.0.0.1"), port}},
                                   sourcePath, QStringLiteral("device-a"),
                                   QStringLiteral("A"), QStringLiteral("cancel-relay"));
    });
    QSignalSpy finishedSpy(&senderWorker, &FileSenderWorker::transferFinished);
    workerThread.start();

    // 等发送端进入中继等待期，再模拟 UI 线程的跨线程取消请求
    QTest::qWait(500);
    QElapsedTimer elapsed;
    elapsed.start();
    senderWorker.requestCancel();

    // 取消应在远小于 30 秒中继等待上限内生效，且恰好终结一次
    QVERIFY2(finishedSpy.wait(5000), "取消请求应在 5 秒内终结传输");
    QVERIFY2(elapsed.elapsed() < 10000, "取消应远快于 30 秒等待上限");
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.first().at(0).toBool(), false);
    QCOMPARE(finishedSpy.first().at(1).toInt(), static_cast<int>(gy::protocol::ErrorCode::UserCancelled));

    workerThread.quit();
    QVERIFY(workerThread.wait(3000));
}

// 独立监听模式基本转发：不经协调节点，直连中继端口完成握手与双向字节转发
void TestRelayChain::testStandaloneRelayForwarding()
{
    RelayServer relay{0, QString()};
    QVERIFY(relay.start());

    auto *sender = new QTcpSocket;
    sender->connectToHost(QHostAddress::LocalHost, relay.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(sender->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject createHello;
    createHello[QStringLiteral("type")] = QStringLiteral("relay_create");
    createHello[QStringLiteral("relay_id")] = QStringLiteral("standalone-forward");
    writeJsonLine(sender, createHello);

    auto *receiver = new QTcpSocket;
    receiver->connectToHost(QHostAddress::LocalHost, relay.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(receiver->state() == QAbstractSocket::ConnectedState, 3000);
    QJsonObject joinHello;
    joinHello[QStringLiteral("type")] = QStringLiteral("relay_join");
    joinHello[QStringLiteral("relay_id")] = QStringLiteral("standalone-forward");
    writeJsonLine(receiver, joinHello);

    QCOMPARE(readJsonLine(sender)[QStringLiteral("type")].toString(), QStringLiteral("relay_ready"));

    sender->write("PING");
    sender->flush();
    QCOMPARE(waitBytes(receiver, 4), QByteArray("PING"));
    receiver->write("PONG");
    receiver->flush();
    QCOMPARE(waitBytes(sender, 4), QByteArray("PONG"));

    sender->disconnectFromHost();
    receiver->disconnectFromHost();
    QTRY_VERIFY_WITH_TIMEOUT(relay.sessionCount() == 0, 3000);
}

QTEST_MAIN(TestRelayChain)
#include "test_relay_chain.moc"
