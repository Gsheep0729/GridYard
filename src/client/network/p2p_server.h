/**
* @file    p2p_server.h
* @version 4.16.1
* @date    2026-06-21
* @author  GridYard Team
* @brief   P2P 文件传输服务器
*
* 监听 TCP 端口（默认 35100），接受来自其他设备的文件传输请求。
* 为每个入站连接创建独立的 QThread 和 FileReceiverWorker，
* 实现接收侧后台化，写盘与 SHA-256 校验在后台线程执行，
* 不阻塞 UI 主线程。
*
* Change Log:
* [v4.16.1] GY   2026-06-21
* * 使用请求快照转发接收信息，删除未使用的 isListening() 访问器
* [v4.15.1] FengChunlin   2026-06-17
* * 删除未使用的 _threads 成员，析构改用 children() 遍历
* [v4.15.0] GY   2026-06-17
* * 为每个连接创建独立的 QThread，实现接收侧后台化
* * worker + socket 移到后台线程，写盘与 SHA-256 不阻塞 UI
* [v4.3.4] FengChunlin   2026-06-04
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 3：初始版本
*/

#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
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
signals:
    // 新的传输请求到达（需要弹窗确认）
    void transferRequestReceived(FileReceiverWorker *worker, const QVariantMap &request);

private slots:
    // 新连接到达
    void onNewConnection();

private:
    ConfigManager *_config = nullptr; // TCP 监听端口配置来源
    QTcpServer    *_server = nullptr; // 接受入站传输连接的服务器
};
