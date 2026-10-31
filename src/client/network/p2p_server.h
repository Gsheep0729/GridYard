/**
* @file    p2p_server.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   P2P 文件传输服务器
*
* 监听 TCP 端口（默认 35100），先按首个完整 TLV 帧分流连接。
* 文件传输交给后台 FileReceiverWorker，聊天连接交接给后续 ChatManager，
* 两种业务共用端口但不共享状态机。
* 直连不可达时，接收端可经中继服务器加入会话，socket 交给同一条首帧路由。
*/

#pragma once

#include <QObject>
#include <QHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVariantMap>

class ConfigManager;
class FrameCodec;
class FileReceiverWorker;

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
    // 聊天连接到达，接收方须在当前线程同步接管 socket 的对象归属
    void chatConnectionReceived(QTcpSocket *socket);
    // 中继加入失败（连接不上、握手超时等）
    void relayJoinFailed(const QString &relayId, const QString &reason);

private slots:
    // 新连接到达
    void onNewConnection();

private:
    // 监听 socket，等待首个完整 TLV 帧；中继加入的连接等待期更长
    void monitorFirstFrame(QTcpSocket *socket, int timeoutMs);
    // 校验首帧并按 Type 路由连接
    void routeFirstFrame(QTcpSocket *socket);
    // 创建后台文件接收 Worker
    void startFileReceiver(QTcpSocket *socket);
    // 关闭尚未交接的入站连接
    void closePendingConnection(QTcpSocket *socket, const QString &reason);
    // 清理待路由首帧的超时计时器
    void stopFirstFrameTimeout(QTcpSocket *socket);
    // 结束一次未完成的中继加入：清理连接并通知失败
    void failRelayJoin(QTcpSocket *socket, const QString &reason);

    ConfigManager *_config = nullptr; // TCP 监听端口配置来源
    QTcpServer    *_server = nullptr; // 接受入站传输连接的服务器
    QHash<QTcpSocket *, QTimer *> _firstFrameTimers;
    QHash<QTcpSocket *, QString> _relayJoins;  // 待就绪的中继连接 -> relayId
};
