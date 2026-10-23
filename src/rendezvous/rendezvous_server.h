/**
 * @file    rendezvous_server.h
 * @version 7.26.1
 * @date    2026-10-23
 * @author  GridYard Team
 * @brief   协调节点服务器
 *
 * 使用原生套接字监听连接，处理 Register 和 ListPeers 请求，
 * 并提供中继邀请（relay_invite / relay_poll）信令。
 * 首行类型为 relay_create / relay_join 的连接剥离握手后移交中继服务器，
 * 使协调与中继可共用同一端口。
 * 会话继承 LineSession，具备行长度上限、握手/空闲超时与连接数上限。
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

#include "line_session.h"
#include "rendezvous_limits.h"

class OnlineRegistry;
class RendezvousProtocol;

namespace gy::net {
class PosixTcpServer;
class PosixTcpSocket;
}

class RendezvousSession : public LineSession {
    Q_OBJECT

public:
    explicit RendezvousSession(gy::net::PosixTcpSocket *socket, OnlineRegistry *registry, const QString &token,
                               int handshakeTimeoutMs, int idleTimeoutMs, QObject *parent = nullptr);

    void start();
    // 调整响应写队列上限（测试可调小）
    void setMaxResponseQueueBytes(qint64 maxBytes);

signals:
    void finished();
    // 中继管道连接移交契约：socket 原生套接字所有权移交给中继服务器，移交前已剥离首行握手，接收方需接管 pendingData 未消费预读缓冲
    void relayPipeRequested(gy::net::PosixTcpSocket *socket, const QJsonObject &hello, const QByteArray &pendingData);

private:
    void processLine(const QByteArray &line);
    void processRequest(const QJsonObject &json);
    void sendResponse(const QJsonObject &json);

    OnlineRegistry *_registry = nullptr;  // 设备在线注册表实例指针
    QString _token;  // 访问鉴权令牌
    int _handshakeTimeoutMs = 0;  // 首行握手超时毫秒数
    int _idleTimeoutMs = 0;  // 会话空闲超时毫秒数
    qint64 _maxResponseQueueBytes = gy::rendezvous::kMaxResponseWriteQueueBytes;  // 响应写积压上限
};

class RendezvousServer : public QObject {
    Q_OBJECT

public:
    explicit RendezvousServer(quint16 port, const QString &token,
                              int maxSessions = -1, QObject *parent = nullptr);
    virtual ~RendezvousServer() override = default;

    bool start();
    void stop();
    bool isListening() const;
    quint16 serverPort() const;
    // 调整会话握手与空闲超时（测试可调小；对新建立的会话生效）
    void setSessionTimeouts(int handshakeTimeoutMs, int idleTimeoutMs);
    // 调整注册表房间数与每房设备数上限（测试可调小）
    void setRegistryLimits(int maxRooms, int maxDevicesPerRoom);
    // 调整过期数据清理周期（测试可调小）
    void setPruneIntervalMs(int intervalMs);
    // 调整响应写队列上限（测试可调小；对新建立的会话生效）
    void setMaxResponseQueueBytes(qint64 maxBytes);

signals:
    void serverStarted(bool success, const QString &error);
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);
    // 会话移交中继管道契约：向 RelayServer 派发已剥离握手的原生 socket 与未消费缓冲，接收方负责管理后续生命周期
    void relayPipeRequested(gy::net::PosixTcpSocket *socket, const QJsonObject &hello, const QByteArray &pendingData);

private slots:
    void onNewConnection();

private:
    gy::net::PosixTcpServer *_server = nullptr;  // 原生 TCP 监听服务端对象指针
    quint16 _port = 0;                           // 服务监听端口号
    QString _token;                              // 访问鉴权令牌
    OnlineRegistry *_registry = nullptr;         // 内存设备在线注册表实例指针
    QList<RendezvousSession *> _sessions;        // 当前活跃会话生命周期列表
    int _maxSessions = 0;             // 并发会话上限
    int _handshakeTimeoutMs = 0;      // 首行握手超时
    int _idleTimeoutMs = 0;           // 会话空闲超时
    int _pruneIntervalMs = gy::rendezvous::kRegistryPruneIntervalMs;  // 过期清理周期
    qint64 _maxResponseQueueBytes = gy::rendezvous::kMaxResponseWriteQueueBytes;  // 响应写积压上限
};
