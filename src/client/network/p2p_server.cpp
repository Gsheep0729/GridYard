/**
* @file    p2p_server.cpp
* @version 4.15.1
* @date    2026-06-17
* @author  GridYard Team
* @brief   P2P 文件传输服务器实现
*
* 监听 TCP 端口，接受入站连接并为每个连接创建独立的 QThread
* 和 FileReceiverWorker。实现接收侧后台化，写盘与 SHA-256 校验
* 在后台线程执行，不阻塞 UI 主线程。
*
* Change Log:
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
#include "config_manager.h"
#include "file_receiver_worker.h"
#include "protocol.h"

#include <QDebug>

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

// 查询服务器是否正在监听
bool P2pServer::isListening() const
{
    return _server->isListening();
}

// 处理新入站连接，为每个连接创建独立后台线程和 FileReceiverWorker
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

        // 优化 socket buffer
        socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

        // 创建独立的后台线程处理这个连接
        auto *thread = new QThread{this};
        auto *worker = new FileReceiverWorker{socket};
        worker->moveToThread(thread);

        qDebug() << "[P2pServer] 创建 FileReceiverWorker 处理连接（后台线程）";

        // 线程启动时初始化 worker（在后台线程中创建 QTimer 和连接信号）
        connect(thread, &QThread::started, worker, &FileReceiverWorker::initialize);

        // 转发传输请求信号（worker 在后台线程，信号通过 Queued Connection 跨线程）
        connect(worker, &FileReceiverWorker::transferRequestReceived,
                this, [this, worker](const QString &senderDeviceId,
                                     const QString &senderName,
                                     const QString &fileName,
                                     qint64 fileSize,
                                     int totalFiles,
                                     qint64 totalBytes) {
            qDebug() << "[P2pServer] 转发传输请求信号到 TransferSessionManager";
            emit transferRequestReceived(worker, senderDeviceId, senderName, fileName,
                                         fileSize, totalFiles, totalBytes);
        });

        // 传输完成时清理线程和 worker
        connect(worker, &FileReceiverWorker::transferFinished,
                this, [worker, thread](bool success, gy::protocol::ErrorCode errorCode, const QString &errorMsg) {
            qDebug() << "[P2pServer] 传输完成"
                     << "成功:" << success
                     << "错误码:" << static_cast<quint16>(errorCode)
                     << "错误:" << errorMsg;
            // 使用 QMetaObject::invokeMethod 在线程中退出事件循环
            QMetaObject::invokeMethod(thread, [thread]() {
                thread->quit();
            }, Qt::QueuedConnection);
        });

        // 线程结束后清理资源
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);

        // 启动后台线程
        thread->start();
    }
}
