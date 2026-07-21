/**
* @file    relay_server.h
* @version 7.3.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   流式中继服务器
*
* 处理最严格的校园网隔离场景，两个客户端可都无法直连时通过中继转发。
* 中继只转发在线字节流，不落盘文件内容。
*
* Change Log:
* [v7.3.0] GY   2026-07-21
* * Stage 7.3：新增流式中继服务器
*/

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>

class RelaySession : public QObject {
    Q_OBJECT

public:
    explicit RelaySession(const QString &relayId, QObject *parent = nullptr);
    virtual ~RelaySession() override;

    QString relayId() const { return _relayId; }
    bool isComplete() const;  // 两端都连接完成
    bool addSender(QTcpSocket *socket);
    bool addReceiver(QTcpSocket *socket);
    void broadcastError(const QString &code, const QString &message);

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
    QByteArray _senderBuffer;
    QByteArray _receiverBuffer;
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

signals:
    void serverStarted(bool success, const QString &error);
    void sessionCreated(const QString &relayId);
    void sessionClosed(const QString &relayId);

private slots:
    void onNewConnection();
    void onSessionClosed();

private:
    QString generateRelayId() const;

    QTcpServer *_server = nullptr;
    quint16 _port = 0;
    QString _token;
    QMap<QString, RelaySession *> _sessions;
    QMap<QTcpSocket *, QString> _socketToRelayId;
};