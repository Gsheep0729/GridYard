/**
* @file    relay_server.cpp
* @version 7.3.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   流式中继服务器实现
*
* Change Log:
* [v7.3.0] GY   2026-07-21
* * Stage 7.3：新增流式中继服务器
*/

#include "relay_server.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUuid>

// -------------------- RelaySession --------------------

RelaySession::RelaySession(const QString &relayId, QObject *parent)
    : QObject{parent}
    , _relayId{relayId}
{
}

RelaySession::~RelaySession()
{
    if (_sender) {
        _sender->disconnect();
        _sender->deleteLater();
    }
    if (_receiver) {
        _receiver->disconnect();
        _receiver->deleteLater();
    }
}

bool RelaySession::isComplete() const
{
    return _sender != nullptr && _receiver != nullptr;
}

bool RelaySession::addSender(QTcpSocket *socket)
{
    if (_sender != nullptr) {
        return false;
    }

    _sender = socket;
    _sender->setParent(this);

    connect(_sender, &QTcpSocket::readyRead, this, &RelaySession::onSenderReadyRead);
    connect(_sender, &QTcpSocket::disconnected, this, &RelaySession::onSenderDisconnected);

    qDebug() << "[RelaySession]" << _relayId << "发送端已连接";
    return true;
}

bool RelaySession::addReceiver(QTcpSocket *socket)
{
    if (_receiver != nullptr) {
        return false;
    }

    _receiver = socket;
    _receiver->setParent(this);

    connect(_receiver, &QTcpSocket::readyRead, this, &RelaySession::onReceiverReadyRead);
    connect(_receiver, &QTcpSocket::disconnected, this, &RelaySession::onReceiverDisconnected);

    qDebug() << "[RelaySession]" << _relayId << "接收端已连接";
    return true;
}

void RelaySession::broadcastError(const QString &code, const QString &message)
{
    QJsonObject error;
    error[QStringLiteral("type")] = QStringLiteral("relay_error");
    error[QStringLiteral("code")] = code;
    error[QStringLiteral("message")] = message;

    QByteArray data = QJsonDocument(error).toJson(QJsonDocument::Compact) + '\n';

    if (_sender && _sender->state() == QTcpSocket::ConnectedState) {
        _sender->write(data);
    }
    if (_receiver && _receiver->state() == QTcpSocket::ConnectedState) {
        _receiver->write(data);
    }
}

void RelaySession::onSenderReadyRead()
{
    if (!_sender || !_receiver) {
        return;
    }

    QByteArray data = _sender->readAll();
    if (!data.isEmpty()) {
        _receiver->write(data);
    }
}

void RelaySession::onReceiverReadyRead()
{
    if (!_sender || !_receiver) {
        return;
    }

    QByteArray data = _receiver->readAll();
    if (!data.isEmpty()) {
        _sender->write(data);
    }
}

void RelaySession::onSenderDisconnected()
{
    qDebug() << "[RelaySession]" << _relayId << "发送端断开";
    _sender = nullptr;

    if (_receiver) {
        broadcastError(QStringLiteral("peer_left"), QStringLiteral("对端已断开"));
    }

    if (!isComplete()) {
        emit sessionClosed();
    }
}

void RelaySession::onReceiverDisconnected()
{
    qDebug() << "[RelaySession]" << _relayId << "接收端断开";
    _receiver = nullptr;

    if (_sender) {
        broadcastError(QStringLiteral("peer_left"), QStringLiteral("对端已断开"));
    }

    if (!isComplete()) {
        emit sessionClosed();
    }
}

// -------------------- RelayServer --------------------

RelayServer::RelayServer(quint16 port, const QString &token, QObject *parent)
    : QObject{parent}
    , _port{port}
    , _token{token}
{
    _server = new QTcpServer{this};
}

RelayServer::~RelayServer()
{
    stop();
}

bool RelayServer::start()
{
    if (!_server->listen(QHostAddress::Any, _port)) {
        emit serverStarted(false, _server->errorString());
        return false;
    }

    qInfo() << "[RelayServer] 中继服务监听端口" << _server->serverPort();
    connect(_server, &QTcpServer::newConnection, this, &RelayServer::onNewConnection);

    emit serverStarted(true, QString());
    return true;
}

void RelayServer::stop()
{
    // 清理所有会话
    for (RelaySession *session : std::as_const(_sessions)) {
        session->deleteLater();
    }
    _sessions.clear();

    _server->close();
    qInfo() << "[RelayServer] 中继服务已停止";
}

quint16 RelayServer::serverPort() const
{
    return _server->serverPort();
}

int RelayServer::sessionCount() const
{
    return _sessions.size();
}

void RelayServer::onNewConnection()
{
    QTcpSocket *socket = _server->nextPendingConnection();
    if (!socket) {
        return;
    }

    QString address = socket->peerAddress().toString();
    qDebug() << "[RelayServer] 新连接来自" << address;

    // 读取第一行 JSON 解析 relay_id 和 join_token
    socket->waitForReadyRead(5000);

    QByteArray data = socket->readAll();
    if (data.isEmpty()) {
        socket->close();
        return;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[RelayServer] 无效的 JSON 请求";
        socket->close();
        return;
    }

    QJsonObject json = doc.object();
    const QString type = json[QStringLiteral("type")].toString();
    const QString relayId = json[QStringLiteral("relay_id")].toString();
    const QString joinToken = json[QStringLiteral("join_token")].toString();

    if (relayId.isEmpty()) {
        qWarning() << "[RelayServer] 缺少 relay_id";
        socket->close();
        return;
    }

    // 查找或创建会话
    RelaySession *session = nullptr;
    if (!_sessions.contains(relayId)) {
        // 新建会话
        session = new RelaySession{relayId, this};
        connect(session, &RelaySession::sessionClosed, this, &RelayServer::onSessionClosed);
        _sessions.insert(relayId, session);
        qDebug() << "[RelayServer] 创建新中继会话" << relayId;
        emit sessionCreated(relayId);
    } else {
        session = _sessions.value(relayId);
    }

    // 添加到会话（发送端或接收端）
    bool isSender = (type == QStringLiteral("relay_create"));
    bool added = isSender ? session->addSender(socket) : session->addReceiver(socket);

    if (!added) {
        qWarning() << "[RelayServer] 会话" << relayId << "无法添加连接（角色已满或已关闭）";
        socket->close();
    } else {
        _socketToRelayId.insert(socket, relayId);
    }
}

void RelayServer::onSessionClosed()
{
    RelaySession *session = qobject_cast<RelaySession *>(sender());
    if (session) {
        QString relayId = session->relayId();
        _sessions.remove(relayId);
        qDebug() << "[RelayServer] 会话" << relayId << "已关闭";
        emit sessionClosed(relayId);
        session->deleteLater();
    }
}

QString RelayServer::generateRelayId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}