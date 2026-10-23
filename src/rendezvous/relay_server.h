/**
 * @file    relay_server.h
 * @version 7.26.1
 * @date    2026-10-24
 * @author  GridYard Team
 * @brief   流式中继服务器
 *
 * 处理最严格的校园网隔离场景，两个客户端可都无法直连时通过中继转发。
 * 中继只转发在线字节流，不落盘文件内容。
 * 两端齐备后向发送端回一行 relay_ready 门控传输开始，任一端离开立即
 * 通知并关闭对端，未完成会话超时回收。
 * 握手等待复用 LineSession（行上限 + 超时），转发带写队列背压，
 * 会话数与积压均有上限；全部回调在事件泵线程触发。
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/net_reactor.h"
#include "core/tcp_server_core.h"
#include "core/tcp_socket_core.h"
#include "line_session.h"
#include "rendezvous_limits.h"

class RelaySession {
public:
    // 会话关闭通知入口（由服务端装配；同一会话可多次触发，接收方按对象判等）
    using ClosedNotify = std::function<void(RelaySession *session)>;
    // 连接延迟回收委托（摘除归属后排空写队列再删除）
    using CloseDelegator = std::function<void(gy::net::TcpSocketCore *socket)>;

    RelaySession(const std::string &relayId, int waitMs, gy::net::NetReactor &reactor,
                 ClosedNotify onClosed, CloseDelegator closeDelegator);
    ~RelaySession();

    RelaySession(const RelaySession &)            = delete;
    RelaySession &operator=(const RelaySession &) = delete;

    const std::string &relayId() const { return _relayId; }
    bool isComplete() const;  // 两端都连接完成
    bool addSender(gy::net::TcpSocketCore *socket, const std::vector<uint8_t> &pendingData = {});
    bool addReceiver(gy::net::TcpSocketCore *socket, const std::vector<uint8_t> &pendingData = {});
    // 两端齐备后向发送端写 relay_ready，门控其开始 TLV 传输
    void notifyReady();
    // 向两端广播错误消息
    void broadcastError(const std::string &code, const std::string &message);
    // 主动结束会话：向两端广播错误并关闭连接
    void abort(const std::string &code, const std::string &message);
    // 发出会话关闭通知（正常关闭与测试注入的迟到重复通知共用入口）
    void notifyClosed();

private:
    // 处理发送端可读事件，向接收端方向转发
    void onSenderRead();
    // 处理接收端可读事件，向发送端方向转发
    void onReceiverRead();
    // 发送端断开：广播错误并关闭对端
    void onSenderClosed();
    // 接收端断开：广播错误并关闭对端
    void onReceiverClosed();
    // 把 from 缓冲里的字节向 to 转发；对端写队列超限时暂停，等待排空续传
    void pump(gy::net::TcpSocketCore *from, gy::net::TcpSocketCore *to);
    // 会话未齐备时缓存积压字节，超限返回 false
    bool appendBacklog(std::vector<uint8_t> &backlog, const std::vector<uint8_t> &data);
    // 装配一端连接的全部回调
    void wireSocket(gy::net::TcpSocketCore *socket, bool isSender);

    std::string _relayId;                             // 中继会话唯一标识符
    gy::net::TcpSocketCore *_sender = nullptr;        // 发送端连接核心指针
    gy::net::TcpSocketCore *_receiver = nullptr;      // 接收端连接核心指针
    std::vector<uint8_t> _senderBacklog;              // 会话未齐备前缓存的发送端字节
    std::vector<uint8_t> _receiverBacklog;            // 会话未齐备前缓存的接收端字节
    gy::net::NetReactor &_reactor;                    // 所属事件泵
    gy::net::NetReactor::TimerId _idleTimer = 0;      // 会话未齐备时的超时回收任务句柄
    ClosedNotify _onClosed;                           // 会话关闭通知入口
    CloseDelegator _closeDelegator;                   // 连接延迟回收委托
};

class RelayServer {
public:
    // reactor 为空时自建独立事件泵（后台线程驱动），否则与调用方共用
    // （协调节点同端口复用场景注入协调服务的事件泵）
    explicit RelayServer(uint16_t port, const std::string &token,
                         gy::net::NetReactor *reactor = nullptr);
    ~RelayServer();

    RelayServer(const RelayServer &)            = delete;
    RelayServer &operator=(const RelayServer &) = delete;

    bool start();
    void stop();
    uint16_t serverPort() const;
    int sessionCount() const;
    // 服务使用的活动泵（自建或外部注入）
    gy::net::NetReactor *reactor() const { return _reactor; }
    // 最近一次监听失败原因
    const std::string &errorString() const { return _listenError; }
    // 调整并发中继会话上限（对新建会话生效）
    void setMaxSessions(int maxSessions);
    // 调整会话等待对端加入的超时（测试可调小；对新建会话生效）
    void setSessionWaitMs(int waitMs);

    // 接纳已由其他监听方完成首行握手的连接（协调节点同端口复用场景；
    // 任意线程可调用，socket 所有权转移给本服务）
    void adoptConnection(gy::net::TcpSocketCore *socket, const nlohmann::json &hello,
                         const std::vector<uint8_t> &pendingData);

    // 会话创建/关闭通知（携带会话对象，relay_id 可由会话取得）
    void setSessionCreatedHandler(std::function<void(RelaySession *session)> handler);
    void setSessionClosedHandler(std::function<void(RelaySession *session)> handler);

    // 会话关闭事件入口：会话回调与外部注入（迟到重复通知）共用，
    // 按对象指针判等，relay_id 复用窗口内不得误删新会话
    void handleSessionClosed(RelaySession *session);

private:
    // 接入新连接：握手等待交给 LineSession（行上限 + 超时）
    void handleNewConnection(int descriptor, const std::string &peerAddress);
    // 解析中继握手并按角色加入会话（事件泵线程）
    void handleRelayHello(gy::net::TcpSocketCore *socket, const nlohmann::json &json,
                          const std::vector<uint8_t> &pendingData);
    // 丢弃未完成握手的连接：排空错误帧后延迟回收
    void dropConnection(gy::net::TcpSocketCore *socket, const std::string &reason);
    // 摘除归属后先排空写队列，断开完成或 1 秒超时后再回收
    void closeSocketLater(gy::net::TcpSocketCore *socket);
    // 回收排空完成的连接（延迟析构，避免回调栈内自毁）
    void reapClosingSocket(gy::net::TcpSocketCore *socket);
    // 延迟析构登记（会话与握手等待对象共用）
    void retireLater(void *object, std::function<void()> deleter);
    // 停机清理：注销监听，回收会话、握手等待与排空中的连接
    void teardown();
    // 跨线程入口统一走事件泵同步执行
    void runOnLoop(std::function<void()> task);

    std::unique_ptr<gy::net::NetReactor> _ownReactor;  // 自建事件泵（外部注入时为空）
    gy::net::NetReactor *_reactor = nullptr;           // 服务使用的活动泵
    std::unique_ptr<gy::net::TcpServerCore> _server;   // 原生 TCP 监听核心
    uint16_t _port = 0;                                // 监听端口号
    std::string _token;                                // 访问鉴权令牌
    std::string _listenError;                          // 最近一次监听失败原因
    std::map<std::string, RelaySession *> _sessions;   // 当前活跃的中继转发会话映射（relayId -> session）
    std::unordered_set<LineSession *> _pendingHellos;  // 等待首行握手的连接集合
    std::atomic<int> _sessionCount{0};                 // 活跃会话数量（跨线程可读）
    int _maxSessions = 0;                              // 最大允许的并发中继会话数量
    int _sessionWaitMs = gy::rendezvous::kRelaySessionWaitMs;  // 新建会话等待对端的超时
    // 延迟析构登记：对象指针 → {延迟任务句柄, 析构器}
    std::unordered_map<void *, std::pair<gy::net::NetReactor::TimerId, std::function<void()>>> _retired;
    // 排空中的连接：socket → 兜底超时任务句柄
    std::unordered_map<gy::net::TcpSocketCore *, gy::net::NetReactor::TimerId> _closingSockets;
    bool _tearingDown = false;                        // 停机清理标志（置位后连接直接回收）
    std::function<void(RelaySession *)> _sessionCreatedHandler;  // 会话创建通知
    std::function<void(RelaySession *)> _sessionClosedHandler;   // 会话关闭通知
};
