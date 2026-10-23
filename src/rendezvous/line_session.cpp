/**
* @file    line_session.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   带上限的按行分帧会话基类实现
*/

#include "line_session.h"

#include "rendezvous_log.h"

#include <algorithm>

// 构造函数：接管 socket 并启动分帧读取
LineSession::LineSession(gy::net::NetReactor &reactor, gy::net::TcpSocketCore *socket,
                         std::ptrdiff_t maxLineBytes)
    : _reactor{reactor}
    , _socket{socket}
    , _maxLineBytes{maxLineBytes}
{
    // 内部读缓冲随行上限封顶，慢连接在内核层形成背压，内存不会无界增长
    _socket->setReadBufferLimit(maxLineBytes * 2);

    _socket->setReadCallback([this]() { onSocketRead(); });
    _socket->setCloseCallback([this](gy::net::SocketCloseReason) { onSocketClosed(); });
    _socket->setErrorCallback([this](const std::string &message) { onSocketError(message); });
}

// 析构函数：注销超时任务并回收仍持有的 socket
LineSession::~LineSession()
{
    _reactor.cancelTimer(_timeoutTimer);
    if (_socket) {
        _socket->setReadCallback(nullptr);
        _socket->setWrittenCallback(nullptr);
        _socket->setCloseCallback(nullptr);
        _socket->setErrorCallback(nullptr);
        delete _socket;
        _socket = nullptr;
    }
}

// 启动首行握手超时
void LineSession::startHandshakeTimeout(int ms)
{
    _handshakePhase = true;
    armTimeout(ms, "握手超时");
}

// 启动空闲超时
void LineSession::startIdleTimeout(int ms)
{
    _handshakePhase = false;
    armTimeout(ms, "空闲超时");
}

// 服务端主动以协议错误终结会话
void LineSession::finishWithError(const std::string &reason)
{
    if (_finished || _detached || _errorSignaled) {
        return;
    }
    _errorSignaled = true;
    gy::rendezvous::logWarn("[LineSession] 终结会话: " + reason);
    if (_onError) {
        _onError(reason);
    }
    if (_socket) {
        _socket->disconnectFromHost();
    }
}

// 握手行之后已到达的剩余字节
std::vector<uint8_t> LineSession::pendingBytes() const
{
    return _buffer;
}

// 剥离 socket 归属并返回（连接移交场景）
gy::net::TcpSocketCore *LineSession::takeSocket()
{
    if (!_socket) {
        return nullptr;
    }
    gy::net::TcpSocketCore *socket = _socket;
    socket->setReadCallback(nullptr);
    socket->setWrittenCallback(nullptr);
    socket->setCloseCallback(nullptr);
    socket->setErrorCallback(nullptr);
    _socket = nullptr;
    _detached = true;
    return socket;
}

// socket 可读：按 \n 切分控制行并施加长度上限
void LineSession::onSocketRead()
{
    if (_finished || _detached || !_socket) {
        return;
    }

    const std::vector<uint8_t> chunk = _socket->readAll();
    _buffer.insert(_buffer.end(), chunk.begin(), chunk.end());

    auto newline = std::find(_buffer.begin(), _buffer.end(), uint8_t('\n'));
    while (newline != _buffer.end()) {
        const std::string line{_buffer.begin(), newline};
        _buffer.erase(_buffer.begin(), newline + 1);

        if (static_cast<std::ptrdiff_t>(line.size()) > _maxLineBytes) {
            finishWithError("控制行超长");
            return;
        }

        // 首行到达即退出握手阶段；空闲超时随每行刷新
        if (_handshakePhase) {
            _handshakePhase = false;
            _reactor.cancelTimer(_timeoutTimer);
            _timeoutTimer = 0;
        } else if (_timeoutTimer != 0) {
            armTimeout(_timeoutIntervalMs, _timeoutReason);
        }

        if (_onLine) {
            _onLine(line);
        }
        if (_finished || _detached) {
            return;  // 行处理中可能移交或终结了会话
        }

        newline = std::find(_buffer.begin(), _buffer.end(), uint8_t('\n'));
    }

    // 尚未凑齐一行的数据同样不得超过行上限
    if (static_cast<std::ptrdiff_t>(_buffer.size()) > _maxLineBytes) {
        finishWithError("控制行超长");
    }
}

// socket 错误说明（对端正常关闭由 close 回调收尾，不视为错误）
void LineSession::onSocketError(const std::string &message)
{
    if (!_finished && !_detached) {
        finishWithError(message);
    }
}

// socket 断开，统一终结出口
void LineSession::onSocketClosed()
{
    finish();
}

// 统一终结出口：断开 socket 并只通知一次 closed
void LineSession::finish()
{
    if (_finished) {
        return;
    }
    _finished = true;
    _reactor.cancelTimer(_timeoutTimer);
    _timeoutTimer = 0;
    if (_socket) {
        _socket->setReadCallback(nullptr);
        _socket->setWrittenCallback(nullptr);
        _socket->setCloseCallback(nullptr);
        _socket->setErrorCallback(nullptr);
    }
    if (_onClosed) {
        _onClosed();
    }
}

// 重置当前超时任务
void LineSession::armTimeout(int ms, std::string reason)
{
    _reactor.cancelTimer(_timeoutTimer);
    _timeoutIntervalMs = ms;
    _timeoutReason = std::move(reason);
    const std::string &fireReason = _timeoutReason;
    _timeoutTimer = _reactor.scheduleAfter(ms, [this, fireReason]() {
        _timeoutTimer = 0;
        finishWithError(fireReason);
    });
}

// 移交后无接收方的兜底回收
void LineSession::deferDeleteSocket(gy::net::TcpSocketCore *socket)
{
    if (!socket) {
        return;
    }
    _reactor.scheduleAfter(0, [socket]() { delete socket; });
}
