/**
* @file    file_receiver_worker.h
* @version 4.14.0
* @date    2026-06-15
* @author  GridYard Team
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
* [v4.14.0] GY   2026-06-15
* * 提供接收完成后的实际保存路径
* [v4.13.1] FengChunlin   2026-06-15
* * 添加目录标记与接收文件夹相对路径查询
* [v4.12.1] FengChunlin   2026-06-14
* * 校验数据块、修正文件夹累计进度，并等待最终完成确认
* [v4.11.0] FengChunlin   2026-06-13
* * 文件夹接收保留顶层目录并避免覆盖同名目标
* [v4.8.3] FengChunlin   2026-06-13
* * 传输请求中使用发送方设备别名
* [v4.4.2] FengChunlin   2026-06-04
* * Stage 4.4：添加超时检测机制
* [v4.3.4] FengChunlin   2026-06-04
* * Stage 4.3：SHA-256 校验实现，多文件接收支持
* [v4.3.1] FengChunlin   2026-06-04
* * Stage 4.3：添加文件列表成员，解析 TransferRequest 时保存所有文件信息
* [v0.3.0] FengChunlin   2026-06-03
* * 添加 setReceivePath() 方法和 _receivePath 成员
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 3：初始版本
*/

#pragma once

#include "dir_serializer.h"

#include <QCryptographicHash>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

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
    QString senderDeviceId() const { return _senderDeviceId; }
    QString senderName()   const { return _senderName; }
    QString fileName()     const { return _displayName; }
    qint64  fileSize()     const { return _fileSize; }
    bool    isDirectory()  const { return _isDirectory; }
    QStringList filePaths() const;
    // 返回同名避让后实际写入的文件或文件夹路径
    QString savedPath() const;

    // 设置接收路径（由 TransferSessionManager 调用）
    void setReceivePath(const QString &path) { _receivePath = path; }

public slots:
    // 用户接受传输
    void acceptTransfer();
    // 用户拒绝传输
    void rejectTransfer(const QString &reason = "");

signals:
    // 传输请求到达（需要弹窗确认）
    void transferRequestReceived(const QString &senderDeviceId,
                                 const QString &senderName,
                                 const QString &fileName,
                                 qint64 fileSize,
                                 int totalFiles,
                                 qint64 totalBytes);
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
    void sendTransferResponse(bool accepted, const QString &reason = "");
    // 发送块确认
    void sendChunkAck(bool verified, const QString &errorMsg = "");
    // 清理资源
    void cleanup();

    QTcpSocket  *_socket = nullptr;
    FrameCodec  *_codec  = nullptr;
    QFile        _file;
    QTimer      *_timeoutTimer = nullptr;

    // 会话信息
    QString _sessionId;
    QString _senderDeviceId;
    QString _senderName;
    QString _displayName;
    QString _rootName;
    QStringList _emptyDirectories;
    int     _totalFiles = 0;
    qint64  _totalBytes = 0;

    // 文件列表（含 sha256）
    QList<gy::FileItem> _fileList;

    // 当前文件信息
    int     _currentFileIndex = 0;
    QString _fileName;
    qint64  _fileSize = 0;
    qint64  _bytesReceived = 0;
    qint64  _totalBytesReceived = 0;

    // 状态
    bool _waitingForUserConfirm = false;
    bool _transferActive = false;
    bool _isDirectory = false;

    // 接收路径
    QString _receivePath;
    QString _destinationRoot;
    QString _singleFilePath;

    // 增量 SHA-256 计算
    QCryptographicHash *_hash = nullptr;
};
