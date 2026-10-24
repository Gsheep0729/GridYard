/**
* @file    p2p_server.cpp
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   P2P 文件传输服务器实现
*
* 监听与接入由纯 C++ 的 TcpServerCore 在 NetReactor 事件泵上完成，接入
* 回调只把描述符投递回 P2pServer 所在线程，包装与路由都在本线程进行。
* 首帧路由阶段窥视 socket 中的完整 TLV 帧确认类型：聊天连接在同一线程
* 同步交接，字节仍留在缓冲区由接管者读取；文件传输路径则把首帧字节
* 显式读出、随接收 Worker 移交，不再依赖缓冲区跨线程携带首帧。
* 中继降级的连接在完成 relay_join 握手后也进入同一条首帧路由。
*/

#include "p2p_server.h"
#include "rendezvous_protocol_keys_qt.h"
#include "chat_message.h"
#include "codec_bridge.h"
#include "config_manager.h"
#include "file_receiver_worker.h"
#include "ip_address.h"
#include "posix_tcp_server.h"
#include "posix_tcp_socket.h"
#include "protocol.h"

#include <QDataStream>
#include <QDebug>
#include <QJsonObject>
#include <QJsonDocument>
#include <QPointer>

#include "core/net_reactor.h"
#include "post_to_ui_thread.h"
#include "core/tcp_server_core.h"

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

// 构造函数，创建纯 C++ 监听服务器并挂接 Reactor 接入回调
P2pServer::P2pServer(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
    , _server{std::make_unique<gy::net::TcpServerCore>(gy::net::sharedReactor())}
{
    // Reactor 线程只投递描述符：包装 socket 与首帧路由都必须在本线程进行
    _server->setConnectionCallback(
        [this](int descriptor, const std::string &peerAddress) {
            gy::bridge::postToUiThread(
                this, [this, descriptor, peerAddress] {
                    handleNewConnection(descriptor, QString::fromStdString(peerAddress));
                });
        });
}

// 析构函数，停止监听并等待所有后台线程退出
P2pServer::~P2pServer()
{
    // 停止服务器监听（监听描述符随核心服务器对象一并关闭）
    if (_server->isListening()) {
        _server->close();
    }

    // 遍历子对象，停止仍在运行的 QThread，避免析构时警告
    const auto kids = children();
    for (QObject *child : kids) {
        if (auto *thread = qobject_cast<QThread*>(child)) {
            if (thread->isRunning()) {
                thread->quit();
                // 接收 worker 取消时可能正等待对端关闭，预留足够收尾时间
                if (!thread->wait(3000)) {
                    qWarning() << "[P2pServer] 后台线程未在超时内退出，强制终止";
                    thread->terminate();
                    thread->wait();
                }
            }
        }
    }
}

// 启动 TCP 服务器，监听配置的端口
bool P2pServer::start()
{
    const quint16 port = _config->tcpPort();

    // 双栈监听（IPv6 通配，内核不支持时核心层自动回退 IPv4）
    if (_server->listen(port, true)) {
        qDebug() << "P2pServer: 监听端口" << port << "成功";
        return true;
    } else {
        qWarning() << "P2pServer: 监听端口" << port << "失败:"
                    << QString::fromStdString(_server->errorString());
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

    auto *socket = new gy::net::PosixTcpSocket{this};
    auto *timer = new QTimer{socket};
    timer->setSingleShot(true);
    _relayJoins.insert(socket, relayId);

    connect(timer, &QTimer::timeout, this, [this, socket]() {
        failRelayJoin(socket, tr("中继连接超时"));
    });
    connect(socket, &gy::net::PosixTcpSocket::connected, this, [this, socket, relayId, timer]() {
        qDebug() << "[P2pServer] 已连接中继服务器，发送 relay_join";
        QJsonObject hello;
        hello[gy::rendezvous::key(gy::rendezvous::kKeyType)] = gy::rendezvous::key(gy::rendezvous::kTypeRelayJoin);
        hello[gy::rendezvous::key(gy::rendezvous::kKeyRelayId)] = relayId;
        // 服务器未启用认证时令牌为空，字段不发送
        const QString token = _config ? _config->rendezvousToken() : QString();
        if (!token.isEmpty()) {
            hello[gy::rendezvous::key(gy::rendezvous::kKeyToken)] = token;
        }
        const QByteArray line = QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n';
        socket->write(line);

        // 握手行已发出，后续字节就是中继转发的标准 TLV 流；
        // 会话两端齐备后发送端才开始传输，这里放宽等待期
        _relayJoins.remove(socket);
        timer->stop();
        monitorFirstFrame(socket, kRelayFirstFrameTimeoutMs);
    });
    connect(socket, &gy::net::PosixTcpSocket::disconnected, this, [this, socket]() {
        if (_relayJoins.contains(socket)) {
            failRelayJoin(socket, tr("中继连接已断开"));
        }
    });
    connect(socket, &gy::net::PosixTcpSocket::errorOccurred, this, [this, socket](gy::net::SocketError) {
        if (_relayJoins.contains(socket)) {
            failRelayJoin(socket, socket->errorString());
        }
    });

    timer->start(kRelayConnectTimeoutMs);
    socket->connectToHost(host, port);
}

// 结束一次未完成的中继加入：清理连接并通知失败
void P2pServer::failRelayJoin(gy::net::PosixTcpSocket *socket, const QString &reason)
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

// 接入描述符在本线程包装为 socket 对象，随后等待完整首帧再分流
void P2pServer::handleNewConnection(int descriptor, const QString &peerAddress)
{
    qDebug() << "[P2pServer] 新连接来自" << peerAddress;

    auto *socket = new gy::net::PosixTcpSocket{this};
    if (!socket->adoptDescriptor(descriptor)) {
        qWarning() << "[P2pServer] 接入描述符包装失败，来自" << peerAddress;
        delete socket;
        return;
    }

    // 扩大收发缓冲到 4MB，减少大文件传输时系统调用和窗口抖动。
    socket->setSocketOption(gy::net::PosixTcpSocket::SendBufferSizeOption, 4 * 1024 * 1024);
    socket->setSocketOption(gy::net::PosixTcpSocket::ReceiveBufferSizeOption, 4 * 1024 * 1024);

    monitorFirstFrame(socket, kDefaultFirstFrameTimeoutMs);
}

// 监听 socket，等待首个完整 TLV 帧
void P2pServer::monitorFirstFrame(gy::net::PosixTcpSocket *socket, int timeoutMs)
{
    // 计时器归 P2pServer 持有，避免随 socket 移交到工作线程后才延迟销毁
    auto *firstFrameTimer = new QTimer{this};
    firstFrameTimer->setSingleShot(true);
    _firstFrameTimers.insert(socket, firstFrameTimer);

    connect(firstFrameTimer, &QTimer::timeout,
            this, [this, socket]() {
        if (socket && socket->parent() == this) {
            closePendingConnection(socket, tr("首帧路由超时"));
        }
    });
    connect(socket, &gy::net::PosixTcpSocket::readyRead,
            this,   [this, socket]() { routeFirstFrame(socket); });
    connect(socket, &gy::net::PosixTcpSocket::disconnected,
            this,   [this, socket]() {
        stopFirstFrameTimeout(socket);
        if (socket->parent() == this) {
            socket->deleteLater();
        }
    });
    connect(socket, &QObject::destroyed,
            this, [this, socket]() {
        QTimer *timer = _firstFrameTimers.take(socket);
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }
    });

    firstFrameTimer->start(timeoutMs);
    routeFirstFrame(socket);
}

// 校验首帧并按 Type 路由连接
void P2pServer::routeFirstFrame(gy::net::PosixTcpSocket *socket)
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

    // 只窥视校验首帧：聊天路径的接管者稍后自行读取缓冲区中的完整首帧；
    // 传输路径在校验通过后把首帧字节显式读出随 Worker 移交。
    const QByteArray firstFrame = socket->peek(frameBytes);
    quint32 decodedType = 0;
    QByteArray decodedPayload;
    bool frameReady = false;
    bool frameError = false;
    gy::core::FrameCodecCore codec(
        [&decodedType, &decodedPayload, &frameReady](uint32_t frameType,
                                                     std::vector<uint8_t> payload) {
            decodedType = frameType;
            decodedPayload = gy::bridge::fromBytes(payload);
            frameReady = true;
        },
        [&frameError](std::string) {
            frameError = true;
        });
    gy::bridge::feedFrame(codec, firstFrame);

    if (frameError || !frameReady || decodedType != type) {
        closePendingConnection(socket, tr("首帧格式无效"));
        return;
    }

    stopFirstFrameTimeout(socket);
    disconnect(socket, &gy::net::PosixTcpSocket::readyRead, this, nullptr);
    disconnect(socket, &gy::net::PosixTcpSocket::disconnected, this, nullptr);

    if (type == gy::protocol::kTypeTransferReq) {
        // 首帧字节在此显式读出，随 Worker 构造移交——不依赖 socket 缓冲区
        // 跨线程携带首帧；后续到达的字节仍留在缓冲区由 Worker 继续读取。
        const QByteArray consumedFirstFrame = socket->read(frameBytes);
        QPointer<gy::net::PosixTcpSocket> guardedSocket{socket};
        gy::bridge::postToUiThread(this, [this, guardedSocket, consumedFirstFrame]() {
            if (!guardedSocket) {
                return;
            }
            if (guardedSocket->parent() != this
                || guardedSocket->state() == gy::net::SocketState::UnconnectedState) {
                // 对端在移交前关闭时不再启动 worker，同时补做删除
                if (guardedSocket->parent() == this) {
                    guardedSocket->deleteLater();
                }
                return;
            }
            startFileReceiver(guardedSocket, consumedFirstFrame);
        });
        return;
    }

    if (type == gy::protocol::kTypeChatText) {
        gy::ChatMessage message;
        if (!gy::bridge::decodeChatMessage(decodedPayload, &message)) {
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

// 创建后台文件接收 Worker，携带 P2pServer 已消费的首帧字节
void P2pServer::startFileReceiver(gy::net::PosixTcpSocket *socket, const QByteArray &firstFrame)
{
    if (!socket || socket->parent() != this) {
        return;
    }

    auto *thread = new QThread{this};
    auto *worker = new FileReceiverWorker{socket, firstFrame};

    // 移交通知器：先在当前线程停用，再把 socket 的父对象交给 worker 一并迁移
    socket->prepareThreadMove();
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
void P2pServer::closePendingConnection(gy::net::PosixTcpSocket *socket, const QString &reason)
{
    qWarning() << "[P2pServer] 关闭入站连接:" << reason;
    stopFirstFrameTimeout(socket);
    disconnect(socket, nullptr, this, nullptr);
    socket->disconnectFromHost();
    socket->deleteLater();
}

// 清理待路由首帧的超时计时器
void P2pServer::stopFirstFrameTimeout(gy::net::PosixTcpSocket *socket)
{
    QTimer *timer = _firstFrameTimers.take(socket);
    if (!timer) {
        return;
    }
    timer->stop();
    timer->deleteLater();
}
