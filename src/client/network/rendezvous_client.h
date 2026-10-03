/**
* @file    rendezvous_client.h
* @version 7.15.15
* @date    2026-10-04
* @author  GridYard Team
* @brief   协调节点客户端
*
* 封装与协调服务器的 TCP 连接和 JSON 协议通信。
* 负责向协调节点注册本机端点、拉取候选设备列表，
* 并收发中继邀请信令（relay_invite / relay_poll）。
*
* 客户端开机后若启用了协调服务器（rendezvousEnabled=true），
* 自动连接并注册本机 deviceId、IP 列表、TCP 端口等信息，
* 每 5 秒心跳刷新 TTL 并顺带轮询中继邀请，断线后自动重连。
*
* Change Log:
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
* [v7.14.0] GY   2026-10-03
* * 所有控制报文统一注入访问令牌字段，配合服务端 token 认证
* [v7.9.0] GY   2026-07-26
* * 新增中继邀请请求与按心跳周期轮询待领取邀请
* [v7.5.0] GY   2026-07-21
* * Stage 7.5 Phase B：新增协调节点客户端
*/

#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTcpSocket>
#include <QTimerEvent>

class ConfigManager;

class RendezvousClient : public QObject {
    Q_OBJECT

public:
    // 协调服务器返回的设备端点信息
    struct PeerEndpoint {
        QString deviceId;
        QString deviceName;
        QStringList addresses;
        quint16 tcpPort = 0;
        quint16 discoveryPort = 0;
        QDateTime lastSeen;
    };

    // 将 PeerEndpoint 转换为 QVariantMap
    static QVariantMap peerEndpointToVariantMap(const PeerEndpoint &peer);

    explicit RendezvousClient(QObject *parent = nullptr);
    ~RendezvousClient();

    // 连接协调服务器
    Q_INVOKABLE void connectToServer(const QString &host, int port);
    // 设置访问令牌：非空时随每个控制报文发送，须在 connectToServer 前调用
    void setToken(const QString &token);
    // 断开连接
    Q_INVOKABLE void disconnectFromServer();

    // 发送注册请求
    Q_INVOKABLE void registerDevice(const QString &room,
                                   const QString &deviceId,
                                   const QString &deviceName,
                                   const QStringList &addresses,
                                   quint16 tcpPort,
                                   quint16 discoveryPort);

    // 发送查询在线设备请求
    Q_INVOKABLE void listPeers(const QString &room);

    // 请求协调服务器向目标设备转发中继邀请（relay_id 由调用方生成）
    Q_INVOKABLE void requestRelayInvite(const QString &relayId, const QString &targetDeviceId,
                                        const QString &fileName, qint64 totalBytes);

    // 当前连接状态
    bool isConnected() const;

signals:
    // 连接状态变化
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);

    // 注册响应
    void registerAckReceived(int ttlSeconds);

    // 设备列表响应（QVariantMap 版本，方便 QML 和其他模块使用）
    void peersReceived(const QList<QVariantMap> &peers);

    // 中继邀请已被协调服务器受理
    void relayInviteAckReceived(const QString &relayId);
    // 轮询到目标为本机的中继邀请
    void relayInvitesReceived(const QList<QVariantMap> &invites);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onReadyRead();

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    void closeSocket();
    void stopHeartbeat();
    void scheduleReconnect();
    void sendJson(const QJsonObject &json);
    bool readJson(QJsonObject *json);
    void handleMessage(const QJsonObject &json);
    void doRegister();
    // 按本机 deviceId 轮询待领取的中继邀请
    void sendRelayPoll();

    QTcpSocket *_socket = nullptr;
    QByteArray _buffer;
    bool _isConnected = false;
    bool _wantConnected = false;
    bool _reconnectScheduled = false;
    QString _serverHost;
    int _serverPort = 0;
    QString _token;              // 访问令牌，空表示服务器未启用认证
    QString _room;               // 房间名，用于心跳重注册
    QString _deviceId;           // 本机设备 ID
    QString _deviceName;         // 本机设备名
    QStringList _addresses;     // 本机地址列表
    quint16 _tcpPort = 0;       // TCP 端口
    quint16 _discoveryPort = 0; // 发现端口
    int _heartbeatTimerId = 0;
    int _ttlSeconds = 30;

    static constexpr int kHeartbeatIntervalMs = 5000;  // 5 秒心跳
    static constexpr int kReconnectDelayMs = 3000;     // 3 秒后重连
};
