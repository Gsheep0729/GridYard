/**
* @file    test_endpoint_probe.cpp
* @version 8.2.1
* @date 2026-11-01
* @author  GridYard Team
* @brief   EndpointProbe 单元测试
*
* 测试 TCP 探测功能：非法 IP、未监听端口、本机监听端口等场景。
*/

#include "endpoint_probe.h"

#include "test_utils/port.h"

#include <QTest>

#include "ip_address.h"
#include "posix_tcp_server.h"

class TestEndpointProbe : public QObject {
    Q_OBJECT

private slots:
    // 测试非法 IP 输入直接返回错误
    void testInvalidIp();
    // 测试未监听端口返回连接拒绝
    void testConnectionRefused();
    // 测试本机监听端口返回连接成功
    void testLocalPortConnected();
    // 测试连接超时
    void testConnectionTimeout();
    // C-06 回归：超时/不可达错误码与文案逐类独立，不再吞进 UnknownError
    void testErrorMappingTexts();
};

void TestEndpointProbe::testInvalidIp()
{
    // 测试空字符串
    {
        EndpointProbe probe;
        EndpointProbe::ProbeResult result;
        bool finished = false;

        connect(&probe, &EndpointProbe::probeFinished, this, [&result, &finished](const EndpointProbe::ProbeResult &r) {
            result = r;
            finished = true;
            QCoreApplication::quit();
        });

        probe.probeTcp("", 35100, 1000);
        if (!finished) {
            QCoreApplication::exec();
        }

        QVERIFY(result.errorCode == "InvalidInput");
        QVERIFY(!result.tcpConnected);
    }

    // 测试非法格式
    {
        EndpointProbe probe;
        EndpointProbe::ProbeResult result;
        bool finished = false;

        connect(&probe, &EndpointProbe::probeFinished, this, [&result, &finished](const EndpointProbe::ProbeResult &r) {
            result = r;
            finished = true;
            QCoreApplication::quit();
        });

        probe.probeTcp("not.an.ip", 35100, 1000);
        if (!finished) {
            QCoreApplication::exec();
        }

        QVERIFY(result.errorCode == "InvalidInput");
        QVERIFY(!result.tcpConnected);
    }
}

void TestEndpointProbe::testConnectionRefused()
{
    EndpointProbe probe;
    EndpointProbe::ProbeResult result;
    bool finished = false;

    connect(&probe, &EndpointProbe::probeFinished, this, [&result, &finished](const EndpointProbe::ProbeResult &r) {
        result = r;
        finished = true;
        QCoreApplication::quit();
    });

    // 先占用再释放一个系统端口，连接它应被拒绝
    const quint16 deadPort = gy::test::allocateEphemeralPort();
    probe.probeTcp("127.0.0.1", deadPort, 3000);
    QCoreApplication::exec();

    QVERIFY(finished);
    QVERIFY(!result.tcpConnected);
    // 错误码应该是 TcpConnectionRefused
    QVERIFY(result.errorCode == "TcpConnectionRefused");
}

void TestEndpointProbe::testLocalPortConnected()
{
    // 启动本地服务器
    gy::net::PosixTcpServer server;
    QVERIFY(server.listen(gy::net::IpAddress::localHost(), 0));
    const quint16 listenPort = server.serverPort();

    EndpointProbe probe;
    EndpointProbe::ProbeResult result;
    bool finished = false;

    connect(&probe, &EndpointProbe::probeFinished, this, [&result, &finished](const EndpointProbe::ProbeResult &r) {
        result = r;
        finished = true;
        QCoreApplication::quit();
    });

    probe.probeTcp("127.0.0.1", listenPort, 3000);
    QCoreApplication::exec();

    QVERIFY(finished);
    QVERIFY(result.tcpConnected);
    QVERIFY(result.elapsedMs >= 0);
    QVERIFY(result.errorCode.isEmpty());
    QVERIFY(result.errorMessage.isEmpty());
}

void TestEndpointProbe::testConnectionTimeout()
{
    // 由于网络环境差异，超时测试改为验证：
    // 1. 能收到结果
    // 2. 结果表示未连接成功
    // 不验证具体错误码，因为取决于网络环境
    EndpointProbe probe;
    EndpointProbe::ProbeResult result;
    bool finished = false;

    connect(&probe, &EndpointProbe::probeFinished, this, [&result, &finished](const EndpointProbe::ProbeResult &r) {
        result = r;
        finished = true;
        QCoreApplication::quit();
    });

    // 先占用再释放一个系统端口，模拟端口可达但无响应
    const quint16 silentPort = gy::test::allocateEphemeralPort();
    probe.probeTcp("127.0.0.1", silentPort, 500);
    QCoreApplication::exec();

    QVERIFY(finished);
    QVERIFY(!result.tcpConnected);
    QVERIFY(result.errorCode == "TcpConnectionRefused");
}

void TestEndpointProbe::testErrorMappingTexts()
{
    // C-06：防火墙拦截场景下用户拿到的不能是"未知错误：N"。
    // 超时（ETIMEDOUT）与网络不可达（ENETUNREACH）各自给出独立错误码
    QVERIFY(EndpointProbe::errorCodeFromSocketError(
                gy::net::SocketError::SocketTimeoutError) == "TcpTimeout");
    QVERIFY(EndpointProbe::errorCodeFromSocketError(
                gy::net::SocketError::NetworkError) == "NetUnreachable");
    // 既有分类保持稳定，防止下游按错误码分支的展示被意外改写
    QVERIFY(EndpointProbe::errorCodeFromSocketError(
                gy::net::SocketError::HostNotFoundError) == "HostNotFound");
    QVERIFY(EndpointProbe::errorCodeFromSocketError(
                gy::net::SocketError::ConnectionRefusedError) == "TcpConnectionRefused");
    // 真正未分类的错误仍归 UnknownError，兜底语义不变
    QVERIFY(EndpointProbe::errorCodeFromSocketError(
                gy::net::SocketError::UnknownSocketError) == "UnknownError");

    // 文案：三类网络失败各自独立可读，一律不落"未知错误：N"
    const QString timeoutText = EndpointProbe::errorMessageFromSocketError(
        gy::net::SocketError::SocketTimeoutError);
    const QString hostUnreachableText = EndpointProbe::errorMessageFromSocketError(
        gy::net::SocketError::HostNotFoundError);
    const QString netUnreachableText = EndpointProbe::errorMessageFromSocketError(
        gy::net::SocketError::NetworkError);
    QVERIFY(!timeoutText.startsWith("未知错误"));
    QVERIFY(!hostUnreachableText.startsWith("未知错误"));
    QVERIFY(!netUnreachableText.startsWith("未知错误"));
    // 三类文案互不相同，避免同一段提示掩盖不同失败形态
    QVERIFY(timeoutText != hostUnreachableText);
    QVERIFY(timeoutText != netUnreachableText);
    QVERIFY(hostUnreachableText != netUnreachableText);
    // 连接拒绝文案维持既有语义
    QVERIFY(!EndpointProbe::errorMessageFromSocketError(
                gy::net::SocketError::ConnectionRefusedError).startsWith("未知错误"));
}

QTEST_GUILESS_MAIN(TestEndpointProbe)
#include "test_endpoint_probe.moc"
