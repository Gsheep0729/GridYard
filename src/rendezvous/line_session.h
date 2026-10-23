/**
* @file    line_session.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   带上限的按行分帧会话基类
*
* 承载"按 \n 分帧的控制行"这一共同模式：单行长度上限、首行握手
* 超时、逐行刷新的空闲超时与断开清理，供协调会话和中继握手等待复用。
* 会话持有 socket 的归属，终结时统一回收；超时由事件泵定时任务驱动，
* 全部回调在事件泵线程触发。
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/net_reactor.h"
#include "core/tcp_socket_core.h"

class LineSession {
public:
    // 收到一条完整控制行
    using LineHandler = std::function<void(const std::string &line)>;
    // 行超长、超时或 socket 错误，会话将断开
    using ErrorHandler = std::function<void(const std::string &reason)>;
    // 会话终结（保证只通知一次），持有方据此回收会话对象
    using ClosedHandler = std::function<void()>;

    // 会话接管 socket 归属；maxLineBytes 为单行长度上限
    LineSession(gy::net::NetReactor &reactor, gy::net::TcpSocketCore *socket,
                std::ptrdiff_t maxLineBytes);
    virtual ~LineSession();

    LineSession(const LineSession &)            = delete;
    LineSession &operator=(const LineSession &) = delete;

    // 启动首行握手超时：期限内未收到完整控制行则断开
    void startHandshakeTimeout(int ms);
    // 启动空闲超时：每收到一行自动刷新
    void startIdleTimeout(int ms);
    // 服务端主动以协议错误终结会话（如握手内容非法）
    void finishWithError(const std::string &reason);

    // 会话持有的连接；已移交或终结后返回 nullptr
    gy::net::TcpSocketCore *socket() const { return _socket; }
    // 握手行之后已到达的剩余字节（中继移交时一并转交）
    std::vector<uint8_t> pendingBytes() const;
    // 剥离 socket 归属并返回（连接移交场景）；此后会话不再触达该 socket
    gy::net::TcpSocketCore *takeSocket();
    // 会话是否已终结（终结后不再发任何回调）
    bool isFinished() const { return _finished; }

    void setLineHandler(LineHandler cb) { _onLine = std::move(cb); }
    void setErrorHandler(ErrorHandler cb) { _onError = std::move(cb); }
    void setClosedHandler(ClosedHandler cb) { _onClosed = std::move(cb); }

protected:
    // 统一终结出口：断开 socket 并只通知一次 closed（子类移交后调用）
    void finish();
    // 移交后无接收方的兜底回收：延迟到回调栈 unwind 后析构
    void deferDeleteSocket(gy::net::TcpSocketCore *socket);

private:
    // socket 可读：按 \n 切分控制行并施加长度上限
    void onSocketRead();
    // socket 错误说明（终结回调之前触发）
    void onSocketError(const std::string &message);
    // socket 断开，统一终结出口
    void onSocketClosed();
    // 重置当前超时任务（覆盖旧任务与原因）
    void armTimeout(int ms, std::string reason);

    gy::net::NetReactor &_reactor;          // 所属事件泵
    gy::net::TcpSocketCore *_socket = nullptr;  // 会话持有的连接
    gy::net::NetReactor::TimerId _timeoutTimer = 0;  // 握手/空闲共用超时任务句柄
    int _timeoutIntervalMs = 0;             // 当前超时时长
    std::string _timeoutReason;             // 当前超时到期的原因描述
    std::vector<uint8_t> _buffer;           // 半行缓冲
    std::ptrdiff_t _maxLineBytes = 0;       // 单行长度上限
    bool _handshakePhase = false;           // 是否仍处于首行握手阶段
    bool _finished = false;                 // 是否已终结
    bool _errorSignaled = false;            // 是否已报告协议错误，避免关闭过程中的重复错误
    bool _detached = false;                 // socket 是否已移交（移交后停止一切处理）

    LineHandler _onLine;                    // 行到达回调
    ErrorHandler _onError;                  // 协议错误回调
    ClosedHandler _onClosed;                // 终结回调
};
