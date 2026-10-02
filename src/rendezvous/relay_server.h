/**
* @file    relay_server.h
* @version 7.9.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   流式中继服务器
*
* 处理最严格的校园网隔离场景，两个客户端可都无法直连时通过中继转发。
* 中继只转发在线字节流，不落盘文件内容。
* 两端齐备后向发送端回一行 relay_ready 门控传输开始，任一端离开立即
* 通知并关闭对端，未完成会话超时回收。
*
* Change Log:
* [v7.9.0] GY   2026-07-26
* * 会话就绪通知与对端关闭传播，支持同端口复用时的外部连接接入
* * 首行握手改异步读取，未完成会话增加超时回收
* [v7.3.0] GY   2026-07-21
* * Stage 7.3：新增流式中继服务器
*/

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QTimer>

class RelaySession : public QObject {
    Q_OBJECT

public:
    explicit RelaySession(const QString &relayId, QObject *parent = nullptr);
    virtual ~RelaySession() override;

    QString relayId() const { return _relayId; }
    bool isComplete() const;  // 两端都连接完成
    bool addSender(QTcpSocket *socket, const QByteArray &pendingData = {});
    bool addReceiver(QTcpSocket *socket, const QByteArray &pendingData = {});
    // 两端齐备后向发送端写 relay_ready，门控其开始 TLV 传输
    void notifyReady();
    // 向两端广播错误消息
    void broadcastError(const QString &code, const QString &message);
    // 主动结束会话：向两端广播错误并关闭连接
    void abort(const QString &code, const QString &message);

signals:
    void sessionClosed();

private slots:
    void onSenderReadyRead();
    void onReceiverReadyRead();
    void onSenderDisconnected();
    void onReceiverDisconnected();

private:
    void relayData(QTcpSocket *from, QTcpSocket *to);

    QString _relayId;
    QTcpSocket *_sender = nullptr;
    QTcpSocket *_receiver = nullptr;
    QByteArray _senderBacklog;   // 会话未齐备前缓存的发送端字节
    QByteArray _receiverBacklog; // 会话未齐备前缓存的接收端字节
    QTimer *_idleTimer = nullptr; // 会话未齐备时的超时回收定时器
};

class RelayServer : public QObject {
    Q_OBJECT

public:
    explicit RelayServer(quint16 port, const QString &token, QObject *parent = nullptr);
    virtual ~RelayServer() override;

    bool start();
    void stop();
    quint16 serverPort() const;
    int sessionCount() const;

    // 接纳已由其他监听方完成首行握手的连接（协调节点同端口复用场景）
    void adoptConnection(QTcpSocket *socket, const QJsonObject &hello, const QByteArray &pendingData);

signals:
    void serverStarted(bool success, const QString &error);
    void sessionCreated(const QString &relayId);
    void sessionClosed(const QString &relayId);

private slots:
    void onNewConnection();
    void onSessionClosed();

private:
    // 解析中继握手并按角色加入会话
    void handleRelayHello(QTcpSocket *socket, const QJsonObject &json, const QByteArray &pendingData);
    // 丢弃未完成握手的连接
    void dropConnection(QTcpSocket *socket, const QString &reason);

    QTcpServer *_server = nullptr;
    quint16 _port = 0;
    QString _token;
    QMap<QString, RelaySession *> _sessions;
    QHash<QTcpSocket *, QByteArray> _helloBuffers;  // 等待首行 JSON 的连接缓冲
};
