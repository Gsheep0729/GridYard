/**
* @file    file_sender_worker.h
* @version 4.16.1
* @date    2026-06-21
* @author  GridYard Team
* @brief   文件发送 Worker（Worker-Object 模式）
*
* 运行在独立后台线程中，负责建立 TCP 连接、发送传输握手请求、
* 等待响应、以 8MB 分块发送文件数据、处理确认帧等完整发送流程。
* 支持多文件/目录传输、SHA-256 校验、取消操作和超时检测。
*
* Change Log:
* [v4.16.1] GY   2026-06-21
* * 补充传输状态成员的职责注释
* [v4.15.1] FengChunlin   2026-06-17
* * 连接 FrameCodec::errorOccurred 信号，协议错误时清理并结束会话
* * 进度节流 static 变量改为成员变量 _sendChunkCount
* * TransferRsp/ChunkAck 响应优先读取 error_code 字段
* [v4.15.0] GY   2026-06-17
* * transferFinished 信号添加 ErrorCode 参数
* [v4.12.1] FengChunlin   2026-06-14
* * 修复多文件重复读取并为大型文件发送增加背压
* [v4.11.0] FengChunlin   2026-06-13
* * 文件夹传输保留顶层目录并支持空文件夹
* [v4.8.3] FengChunlin   2026-06-13
* * 使用传入的设备别名作为发送方名称
* [v4.5.3] GY   2026-06-04
* * Stage 4.5：调大 socket buffer，减少进度信号频率
* [v4.4.2] FengChunlin   2026-06-04
* * Stage 4.4：添加超时检测机制
* [v4.2.0] GY   2026-06-04
* * Stage 4.2：支持多文件/目录传输，SHA-256 校验
* [v0.3.1] GY   2026-06-03
* * Stage 3.10：添加 cancel() 槽函数
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 3：初始版本
*/

#pragma once

#include "dir_serializer.h"
#include "protocol.h"

#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

class FrameCodec;

// 分块大小：8MB（优化吞吐量）
static constexpr qint64 kChunkSize = 8 * 1024 * 1024;
// 限制 Qt socket 写队列，避免大型文件一次性堆积到内存
static constexpr qint64 kMaxQueuedBytes = 16 * 1024 * 1024;

class FileSenderWorker : public QObject {
    Q_OBJECT

public:
    explicit FileSenderWorker(QObject *parent = nullptr);
    virtual ~FileSenderWorker() override;

    FileSenderWorker(const FileSenderWorker &)            = delete;
    FileSenderWorker &operator=(const FileSenderWorker &) = delete;

public slots:
    // 启动传输（在工作线程中调用，支持文件或目录）
    void startTransfer(const QString &host, quint16 port, const QString &path,
                       const QString &senderDeviceId, const QString &senderName);
    // 取消传输
    void cancel();

signals:
    // 传输进度更新
    void progressChanged(qint64 bytesSent, qint64 totalBytes);
    // 传输完成
    void transferFinished(bool success, gy::protocol::ErrorCode errorCode, const QString &errorMsg);
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
    // 超时处理
    void onTimeout();
    // socket 写入进度
    void onBytesWritten(qint64 bytes);

private:
    // 发送握手请求
    void sendTransferRequest();
    // 发送下一个数据块
    void sendNextChunk();
    // 在事件循环中安排下一块发送
    void scheduleNextChunk();
    // 打开下一个文件
    bool openNextFile();
    // 发送传输完成帧
    void sendTransferDone();
    // 发送取消请求
    void sendCancel(const QString &reason);
    // 清理资源
    void cleanup();

    QTcpSocket  *_socket = nullptr;       // 与接收端通信的 TCP 连接
    FrameCodec  *_codec  = nullptr;       // 接收响应帧的 TLV 解码器
    QFile        _file;                    // 当前正在读取的源文件
    QTimer      *_timeoutTimer = nullptr; // 等待响应和传输进度的超时计时器
    QString      _rootPath;         // 传入的根路径
    QString      _sessionId;              // 本次传输的唯一标识
    QString      _senderDeviceId;         // 发送端本机设备标识
    QString      _senderName;             // 发送端本机显示名称
    QString      _rootName;               // 文件或目录传输的根名称
    QStringList  _emptyDirectories;       // 目录传输中需要创建的空目录
    bool         _isDirectory = false;    // 当前任务是否为目录传输
    bool         _transferActive = false; // 是否已收到接收端确认并开始发送
    bool         _waitingForFileAck = false; // 是否等待当前文件校验确认
    bool         _sendScheduled = false;  // 是否已投递下一块发送任务
    qint64       _totalBytes = 0;         // 本次传输的文件总字节数
    qint64       _bytesSent  = 0;         // 已成功写入 socket 的总字节数

    // 多文件支持
    QList<gy::FileItem> _fileList;        // 待发送文件及其校验信息
    int          _currentFileIndex = 0;   // 当前发送文件在列表中的索引
    qint64       _currentFileBytesSent = 0; // 当前文件已发送字节数
    int          _sendChunkCount = 0;     // 用于进度节流的分块计数
};
