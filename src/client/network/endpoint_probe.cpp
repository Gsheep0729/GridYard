/**
* @file    endpoint_probe.cpp
* @version 8.2.1
* @date 2026-11-01
* @author  GridYard Team
* @brief   网络端点探测实现
*
* 使用原生 TCP 套接字发起非阻塞连接，由事件循环驱动完成回调。
* 连接成功、连接失败与超时三条路径互斥收尾，保证只发射一次结果。
*/

#include "endpoint_probe.h"

#include "ip_address.h"
#include "posix_tcp_socket.h"

#include <QElapsedTimer>
#include <QTimer>

#include <atomic>
#include <memory>

// 未指定超时时的默认等待时长（毫秒）
static constexpr int kDefaultTimeoutMs = 5000;

// 构造函数
EndpointProbe::EndpointProbe(QObject *parent)
    : QObject{parent}
{
}

// 异步探测指定地址与端口
void EndpointProbe::probeTcp(const QString &ip, quint16 port, int timeoutMs)
{
    // 输入校验
    const gy::net::IpAddress address = gy::net::IpAddress::fromString(ip);
    if (ip.isEmpty() || !address.isValid()) {
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
    auto *socket = new gy::net::PosixTcpSocket(this);
    auto finished = std::make_shared<std::atomic_bool>(false);

    const int effectiveTimeout = (timeoutMs > 0) ? timeoutMs : kDefaultTimeoutMs;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    // 超时处理 timer
    auto *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this,
            [this, socket, ip, port, timeoutTimer, effectiveTimeout, finished]() {
                if (finished->exchange(true)) {
                    return;
                }
                if (socket->state() != gy::net::SocketState::ConnectedState
                    && socket->state() != gy::net::SocketState::ClosingState) {
                    socket->abort();
                    ProbeResult result;
                    result.targetIp = ip;
                    result.targetPort = port;
                    result.tcpConnected = false;
                    result.elapsedMs = effectiveTimeout;
                    result.errorCode = QStringLiteral("TcpTimeout");
                    // 与 errorMessageFromSocketError 的超时文案保持一致，两路超时同提示
                    result.errorMessage =
                        QStringLiteral("连接超时，对方可能不在线或防火墙拦截了连接");
                    emit probeFinished(result);
                    socket->deleteLater();
                }
                timeoutTimer->deleteLater();
            });

    // 连接建立成功
    connect(socket, &gy::net::PosixTcpSocket::connected, this,
            [this, socket, ip, port, elapsedTimer, timeoutTimer, finished]() {
                if (finished->exchange(true)) {
                    return;
                }
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
    connect(socket, &gy::net::PosixTcpSocket::errorOccurred, this,
            [this, socket, ip, port, timeoutTimer, finished](gy::net::SocketError socketError) {
                // 成功后迟到的断开事件不再改写探测结果
                if (finished->exchange(true)) {
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

    // 先启动计时再发起连接，避免同步完成时计时器晚于结果收尾
    timeoutTimer->start(effectiveTimeout);
    socket->connectToHost(address, port);
}

// 网络错误类型转稳定错误码
QString EndpointProbe::errorCodeFromSocketError(gy::net::SocketError socketError)
{
    switch (socketError) {
    case gy::net::SocketError::HostNotFoundError:
        return QStringLiteral("HostNotFound");
    case gy::net::SocketError::ConnectionRefusedError:
        return QStringLiteral("TcpConnectionRefused");
    case gy::net::SocketError::SocketTimeoutError:
        return QStringLiteral("TcpTimeout");
    case gy::net::SocketError::NetworkError:
        return QStringLiteral("NetUnreachable");
    default:
        return QStringLiteral("UnknownError");
    }
}

// 网络错误类型转用户可读说明
QString EndpointProbe::errorMessageFromSocketError(gy::net::SocketError socketError)
{
    switch (socketError) {
    case gy::net::SocketError::HostNotFoundError:
        // errno 侧 EHOSTUNREACH/EHOSTDOWN 归入本类：主机不回应常因离线或防火墙拦截
        return QStringLiteral("主机不可达，请检查 IP 地址是否正确、是否与对方同一网络");
    case gy::net::SocketError::ConnectionRefusedError:
        return QStringLiteral("连接被拒绝，目标端口可能未开启服务");
    case gy::net::SocketError::SocketTimeoutError:
        // 与超时计时器路径文案保持一致：两条超时路径用户看到同一段提示
        return QStringLiteral("连接超时，对方可能不在线或防火墙拦截了连接");
    case gy::net::SocketError::NetworkError:
        return QStringLiteral("网络不可达，请检查本机网络连接是否正常");
    default:
        return QStringLiteral("未知错误：%1").arg(static_cast<int>(socketError));
    }
}
