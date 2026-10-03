/**
* @file    p2p_server.h
* @version 7.15.13
* @date    2026-10-04
* @author  GridYard Team
* @brief   P2P 文件传输服务器
*
* 监听 TCP 端口（默认 35100），先按首个完整 TLV 帧分流连接。
* 文件传输交给后台 FileReceiverWorker，聊天连接交接给后续 ChatManager，
* 两种业务共用端口但不共享状态机。
* 直连不可达时，接收端可经中继服务器加入会话，socket 交给同一条首帧路由。
*
* Change Log:
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
* [v7.9.0] GY   2026-07-26
* * 新增 joinRelaySession：接入中继降级连接并复用首帧路由
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v5.0.0] FengChunlin   2026-06-23
* * 按首个完整 TLV 帧分流文件传输和在线聊天连接
* [v4.16.1] GY   2026-06-21
* * 使用请求快照转发接收信息，删除未使用的 isListening() 访问器
* [v4.15.1] FengChunlin   2026-06-16
* * 删除未使用的 _threads 成员，析构改用 children() 遍历
* [v4.15.0] FengChunlin   2026-06-16
* * 为每个连接创建独立的 QThread，实现接收侧后台化
* * worker + socket 移到后台线程，写盘与 SHA-256 不阻塞 UI
* * transferFinished 信号添加 ErrorCode 参数
* [v4.3.4] GY   2026-05-27
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.2.0] FengChunlin   2026-05-03
* * Stage 3：初始版本
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
