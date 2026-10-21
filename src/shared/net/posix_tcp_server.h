/**
* @file    posix_tcp_server.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   基于原生描述符的 TCP 监听器
*
* 监听描述符由 QSocketNotifier 驱动，接入连接先进入待取队列，
* 调用方通过两段式接口逐个接管，与既有服务端的接入节奏一致。
* 双栈模式显式关闭 IPV6_V6ONLY，内核不支持时回退为 IPv4。
*/

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include "ip_address.h"

class QSocketNotifier;

namespace gy::net {

class PosixTcpSocket;

class PosixTcpServer : public QObject {
private:
    Q_OBJECT

public:
    // 监听地址族模式
    enum ListenMode {
        Ipv4Only,  // 仅 IPv4
        DualStack  // IPv6 双栈，IPv4 客户端映射后接入
    };

    explicit PosixTcpServer(QObject *parent = nullptr);
    virtual ~PosixTcpServer() override;

    PosixTcpServer(const PosixTcpServer &)            = delete;
    PosixTcpServer &operator=(const PosixTcpServer &) = delete;

    // 按地址值绑定并监听
    bool listen(const IpAddress &address, quint16 port, ListenMode mode = Ipv4Only);
    // 按地址文本绑定并监听
    bool listen(const QString &address, quint16 port, ListenMode mode = Ipv4Only);
    // 停止监听并关闭待取连接
    void close();

    // 是否正在监听
    bool isListening() const;
    // 实际监听端口
    quint16 serverPort() const;
    // 最近一次监听失败原因
    QString errorString() const;

    // 是否有待取连接
    bool hasPendingConnections() const;
    // 取出下一个连接，所有权转移给调用方
    PosixTcpSocket *nextPendingConnection();

signals:
    // 有新连接到达
    void newConnection();

private:
    // 待取连接描述符
    struct PendingConnection {
        int descriptor = -1;  // 原生连接描述符
    };

    // 接受事件处理
    void onAcceptActivated();
    // 关闭监听描述符
    void closeDescriptor();
    // 创建监听描述符
    bool createListener(const IpAddress &address, quint16 port, ListenMode mode);
    // 读取实际监听端口
    quint16 queryPort() const;

private:
    int _fd = -1;                             // 监听描述符
    QSocketNotifier *_notifier = nullptr;     // 接受事件通知器
    QList<PendingConnection> _pending;        // 待取连接队列
    quint16 _port = 0;                        // 实际监听端口
    QString _errorText;                       // 最近一次错误说明
};

}  // namespace gy::net
