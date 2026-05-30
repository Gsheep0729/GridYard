/**
* @file    file_sender_worker.h
* @date    2026-06-02
* @author  GY
* @brief   文件发送 Worker（Worker-Object 模式）
*
* 运行在独立线程中，负责：
* 1. 建立 TCP 连接到接收端
* 2. 发送 kTypeTransferReq 握手请求
* 3. 等待 kTypeTransferRsp 响应
* 4. 以 4MB 分块发送文件数据
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
* [v0.2] GY   2026-06-03
* * Stage 3.10：添加 cancel() 槽函数
* [v0.3] GY   2026-06-04
* * Stage 4：支持多文件/目录传输，SHA-256 校验
*/

#pragma once

#include "dir_serializer.h"

#include <QFile>
#include <QObject>
#include <QTcpSocket>

class FrameCodec;

// 分块大小：4MB
static constexpr qint64 kChunkSize = 4 * 1024 * 1024;

class FileSenderWorker : public QObject {
    Q_OBJECT

public:
    explicit FileSenderWorker(QObject *parent = nullptr);
    virtual ~FileSenderWorker() override;

    FileSenderWorker(const FileSenderWorker &)            = delete;
    FileSenderWorker &operator=(const FileSenderWorker &) = delete;

public slots:
    // 启动传输（在工作线程中调用，支持文件或目录）
    void startTransfer(const QString &host, quint16 port, const QString &path);
    // 取消传输
    void cancel();

signals:
    // 传输进度更新
    void progressChanged(qint64 bytesSent, qint64 totalBytes);
    // 传输完成
    void transferFinished(bool success, const QString &errorMsg);
    // 请求被接受
    void requestAccepted();
    // 请求被拒绝
    void requestRejected(const QString &reason);

private slots:
    // 接收数据
    void onReadyRead();
    // 连接断开
    void onDisconnected();
    // 处理收到的帧
    void onFrameReady(quint32 type, const QByteArray &payload);

private:
    // 发送握手请求
    void sendTransferRequest();
    // 发送下一个数据块
    void sendNextChunk();
    // 打开下一个文件
    bool openNextFile();
    // 发送传输完成帧
    void sendTransferDone();
    // 发送取消请求
    void sendCancel(const QString &reason);

    QTcpSocket  *_socket = nullptr;
    FrameCodec  *_codec  = nullptr;
    QFile        _file;
    QString      _rootPath;         // 传入的根路径
    QString      _sessionId;
    qint64       _totalBytes = 0;
    qint64       _bytesSent  = 0;

    // 多文件支持
    QList<gy::FileItem> _fileList;
    int          _currentFileIndex = 0;
    qint64       _currentFileBytesSent = 0;
};
