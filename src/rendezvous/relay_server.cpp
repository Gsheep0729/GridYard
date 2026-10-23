/**
* @file    relay_server.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   流式中继服务器实现
*/

#include "relay_server.h"
#include "rendezvous_limits.h"
#include "rendezvous_log.h"
#include "rendezvous_protocol_keys.h"

#include <utility>

using namespace gy::rendezvous;

// -------------------- RelaySession --------------------

// 构造函数
RelaySession::RelaySession(const std::string &relayId, int waitMs, gy::net::NetReactor &reactor,
                           ClosedNotify onClosed, CloseDelegator closeDelegator)
    : _relayId{relayId}
    , _reactor{reactor}
    , _onClosed{std::move(onClosed)}
    , _closeDelegator{std::move(closeDelegator)}
{
    // 只有一端连接的会话可能永远等不到对端，超时后回收，避免连接长期悬挂
    _idleTimer = _reactor.scheduleAfter(waitMs, [this]() {
        _idleTimer = 0;
        if (!isComplete()) {
            logWarn("[RelaySession] " + _relayId + " 等待对端加入超时，回收会话");
            abort("session_timeout", "等待对端加入超时");
        }
    });
}

// 析构函数：持有中的连接委托服务端延迟回收，保证错误帧排空
RelaySession::~RelaySession()
{
    _reactor.cancelTimer(_idleTimer);
    gy::net::TcpSocketCore *sockets[] = {_sender, _receiver};
    _sender = nullptr;
    _receiver = nullptr;
    for (gy::net::TcpSocketCore *socket : sockets) {
        if (socket && _closeDelegator) {
            _closeDelegator(socket);
        } else {
            delete socket;
        }
    }
}

// 检查两端是否都连接完成
bool RelaySession::isComplete() const
{
    return _sender != nullptr && _receiver != nullptr;
}

// 添加发送端连接
bool RelaySession::addSender(gy::net::TcpSocketCore *socket, const std::vector<uint8_t> &pendingData)
{
    if (_sender != nullptr) {
        return false;
    }

    _sender = socket;
    // 读缓冲封顶：转发暂停时形成 TCP 背压，内存有界
    _sender->setReadBufferLimit(kRelayPipeReadBufferBytes);

    // 同端口复用场景下握手行之后可能已捎带少量字节，缓存到齐备后转发
    if (!appendBacklog(_senderBacklog, pendingData)) {
        _sender = nullptr;
        return false;
    }

    wireSocket(_sender, true);
    logInfo("[RelaySession] " + _relayId + " 发送端已连接");
    notifyReady();
    return true;
}

// 添加接收端连接
bool RelaySession::addReceiver(gy::net::TcpSocketCore *socket, const std::vector<uint8_t> &pendingData)
{
    if (_receiver != nullptr) {
        return false;
    }

    _receiver = socket;
    _receiver->setReadBufferLimit(kRelayPipeReadBufferBytes);

    if (!appendBacklog(_receiverBacklog, pendingData)) {
        _receiver = nullptr;
        return false;
    }

    wireSocket(_receiver, false);
    logInfo("[RelaySession] " + _relayId + " 接收端已连接");
    notifyReady();
    return true;
}

// 会话未齐备时缓存积压字节，超限拒绝接入
bool RelaySession::appendBacklog(std::vector<uint8_t> &backlog, const std::vector<uint8_t> &data)
{
    if (static_cast<std::ptrdiff_t>(backlog.size() + data.size()) > kMaxRelayBacklogBytes) {
        logWarn("[RelaySession] " + _relayId + " 积压超过上限，拒绝连接");
        return false;
    }
    if (!data.empty()) {
        backlog.insert(backlog.end(), data.begin(), data.end());
    }
    return true;
}

// 装配一端连接的全部回调
void RelaySession::wireSocket(gy::net::TcpSocketCore *socket, bool isSender)
{
    socket->setReadCallback([this, isSender]() { isSender ? onSenderRead() : onReceiverRead(); });
    // 写队列排空（字节送入内核）即恢复对端方向的续传
    socket->setWrittenCallback([this, isSender](int64_t) { isSender ? onReceiverRead() : onSenderRead(); });
    socket->setCloseCallback([this, isSender](gy::net::SocketCloseReason) {
        isSender ? onSenderClosed() : onReceiverClosed();
    });
    socket->setErrorCallback(nullptr);
}

// 两端齐备后通知发送端可以传输，并转发齐备前缓存的字节
void RelaySession::notifyReady()
{
    if (!isComplete()) {
        return;
    }

    _reactor.cancelTimer(_idleTimer);
    _idleTimer = 0;

    nlohmann::json ready;
    ready[kKeyType] = kTypeRelayReady;
    const std::string line = ready.dump() + '\n';
    _sender->write(reinterpret_cast<const uint8_t *>(line.data()), line.size());

    if (!_senderBacklog.empty()) {
        _receiver->write(_senderBacklog);
        _senderBacklog.clear();
    }
    if (!_receiverBacklog.empty()) {
        _sender->write(_receiverBacklog);
        _receiverBacklog.clear();
    }

    logInfo("[RelaySession] " + _relayId + " 两端齐备，已通知发送端");
}

// 主动结束会话：向两端广播错误并关闭连接
void RelaySession::abort(const std::string &code, const std::string &message)
{
    broadcastError(code, message);

    // 先摘除引用再关闭，避免关闭回调重入
    gy::net::TcpSocketCore *sockets[] = {_sender, _receiver};
    _sender = nullptr;
    _receiver = nullptr;
    _reactor.cancelTimer(_idleTimer);
    _idleTimer = 0;

    for (gy::net::TcpSocketCore *socket : sockets) {
        if (socket && _closeDelegator) {
            _closeDelegator(socket);
        }
    }

    notifyClosed();
}

// 发出会话关闭通知
void RelaySession::notifyClosed()
{
    if (_onClosed) {
        _onClosed(this);
    }
}

// 向两端广播错误消息
void RelaySession::broadcastError(const std::string &code, const std::string &message)
{
    nlohmann::json error;
    error[kKeyType] = kTypeRelayError;
    error[kKeyCode] = code;
    error[kKeyMessage] = message;

    const std::string data = error.dump() + '\n';
    const auto bytes = reinterpret_cast<const uint8_t *>(data.data());

    if (_sender && _sender->isConnected()) {
        _sender->write(bytes, data.size());
    }
    if (_receiver && _receiver->isConnected()) {
        _receiver->write(bytes, data.size());
    }
}

// 处理发送端可读事件，向接收端方向转发
void RelaySession::onSenderRead()
{
    pump(_sender, _receiver);
}

// 处理接收端可读事件，向发送端方向转发
void RelaySession::onReceiverRead()
{
    pump(_receiver, _sender);
}

// 把 from 缓冲里的字节向 to 转发；对端写队列超限时暂停，其排空后由写完成回调续传
void RelaySession::pump(gy::net::TcpSocketCore *from, gy::net::TcpSocketCore *to)
{
    if (!from || !to || !isComplete()) {
        return;
    }
    if (from->bytesAvailable() == 0) {
        return;
    }
    if (to->bytesToWrite() >= kMaxRelayPeerWriteQueue) {
        return;  // 等待 to 的写完成回调再次触发本方向
    }

    const std::vector<uint8_t> data = from->readAll();
    if (!data.empty()) {
        to->write(data);
    }
}

// 处理发送端断开事件
void RelaySession::onSenderClosed()
{
    logInfo("[RelaySession] " + _relayId + " 发送端断开");
    _sender = nullptr;

    // 关闭对端让传输方立刻感知断连，而不是等到超时
    if (_receiver) {
        broadcastError("peer_left", "对端已断开");
        gy::net::TcpSocketCore *receiver = _receiver;
        _receiver = nullptr;
        if (_closeDelegator) {
            _closeDelegator(receiver);
        }
    }

    notifyClosed();
}

// 处理接收端断开事件
void RelaySession::onReceiverClosed()
{
    logInfo("[RelaySession] " + _relayId + " 接收端断开");
    _receiver = nullptr;

    if (_sender) {
        broadcastError("peer_left", "对端已断开");
        gy::net::TcpSocketCore *sender = _sender;
        _sender = nullptr;
        if (_closeDelegator) {
            _closeDelegator(sender);
        }
    }

    notifyClosed();
}

// -------------------- RelayServer --------------------

// 构造函数
RelayServer::RelayServer(uint16_t port, const std::string &token, gy::net::NetReactor *reactor)
    : _port{port}
    , _token{token}
    , _maxSessions{kMaxRelaySessions}
{
    if (reactor) {
        _reactor = reactor;
    } else {
        _ownReactor = std::make_unique<gy::net::NetReactor>();
        _reactor = _ownReactor.get();
        _ownReactor->start();
    }
    _server = std::make_unique<gy::net::TcpServerCore>(*_reactor);
}

// 析构函数：停机清理并回收自建事件泵
RelayServer::~RelayServer()
{
    if (_reactor->isRunning() && !_reactor->onReactorThread()) {
        _reactor->dispatchSync([this]() { teardown(); });
    } else {
        teardown();
    }
    if (_ownReactor) {
        _ownReactor->stop();
    }
}

// 启动中继服务
bool RelayServer::start()
{
    if (!_server->listen(_port, true)) {
        _listenError = _server->errorString();
        logWarn("[RelayServer] 中继监听启动失败: " + _listenError);
        return false;
    }
    _listenError.clear();
    _port = _server->serverPort();

    logInfo("[RelayServer] 中继服务监听端口 " + std::to_string(_server->serverPort()));
    _server->setConnectionCallback(
        [this](int descriptor, const std::string &peerAddress) {
            handleNewConnection(descriptor, peerAddress);
        });
    return true;
}

// 停止中继服务：注销监听，存量会话与握手等待转入延迟回收
void RelayServer::stop()
{
    runOnLoop([this]() {
        _server->close();

        for (LineSession *hello : _pendingHellos) {
            retireLater(hello, [hello]() { delete hello; });
        }
        _pendingHellos.clear();

        for (const auto &entry : _sessions) {
            RelaySession *session = entry.second;
            retireLater(session, [session]() { delete session; });
        }
        _sessions.clear();
        _sessionCount.store(0);
    });
    logInfo("[RelayServer] 中继服务已停止");
}

// 获取服务监听端口
uint16_t RelayServer::serverPort() const
{
    return _server->serverPort();
}

// 获取当前活跃会话数量
int RelayServer::sessionCount() const
{
    return _sessionCount.load();
}

// 调整并发中继会话上限
void RelayServer::setMaxSessions(int maxSessions)
{
    if (maxSessions > 0) {
        runOnLoop([this, maxSessions]() { _maxSessions = maxSessions; });
    }
}

// 调整会话等待对端加入的超时
void RelayServer::setSessionWaitMs(int waitMs)
{
    if (waitMs > 0) {
        runOnLoop([this, waitMs]() { _sessionWaitMs = waitMs; });
    }
}

// 接纳已由其他监听方完成首行握手的连接
void RelayServer::adoptConnection(gy::net::TcpSocketCore *socket, const nlohmann::json &hello,
                                  const std::vector<uint8_t> &pendingData)
{
    // 拷贝入队：跨线程调用时 hello/pendingData 的生命周期不归属本服务
    runOnLoop([this, socket, hello, pendingData]() {
        handleRelayHello(socket, hello, pendingData);
    });
}

// 装配会话创建通知
void RelayServer::setSessionCreatedHandler(std::function<void(RelaySession *)> handler)
{
    _sessionCreatedHandler = std::move(handler);
}

// 装配会话关闭通知
void RelayServer::setSessionClosedHandler(std::function<void(RelaySession *)> handler)
{
    _sessionClosedHandler = std::move(handler);
}

// 会话关闭事件入口（按对象指针判等，迟到通知不误删新会话）
void RelayServer::handleSessionClosed(RelaySession *session)
{
    if (!session) {
        return;
    }
    // 一端断开会二次触发关闭通知，relay_id 复用窗口内注册表里的同名键
    // 可能已指向新会话，此时旧会话的迟到关闭不得误删新会话
    const auto it = _sessions.find(session->relayId());
    if (it == _sessions.end() || it->second != session) {
        return;
    }
    const std::string relayId = session->relayId();
    _sessions.erase(it);
    _sessionCount.store(static_cast<int>(_sessions.size()));
    logInfo("[RelayServer] 会话 " + relayId + " 已关闭");
    if (_sessionClosedHandler) {
        _sessionClosedHandler(session);
    }
    retireLater(session, [session]() { delete session; });
}

// 接入新连接：握手等待交给 LineSession（行上限 + 超时）
void RelayServer::handleNewConnection(int descriptor, const std::string &peerAddress)
{
    logInfo("[RelayServer] 新连接来自 " + peerAddress);

    // 握手等待连接同样有上限，防止空连接堆积
    if (static_cast<int>(_pendingHellos.size()) >= _maxSessions) {
        logWarn("[RelayServer] 握手等待达到上限，拒绝新连接");
        gy::net::TcpSocketCore rejected{*_reactor};
        rejected.adopt(descriptor);
        rejected.abort();
        return;
    }

    auto *socket = new gy::net::TcpSocketCore{*_reactor};
    socket->adopt(descriptor);
    auto *hello = new LineSession{*_reactor, socket, kMaxRelayHelloBytes};
    _pendingHellos.insert(hello);

    hello->setLineHandler([this, hello](const std::string &line) {
        const nlohmann::json doc = nlohmann::json::parse(line, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            hello->finishWithError("无效握手");
            return;
        }

        // 拿到合法握手：socket 移交中继会话，等待对象完成使命
        _pendingHellos.erase(hello);
        const std::vector<uint8_t> leftover = hello->pendingBytes();
        gy::net::TcpSocketCore *pipe = hello->takeSocket();
        retireLater(hello, [hello]() { delete hello; });
        handleRelayHello(pipe, doc, leftover);
    });
    hello->setClosedHandler([this, hello]() {
        _pendingHellos.erase(hello);
        retireLater(hello, [hello]() { delete hello; });
    });

    hello->startHandshakeTimeout(kRelayHelloTimeoutMs);
}

// 解析中继握手并按角色加入会话
void RelayServer::handleRelayHello(gy::net::TcpSocketCore *socket, const nlohmann::json &json,
                                   const std::vector<uint8_t> &pendingData)
{
    std::string type;
    std::string relayId;
    std::string token;
    const auto typeIt = json.find(kKeyType);
    if (typeIt != json.end() && typeIt->is_string()) {
        type = typeIt->get<std::string>();
    }
    const auto relayIt = json.find(kKeyRelayId);
    if (relayIt != json.end() && relayIt->is_string()) {
        relayId = relayIt->get<std::string>();
    }
    const auto tokenIt = json.find(kKeyToken);
    if (tokenIt != json.end() && tokenIt->is_string()) {
        token = tokenIt->get<std::string>();
    }

    // 期望令牌非空时统一校验：协调分流与独立监听两条路径都经本入口，
    // 校验失败回 relay_error 后断开，消除独立模式无认证的死角
    if (!_token.empty() && token != _token) {
        logWarn("[RelayServer] 中继握手令牌校验失败，拒绝连接");
        nlohmann::json error;
        error[kKeyType] = kTypeRelayError;
        error[kKeyCode] = "unauthorized";
        error[kKeyMessage] = "访问令牌错误";
        const std::string data = error.dump() + '\n';
        socket->write(reinterpret_cast<const uint8_t *>(data.data()), data.size());
        dropConnection(socket, "访问令牌错误");
        return;
    }

    if (relayId.empty()) {
        logWarn("[RelayServer] 缺少 relay_id");
        dropConnection(socket, "缺少 relay_id");
        return;
    }
    if (type != kTypeRelayCreate && type != kTypeRelayJoin) {
        logWarn("[RelayServer] 未知的中继握手类型 " + type);
        dropConnection(socket, "未知握手类型");
        return;
    }

    // 查找或创建会话；接收端先于发送端到达时同样允许先建会话
    RelaySession *session = nullptr;
    const auto sessionIt = _sessions.find(relayId);
    if (sessionIt != _sessions.end()) {
        session = sessionIt->second;
    }
    if (!session) {
        // 并发会话达到上限时拒绝新建，已有会话不受影响
        if (static_cast<int>(_sessions.size()) >= _maxSessions) {
            logWarn("[RelayServer] 会话数达到上限 " + std::to_string(_maxSessions) + "，拒绝新会话");
            dropConnection(socket, "会话数达到上限");
            return;
        }
        session = new RelaySession{relayId, _sessionWaitMs, *_reactor,
                                   [this](RelaySession *closed) { handleSessionClosed(closed); },
                                   [this](gy::net::TcpSocketCore *sock) { closeSocketLater(sock); }};
        _sessions.emplace(relayId, session);
        _sessionCount.store(static_cast<int>(_sessions.size()));
        logInfo("[RelayServer] 创建新中继会话 " + relayId);
        if (_sessionCreatedHandler) {
            _sessionCreatedHandler(session);
        }
    }

    const bool isSender = (type == kTypeRelayCreate);
    const bool added = isSender ? session->addSender(socket, pendingData)
                                : session->addReceiver(socket, pendingData);

    if (!added) {
        logWarn("[RelayServer] 会话 " + relayId + " 无法添加连接（角色已满或已关闭）");
        dropConnection(socket, "会话角色已满");
    }
}

// 丢弃未完成握手的连接
void RelayServer::dropConnection(gy::net::TcpSocketCore *socket, const std::string &reason)
{
    logWarn("[RelayServer] 关闭中继连接: " + reason);
    if (!socket) {
        return;
    }
    // 错误帧可能仍在写队列里，等断连后再回收，超时兜底
    closeSocketLater(socket);
}

// 摘除归属后先排空写队列，断开完成或 1 秒超时后再回收
void RelayServer::closeSocketLater(gy::net::TcpSocketCore *socket)
{
    if (!socket) {
        return;
    }
    if (_tearingDown) {
        delete socket;  // 停机路径无并发回调，直接回收
        return;
    }
    socket->setReadCallback(nullptr);
    socket->setWrittenCallback(nullptr);
    socket->setErrorCallback(nullptr);
    _closingSockets[socket] = _reactor->scheduleAfter(1000, [this, socket]() {
        reapClosingSocket(socket);
    });
    socket->setCloseCallback([this, socket](gy::net::SocketCloseReason) { reapClosingSocket(socket); });
    socket->disconnectFromHost();
}

// 回收排空完成的连接
void RelayServer::reapClosingSocket(gy::net::TcpSocketCore *socket)
{
    const auto it = _closingSockets.find(socket);
    if (it == _closingSockets.end()) {
        return;  // 已回收
    }
    _reactor->cancelTimer(it->second);
    _closingSockets.erase(it);
    // 延迟析构：可能正处在本连接的回调栈内，unwind 后再删除
    retireLater(socket, [socket]() { delete socket; });
}

// 延迟析构登记
void RelayServer::retireLater(void *object, std::function<void()> deleter)
{
    if (_retired.count(object)) {
        return;  // 同一对象只登记一次
    }
    const gy::net::NetReactor::TimerId timer =
        _reactor->scheduleAfter(0, [this, object]() {
            const auto it = _retired.find(object);
            if (it == _retired.end()) {
                return;  // 已在停机清理中回收
            }
            std::function<void()> pending = std::move(it->second.second);
            _retired.erase(it);
            pending();
        });
    _retired.emplace(object, std::make_pair(timer, std::move(deleter)));
}

// 停机清理
void RelayServer::teardown()
{
    _tearingDown = true;
    _server->close();

    for (const auto &entry : _retired) {
        _reactor->cancelTimer(entry.second.first);
        entry.second.second();
    }
    _retired.clear();

    for (const auto &entry : _closingSockets) {
        _reactor->cancelTimer(entry.second);
        delete entry.first;
    }
    _closingSockets.clear();

    for (LineSession *hello : _pendingHellos) {
        delete hello;
    }
    _pendingHellos.clear();

    for (const auto &entry : _sessions) {
        delete entry.second;
    }
    _sessions.clear();
    _sessionCount.store(0);
}

// 跨线程入口统一走事件泵同步执行
void RelayServer::runOnLoop(std::function<void()> task)
{
    _reactor->dispatchSync(std::move(task));
}
