/**
* @file    p2p_server.cpp
* @version 4.10.0
* @date    2026-06-13
* @author  GY
* @brief   P2pServer 实现
*
* Change Log:
* [v4.3.4] GY   2026-06-04
* * Stage 4.3：信号转发添加 totalFiles/totalBytes 参数
* [v0.2.0] GY   2026-06-02
* * Stage 3：初始版本
*/

#include "p2p_server.h"
#include "config_manager.h"
#include "file_receiver_worker.h"

#include <QDebug>

P2pServer::P2pServer(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
    , _server{new QTcpServer{this}}
{
    // 新连接信号
    connect(_server, &QTcpServer::newConnection,
            this,    &P2pServer::onNewConnection);
}

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

void P2pServer::stop()
{
    if (_server->isListening()) {
        _server->close();
        qDebug() << "P2pServer: 已停止监听";
    }
}

bool P2pServer::isListening() const
{
    return _server->isListening();
}

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

        // 创建 FileReceiverWorker 处理这个连接
        auto *worker = new FileReceiverWorker{socket, this};
        qDebug() << "[P2pServer] 创建 FileReceiverWorker 处理连接";

        // 转发传输请求信号
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

        // 传输完成时清理
        connect(worker, &FileReceiverWorker::transferFinished,
                this, [worker](bool success, const QString &errorMsg) {
            qDebug() << "[P2pServer] 传输完成"
                     << "成功:" << success
                     << "错误:" << errorMsg;
            worker->deleteLater();
        });
    }
}
