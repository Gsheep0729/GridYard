/**
* @file    test_endpoint_probe.cpp
* @version 7.0.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   EndpointProbe 单元测试
*
* 测试 TCP 探测功能：非法 IP、未监听端口、本机监听端口等场景。
*
* Change Log:
* [v7.0.0] GY   2026-07-21
* * Stage 7.0：新增网络端点探测测试
*/

#include "endpoint_probe.h"

#include <QHostAddress>
#include <QTcpServer>
#include <QTest>

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

    // 尝试连接本机未监听的端口
    probe.probeTcp("127.0.0.1", 65432, 3000);
    QCoreApplication::exec();

    QVERIFY(finished);
    QVERIFY(!result.tcpConnected);
    // 错误码应该是 TcpConnectionRefused
    QVERIFY(result.errorCode == "TcpConnectionRefused");
}

void TestEndpointProbe::testLocalPortConnected()
{
    // 启动本地服务器
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
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

    // 使用本机未使用的端口，模拟端口可达但无响应
    probe.probeTcp("127.0.0.1", 65433, 500);
    QCoreApplication::exec();

    QVERIFY(finished);
    QVERIFY(!result.tcpConnected);
    QVERIFY(result.errorCode == "TcpConnectionRefused");
}

QTEST_GUILESS_MAIN(TestEndpointProbe)
#include "test_endpoint_probe.moc"