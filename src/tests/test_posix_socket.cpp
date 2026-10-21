/**
* @file    test_posix_socket.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   原生套接字原语层测试
*
* 覆盖连接收发、缓冲语义、短写补队、事件通知、跨线程移交、地址归一化
* 与错误映射，作为业务层迁移前的独立安全网。
*/

#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>

#include <memory>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ip_address.h"
#include "net_interfaces.h"
#include "posix_net_error.h"
#include "posix_tcp_server.h"
#include "posix_tcp_socket.h"
#include "posix_udp_socket.h"

using gy::net::IpAddress;
using gy::net::PosixTcpServer;
using gy::net::PosixTcpSocket;
using gy::net::PosixUdpSocket;
using gy::net::SocketError;

namespace {

// 事件循环驱动的谓词等待
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMs = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate()) {
        if (elapsed.hasExpired(timeoutMs)) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

// 接受一个待处理连接
std::unique_ptr<PosixTcpSocket> acceptOne(PosixTcpServer &server, int timeoutMs = 3000)
{
    if (!waitFor([&server]() { return server.hasPendingConnections(); }, timeoutMs)) {
        return nullptr;
    }
    return std::unique_ptr<PosixTcpSocket>(server.nextPendingConnection());
}

}  // namespace

class TestPosixSocket : public QObject {
    Q_OBJECT

private slots:
    void testTcpConnectAndEcho();
    void testTcpConnectEventDriven();
    void testTcpHalfCloseAndEof();
    void testTcpPeekDoesNotConsume();
    void testTcpBytesAvailableIncludesKernelBuffer();
    void testTcpWriteAcceptsFullLength();
    void testTcpWriteBackpressureAndBytesWritten();
    void testTcpServerPendingTwoStage();
    void testTcpServerPortZeroAllocation();
    void testTcpNoDelayEnabled();
    void testTcpConnectFailure();
    void testTcpConnectTimeout();
    void testTcpNoSigpipeOnPeerClosed();
    void testUdpBroadcastReceive();
    void testUdpUnicastReceive();
    void testUdpLargeDatagramIntegrity();
    void testNetInterfaceEnumeration();
    void testThreadHandoverKeepsBufferedBytes();
    void testThreadHandoverLatch();
    void testIpv4MappedNormalization();
    void testErrorMapping();
};

// 连接建立、双向收发与字节一致性
void TestPosixSocket::testTcpConnectAndEcho()
{
    PosixTcpServer server;
    QVERIFY2(server.listen(IpAddress::localHost(), 0), qPrintable(server.errorString()));
    QVERIFY(server.serverPort() != 0);

    PosixTcpSocket client;
    QSignalSpy connectedSpy(&client, &PosixTcpSocket::connected);
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    QCOMPARE(client.state(), gy::net::SocketState::ConnectedState);
    QCOMPARE(connectedSpy.count(), 1);

    auto accepted = acceptOne(server);
    QVERIFY(accepted);
    QCOMPARE(accepted->state(), gy::net::SocketState::ConnectedState);

    QCOMPARE(client.write(QByteArrayLiteral("hello")), qint64(5));
    QVERIFY(waitFor([&accepted]() { return accepted->bytesAvailable() >= 5; }));
    QCOMPARE(accepted->readAll(), QByteArrayLiteral("hello"));

    QCOMPARE(accepted->write(QByteArrayLiteral("world")), qint64(5));
    QVERIFY(waitFor([&client]() { return client.bytesAvailable() >= 5; }));
    QCOMPARE(client.readAll(), QByteArrayLiteral("world"));
}

// 事件循环驱动下异步连接也能完成
void TestPosixSocket::testTcpConnectEventDriven()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    QSignalSpy connectedSpy(&client, &PosixTcpSocket::connected);
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(waitFor([&client]() {
        return client.state() == gy::net::SocketState::ConnectedState;
    }));
    QCOMPARE(connectedSpy.count(), 1);
    QVERIFY(acceptOne(server));
}

// 对端关闭后本端读到文件结尾并转为未连接状态
void TestPosixSocket::testTcpHalfCloseAndEof()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    QSignalSpy disconnectedSpy(&client, &PosixTcpSocket::disconnected);
    accepted->disconnectFromHost();
    QVERIFY(waitFor([&client]() {
        return client.state() == gy::net::SocketState::UnconnectedState;
    }));
    QVERIFY(disconnectedSpy.count() >= 1);
}

// 窥探不消费缓冲内容
void TestPosixSocket::testTcpPeekDoesNotConsume()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    accepted->write(QByteArrayLiteral("abcdef"));
    QVERIFY(waitFor([&client]() { return client.bytesAvailable() >= 6; }));

    QCOMPARE(client.peek(3), QByteArrayLiteral("abc"));
    QCOMPARE(client.bytesAvailable(), qint64(6));
    QCOMPARE(client.readAll(), QByteArrayLiteral("abcdef"));
    QCOMPARE(client.bytesAvailable(), qint64(0));
}

// 可读字节数包含内核接收队列中的未读数据
void TestPosixSocket::testTcpBytesAvailableIncludesKernelBuffer()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    const QByteArray payload(1024 * 1024, 'k');
    QCOMPARE(accepted->write(payload), qint64(payload.size()));
    QVERIFY(waitFor([&client, &payload]() {
        return client.bytesAvailable() >= payload.size();
    }, 5000));
    QCOMPARE(client.readAll(), payload);
}

// 写接口返回完整长度而不是短写长度
void TestPosixSocket::testTcpWriteAcceptsFullLength()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    const QByteArray payload(4 * 1024 * 1024, 'w');
    QCOMPARE(client.write(payload), qint64(payload.size()));
    QVERIFY(client.bytesToWrite() > 0);
}

// 写缓冲回落时发出字节写入信号
void TestPosixSocket::testTcpWriteBackpressureAndBytesWritten()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    QSignalSpy writtenSpy(&client, &PosixTcpSocket::bytesWritten);
    const QByteArray payload(16 * 1024 * 1024, 'b');
    QCOMPARE(client.write(payload), qint64(payload.size()));
    QVERIFY(client.bytesToWrite() > 0);

    const bool drained = waitFor([&client, &accepted]() {
        if (accepted->bytesAvailable() > 0) {
            accepted->readAll();
        }
        return client.bytesToWrite() == 0;
    }, 10000);
    QVERIFY(drained);
    QVERIFY(writtenSpy.count() > 0);
}

// 监听器只排队连接，由调用方两段式取走
void TestPosixSocket::testTcpServerPendingTwoStage()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket first;
    PosixTcpSocket second;
    first.connectToHost(IpAddress::localHost(), server.serverPort());
    second.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(first.waitForConnected(2000));
    QVERIFY(second.waitForConnected(2000));

    QVERIFY(waitFor([&server]() { return server.hasPendingConnections(); }));
    QVERIFY(server.hasPendingConnections());

    std::unique_ptr<PosixTcpSocket> accepted(server.nextPendingConnection());
    QVERIFY(accepted);
    QCOMPARE(accepted->state(), gy::net::SocketState::ConnectedState);
    QVERIFY(server.hasPendingConnections());

    std::unique_ptr<PosixTcpSocket> next(server.nextPendingConnection());
    QVERIFY(next);
    QVERIFY(!server.hasPendingConnections());
}

// 零端口由内核分配实际端口
void TestPosixSocket::testTcpServerPortZeroAllocation()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));
    QVERIFY(server.serverPort() != 0);

    PosixTcpServer second;
    QVERIFY(second.listen(IpAddress::localHost(), 0));
    QVERIFY(second.serverPort() != 0);
    QVERIFY(second.serverPort() != server.serverPort());
}

// 连接两端默认开启 TCP_NODELAY
void TestPosixSocket::testTcpNoDelayEnabled()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    int enabled = 0;
    socklen_t length = sizeof(enabled);
    QCOMPARE(::getsockopt(client.socketDescriptor(), IPPROTO_TCP, TCP_NODELAY, &enabled, &length), 0);
    QCOMPARE(enabled, 1);

    enabled = 0;
    length = sizeof(enabled);
    QCOMPARE(::getsockopt(accepted->socketDescriptor(), IPPROTO_TCP, TCP_NODELAY, &enabled, &length), 0);
    QCOMPARE(enabled, 1);
}

// 连接已关闭端口时返回失败并给出错误类型
void TestPosixSocket::testTcpConnectFailure()
{
    quint16 closedPort = 0;
    {
        PosixTcpServer server;
        QVERIFY(server.listen(IpAddress::localHost(), 0));
        closedPort = server.serverPort();
    }

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), closedPort);
    QVERIFY(!client.waitForConnected(2000));
    QVERIFY(client.error() != SocketError::NoError);
    QVERIFY(!client.errorString().isEmpty());
}

// 连接队列拥塞时阻塞等待按超时返回
void TestPosixSocket::testTcpConnectTimeout()
{
    const int listener = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    QVERIFY(listener >= 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    QCOMPARE(::bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)), 0);
    QCOMPARE(::listen(listener, 0), 0);

    socklen_t length = sizeof(address);
    QCOMPARE(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length), 0);
    const quint16 port = ntohs(address.sin_port);

    QList<int> fillers;
    for (int index = 0; index < 8; ++index) {
        const int descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        QVERIFY(descriptor >= 0);
        ::connect(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
        fillers.append(descriptor);
    }
    QTest::qWait(50);

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), port);
    QVERIFY(!client.waitForConnected(300));
    QCOMPARE(client.error(), SocketError::SocketTimeoutError);
    client.abort();

    for (const int descriptor : fillers) {
        ::close(descriptor);
    }
    ::close(listener);
}

// 对端关闭后写入不触发进程终止
void TestPosixSocket::testTcpNoSigpipeOnPeerClosed()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    accepted->abort();
    const QByteArray payload(2 * 1024 * 1024, 'p');
    const qint64 written = client.write(payload);
    QVERIFY(written == payload.size() || written == -1);
    QTest::qWait(50);
}

// UDP 广播地址可送达本机监听端口
void TestPosixSocket::testUdpBroadcastReceive()
{
    PosixUdpSocket receiver;
    QVERIFY2(receiver.bind(IpAddress::any(), 0,
                           PosixUdpSocket::ShareAddress | PosixUdpSocket::ReuseAddressHint),
             qPrintable(receiver.errorString()));

    PosixUdpSocket sender;
    QVERIFY(sender.bind(IpAddress::any(), 0));

    const IpAddress broadcast = IpAddress::fromString(QStringLiteral("255.255.255.255"));
    QVERIFY(broadcast.isValid());
    QCOMPARE(sender.writeDatagram(QByteArrayLiteral("broadcast"),
                                  broadcast, receiver.localPort()),
             qint64(9));
    QVERIFY(waitFor([&receiver]() { return receiver.hasPendingDatagrams(); }));
    QCOMPARE(receiver.pendingDatagramSize(), qint64(9));

    char buffer[32] = {};
    IpAddress source;
    quint16 sourcePort = 0;
    QCOMPARE(receiver.readDatagram(buffer, sizeof(buffer), &source, &sourcePort), qint64(9));
    QCOMPARE(QByteArray(buffer, 9), QByteArrayLiteral("broadcast"));
    QVERIFY(source.isValid());
    QVERIFY(sourcePort != 0);
}

// UDP 单播保留报文边界与来源信息
void TestPosixSocket::testUdpUnicastReceive()
{
    PosixUdpSocket receiver;
    QVERIFY(receiver.bind(IpAddress::localHost(), 0));

    PosixUdpSocket sender;
    QVERIFY(sender.bind(IpAddress::localHost(), 0));

    QCOMPARE(sender.writeDatagram(QByteArrayLiteral("one"),
                                  IpAddress::localHost(), receiver.localPort()),
             qint64(3));
    QCOMPARE(sender.writeDatagram(QByteArrayLiteral("second"),
                                  IpAddress::localHost(), receiver.localPort()),
             qint64(6));

    QVERIFY(waitFor([&receiver]() { return receiver.hasPendingDatagrams(); }));
    QCOMPARE(receiver.pendingDatagramSize(), qint64(3));

    char first[32] = {};
    QCOMPARE(receiver.readDatagram(first, sizeof(first)), qint64(3));
    QCOMPARE(QByteArray(first, 3), QByteArrayLiteral("one"));
    QCOMPARE(receiver.pendingDatagramSize(), qint64(6));

    char second[32] = {};
    IpAddress source;
    quint16 sourcePort = 0;
    QCOMPARE(receiver.readDatagram(second, sizeof(second), &source, &sourcePort), qint64(6));
    QCOMPARE(QByteArray(second, 6), QByteArrayLiteral("second"));
    QCOMPARE(sourcePort, sender.localPort());
}

// 接近 64KB 的数据报完整送达，不被接收缓冲截断
void TestPosixSocket::testUdpLargeDatagramIntegrity()
{
    PosixUdpSocket receiver;
    QVERIFY(receiver.bind(IpAddress::localHost(), 0));

    PosixUdpSocket sender;
    QVERIFY(sender.bind(IpAddress::localHost(), 0));

    const QByteArray payload(60000, 'd');
    QCOMPARE(sender.writeDatagram(payload, IpAddress::localHost(), receiver.localPort()),
             qint64(payload.size()));
    QVERIFY(waitFor([&receiver]() { return receiver.hasPendingDatagrams(); }, 5000));
    QCOMPARE(receiver.pendingDatagramSize(), qint64(payload.size()));

    QByteArray buffer(payload.size(), '\0');
    const qint64 amount = receiver.readDatagram(buffer.data(), payload.size());
    QCOMPARE(amount, qint64(payload.size()));
    QCOMPARE(buffer.left(static_cast<int>(amount)), payload);
}

// 网卡枚举返回有效 IPv4 地址与广播地址
void TestPosixSocket::testNetInterfaceEnumeration()
{
    const QList<gy::net::NetInterface> interfaces = gy::net::listInterfaces();
    QVERIFY(!interfaces.isEmpty());

    bool hasLoopback = false;
    bool hasBroadcast = false;
    for (const gy::net::NetInterface &interface : interfaces) {
        QVERIFY(interface.address.isValid());
        hasLoopback = hasLoopback || interface.isLoopback;
        if (interface.canBroadcast && interface.broadcast.isValid()) {
            hasBroadcast = true;
        }
    }
    QVERIFY(hasLoopback);
    QVERIFY(hasBroadcast);
    QVERIFY(!gy::net::listIpv4Addresses().isEmpty());
}

// 跨线程移交后已窥探字节不丢失
void TestPosixSocket::testThreadHandoverKeepsBufferedBytes()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    auto client = std::make_unique<PosixTcpSocket>();
    client->connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client->waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    accepted->write(QByteArrayLiteral("ABCD"));
    QVERIFY(waitFor([&client]() { return client->bytesAvailable() >= 4; }));
    QCOMPARE(client->peek(4), QByteArrayLiteral("ABCD"));

    QThread workerThread;
    QSignalSpy readySpy(client.get(), &PosixTcpSocket::readyRead);
    client->prepareThreadMove();
    QVERIFY(client->isThreadMovePending());
    client->moveToThread(&workerThread);
    workerThread.start();

    QMetaObject::invokeMethod(client.get(), [socket = client.get()]() {
        socket->finishThreadMove();
    }, Qt::BlockingQueuedConnection);
    QVERIFY(!client->isThreadMovePending());

    accepted->write(QByteArrayLiteral("EFGH"));
    QVERIFY(waitFor([&readySpy]() { return !readySpy.isEmpty(); }));

    QByteArray received;
    QMetaObject::invokeMethod(client.get(), [socket = client.get(), &received]() {
        received = socket->readAll();
    }, Qt::BlockingQueuedConnection);
    QCOMPARE(received, QByteArrayLiteral("ABCDEFGH"));

    QThread *mainThread = QThread::currentThread();
    QMetaObject::invokeMethod(client.get(), [socket = client.get(), mainThread]() {
        socket->abort();
        socket->moveToThread(mainThread);
    }, Qt::BlockingQueuedConnection);
    workerThread.quit();
    QVERIFY(workerThread.wait(3000));
}

// 移交窗口内通知器保持关闭，结束后恢复
void TestPosixSocket::testThreadHandoverLatch()
{
    PosixTcpServer server;
    QVERIFY(server.listen(IpAddress::localHost(), 0));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));
    auto accepted = acceptOne(server);
    QVERIFY(accepted);

    QVERIFY(!client.isThreadMovePending());
    client.prepareThreadMove();
    QVERIFY(client.isThreadMovePending());
    client.finishThreadMove();
    QVERIFY(!client.isThreadMovePending());

    accepted->write(QByteArrayLiteral("ok"));
    QVERIFY(waitFor([&client]() { return client.bytesAvailable() >= 2; }));
    QCOMPARE(client.readAll(), QByteArrayLiteral("ok"));
}

// 双栈监听下 IPv4 客户端地址归一化为点分十进制
void TestPosixSocket::testIpv4MappedNormalization()
{
    PosixTcpServer server;
    QVERIFY2(server.listen(IpAddress::anyIpv6(), 0, PosixTcpServer::DualStack),
             qPrintable(server.errorString()));

    PosixTcpSocket client;
    client.connectToHost(IpAddress::localHost(), server.serverPort());
    QVERIFY(client.waitForConnected(2000));

    auto accepted = acceptOne(server);
    QVERIFY(accepted);
    QCOMPARE(accepted->peerAddress().toString(), QStringLiteral("127.0.0.1"));
    QVERIFY(accepted->localAddress().isValid());

    const IpAddress mapped = IpAddress::fromString(QStringLiteral("::ffff:192.168.1.9"));
    QCOMPARE(mapped.toString(), QStringLiteral("192.168.1.9"));
}

// errno 到错误类型与中文文案的逐项映射
void TestPosixSocket::testErrorMapping()
{
    QCOMPARE(gy::net::socketErrorFromErrno(ECONNREFUSED), SocketError::ConnectionRefusedError);
    QCOMPARE(gy::net::socketErrorFromErrno(ECONNRESET), SocketError::RemoteHostClosedError);
    QCOMPARE(gy::net::socketErrorFromErrno(ETIMEDOUT), SocketError::SocketTimeoutError);
    QCOMPARE(gy::net::socketErrorFromErrno(EHOSTUNREACH), SocketError::HostNotFoundError);
    QCOMPARE(gy::net::socketErrorFromErrno(ENETUNREACH), SocketError::NetworkError);
    QCOMPARE(gy::net::socketErrorFromErrno(EACCES), SocketError::SocketAccessError);
    QCOMPARE(gy::net::socketErrorFromErrno(EADDRINUSE), SocketError::AddressInUseError);
    QCOMPARE(gy::net::socketErrorFromErrno(EADDRNOTAVAIL), SocketError::SocketAddressNotAvailableError);
    QCOMPARE(gy::net::socketErrorFromErrno(EMFILE), SocketError::SocketResourceError);
    QCOMPARE(gy::net::socketErrorFromErrno(EAGAIN), SocketError::NoError);
    QCOMPARE(gy::net::socketErrorFromErrno(EINTR), SocketError::NoError);

    QCOMPARE(gy::net::socketErrorMessage(SocketError::ConnectionRefusedError),
             QStringLiteral("对端拒绝连接"));
    QCOMPARE(gy::net::socketErrorMessage(SocketError::RemoteHostClosedError),
             QStringLiteral("远程主机关闭了连接"));
    QCOMPARE(gy::net::errorMessageFromErrno(ETIMEDOUT), QStringLiteral("连接超时"));
}

QTEST_GUILESS_MAIN(TestPosixSocket)
#include "test_posix_socket.moc"
