/**
* @file    file_receiver_worker.h
* @date    2026-06-02
* @author  GY
* @brief   文件接收 Worker（Worker-Object 模式）
*
* 由 P2pServer 为每个入站连接创建，负责：
* 1. 接收 kTypeTransferReq 握手请求
* 2. 发射信号通知 UI 弹窗确认
* 3. 等待用户确认后发送 kTypeTransferRsp
* 4. 接收 kTypeDataChunk 并写入文件
* 5. 接收完成后发送 kTypeChunkAck
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
*/

#pragma once

#include <QFile>
#include <QObject>
#include <QTcpSocket>

class FrameCodec;

class FileReceiverWorker : public QObject {
    Q_OBJECT

public:
    explicit FileReceiverWorker(QTcpSocket *socket, QObject *parent = nullptr);
    virtual ~FileReceiverWorker() override;

    FileReceiverWorker(const FileReceiverWorker &)            = delete;
    FileReceiverWorker &operator=(const FileReceiverWorker &) = delete;

    // 获取会话信息
    QString sessionId()    const { return _sessionId; }
    QString senderName()   const { return _senderName; }
    QString fileName()     const { return _fileName; }
    qint64  fileSize()     const { return _fileSize; }

public slots:
    // 用户接受传输
    void acceptTransfer();
    // 用户拒绝传输
    void rejectTransfer(const QString &reason = "");

signals:
    // 传输请求到达（需要弹窗确认）
    void transferRequestReceived(const QString &senderName,
                                 const QString &fileName,
                                 qint64 fileSize);
    // 传输进度更新
    void progressChanged(qint64 bytesReceived, qint64 totalBytes);
    // 传输完成
    void transferFinished(bool success, const QString &errorMsg);

private slots:
    // 接收数据
    void onReadyRead();
    // 连接断开
    void onDisconnected();
    // 处理收到的帧
    void onFrameReady(quint32 type, const QByteArray &payload);

private:
    // 处理握手请求
    void handleTransferRequest(const QByteArray &payload);
    // 处理数据块
    void handleDataChunk(const QByteArray &payload);
    // 处理取消请求
    void handleCancel(const QByteArray &payload);
    // 发送握手响应
    void sendTransferResponse(bool accepted, const QString &reason = "");
    // 发送块确认
    void sendChunkAck(bool verified, const QString &errorMsg = "");

    QTcpSocket  *_socket = nullptr;
    FrameCodec  *_codec  = nullptr;
    QFile        _file;

    // 会话信息
    QString _sessionId;
    QString _senderName;
    int     _totalFiles = 0;
    qint64  _totalBytes = 0;

    // 当前文件信息
    int     _currentFileIndex = 0;
    QString _fileName;
    qint64  _fileSize = 0;
    qint64  _bytesReceived = 0;

    // 状态
    bool _waitingForUserConfirm = false;
    bool _transferActive = false;
};
