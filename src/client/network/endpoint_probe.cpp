/**
* @file    endpoint_probe.cpp
* @version 7.0.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   网络端点探测实现
*
* 实现 TCP 连接探测功能，使用异步 QTcpSocket 尝试连接到目标地址。
* 通过状态机检测连接结果，完成后自动清理 socket 并发射信号。
*
* Change Log:
* [v7.0.0] GY   2026-07-21
* * Stage 7.0：新增网络端点探测，支持 TCP 可达性诊断
*/

#include "endpoint_probe.h"

#include <QAbstractSocket>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QNetworkProxy>
#include <QTimer>

// 探测结果有效期（秒），超时后自动清理
static constexpr int kDefaultTimeoutMs = 5000;

EndpointProbe::EndpointProbe(QObject *parent)
    : QObject{parent}
{
}

void EndpointProbe::probeTcp(const QString &ip, quint16 port, int timeoutMs)
{
    // 输入校验
    QHostAddress address;
    if (ip.isEmpty() || !address.setAddress(ip)) {
        ProbeResult result;
        result.targetIp = ip;
        result.targetPort = port;
        result.tcpConnected = false;
        result.elapsedMs = 0;
        result.errorCode = QStringLiteral("InvalidInput");
        result.errorMessage = QStringLiteral("无效的 IP 地址");
        emit probeFinished(result);
        return;
    }

    // 创建临时 socket，探测完成后自动清理
    QTcpSocket *socket = new QTcpSocket(this);

    // 禁用代理
    QNetworkProxy noProxy;
    noProxy.setType(QNetworkProxy::NoProxy);
    socket->setProxy(noProxy);

    // 记录开始时间
    const int effectiveTimeout = (timeoutMs > 0) ? timeoutMs : kDefaultTimeoutMs;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    // 超时处理 timer
    QTimer *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this, socket, ip, port, timeoutTimer, effectiveTimeout]() {
        if (socket->state() != QAbstractSocket::ConnectedState &&
            socket->state() != QAbstractSocket::ClosingState) {
            socket->abort();
            ProbeResult result;
            result.targetIp = ip;
            result.targetPort = port;
            result.tcpConnected = false;
            result.elapsedMs = effectiveTimeout;
            result.errorCode = QStringLiteral("TcpTimeout");
            result.errorMessage = QStringLiteral("连接超时");
            emit probeFinished(result);
        }
        timeoutTimer->deleteLater();
    });

    // 连接建立成功
    connect(socket, &QTcpSocket::connected, this, [this, socket, ip, port, elapsedTimer, timeoutTimer]() {
        timeoutTimer->stop();
        ProbeResult result;
        result.targetIp = ip;
        result.targetPort = port;
        result.tcpConnected = true;
        result.elapsedMs = static_cast<int>(elapsedTimer.elapsed());
        emit probeFinished(result);
        socket->deleteLater();
        timeoutTimer->deleteLater();
    });

    // 连接错误
    connect(socket, &QTcpSocket::errorOccurred,
            this, [this, socket, ip, port, timeoutTimer](QAbstractSocket::SocketError socketError) {
                // 忽略主动关闭错误
                if (socketError == QAbstractSocket::RemoteHostClosedError) {
                    return;
                }
                timeoutTimer->stop();
                ProbeResult result;
                result.targetIp = ip;
                result.targetPort = port;
                result.tcpConnected = false;
                result.elapsedMs = 0;
                result.errorCode = errorCodeFromSocketError(socketError);
                result.errorMessage = errorMessageFromSocketError(socketError);
                emit probeFinished(result);
                socket->deleteLater();
                timeoutTimer->deleteLater();
            });

    // 启动连接
    socket->connectToHost(address, port);
    timeoutTimer->start(effectiveTimeout);
}

QString EndpointProbe::errorCodeFromSocketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("HostNotFound");
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("TcpConnectionRefused");
    default:
        return QStringLiteral("UnknownError");
    }
}

QString EndpointProbe::errorMessageFromSocketError(QAbstractSocket::SocketError socketError)
{
    switch (socketError) {
    case QAbstractSocket::HostNotFoundError:
        return QStringLiteral("主机未找到，请检查 IP 地址是否正确");
    case QAbstractSocket::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝，目标端口可能未开启服务");
    default:
        return QStringLiteral("未知错误：%1").arg(socketError);
    }
}