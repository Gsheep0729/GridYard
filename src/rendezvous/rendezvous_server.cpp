/**
* @file    rendezvous_server.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   协调节点服务器实现
*/

#include "rendezvous_server.h"
#include "online_registry.h"
#include "rendezvous_limits.h"
#include "rendezvous_log.h"
#include "rendezvous_protocol.h"
#include "rendezvous_protocol_keys.h"

#include <algorithm>
#include <utility>

using namespace gy::rendezvous;

// 会话构造函数
RendezvousSession::RendezvousSession(gy::net::NetReactor &reactor, gy::net::TcpSocketCore *socket,
                                     OnlineRegistry *registry, const std::string &token,
                                     int handshakeTimeoutMs, int idleTimeoutMs)
    : LineSession{reactor, socket, kMaxRendezvousLineBytes}
    , _registry{registry}
    , _token{token}
    , _handshakeTimeoutMs{handshakeTimeoutMs}
    , _idleTimeoutMs{idleTimeoutMs}
{
    setLineHandler([this](const std::string &line) { processLine(line); });
}

// 启动会话处理：开启首行握手超时
void RendezvousSession::start()
{
    logInfo("[RendezvousSession] 新会话来自 " + (socket() ? socket()->peerAddressText() : std::string()));
    startHandshakeTimeout(_handshakeTimeoutMs);
}

// 调整响应写队列上限
void RendezvousSession::setMaxResponseQueueBytes(int64_t maxBytes)
{
    if (maxBytes > 0) {
        _maxResponseQueueBytes = maxBytes;
    }
}

// 处理一条控制行：刷新空闲超时并按 JSON 解析
void RendezvousSession::processLine(const std::string &line)
{
    if (_idleTimeoutMs > 0) {
        startIdleTimeout(_idleTimeoutMs);
    }

    const nlohmann::json doc = nlohmann::json::parse(line, nullptr, false);

    if (doc.is_discarded()) {
        sendResponse(RendezvousProtocol::buildError("无效的 JSON"));
        return;
    }

    if (!doc.is_object()) {
        sendResponse(RendezvousProtocol::buildError("请求必须是 JSON 对象"));
        return;
    }

    processRequest(doc);
}

// 处理 JSON 请求消息
void RendezvousSession::processRequest(const nlohmann::json &json)
{
    // 所有请求（含中继握手）先验令牌：令牌校验必须发生在分流之前，
    // 否则配了 --token 的服务器会被中继管道绕过认证
    const std::string providedToken = RendezvousProtocol::extractToken(json);
    if (!RendezvousProtocol::validateToken(providedToken, _token)) {
        sendResponse(RendezvousProtocol::buildError("无效的访问口令"));
        return;
    }

    // 中继管道连接：剥离握手行后整条移交，后续字节流不再按 JSON 行解析
    std::string typeValue;
    const auto typeIt = json.find(kKeyType);
    if (typeIt != json.end() && typeIt->is_string()) {
        typeValue = typeIt->get<std::string>();
    }
    if (typeValue == kTypeRelayCreate || typeValue == kTypeRelayJoin) {
        const std::vector<uint8_t> pending = pendingBytes();
        gy::net::TcpSocketCore *pipeSocket = takeSocket();
        if (_relayPipeHandler) {
            _relayPipeHandler(pipeSocket, json, pending);
        } else if (pipeSocket) {
            // 没有接收方接管时主动回收，避免移交后的连接悬挂
            pipeSocket->abort();
            LineSession::deferDeleteSocket(pipeSocket);
        }
        finish();  // 会话职责已移交，通知持有方回收本对象
        return;
    }

    std::string errorString;
    RendezvousProtocol::MessageType type = RendezvousProtocol::parseRequest(json, &errorString);

    if (type == RendezvousProtocol::MessageType::Error) {
        sendResponse(RendezvousProtocol::buildError(errorString));
        return;
    }

    const std::string room = RendezvousProtocol::extractRoom(json);
    if (room.empty()) {
        sendResponse(RendezvousProtocol::buildError("缺少 room 参数"));
        return;
    }

    switch (type) {
    case RendezvousProtocol::MessageType::Register: {
        OnlineRegistry::PeerInfo peer = RendezvousProtocol::extractPeerInfo(json);
        if (!_registry->upsertPeer(room, peer)) {
            sendResponse(RendezvousProtocol::buildError("房间或设备数量已达上限"));
            break;
        }
        sendResponse(RendezvousProtocol::buildRegisterAck(peer.ttlSeconds > 0 ? peer.ttlSeconds : 30));
        break;
    }

    case RendezvousProtocol::MessageType::ListPeers: {
        const std::string deviceId = RendezvousProtocol::extractDeviceId(json);
        std::vector<OnlineRegistry::PeerInfo> peers = _registry->peersForRoom(room);

        // 过滤掉请求者自身
        peers.erase(std::remove_if(peers.begin(), peers.end(),
                                   [&deviceId](const OnlineRegistry::PeerInfo &p) {
                                       return p.deviceId == deviceId;
                                   }),
                    peers.end());

        sendResponse(RendezvousProtocol::buildPeersResponse(peers));
        break;
    }

    case RendezvousProtocol::MessageType::RelayInvite: {
        OnlineRegistry::RelayInvite invite = RendezvousProtocol::extractRelayInvite(json);
        if (invite.relayId.empty() || invite.targetDeviceId.empty()) {
            sendResponse(RendezvousProtocol::buildError("中继邀请缺少 relay_id 或 target_device_id"));
            break;
        }
        // 目标不在线时直接拒绝，发送端可立即改走失败提示而不是干等超时
        if (!_registry->hasPeer(room, invite.targetDeviceId)) {
            sendResponse(RendezvousProtocol::buildError("目标设备不在线或未注册"));
            break;
        }
        _registry->addRelayInvite(room, invite);
        sendResponse(RendezvousProtocol::buildRelayInviteAck(invite.relayId));
        break;
    }

    case RendezvousProtocol::MessageType::RelayPoll: {
        const std::string deviceId = RendezvousProtocol::extractDeviceId(json);
        if (deviceId.empty()) {
            sendResponse(RendezvousProtocol::buildError("缺少 device_id"));
            break;
        }
        // 邀请一次性消费，领取后目标设备凭 relay_id 建立中继连接
        const std::vector<OnlineRegistry::RelayInvite> invites =
            _registry->consumeRelayInvites(room, deviceId);
        sendResponse(RendezvousProtocol::buildRelayInvites(invites));
        break;
    }

    default:
        sendResponse(RendezvousProtocol::buildError("不支持的消息类型"));
        break;
    }
}

// 发送 JSON 响应
void RendezvousSession::sendResponse(const nlohmann::json &json)
{
    if (!socket()) {
        return;  // 会话已移交或断开
    }
    // 写队列积压说明客户端消费过慢，断开以保护服务端内存
    if (socket()->bytesToWrite() >= _maxResponseQueueBytes) {
        logWarn("[RendezvousSession] 响应写积压超过上限，断开连接");
        socket()->abort();
        return;
    }
    const std::string data = json.dump() + '\n';
    socket()->write(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

// -------------------- RendezvousServer --------------------

// 构造函数
RendezvousServer::RendezvousServer(uint16_t port, const std::string &token,
                                   int maxSessions, gy::net::NetReactor *reactor)
    : _port{port}
    , _token{token}
    , _maxSessions{maxSessions > 0 ? maxSessions : kMaxRendezvousSessions}
    , _handshakeTimeoutMs{kRendezvousHandshakeTimeoutMs}
    , _idleTimeoutMs{kRendezvousIdleTimeoutMs}
{
    if (reactor) {
        _reactor = reactor;
    } else {
        _ownReactor = std::make_unique<gy::net::NetReactor>();
        _reactor = _ownReactor.get();
        _ownReactor->start();
    }
    _registry = std::make_unique<OnlineRegistry>(30);
    _server = std::make_unique<gy::net::TcpServerCore>(*_reactor);
}

// 析构函数：停机清理并回收自建事件泵
RendezvousServer::~RendezvousServer()
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

// 启动监听服务
bool RendezvousServer::start()
{
    if (!_server->listen(_port, true)) {
        _listenError = _server->errorString();
        logWarn("[RendezvousServer] 监听启动失败: " + _listenError);
        return false;
    }
    _listenError.clear();
    _port = _server->serverPort();

    logInfo("[RendezvousServer] 监听端口 " + std::to_string(_server->serverPort()));
    _server->setConnectionCallback(
        [this](int descriptor, const std::string &peerAddress) {
            handleNewConnection(descriptor, peerAddress);
        });

    schedulePrune();
    return true;
}

// 停止监听服务（存量会话保持到自然终结）
void RendezvousServer::stop()
{
    runOnLoop([this]() {
        _server->close();
        _reactor->cancelTimer(_pruneTimer);
        _pruneTimer = 0;
    });
    logInfo("[RendezvousServer] 已停止");
}

// 是否正在监听
bool RendezvousServer::isListening() const
{
    return _server->isListening();
}

// 获取服务监听端口
uint16_t RendezvousServer::serverPort() const
{
    return _server->serverPort();
}

// 调整会话握手与空闲超时
void RendezvousServer::setSessionTimeouts(int handshakeTimeoutMs, int idleTimeoutMs)
{
    runOnLoop([this, handshakeTimeoutMs, idleTimeoutMs]() {
        _handshakeTimeoutMs = handshakeTimeoutMs;
        _idleTimeoutMs = idleTimeoutMs;
    });
}

// 调整注册表房间数与每房设备数上限
void RendezvousServer::setRegistryLimits(int maxRooms, int maxDevicesPerRoom)
{
    runOnLoop([this, maxRooms, maxDevicesPerRoom]() {
        _registry->setRegistryLimits(maxRooms, maxDevicesPerRoom);
    });
}

// 调整过期数据清理周期
void RendezvousServer::setPruneIntervalMs(int intervalMs)
{
    if (intervalMs > 0) {
        runOnLoop([this, intervalMs]() { _pruneIntervalMs = intervalMs; });
    }
}

// 调整响应写队列上限
void RendezvousServer::setMaxResponseQueueBytes(int64_t maxBytes)
{
    if (maxBytes > 0) {
        runOnLoop([this, maxBytes]() { _maxResponseQueueBytes = maxBytes; });
    }
}

// 装配中继管道移交回调
void RendezvousServer::setRelayPipeHandler(RelayPipeHandler handler)
{
    _relayPipeHandler = std::move(handler);
}

// 装配客户端接入通知
void RendezvousServer::setClientConnectedHandler(std::function<void(const std::string &)> handler)
{
    _clientConnected = std::move(handler);
}

// 装配客户端离开通知
void RendezvousServer::setClientDisconnectedHandler(std::function<void(const std::string &)> handler)
{
    _clientDisconnected = std::move(handler);
}

// 接入新连接（事件泵线程）
void RendezvousServer::handleNewConnection(int descriptor, const std::string &peerAddress)
{
    // 并发会话达到上限时直接拒绝，防止连接堆积拖垮服务
    if (static_cast<int>(_sessions.size()) >= _maxSessions) {
        logWarn("[RendezvousServer] 会话数达到上限 " + std::to_string(_maxSessions) + "，拒绝新连接");
        gy::net::TcpSocketCore rejected{*_reactor};
        rejected.adopt(descriptor);
        rejected.abort();
        return;
    }

    logInfo("[RendezvousServer] 新连接来自 " + peerAddress);
    if (_clientConnected) {
        _clientConnected(peerAddress);
    }

    auto *socket = new gy::net::TcpSocketCore{*_reactor};
    socket->adopt(descriptor);
    auto *session = new RendezvousSession{*_reactor, socket, _registry.get(), _token,
                                          _handshakeTimeoutMs, _idleTimeoutMs};
    session->setMaxResponseQueueBytes(_maxResponseQueueBytes);
    session->setRelayPipeHandler(
        [this](gy::net::TcpSocketCore *pipeSocket, const nlohmann::json &hello,
               const std::vector<uint8_t> &pendingData) {
            dispatchRelayPipe(pipeSocket, hello, pendingData);
        });
    const std::string address = peerAddress;
    session->setClosedHandler([this, session, address]() { handleSessionClosed(session, address); });
    _sessions.push_back(session);

    session->start();
}

// 会话终结回收（事件泵线程）
void RendezvousServer::handleSessionClosed(RendezvousSession *session, const std::string &address)
{
    _sessions.erase(std::remove(_sessions.begin(), _sessions.end(), session), _sessions.end());
    if (_clientDisconnected) {
        _clientDisconnected(address);
    }
    retireLater(session);
}

// 派发中继管道移交（事件泵线程）
void RendezvousServer::dispatchRelayPipe(gy::net::TcpSocketCore *socket, const nlohmann::json &hello,
                                         const std::vector<uint8_t> &pendingData)
{
    if (_relayPipeHandler) {
        _relayPipeHandler(socket, hello, pendingData);
    }
}

// 延迟析构登记
void RendezvousServer::retireLater(RendezvousSession *session)
{
    if (_retired.count(session)) {
        return;  // 同一对象只登记一次
    }
    _retired[session] = _reactor->scheduleAfter(0, [this, session]() { reapRetired(session); });
}

// 回收延迟析构的会话
void RendezvousServer::reapRetired(RendezvousSession *session)
{
    const auto it = _retired.find(session);
    if (it == _retired.end()) {
        return;  // 已在停机清理中回收
    }
    _retired.erase(it);
    delete session;
}

// 周期清理过期设备与邀请（自重排，新周期时长即时生效）
void RendezvousServer::schedulePrune()
{
    _reactor->cancelTimer(_pruneTimer);
    _pruneTimer = _reactor->scheduleAfter(_pruneIntervalMs, [this]() {
        _registry->pruneExpired();
        schedulePrune();
    });
}

// 停机清理
void RendezvousServer::teardown()
{
    _reactor->cancelTimer(_pruneTimer);
    _pruneTimer = 0;
    _server->close();

    for (const auto &entry : _retired) {
        _reactor->cancelTimer(entry.second);
        delete entry.first;
    }
    _retired.clear();

    for (RendezvousSession *session : _sessions) {
        delete session;
    }
    _sessions.clear();
}

// 跨线程入口统一走事件泵同步执行
void RendezvousServer::runOnLoop(std::function<void()> task)
{
    _reactor->dispatchSync(std::move(task));
}
