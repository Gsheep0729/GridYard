/**
* @file    endpoint_probe.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   网络端点探测
*
* 提供 TCP 连接探测功能，用于判断已知 IP:端口是否可达。
* 异步执行，不阻塞 UI 线程，探测完成后发射 probeFinished 信号。
*
* Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 版本头对齐到 v7.19.0
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.0.0] GY   2026-07-21
* * Stage 7.0：新增网络端点探测，支持 TCP 可达性诊断
*/

#pragma once

#include <QObject>
#include <QString>
#include <QHostAddress>

class EndpointProbe : public QObject {
    Q_OBJECT

public:
    // TCP 探测结果
    struct ProbeResult {
        QString targetIp;       // 目标 IP 地址
        quint16 targetPort = 0; // 目标端口
        bool tcpConnected = false; // TCP 是否连接成功
        int elapsedMs = 0;     // 探测耗时（毫秒）
        QString errorCode;     // 错误码
        QString errorMessage;  // 错误信息
    };

    explicit EndpointProbe(QObject *parent = nullptr);
    virtual ~EndpointProbe() override = default;

    EndpointProbe(const EndpointProbe &)            = delete;
    EndpointProbe &operator=(const EndpointProbe &) = delete;

    // 异步 TCP 探测，探测完成后发射 probeFinished 信号
    Q_INVOKABLE void probeTcp(const QString &ip, quint16 port, int timeoutMs = 5000);

signals:
    // 探测完成信号，携带完整探测结果
    void probeFinished(const EndpointProbe::ProbeResult &result);

private:
    // 生成错误码和错误信息
    static QString errorCodeFromSocketError(QAbstractSocket::SocketError socketError);
    static QString errorMessageFromSocketError(QAbstractSocket::SocketError socketError);
};
