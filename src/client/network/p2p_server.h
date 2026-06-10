/**
* @file    p2p_server.h
* @date    2026-06-02
* @author  GY
* @brief   P2P 文件传输服务器
*
* 监听 TCP 端口，接受来自其他设备的文件传输请求。
* 为每个入站连接创建 FrameCodec 和 FileReceiverWorker。
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
* [v0.2] GY   2026-06-04
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
*/

#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

class ConfigManager;
class FrameCodec;
class FileReceiverWorker;

class P2pServer : public QObject {
    Q_OBJECT

public:
    explicit P2pServer(ConfigManager *config, QObject *parent = nullptr);
    virtual ~P2pServer() override = default;

    P2pServer(const P2pServer &)            = delete;
    P2pServer &operator=(const P2pServer &) = delete;

    // 启动服务器监听
    bool start();
    // 停止服务器
    void stop();
    // 是否正在监听
    bool isListening() const;

signals:
    // 新的传输请求到达（需要弹窗确认）
    void transferRequestReceived(FileReceiverWorker *worker,
                                 const QString &senderDeviceId,
                                 const QString &senderName,
                                 const QString &fileName,
                                 qint64 fileSize,
                                 int totalFiles,
                                 qint64 totalBytes);

private slots:
    // 新连接到达
    void onNewConnection();

private:
    ConfigManager *_config = nullptr;
    QTcpServer    *_server = nullptr;
};
