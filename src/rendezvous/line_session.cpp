/**
* @file    line_session.cpp
* @version 7.12.0
* @date    2026-10-02
* @author  GridYard Team
* @brief   带上限的按行分帧会话基类实现
*
* Change Log:
* [v7.12.0] GY   2026-10-02
* * 自协调会话与中继握手的共同逻辑抽出分帧会话基类
*/

#include "line_session.h"

#include <QDebug>

// 构造函数：接管 socket 并启动分帧读取
LineSession::LineSession(QTcpSocket *socket, qsizetype maxLineBytes, QObject *parent)
    : QObject{parent}
    , _socket{socket}
    , _maxLineBytes{maxLineBytes}
{
    _socket->setParent(this);
    // 内部读缓冲随行上限封顶，慢连接在内核层形成背压，内存不会无界增长
    _socket->setReadBufferSize(maxLineBytes * 2);

    _timeoutTimer = new QTimer{this};
    _timeoutTimer->setSingleShot(true);
    connect(_timeoutTimer, &QTimer::timeout, this, [this]() {
        finishWithError(_timeoutReason);
    });

    connect(_socket, &QTcpSocket::readyRead, this, &LineSession::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected, this, &LineSession::onDisconnected);
    connect(_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        // 远端正常关闭不算错误，由 disconnected 路径终结
        if (error != QAbstractSocket::RemoteHostClosedError && !_finished && !_detached) {
            finishWithError(_socket->errorString());
        }
    });
}

// 析构函数：socket 作为子对象随之回收
LineSession::~LineSession() = default;

// 启动首行握手超时
void LineSession::startHandshakeTimeout(int ms)
{
    _handshakePhase = true;
    _timeoutReason = QStringLiteral("握手超时");
    _timeoutTimer->start(ms);
}

// 启动空闲超时：每收到一行自动刷新
void LineSession::startIdleTimeout(int ms)
{
    _handshakePhase = false;
    _timeoutReason = QStringLiteral("空闲超时");
    _timeoutTimer->start(ms);
}

// 服务端主动以协议错误终结会话
void LineSession::finishWithError(const QString &reason)
{
    if (_finished || _detached) {
        return;
    }
    qWarning() << "[LineSession] 终结会话:" << reason;
    emit protocolError(reason);
    _socket->disconnectFromHost();
}

// 握手行之后已到达的剩余字节
QByteArray LineSession::pendingBytes() const
{
    return _buffer;
}

// 剥离 socket 归属并返回（连接移交场景）
QTcpSocket *LineSession::takeSocket()
{
    if (!_socket) {
        return nullptr;
    }
    disconnect(_socket, nullptr, this, nullptr);
    _socket->setParent(nullptr);
    QTcpSocket *socket = _socket;
    _socket = nullptr;
    _detached = true;
    return socket;
}

// 处理可读数据，按 \n 切分控制行并施加长度上限
void LineSession::onReadyRead()
{
    if (_finished || _detached) {
        return;
    }

    _buffer += _socket->readAll();

    int newlineIndex;
    while ((newlineIndex = _buffer.indexOf('\n')) >= 0) {
        const QByteArray line = _buffer.left(newlineIndex);
        _buffer.remove(0, newlineIndex + 1);

        if (line.size() > _maxLineBytes) {
            finishWithError(QStringLiteral("控制行超长"));
            return;
        }

        // 首行到达即退出握手阶段；空闲超时随每行刷新
        if (_handshakePhase) {
            _handshakePhase = false;
            _timeoutTimer->stop();
        } else if (_timeoutTimer->isActive()) {
            _timeoutTimer->start();
        }

        emit lineReady(line);
        if (_finished || _detached) {
            return;  // 行处理中可能移交或终结了会话
        }
    }

    // 尚未凑齐一行的数据同样不得超过行上限
    if (_buffer.size() > _maxLineBytes) {
        finishWithError(QStringLiteral("控制行超长"));
    }
}

// 处理连接断开，统一终结出口
void LineSession::onDisconnected()
{
    finish();
}

// 统一终结出口：断开 socket 并只发射一次 closed
void LineSession::finish()
{
    if (_finished) {
        return;
    }
    _finished = true;
    _timeoutTimer->stop();
    if (_socket) {
        disconnect(_socket, nullptr, this, nullptr);
    }
    emit closed();
}
