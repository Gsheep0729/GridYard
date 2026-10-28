/**
 * @file    rendezvous_server.h
 * @version 8.2.0
 * @date    2026-10-29
 * @author  GridYard Team
 * @brief   协调节点服务器
 *
 * 使用纯 POSIX 套接字监听连接，处理 Register 和 ListPeers 请求，
 * 并提供中继邀请（relay_invite / relay_poll）信令。
 * 首行类型为 relay_create / relay_join 的连接剥离握手后移交中继服务器，
 * 使协调与中继可共用同一端口。
 * 会话继承 LineSession，具备行长度上限、握手/空闲超时与连接数上限；
 * 全部回调在事件泵线程触发，跨线程入口经事件泵同步执行。
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/net_reactor.h"
#include "core/tcp_server_core.h"
#include "core/tcp_socket_core.h"
#include "line_session.h"
#include "rendezvous_limits.h"

class OnlineRegistry;
class RendezvousProtocol;

class RendezvousSession : public LineSession {
public:
    // 中继管道连接移交契约：socket 所有权移交给接收方，移交前已剥离首行握手，
    // 接收方需接管 pendingData 未消费预读缓冲
    using RelayPipeHandler =
        std::function<void(gy::net::TcpSocketCore *socket, const nlohmann::json &hello,
                           const std::vector<uint8_t> &pendingData)>;

    // 构造协调会话并接管已接入 socket（注册表与鉴权令牌由服务端注入）
    RendezvousSession(gy::net::NetReactor &reactor, gy::net::TcpSocketCore *socket,
                      OnlineRegistry *registry, const std::string &token,
                      int handshakeTimeoutMs, int idleTimeoutMs);

    // 启动会话：启动首行握手超时
    void start();
    // 调整响应写队列上限（测试可调小）
    void setMaxResponseQueueBytes(int64_t maxBytes);
    // 中继管道移交回调（接收方在回调内接管 socket 所有权）
    void setRelayPipeHandler(RelayPipeHandler handler) { _relayPipeHandler = std::move(handler); }

private:
    // 处理一条控制行：刷新空闲超时并按 JSON 解析
    void processLine(const std::string &line);
    // 处理 JSON 请求消息
    void processRequest(const nlohmann::json &json);
    // 发送 JSON 响应
    void sendResponse(const nlohmann::json &json);

    OnlineRegistry *_registry = nullptr;  // 设备在线注册表实例指针
    std::string _token;                   // 访问鉴权令牌
    int _handshakeTimeoutMs = 0;          // 首行握手超时毫秒数
    int _idleTimeoutMs = 0;               // 会话空闲超时毫秒数
    int64_t _maxResponseQueueBytes = gy::rendezvous::kMaxResponseWriteQueueBytes;  // 响应写积压上限
    RelayPipeHandler _relayPipeHandler;   // 中继管道移交回调
};

class RendezvousServer {
public:
    // 会话移交中继管道契约：向接入方派发已剥离握手的 socket 与未消费缓冲，
    // 接收方负责管理后续生命周期
    using RelayPipeHandler = RendezvousSession::RelayPipeHandler;

    // reactor 为空时自建独立事件泵（后台线程驱动），否则与调用方共用
    explicit RendezvousServer(uint16_t port, const std::string &token,
                              int maxSessions = -1, gy::net::NetReactor *reactor = nullptr);
    // 析构：执行停机清理；自建事件泵时一并停止
    ~RendezvousServer();

    RendezvousServer(const RendezvousServer &)            = delete;
    RendezvousServer &operator=(const RendezvousServer &) = delete;

    // 启动协调监听（双栈；失败原因见 errorString）
    bool start();
    // 停止监听并取消过期清理定时器
    void stop();
    // 是否正在监听
    bool isListening() const;
    // 实际监听端口
    uint16_t serverPort() const;
    // 服务使用的活动泵（自建或外部注入），供同进程复用（如同端口中继）
    gy::net::NetReactor *reactor() const { return _reactor; }
    // 最近一次监听失败原因
    const std::string &errorString() const { return _listenError; }

    // 调整会话握手与空闲超时（测试可调小；对新建立的会话生效）
    void setSessionTimeouts(int handshakeTimeoutMs, int idleTimeoutMs);
    // 调整注册表房间数与每房设备数上限（测试可调小）
    void setRegistryLimits(int maxRooms, int maxDevicesPerRoom);
    // 调整待领取中继邀请的登记上限（测试可调小）
    void setMaxRelayInvites(int maxInvites);
    // 调整过期数据清理周期（测试可调小；下一周期生效）
    void setPruneIntervalMs(int intervalMs);
    // 调整响应写队列上限（测试可调小；对新建立的会话生效）
    void setMaxResponseQueueBytes(int64_t maxBytes);

    // 中继管道移交回调（同端口复用场景的装配入口）
    void setRelayPipeHandler(RelayPipeHandler handler);
    // 客户端接入/离开通知（携带对端地址文本）
    void setClientConnectedHandler(std::function<void(const std::string &)> handler);
    void setClientDisconnectedHandler(std::function<void(const std::string &)> handler);

private:
    // 接入新连接（事件泵线程）
    void handleNewConnection(int descriptor, const std::string &peerAddress);
    // 会话终结回收（事件泵线程）
    void handleSessionClosed(RendezvousSession *session, const std::string &address);
    // 派发中继管道移交（事件泵线程）
    void dispatchRelayPipe(gy::net::TcpSocketCore *socket, const nlohmann::json &hello,
                           const std::vector<uint8_t> &pendingData);
    // 延迟析构登记：回调栈 unwind 后再回收对象
    void retireLater(RendezvousSession *session);
    void reapRetired(RendezvousSession *session);
    // 周期清理过期设备与邀请（自重排）
    void schedulePrune();
    // 停机清理：注销监听与定时任务，回收全部会话
    void teardown();
    // 跨线程入口统一走事件泵同步执行
    void runOnLoop(std::function<void()> task);

    std::unique_ptr<gy::net::NetReactor> _ownReactor;  // 自建事件泵（外部注入时为空）
    gy::net::NetReactor *_reactor = nullptr;           // 服务使用的活动泵
    std::unique_ptr<gy::net::TcpServerCore> _server;   // 原生 TCP 监听核心
    std::unique_ptr<OnlineRegistry> _registry;         // 内存设备在线注册表
    std::vector<RendezvousSession *> _sessions;        // 当前活跃会话生命周期列表
    std::unordered_map<RendezvousSession *, gy::net::NetReactor::TimerId> _retired;  // 待回收会话及延迟任务
    uint16_t _port = 0;                  // 服务监听端口号
    std::string _token;                  // 访问鉴权令牌
    std::string _listenError;            // 最近一次监听失败原因
    bool _tearingDown = false;           // 停机清理标志（置位后接入完成直接回收描述符）
    int _maxSessions = 0;                // 并发会话上限
    int _handshakeTimeoutMs = 0;         // 首行握手超时
    int _idleTimeoutMs = 0;              // 会话空闲超时
    int _pruneIntervalMs = gy::rendezvous::kRegistryPruneIntervalMs;  // 过期清理周期
    int64_t _maxResponseQueueBytes = gy::rendezvous::kMaxResponseWriteQueueBytes;  // 响应写积压上限
    gy::net::NetReactor::TimerId _pruneTimer = 0;  // 周期清理任务句柄
    RelayPipeHandler _relayPipeHandler;  // 中继管道移交回调
    std::function<void(const std::string &)> _clientConnected;     // 客户端接入通知
    std::function<void(const std::string &)> _clientDisconnected;  // 客户端离开通知
};
