/**
* @file    rendezvous_server.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   协调节点服务器
*
* 使用 QTcpServer 监听连接，处理 Register 和 ListPeers 请求，
* 并提供中继邀请（relay_invite / relay_poll）信令。
* 首行类型为 relay_create / relay_join 的连接剥离握手后移交中继服务器，
* 使协调与中继可共用同一端口。
* 会话继承 LineSession，具备行长度上限、握手/空闲超时与连接数上限。
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
* [v7.15.8] GY   2026-10-03
* * 构造函数删除收而未用的 host 死参数
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.14.0] GY   2026-10-03
* * 令牌校验前置到中继分流之前
* [v7.13.4] GY   2026-10-03
* * 注册超限回错误响应；响应写积压超限断开连接；清理周期与注册表上限可调
* [v7.12.0] GY   2026-10-02
* * 会话迁移到 LineSession 基类，补齐行上限、握手/空闲超时与连接数上限
* * 移除存而不用的 _host 与空槽 onSessionFinished
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请信令与同端口中继连接移交
* * 会话断开后释放会话与 socket，修复长驻进程的连接泄漏
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点服务器
*/

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include "line_session.h"
#include "rendezvous_limits.h"

class OnlineRegistry;
class RendezvousProtocol;

class RendezvousSession : public LineSession {
    Q_OBJECT

public:
    explicit RendezvousSession(QTcpSocket *socket, OnlineRegistry *registry, const QString &token,
                               int handshakeTimeoutMs, int idleTimeoutMs, QObject *parent = nullptr);

    void start();
    // 调整响应写队列上限（测试可调小）
    void setMaxResponseQueueBytes(qint64 maxBytes);

signals:
    void finished();
    // 中继管道连接移交：握手行已剥离，socket 与剩余字节一并交给中继服务器
    void relayPipeRequested(QTcpSocket *socket, const QJsonObject &hello, const QByteArray &pendingData);

private:
    void processLine(const QByteArray &line);
    void processRequest(const QJsonObject &json);
    void sendResponse(const QJsonObject &json);

    OnlineRegistry *_registry = nullptr;
    QString _token;
    int _handshakeTimeoutMs = 0;
    int _idleTimeoutMs = 0;
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
    // 会话移交的中继管道连接，由接入方（RelayServer）承接
    void relayPipeRequested(QTcpSocket *socket, const QJsonObject &hello, const QByteArray &pendingData);

private slots:
    void onNewConnection();

private:
    QTcpServer *_server = nullptr;
    quint16 _port = 0;
    QString _token;
    OnlineRegistry *_registry = nullptr;
    QList<RendezvousSession *> _sessions;
    int _maxSessions = 0;             // 并发会话上限
    int _handshakeTimeoutMs = 0;      // 首行握手超时
    int _idleTimeoutMs = 0;           // 会话空闲超时
    int _pruneIntervalMs = gy::rendezvous::kRegistryPruneIntervalMs;  // 过期清理周期
    qint64 _maxResponseQueueBytes = gy::rendezvous::kMaxResponseWriteQueueBytes;  // 响应写积压上限
};
