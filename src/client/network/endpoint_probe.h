/**
* @file    endpoint_probe.h
* @version 7.0.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   网络端点探测
*
* 提供 TCP 连接探测功能，用于判断已知 IP:端口是否可达。
* 异步执行，不阻塞 UI 线程，探测完成后发射 probeFinished 信号。
*
* Change Log:
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