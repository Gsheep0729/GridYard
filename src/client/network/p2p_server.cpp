/**
* @file    p2p_server.cpp
* @version 7.15.17
* @date    2026-10-04
* @author  GridYard Team
* @brief   P2P 文件传输服务器实现
*
* 首帧路由阶段只窥视 socket 中的完整 TLV 帧，不读取其字节。
* 确认 Type 后再把原始 socket 交给文件接收 Worker 或聊天连接处理者，
* 保证目标处理者能自行解析完整首帧。
* 中继降级的连接在完成 relay_join 握手后也进入同一条首帧路由。
*
* Change Log:
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
* [v7.14.0] GY   2026-10-03
* * relay_join 握手行携带访问令牌
* [v7.9.0] GY   2026-07-26
* * 新增 joinRelaySession：中继加入的连接复用首帧路由，等待期放宽到 30 秒
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v5.0.0] FengChunlin   2026-06-23
* * 新增首帧路由和聊天连接交接逻辑
* [v4.16.1] GY   2026-06-21
* * 使用请求快照转发接收信息，删除未使用的 isListening() 访问器
* [v4.15.1] FengChunlin   2026-06-16
* * 删除未使用的 _threads.append 调用，修正析构注释
* [v4.15.0] FengChunlin   2026-06-16
* * 为每个连接创建独立的 QThread，实现接收侧后台化
* * worker + socket 移到后台线程，写盘与 SHA-256 不阻塞 UI
* * transferFinished 信号添加 ErrorCode 参数
* [v4.3.4] GY   2026-05-27
* * Stage 4.3：信号转发添加 totalFiles/totalBytes 参数
* [v0.2.0] FengChunlin   2026-05-03
* * Stage 3：初始版本
*/

#include "p2p_server.h"
#include "rendezvous_protocol_keys.h"
#include "chat_message.h"
#include "config_manager.h"
#include "file_receiver_worker.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>

namespace {

// 直连首帧路由超时
static constexpr int kDefaultFirstFrameTimeoutMs = 8000;
// 中继加入后等待首帧的超时：需覆盖发送端 relay_ready 门控与请求准备时间
static constexpr int kRelayFirstFrameTimeoutMs = 30000;
// 中继服务器连接超时
static constexpr int kRelayConnectTimeoutMs = 5000;
// 同时挂起的中继加入数量上限，防止异常邀请占用连接资源
static constexpr int kMaxPendingRelayJoins = 3;

// 读取 TLV 帧头中的 Type 和 Payload 长度
bool readFrameHeader(const QByteArray &header, quint32 *type, quint32 *payloadLength)
{
    if (header.size() != static_cast<qsizetype>(gy::protocol::kHeaderBytes)) {
        return false;
    }

    QDataStream stream{header};
    stream.setByteOrder(QDataStream::BigEndian);
    stream >> *type;
    stream >> *payloadLength;
    return stream.status() == QDataStream::Ok;
}

}

// 构造函数，创建 TCP 服务器并连接新连接信号
P2pServer::P2pServer(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
    , _server{new QTcpServer{this}}
{
    // 新连接信号
    connect(_server, &QTcpServer::newConnection,
            this,    &P2pServer::onNewConnection);
}

// 析构函数，停止监听并等待所有后台线程退出
P2pServer::~P2pServer()
{
    // 停止服务器监听
    if (_server->isListening()) {
        _server->close();
    }

    // 遍历子对象，停止仍在运行的 QThread，避免析构时警告
    const auto kids = children();
    for (QObject *child : kids) {
        if (auto *thread = qobject_cast<QThread*>(child)) {
            if (thread->isRunning()) {
                thread->quit();
                thread->wait(1000);
            }
        }
    }
}

// 启动 TCP 服务器，监听配置的端口
bool P2pServer::start()
{
    const quint16 port = _config->tcpPort();

    if (_server->listen(QHostAddress::AnyIPv4, port)) {
        qDebug() << "P2pServer: 监听端口" << port << "成功";
        return true;
    } else {
        qWarning() << "P2pServer: 监听端口" << port << "失败:"
                    << _server->errorString();
        return false;
    }
}

// 停止 TCP 服务器监听
void P2pServer::stop()
{
    if (_server->isListening()) {
        _server->close();
        qDebug() << "P2pServer: 已停止监听";
    }
}

// 获取监听端口
quint16 P2pServer::serverPort() const
{
    return _server ? _server->serverPort() : 0;
}

// 通过中继服务器加入指定会话：连接、发送 relay_join 握手后交给首帧路由
void P2pServer::joinRelaySession(const QString &host, quint16 port, const QString &relayId)
{
    if (relayId.isEmpty()) {
        return;
    }

    // 同一 relay_id 只加入一次，避免邀请重复投递引发重复连接
    for (auto it = _relayJoins.cbegin(); it != _relayJoins.cend(); ++it) {
        if (it.value() == relayId) {
            qDebug() << "[P2pServer] 中继会话" << relayId << "已在加入流程中，忽略重复邀请";
            return;
        }
    }
    if (_relayJoins.size() >= kMaxPendingRelayJoins) {
        qWarning() << "[P2pServer] 挂起的中继加入过多，忽略新邀请" << relayId;
        return;
    }

    auto *socket = new QTcpSocket{this};
    auto *timer = new QTimer{socket};
    timer->setSingleShot(true);
    _relayJoins.insert(socket, relayId);

    connect(timer, &QTimer::timeout, this, [this, socket]() {
        failRelayJoin(socket, tr("中继连接超时"));
    });
    connect(socket, &QTcpSocket::connected, this, [this, socket, relayId, timer]() {
        qDebug() << "[P2pServer] 已连接中继服务器，发送 relay_join";
        QJsonObject hello;
        hello[gy::rendezvous::kKeyType] = gy::rendezvous::kTypeRelayJoin;
        hello[gy::rendezvous::kKeyRelayId] = relayId;
        // 服务器未启用认证时令牌为空，字段不发送
        const QString token = _config ? _config->rendezvousToken() : QString();
        if (!token.isEmpty()) {
            hello[gy::rendezvous::kKeyToken] = token;
        }
        const QByteArray line = QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n';
        socket->write(line);

        // 握手行已发出，后续字节就是中继转发的标准 TLV 流；
        // 会话两端齐备后发送端才开始传输，这里放宽等待期
        _relayJoins.remove(socket);
        timer->stop();
        monitorFirstFrame(socket, kRelayFirstFrameTimeoutMs);
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        if (_relayJoins.contains(socket)) {
            failRelayJoin(socket, tr("中继连接已断开"));
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError) {
        if (_relayJoins.contains(socket)) {
            failRelayJoin(socket, socket->errorString());
        }
    });

    timer->start(kRelayConnectTimeoutMs);
    socket->connectToHost(host, port);
}

// 结束一次未完成的中继加入：清理连接并通知失败
void P2pServer::failRelayJoin(QTcpSocket *socket, const QString &reason)
{
    const QString relayId = _relayJoins.take(socket);
    if (relayId.isEmpty()) {
        return;  // 断连与错误信号可能先后触发，只处理一次
    }

    qWarning() << "[P2pServer] 中继加入失败" << relayId << ":" << reason;
    emit relayJoinFailed(relayId, reason);
    disconnect(socket, nullptr, this, nullptr);
    socket->disconnectFromHost();
    socket->deleteLater();
}

// 处理新入站连接，先等待完整首帧再分流
void P2pServer::onNewConnection()
{
    qDebug() << "[P2pServer] 检测到新连接";

    while (_server->hasPendingConnections()) {
        QTcpSocket *socket = _server->nextPendingConnection();
        if (!socket) {
            qWarning() << "[P2pServer] 获取 socket 失败";
            continue;
        }

        qDebug() << "[P2pServer] 新连接来自"
                 << socket->peerAddress().toString()
                 << ":" << socket->peerPort();

        socket->setParent(this);

        // 扩大收发缓冲到 4MB，减少大文件传输时系统调用和窗口抖动。
        socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

        monitorFirstFrame(socket, kDefaultFirstFrameTimeoutMs);
    }
}

// 监听 socket，等待首个完整 TLV 帧
void P2pServer::monitorFirstFrame(QTcpSocket *socket, int timeoutMs)
{
    auto *firstFrameTimer = new QTimer{socket};
    firstFrameTimer->setSingleShot(true);
    _firstFrameTimers.insert(socket, firstFrameTimer);

    connect(firstFrameTimer, &QTimer::timeout,
            this, [this, socket]() {
        if (socket && socket->parent() == this) {
            closePendingConnection(socket, tr("首帧路由超时"));
        }
    });
    connect(socket, &QTcpSocket::readyRead,
            this,   [this, socket]() { routeFirstFrame(socket); });
    connect(socket, &QTcpSocket::disconnected,
            this,   [this, socket]() {
        stopFirstFrameTimeout(socket);
        if (socket->parent() == this) {
            socket->deleteLater();
        }
    });
    connect(socket, &QObject::destroyed,
            this, [this, socket]() {
        _firstFrameTimers.remove(socket);
    });

    firstFrameTimer->start(timeoutMs);
    routeFirstFrame(socket);
}

// 校验首帧并按 Type 路由连接
void P2pServer::routeFirstFrame(QTcpSocket *socket)
{
    // socket 已被其他处理者接管（parent 变化）时不再处理
    if (!socket || socket->parent() != this) {
        return;
    }

    // 帧头尚未完整到达，等待更多数据触发下一次 readyRead
    if (socket->bytesAvailable() < static_cast<qint64>(gy::protocol::kHeaderBytes)) {
        return;
    }

    // peek 而非 read：帧头字节仍留在缓冲区，后续接管者可从头读取
    const QByteArray header = socket->peek(gy::protocol::kHeaderBytes);
    quint32 type = 0;
    quint32 payloadLength = 0;
    if (!readFrameHeader(header, &type, &payloadLength)) {
        closePendingConnection(socket, tr("无法读取首帧头"));
        return;
    }

    // 按 Type 分级检查载荷上限，防止恶意帧占用大量内存
    const quint32 maxPayload = gy::protocol::maxPayloadForType(type);
    if (payloadLength > maxPayload) {
        closePendingConnection(socket, tr("首帧载荷超出限制"));
        return;
    }

    const qint64 frameBytes = static_cast<qint64>(gy::protocol::kHeaderBytes) + payloadLength;
    // 完整帧尚未到达，继续等待
    if (socket->bytesAvailable() < frameBytes) {
        return;
    }

    // 这里只 peek 校验首帧，不能 read；后续接管者还要从 socket 读取完整首帧。
    const QByteArray firstFrame = socket->peek(frameBytes);
    FrameCodec codec;
    quint32 decodedType = 0;
    QByteArray decodedPayload;
    bool frameReady = false;
    bool frameError = false;
    connect(&codec, &FrameCodec::frameReady,
            &codec, [&decodedType, &decodedPayload, &frameReady](quint32 frameType,
                                                                  const QByteArray &payload) {
        decodedType = frameType;
        decodedPayload = payload;
        frameReady = true;
    });
    connect(&codec, &FrameCodec::errorOccurred,
            &codec, [&frameError](gy::protocol::ErrorCode, const QString &) {
        frameError = true;
    });
    codec.feed(firstFrame);

    if (frameError || !frameReady || decodedType != type) {
        closePendingConnection(socket, tr("首帧格式无效"));
        return;
    }

    stopFirstFrameTimeout(socket);
    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    disconnect(socket, &QTcpSocket::disconnected, this, nullptr);

    if (type == gy::protocol::kTypeTransferReq) {
        // 文件接收 Worker 会在线程启动后重新读取保留在 socket 缓冲区里的首帧。
        startFileReceiver(socket);
        return;
    }

    if (type == gy::protocol::kTypeChatText) {
        gy::ChatMessage message;
        if (!gy::ChatMessageCodec::decode(decodedPayload, &message)) {
            closePendingConnection(socket, tr("聊天首帧消息无效"));
            return;
        }

        emit chatConnectionReceived(socket);
        if (socket->parent() == this) {
            // 没有处理者接管 parent 时必须关闭，避免悬挂的入站连接泄漏。
            closePendingConnection(socket, tr("没有聊天连接处理者"));
        }
        return;
    }

    closePendingConnection(socket, tr("不支持的首帧类型"));
}

// 创建后台文件接收 Worker
void P2pServer::startFileReceiver(QTcpSocket *socket)
{
    auto *thread = new QThread{this};
    auto *worker = new FileReceiverWorker{socket};
    worker->moveToThread(thread);

    qDebug() << "[P2pServer] 创建 FileReceiverWorker 处理文件传输（后台线程）";

    // 线程启动时初始化 worker（在后台线程中创建 QTimer 和连接信号）
    connect(thread, &QThread::started, worker, &FileReceiverWorker::initialize);

    // 转发传输请求信号（worker 在后台线程，信号通过 Queued Connection 跨线程）
    connect(worker, &FileReceiverWorker::transferRequestReceived,
            this, [this, worker](const QVariantMap &request) {
        qDebug() << "[P2pServer] 转发传输请求信号到 TransferSessionManager";
        emit transferRequestReceived(worker, request);
    });

    // 传输完成时清理线程和 worker
    connect(worker, &FileReceiverWorker::transferFinished,
            this, [thread](bool success, gy::protocol::ErrorCode errorCode,
                           const QString &errorMsg, const QString &) {
        qDebug() << "[P2pServer] 传输完成"
                 << "成功:" << success
                 << "错误码:" << static_cast<quint16>(errorCode)
                 << "错误:" << errorMsg;
        QMetaObject::invokeMethod(thread, [thread]() {
            thread->quit();
        }, Qt::QueuedConnection);
    });

    // 线程结束后清理资源
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    thread->start();
}

// 关闭尚未交接的入站连接
void P2pServer::closePendingConnection(QTcpSocket *socket, const QString &reason)
{
    qWarning() << "[P2pServer] 关闭入站连接:" << reason;
    stopFirstFrameTimeout(socket);
    disconnect(socket, nullptr, this, nullptr);
    socket->disconnectFromHost();
    socket->deleteLater();
}

// 清理待路由首帧的超时计时器
void P2pServer::stopFirstFrameTimeout(QTcpSocket *socket)
{
    QTimer *timer = _firstFrameTimers.take(socket);
    if (!timer) {
        return;
    }
    timer->stop();
    timer->deleteLater();
}
