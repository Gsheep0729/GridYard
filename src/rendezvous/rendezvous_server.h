/**
* @file    rendezvous_server.h
* @version 7.2.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点服务器
*
* 使用 QTcpServer 监听连接，处理 Register 和 ListPeers 请求。
*
* Change Log:
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点服务器
*/

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class OnlineRegistry;
class RendezvousProtocol;

class RendezvousSession : public QObject {
    Q_OBJECT

public:
    explicit RendezvousSession(QTcpSocket *socket, OnlineRegistry *registry, const QString &token, QObject *parent = nullptr);
    virtual ~RendezvousSession() override = default;

    void start();

signals:
    void finished();

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    void processRequest(const QJsonObject &json);
    void sendResponse(const QJsonObject &json);

    QTcpSocket *_socket = nullptr;
    OnlineRegistry *_registry = nullptr;
    QString _token;
    QByteArray _buffer;
};

class RendezvousServer : public QObject {
    Q_OBJECT

public:
    explicit RendezvousServer(const QString &host, quint16 port, const QString &token, QObject *parent = nullptr);
    virtual ~RendezvousServer() override = default;

    bool start();
    void stop();
    bool isListening() const;
    quint16 serverPort() const;

signals:
    void serverStarted(bool success, const QString &error);
    void clientConnected(const QString &address);
    void clientDisconnected(const QString &address);

private slots:
    void onNewConnection();
    void onSessionFinished();

private:
    QTcpServer *_server = nullptr;
    QString _host;
    quint16 _port = 0;
    QString _token;
    OnlineRegistry *_registry = nullptr;
    QList<RendezvousSession *> _sessions;
};