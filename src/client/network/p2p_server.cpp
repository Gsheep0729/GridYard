/**
* @file    p2p_server.cpp
* @date    2026-06-02
* @author  GY
* @brief   P2pServer 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
*/

#include "p2p_server.h"
#include "config_manager.h"
#include "frame_codec.h"

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
    while (_server->hasPendingConnections()) {
        QTcpSocket *socket = _server->nextPendingConnection();
        if (!socket) continue;

        qDebug() << "P2pServer: 新连接来自"
                 << socket->peerAddress().toString()
                 << ":" << socket->peerPort();

        // 创建 FrameCodec 处理这个连接
        auto *codec = new FrameCodec{this};

        // 连接 socket 数据到达信号到 codec
        connect(socket, &QTcpSocket::readyRead, this, [socket, codec]() {
            codec->feed(socket->readAll());
        });

        // 连接 socket 断开信号
        connect(socket, &QTcpSocket::disconnected, this, [socket, codec]() {
            qDebug() << "P2pServer: 连接断开"
                     << socket->peerAddress().toString();
            socket->deleteLater();
            codec->deleteLater();
        });

        // TODO Stage 3 任务 3.4：创建 FileReceiverWorker
        // 连接 codec 的 frameReady 信号到 FileReceiverWorker
    }
}
