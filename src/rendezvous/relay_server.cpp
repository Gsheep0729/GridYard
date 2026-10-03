/**
* @file    relay_server.cpp
* @version 7.16.0
* @date    2026-10-04
* @author  GridYard Team
* @brief   流式中继服务器实现
*
* Change Log:
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
* [v7.15.2] GY   2026-10-03
* * relay_error 响应的 code/message 字段名改用协议常量
* [v7.13.4] GY   2026-10-03
* * 会话关闭判等改对象指针；积压回滚补断信号；等待时长迁常量并可调
* [v7.12.0] GY   2026-10-02
* * 握手等待迁移到 LineSession 基类，补齐会话数与积压上限
* * 转发增加写队列背压与读缓冲上限，消除大文件转发内存无界增长
* [v7.9.0] GY   2026-07-26
* * 会话就绪通知、对端关闭传播与未完成会话超时回收
* * 握手首行改异步读取，新增同端口复用的连接接入入口
* [v7.3.0] GY   2026-07-21
* * Stage 7.3：新增流式中继服务器
*/

#include "relay_server.h"
#include "rendezvous_limits.h"
#include "rendezvous_protocol_keys.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

using namespace gy::rendezvous;

// -------------------- RelaySession --------------------

// 构造函数
RelaySession::RelaySession(const QString &relayId, int waitMs, QObject *parent)
    : QObject{parent}
    , _relayId{relayId}
{
    // 只有一端连接的会话可能永远等不到对端，超时后回收，避免 socket 长期悬挂
    _idleTimer = new QTimer{this};
    _idleTimer->setSingleShot(true);
    _idleTimer->setInterval(waitMs);
    connect(_idleTimer, &QTimer::timeout, this, [this]() {
        if (!isComplete()) {
            qWarning() << "[RelaySession]" << _relayId << "等待对端加入超时，回收会话";
            abort(QStringLiteral("session_timeout"), QStringLiteral("等待对端加入超时"));
        }
    });
    _idleTimer->start();
}

// 析构函数，清理 socket
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

// 检查两端是否都连接完成
bool RelaySession::isComplete() const
{
    return _sender != nullptr && _receiver != nullptr;
}

bool RelaySession::addSender(QTcpSocket *socket, const QByteArray &pendingData)
{
    if (_sender != nullptr) {
        return false;
    }

    _sender = socket;
    _sender->setParent(this);
    // 读缓冲封顶：转发暂停时形成 TCP 背压，内存有界
    _sender->setReadBufferSize(kRelayPipeReadBufferBytes);

    connect(_sender, &QTcpSocket::readyRead, this, &RelaySession::onSenderReadyRead);
    connect(_sender, &QTcpSocket::bytesWritten, this, &RelaySession::onReceiverReadyRead);
    connect(_sender, &QTcpSocket::disconnected, this, &RelaySession::onSenderDisconnected);

    // 同端口复用场景下握手行之后可能已捎带少量字节，缓存到齐备后转发
    if (!appendBacklog(_senderBacklog, pendingData)) {
        // 拒绝接入前先断开已连的信号，否则 socket 后续事件仍会打进本会话
        _sender->disconnect(this);
        _sender = nullptr;
        return false;
    }

    qDebug() << "[RelaySession]" << _relayId << "发送端已连接";
    notifyReady();
    return true;
}

// 添加接收端连接
bool RelaySession::addReceiver(QTcpSocket *socket, const QByteArray &pendingData)
{
    if (_receiver != nullptr) {
        return false;
    }

    _receiver = socket;
    _receiver->setParent(this);
    _receiver->setReadBufferSize(kRelayPipeReadBufferBytes);

    connect(_receiver, &QTcpSocket::readyRead, this, &RelaySession::onReceiverReadyRead);
    connect(_receiver, &QTcpSocket::bytesWritten, this, &RelaySession::onSenderReadyRead);
    connect(_receiver, &QTcpSocket::disconnected, this, &RelaySession::onReceiverDisconnected);

    if (!appendBacklog(_receiverBacklog, pendingData)) {
        // 拒绝接入前先断开已连的信号，否则 socket 后续事件仍会打进本会话
        _receiver->disconnect(this);
        _receiver = nullptr;
        return false;
    }

    qDebug() << "[RelaySession]" << _relayId << "接收端已连接";
    notifyReady();
    return true;
}

// 会话未齐备时缓存积压字节，超限拒绝接入
bool RelaySession::appendBacklog(QByteArray &backlog, const QByteArray &data)
{
    if (backlog.size() + data.size() > kMaxRelayBacklogBytes) {
        qWarning() << "[RelaySession]" << _relayId << "积压超过上限，拒绝连接";
        return false;
    }
    if (!data.isEmpty()) {
        backlog.append(data);
    }
    return true;
}

// 两端齐备后通知发送端可以开始传输，并转发齐备前缓存的字节
void RelaySession::notifyReady()
{
    if (!isComplete()) {
        return;
    }

    _idleTimer->stop();

    QJsonObject ready;
    ready[gy::rendezvous::kKeyType] = gy::rendezvous::kTypeRelayReady;
    _sender->write(QJsonDocument(ready).toJson(QJsonDocument::Compact) + '\n');

    if (!_senderBacklog.isEmpty()) {
        _receiver->write(_senderBacklog);
        _senderBacklog.clear();
    }
    if (!_receiverBacklog.isEmpty()) {
        _sender->write(_receiverBacklog);
        _receiverBacklog.clear();
    }

    qDebug() << "[RelaySession]" << _relayId << "两端齐备，已通知发送端";
}

// 主动结束会话：向两端广播错误并关闭连接
void RelaySession::abort(const QString &code, const QString &message)
{
    broadcastError(code, message);

    // 先摘除引用再关闭，避免 disconnected 槽重入
    QTcpSocket *sockets[] = {_sender, _receiver};
    _sender = nullptr;
    _receiver = nullptr;
    _idleTimer->stop();

    for (QTcpSocket *socket : sockets) {
        if (socket) {
            socket->disconnect(this);
            socket->disconnectFromHost();
            socket->deleteLater();
        }
    }

    emit sessionClosed();
}

// 向两端广播错误消息
void RelaySession::broadcastError(const QString &code, const QString &message)
{
    QJsonObject error;
    error[gy::rendezvous::kKeyType] = gy::rendezvous::kTypeRelayError;
    error[gy::rendezvous::kKeyCode] = code;
    error[gy::rendezvous::kKeyMessage] = message;

    QByteArray data = QJsonDocument(error).toJson(QJsonDocument::Compact) + '\n';

    if (_sender && _sender->state() == QTcpSocket::ConnectedState) {
        _sender->write(data);
    }
    if (_receiver && _receiver->state() == QTcpSocket::ConnectedState) {
        _receiver->write(data);
    }
}

// 处理发送端可读事件，向接收端方向转发
void RelaySession::onSenderReadyRead()
{
    pump(_sender, _receiver);
}

// 处理接收端可读事件，向发送端方向转发
void RelaySession::onReceiverReadyRead()
{
    pump(_receiver, _sender);
}

// 把 from 缓冲里的字节向 to 转发；对端写队列超限时暂停，其排空后由 bytesWritten 续传
void RelaySession::pump(QTcpSocket *from, QTcpSocket *to)
{
    if (!from || !to || !isComplete()) {
        return;
    }
    if (from->bytesAvailable() == 0) {
        return;
    }
    if (to->bytesToWrite() >= kMaxRelayPeerWriteQueue) {
        return;  // 等待 to 的 bytesWritten 再次触发本方向
    }

    const QByteArray data = from->readAll();
    if (!data.isEmpty()) {
        to->write(data);
    }
}

// 处理发送端断开事件
void RelaySession::onSenderDisconnected()
{
    qDebug() << "[RelaySession]" << _relayId << "发送端断开";
    _sender = nullptr;

    // 关闭对端让传输方立刻感知断连，而不是等到超时
    if (_receiver) {
        broadcastError(QStringLiteral("peer_left"), QStringLiteral("对端已断开"));
        _receiver->disconnectFromHost();
    }

    emit sessionClosed();
}

// 处理接收端断开事件
void RelaySession::onReceiverDisconnected()
{
    qDebug() << "[RelaySession]" << _relayId << "接收端断开";
    _receiver = nullptr;

    if (_sender) {
        broadcastError(QStringLiteral("peer_left"), QStringLiteral("对端已断开"));
        _sender->disconnectFromHost();
    }

    emit sessionClosed();
}

// -------------------- RelayServer --------------------

// 构造函数
RelayServer::RelayServer(quint16 port, const QString &token, QObject *parent)
    : QObject{parent}
    , _port{port}
    , _token{token}
    , _maxSessions{kMaxRelaySessions}
{
    _server = new QTcpServer{this};
}

// 析构函数，停止服务
RelayServer::~RelayServer()
{
    stop();
}

// 启动中继服务
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

// 停止中继服务并清理会话
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

// 获取服务监听端口
quint16 RelayServer::serverPort() const
{
    return _server->serverPort();
}

// 获取当前活跃会话数量
int RelayServer::sessionCount() const
{
    return _sessions.size();
}

// 调整并发中继会话上限
void RelayServer::setMaxSessions(int maxSessions)
{
    if (maxSessions > 0) {
        _maxSessions = maxSessions;
    }
}

// 调整会话等待对端加入的超时
void RelayServer::setSessionWaitMs(int waitMs)
{
    if (waitMs > 0) {
        _sessionWaitMs = waitMs;
    }
}

// 接纳已由其他监听方完成首行握手的连接（协调节点同端口复用场景）
void RelayServer::adoptConnection(QTcpSocket *socket, const QJsonObject &hello,
                                  const QByteArray &pendingData)
{
    handleRelayHello(socket, hello, pendingData);
}

// 处理新的客户端连接：握手等待交给 LineSession（行上限 + 超时）
void RelayServer::onNewConnection()
{
    while (_server->hasPendingConnections()) {
        QTcpSocket *socket = _server->nextPendingConnection();
        if (!socket) {
            continue;
        }

        qDebug() << "[RelayServer] 新连接来自" << socket->peerAddress().toString();

        // 握手等待连接同样有上限，防止空连接堆积
        if (_pendingHellos.size() >= _maxSessions) {
            qWarning() << "[RelayServer] 握手等待达到上限，拒绝新连接";
            dropConnection(socket, QStringLiteral("握手等待达到上限"));
            continue;
        }

        auto *hello = new LineSession{socket, kMaxRelayHelloBytes, this};
        _pendingHellos.insert(hello);

        connect(hello, &LineSession::lineReady, this, [this, hello](const QByteArray &line) {
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(line, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                hello->finishWithError(QStringLiteral("无效握手"));
                return;
            }

            // 拿到合法握手：socket 移交中继会话，等待对象完成使命
            _pendingHellos.remove(hello);
            const QByteArray leftover = hello->pendingBytes();
            QTcpSocket *pipe = hello->takeSocket();
            hello->deleteLater();
            handleRelayHello(pipe, doc.object(), leftover);
        });
        connect(hello, &LineSession::closed, this, [this, hello]() {
            _pendingHellos.remove(hello);
            hello->deleteLater();
        });

        hello->startHandshakeTimeout(kRelayHelloTimeoutMs);
    }
}

// 解析中继握手并按角色加入会话
void RelayServer::handleRelayHello(QTcpSocket *socket, const QJsonObject &json,
                                   const QByteArray &pendingData)
{
    const QString type = json[gy::rendezvous::kKeyType].toString();
    const QString relayId = json[gy::rendezvous::kKeyRelayId].toString();

    // 期望令牌非空时统一校验：协调分流与独立监听两条路径都经本入口，
    // 校验失败回 relay_error 后断开，消除独立模式无认证的死角
    if (!_token.isEmpty() && json[gy::rendezvous::kKeyToken].toString() != _token) {
        qWarning() << "[RelayServer] 中继握手令牌校验失败，拒绝连接";
        QJsonObject error;
        error[gy::rendezvous::kKeyType] = gy::rendezvous::kTypeRelayError;
        error[gy::rendezvous::kKeyCode] = QStringLiteral("unauthorized");
        error[gy::rendezvous::kKeyMessage] = QStringLiteral("访问令牌错误");
        socket->write(QJsonDocument(error).toJson(QJsonDocument::Compact) + '\n');
        socket->flush();
        dropConnection(socket, QStringLiteral("访问令牌错误"));
        return;
    }

    if (relayId.isEmpty()) {
        qWarning() << "[RelayServer] 缺少 relay_id";
        dropConnection(socket, QStringLiteral("缺少 relay_id"));
        return;
    }
    if (type != gy::rendezvous::kTypeRelayCreate && type != gy::rendezvous::kTypeRelayJoin) {
        qWarning() << "[RelayServer] 未知的中继握手类型" << type;
        dropConnection(socket, QStringLiteral("未知握手类型"));
        return;
    }

    // 查找或创建会话；接收端先于发送端到达时同样允许先建会话
    RelaySession *session = _sessions.value(relayId);
    if (!session) {
        // 并发会话达到上限时拒绝新建，已有会话不受影响
        if (_sessions.size() >= _maxSessions) {
            qWarning() << "[RelayServer] 会话数达到上限" << _maxSessions << "，拒绝新会话";
            dropConnection(socket, QStringLiteral("会话数达到上限"));
            return;
        }
        session = new RelaySession{relayId, _sessionWaitMs, this};
        connect(session, &RelaySession::sessionClosed, this, &RelayServer::onSessionClosed);
        _sessions.insert(relayId, session);
        qDebug() << "[RelayServer] 创建新中继会话" << relayId;
        emit sessionCreated(relayId);
    }

    const bool isSender = (type == gy::rendezvous::kTypeRelayCreate);
    const bool added = isSender ? session->addSender(socket, pendingData)
                                : session->addReceiver(socket, pendingData);

    if (!added) {
        qWarning() << "[RelayServer] 会话" << relayId << "无法添加连接（角色已满或已关闭）";
        dropConnection(socket, QStringLiteral("会话角色已满"));
    }
}

// 丢弃未完成握手的连接
void RelayServer::dropConnection(QTcpSocket *socket, const QString &reason)
{
    qWarning() << "[RelayServer] 关闭中继连接:" << reason;
    socket->disconnectFromHost();
    socket->deleteLater();
}

// 处理会话关闭事件
void RelayServer::onSessionClosed()
{
    RelaySession *session = qobject_cast<RelaySession *>(sender());
    if (!session) {
        return;
    }
    // 按对象指针判等：一端断开会二次触发 sessionClosed，relay_id 复用窗口内
    // 注册表里的同名键可能已指向新会话，此时旧会话的迟到关闭不得误删新会话
    if (_sessions.value(session->relayId()) != session) {
        return;
    }
    QString relayId = session->relayId();
    _sessions.remove(relayId);
    qDebug() << "[RelayServer] 会话" << relayId << "已关闭";
    emit sessionClosed(relayId);
    session->deleteLater();
}
