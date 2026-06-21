/**
* @file    p2p_server.cpp
* @version 4.16.4
* @date    2026-06-24
* @author  GridYard Team
* @brief   P2P 文件传输服务器实现
*
* 首帧路由阶段只窥视 socket 中的完整 TLV 帧，不读取其字节。
* 确认 Type 后再把原始 socket 交给文件接收 Worker 或聊天连接处理者，
* 保证目标处理者能自行解析完整首帧。
*
* Change Log:
* [v4.16.4] GY   2026-06-24
* * 新增首帧路由和聊天连接交接逻辑
* [v4.16.1] GY   2026-06-21
* * 使用请求快照转发接收信息，删除未使用的 isListening() 访问器
* [v4.15.1] FengChunlin   2026-06-17
* * 删除 _threads.append 调用，修正析构注释
* [v4.15.0] GY   2026-06-17
* * 为每个连接创建独立的 QThread，实现接收侧后台化
* * worker + socket 移到后台线程，写盘与 SHA-256 不阻塞 UI
* * transferFinished 信号添加 ErrorCode 参数
* [v4.3.4] FengChunlin   2026-06-04
* * Stage 4.3：信号转发添加 totalFiles/totalBytes 参数
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 3：初始版本
*/

#include "p2p_server.h"
#include "chat_message.h"
#include "config_manager.h"
#include "file_receiver_worker.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QDebug>

namespace {

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

        // 优化 socket buffer
        socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

        monitorFirstFrame(socket);
    }
}

// 监听 socket，等待首个完整 TLV 帧
void P2pServer::monitorFirstFrame(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::readyRead,
            this,   [this, socket]() { routeFirstFrame(socket); });
    connect(socket, &QTcpSocket::disconnected,
            this,   [this, socket]() {
        if (socket->parent() == this) {
            socket->deleteLater();
        }
    });

    routeFirstFrame(socket);
}

// 校验首帧并按 Type 路由连接
void P2pServer::routeFirstFrame(QTcpSocket *socket)
{
    if (!socket || socket->parent() != this) {
        return;
    }

    if (socket->bytesAvailable() < static_cast<qint64>(gy::protocol::kHeaderBytes)) {
        return;
    }

    const QByteArray header = socket->peek(gy::protocol::kHeaderBytes);
    quint32 type = 0;
    quint32 payloadLength = 0;
    if (!readFrameHeader(header, &type, &payloadLength)) {
        closePendingConnection(socket, tr("无法读取首帧头"));
        return;
    }

    const quint32 maxPayload = gy::protocol::maxPayloadForType(type);
    if (payloadLength > maxPayload) {
        closePendingConnection(socket, tr("首帧载荷超出限制"));
        return;
    }

    const qint64 frameBytes = static_cast<qint64>(gy::protocol::kHeaderBytes) + payloadLength;
    if (socket->bytesAvailable() < frameBytes) {
        return;
    }

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

    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
    disconnect(socket, &QTcpSocket::disconnected, this, nullptr);

    if (type == gy::protocol::kTypeTransferReq) {
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
    disconnect(socket, nullptr, this, nullptr);
    socket->disconnectFromHost();
    socket->deleteLater();
}
