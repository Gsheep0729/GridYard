/**
* @file    file_receiver_worker.h
* @version 6.6.2
* @date    2026-06-23
* @author  GridYard Team
* @brief   文件接收 Worker（Worker-Object 模式）
*
* 由 P2pServer 为每个入站连接创建，运行在独立后台线程中。
* 负责接收传输请求、通知 UI 弹窗确认、接收文件数据并写入磁盘、
* SHA-256 校验、发送确认帧等完整接收流程。
* 提供 fillReceiveSession() 方法将会话信息填充到 QVariantMap。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v5.0.0] FengChunlin   2026-06-23
* * 适配首帧路由后保留的 socket 缓冲数据
* [v4.16.1] GY   2026-06-21
* * 通过值类型请求快照和完成结果传递接收状态，移除内部状态读取函数
* [v4.15.1] FengChunlin   2026-06-16
* * 连接 FrameCodec::errorOccurred 信号，协议错误时清理并结束会话
* * 进度节流 static 变量改为成员变量 _receiveChunkCount
* * sendTransferResponse/sendChunkAck 签名添加 ErrorCode 参数
* [v4.15.0] FengChunlin   2026-06-16
* * 新增 initialize() 方法，在后台线程中创建 QTimer 和连接信号
* * transferFinished 信号添加 ErrorCode 参数
* * rejectTransfer() 发射 transferFinished 信号
* * socket 父对象设为 this，随 worker 一起 moveToThread
* [v4.14.0] GY   2026-06-15
* * 提供接收完成后的实际保存路径
* [v4.13.1] DuRuoxian   2026-06-15
* * 添加目录标记与接收文件夹相对路径查询
* [v4.12.1] FengChunlin   2026-06-14
* * 校验数据块、修正文件夹累计进度，并等待最终完成确认
* [v4.11.0] FengChunlin   2026-06-13
* * 文件夹接收保留顶层目录并避免覆盖同名目标
* [v4.8.3] FengChunlin   2026-06-10
* * 传输请求中使用发送方设备别名
* [v4.4.2] FengChunlin   2026-05-30
* * Stage 4.4：添加超时检测机制
* [v4.3.4] FengChunlin   2026-05-26
* * Stage 4.3：SHA-256 校验实现，多文件接收支持
* [v4.3.1] FengChunlin   2026-05-25
* * Stage 4.3：添加文件列表成员，解析 TransferRequest 时保存所有文件信息
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 setReceivePath() 方法和 _receivePath 成员
* [v0.2.0] FengChunlin   2026-05-07
* * Stage 3：初始版本
*/

#pragma once

#include "dir_serializer.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

class FrameCodec;

class FileReceiverWorker : public QObject {
    Q_OBJECT

public:
    explicit FileReceiverWorker(QTcpSocket *socket, QObject *parent = nullptr);
    virtual ~FileReceiverWorker() override;

    FileReceiverWorker(const FileReceiverWorker &)            = delete;
    FileReceiverWorker &operator=(const FileReceiverWorker &) = delete;

    // 设置接收路径（由 TransferSessionManager 调用）
    void setReceivePath(const QString &path) { _receivePath = path; }

public slots:
    // 初始化（在后台线程中调用，创建 QTimer 并连接 socket 信号）
    void initialize();
    // 用户接受传输
    void acceptTransfer();
    // 用户拒绝传输
    void rejectTransfer(const QString &reason = "");

signals:
    // 传输请求到达（需要弹窗确认）
    void transferRequestReceived(const QVariantMap &request);
    // 传输进度更新
    void progressChanged(qint64 bytesReceived, qint64 totalBytes);
    // 传输完成
    void transferFinished(bool success, gy::protocol::ErrorCode errorCode,
                          const QString &errorMsg, const QString &savedPath);

private slots:
    // 接收数据
    void onReadyRead();
    // 连接断开
    void onDisconnected();
    // 处理收到的帧
    void onFrameReady(quint32 type, const QByteArray &payload);
    // 超时处理
    void onTimeout();

private:
    // 处理握手请求
    void handleTransferRequest(const QByteArray &payload);
    // 处理数据块
    void handleDataChunk(const QByteArray &payload);
    // 处理取消请求
    void handleCancel(const QByteArray &payload);
    // 处理传输完成请求
    void handleTransferDone();
    // 打开当前文件
    bool openCurrentFile();
    // 发送握手响应
    void sendTransferResponse(bool accepted, gy::protocol::ErrorCode errorCode = gy::protocol::ErrorCode::Success, const QString &reason = "");
    // 发送块确认
    void sendChunkAck(bool verified, gy::protocol::ErrorCode errorCode = gy::protocol::ErrorCode::Success, const QString &errorMsg = "");
    // 清理资源
    void cleanup();
    // 构建接收请求的不可变快照，跨线程交付给会话管理器
    QVariantMap receiveRequestSnapshot() const;

    QTcpSocket  *_socket = nullptr;       // 与发送端通信的 TCP 连接
    FrameCodec  *_codec  = nullptr;       // 解析入站 TLV 帧的编解码器
    QFile        _file;                    // 当前正在写入的接收文件
    QTimer      *_timeoutTimer = nullptr; // 等待确认和数据帧的超时计时器

    // 会话信息
    QString _sessionId;              // 本次传输的唯一标识
    QString _senderDeviceId;         // 发送端设备标识
    QString _senderName;             // 发送端显示名称
    QString _displayName;            // 确认弹窗和会话列表使用的名称
    QString _rootName;               // 目录传输的根目录名称
    QStringList _emptyDirectories;   // 需要在接收端创建的空目录
    int     _totalFiles = 0;         // 请求声明的文件总数
    qint64  _totalBytes = 0;         // 请求声明的文件总字节数

    QList<gy::FileItem> _fileList;        // 待接收文件列表（含 sha256）

    // 当前文件信息
    int     _currentFileIndex = 0;      // 当前接收文件在列表中的索引
    QString _fileName;                  // 当前接收文件的相对路径
    qint64  _fileSize = 0;              // 当前接收文件的总大小
    qint64  _bytesReceived = 0;         // 当前文件已写入的字节数
    qint64  _totalBytesReceived = 0;    // 本次任务累计已写入的字节数

    // 状态
    bool _waitingForUserConfirm = false; // 是否等待用户接受或拒绝
    bool _transferActive = false;        // 是否处于实际接收数据阶段
    bool _isDirectory = false;           // 当前任务是否为目录传输

    // 接收路径
    QString _receivePath;                // 配置的接收文件根目录
    QString _destinationRoot;            // 目录任务的实际目标根目录
    QString _singleFilePath;             // 单文件任务的实际目标路径

    // 增量 SHA-256 计算
    QCryptographicHash *_hash = nullptr; // 当前文件的增量 SHA-256 计算器
    int     _receiveChunkCount = 0;      // 用于进度节流的分块计数
};
