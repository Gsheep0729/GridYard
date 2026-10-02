/**
* @file    test_relay_chain.cpp
* @version 7.9.0
* @date    2026-07-26
* @author  GY
* @brief   Relay 降级链路测试
*
* 测试用例：中继邀请登记与领取、协调端口复用的中继管道、
* 端到端经中继的文件传输（真实 RendezvousClient + RelayServer + P2pServer + FileSenderWorker）。
*/

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>

#include "config_manager.h"
#include "file_receiver_worker.h"
#include "file_sender_worker.h"
#include "p2p_server.h"
#include "relay_server.h"
#include "rendezvous_client.h"
#include "rendezvous_server.h"

namespace {

// 等待 socket 建立连接（事件循环驱动，保证 QTcpServer 能并行处理新连接）
bool waitConnected(QTcpSocket *socket, int timeoutMs = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (socket->state() != QAbstractSocket::ConnectedState && !elapsed.hasExpired(timeoutMs)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return socket->state() == QAbstractSocket::ConnectedState;
}

// 等待读取一行 JSON（事件循环驱动，仅用于协议往返断言）
QJsonObject readJsonLine(QTcpSocket *socket, int timeoutMs = 3000)
{
    QByteArray buffer;
    QElapsedTimer elapsed;
    elapsed.start();

    while (!buffer.contains('\n')) {
        if (elapsed.hasExpired(timeoutMs)) {
            return {};
        }
        buffer += socket->readAll();
        if (buffer.contains('\n')) {
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    const int newlineIndex = buffer.indexOf('\n');
    const QJsonDocument doc = QJsonDocument::fromJson(buffer.left(newlineIndex));
    return doc.object();
}

// 发送一行 JSON
void writeJsonLine(QTcpSocket *socket, const QJsonObject &json)
{
    socket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

// 等待 socket 读到至少 expected 字节并返回已读数据
QByteArray waitBytes(QTcpSocket *socket, int expected, int timeoutMs = 3000)
{
    QByteArray data;
    QElapsedTimer elapsed;
    elapsed.start();
    while (data.size() < expected && !elapsed.hasExpired(timeoutMs)) {
        data += socket->readAll();
        if (data.size() >= expected) {
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return data;
}

}

class TestRelayChain : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testInvitePollFlow();
    void testRelayPipeOnRendezvousPort();
    void testEndToEndFileTransferThroughRelay();

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
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
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
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
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
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
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

QTEST_MAIN(TestRelayChain)
#include "test_relay_chain.moc"
