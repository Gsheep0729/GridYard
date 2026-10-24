/**
 * @file    p2p_server.h
 * @version 7.26.1
 * @date    2026-10-25
 * @author  GridYard Team
 * @brief   P2P 文件传输服务器
 *
 * 监听 TCP 端口（默认 35100），先按首个完整 TLV 帧分流连接。
 * 监听与接入由纯 C++ 的 TcpServerCore（NetReactor 事件泵）承担，接入
 * 描述符在 P2pServer 所在线程包装后进入首帧路由。
 * 文件传输的首帧字节被显式读出并随 FileReceiverWorker 移交，不依赖
 * socket 缓冲区跨线程携带；聊天连接在同一线程同步交接，仍由接管者读取。
 * 直连不可达时，接收端可经中继服务器加入会话，socket 交给同一条首帧路由。
 */

#pragma once

#include <QHash>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

#include <memory>
#include <mutex>
#include <set>

class ConfigManager;
class FileReceiverWorker;

namespace gy::net {
class PosixTcpSocket;
class TcpServerCore;
}

class P2pServer : public QObject {
    Q_OBJECT

public:
    explicit P2pServer(ConfigManager *config, QObject *parent = nullptr);
    virtual ~P2pServer() override;

    P2pServer(const P2pServer &)            = delete;
    P2pServer &operator=(const P2pServer &) = delete;

    // 启动服务器监听
    bool start();
    // 停止服务器
    void stop();
    // 获取监听端口（start 前 start() 用端口 0 时为系统分配值）
    quint16 serverPort() const;
    // 通过中继服务器加入指定会话（接收端中继降级入口），就绪后按首帧分流处理
    void joinRelaySession(const QString &host, quint16 port, const QString &relayId);

signals:
    // 新的传输请求到达（需要弹窗确认）
    void transferRequestReceived(FileReceiverWorker *worker, const QVariantMap &request);
    // 聊天连接到达契约：向 ChatManager 投递新建立的原生套接字，接收方必须在当前事件循环同步接管 socket 所有权与生命周期
    void chatConnectionReceived(gy::net::PosixTcpSocket *socket);
    // 中继加入失败（连接不上、握手超时等）
    void relayJoinFailed(const QString &relayId, const QString &reason);

private:
    // 接入描述符在本线程包装为 socket 对象并进入首帧路由
    void handleNewConnection(int descriptor, const QString &peerAddress);
    // 监听 socket，等待首个完整 TLV 帧；中继加入的连接等待期更长
    void monitorFirstFrame(gy::net::PosixTcpSocket *socket, int timeoutMs);
    // 校验首帧并按 Type 路由连接
    void routeFirstFrame(gy::net::PosixTcpSocket *socket);
    // 创建后台文件接收 Worker，显式携带已消费的首帧字节
    void startFileReceiver(gy::net::PosixTcpSocket *socket, const QByteArray &firstFrame);
    // 关闭尚未交接的入站连接
    void closePendingConnection(gy::net::PosixTcpSocket *socket, const QString &reason);
    // 清理待路由首帧的超时计时器
    void stopFirstFrameTimeout(gy::net::PosixTcpSocket *socket);
    // 结束一次未完成的中继加入：清理连接并通知失败
    void failRelayJoin(gy::net::PosixTcpSocket *socket, const QString &reason);

    ConfigManager *_config = nullptr; // TCP 监听端口配置来源
    std::unique_ptr<gy::net::TcpServerCore> _server;  // 接受入站传输连接的纯 C++ 监听服务器
    std::set<int> _pendingConnectionDescriptors;  // 已投递未消费的接入描述符，析构时兜底关闭
    std::mutex _pendingConnectionMutex;  // 接入回调在 Reactor 线程，登记与消费跨线程互斥
    QHash<gy::net::PosixTcpSocket *, QTimer *> _firstFrameTimers;  // 待路由连接的首帧计时器
    QHash<gy::net::PosixTcpSocket *, QString> _relayJoins;  // 待就绪的中继连接 -> relayId
};
