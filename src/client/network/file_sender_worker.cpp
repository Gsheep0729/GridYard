/**
* @file    file_sender_worker.cpp
* @version 7.17.2
* @date    2026-10-04
* @author  GridYard Team
* @brief   文件发送 Worker 实现
*
* 实现完整的文件发送流程：按候选端点顺序建立 TCP 连接、可选的中继握手、
* 发送传输请求、等待响应、以 8MB 分块发送数据、处理确认帧。
* 支持多文件/目录传输、背压控制、取消操作和超时检测。
*
* Change Log:
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 按接收端错误码区分发送失败文案，磁盘写入失败不再误报校验失败
* * 删除无连接的 requestAccepted/requestRejected 死信号及其发射点
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 处理接收端的 Cancel 帧：按对端取消失败终结并给出明确文案，不再落入未知帧类型继续发送至超时
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
* [v7.15.2] GY   2026-10-03
* * 中继控制行类型串改用协议常量
* [v7.14.0] GY   2026-10-03
* * relay_create 握手行携带访问令牌
* [v7.9.0] GY   2026-07-26
* * 候选端点按序轮询连接；中继模式下完成 relay_create/relay_ready 握手
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.1] GY   2026-06-21
* * 优化封装性，补充注释
* [v4.15.1] FengChunlin   2026-06-16
* * 连接 FrameCodec::errorOccurred，协议错误时 cleanup + disconnect + emit transferFinished
* * sendNextChunk 进度节流 static 改为成员 _sendChunkCount，startTransfer 时重置
* * kTypeTransferRsp 分支优先读 error_code，非默认值直接作为 errorCode 传递
* * kTypeChunkAck 分支优先读 error_code，失败时按响应携带的错误码传递
* [v4.15.0] FengChunlin   2026-06-16
* * transferFinished 信号添加 ErrorCode 参数
* [v4.12.1] FengChunlin   2026-06-14
* * 修复多文件最后一块重复读取并限制大型文件写队列
* [v4.11.0] FengChunlin   2026-06-13
* * 文件夹传输保留顶层目录并支持空文件夹
* [v4.8.3] FengChunlin   2026-06-10
* * 使用传入的设备别名作为发送方名称
* [v4.5.3] GY   2026-06-02
* * Stage 4.5：调大 socket buffer，减少进度信号频率
* [v4.4.2] FengChunlin   2026-05-30
* * Stage 4.4：添加超时检测机制
* [v4.2.0] FengChunlin   2026-05-24
* * Stage 4.2：支持多文件/目录传输，SHA-256 校验
* [v0.2.0] DuRuoxian   2026-05-17
* * Stage 3：初始版本
*/

#include "file_sender_worker.h"
#include "rendezvous_protocol_keys.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTimer>
#include <QUuid>

// 传输超时时间：30 秒，超过该时间没有网络进展则判定失败
static constexpr int kTimeoutMs = 30000;
// 单个候选端点的连接超时
static constexpr int kConnectTimeoutMs = 5000;
// 中继等待接收端加入的超时：覆盖接收端轮询延迟（心跳周期）与用户确认链路
static constexpr int kRelayReadyTimeoutMs = 30000;

// 构造函数，初始化 TCP socket、FrameCodec 和超时定时器
FileSenderWorker::FileSenderWorker(QObject *parent)
    : QObject{parent}
    , _socket{new QTcpSocket{this}}
    , _codec{new FrameCodec{this}}
    , _timeoutTimer{new QTimer{this}}
{
    // 禁用代理，避免局域网连接被代理拦截
    QNetworkProxy noProxy;
    noProxy.setType(QNetworkProxy::NoProxy);
    _socket->setProxy(noProxy);
    connect(_socket, &QTcpSocket::readyRead,
            this,    &FileSenderWorker::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected,
            this,    &FileSenderWorker::onDisconnected);
    connect(_socket, &QTcpSocket::bytesWritten,
            this,    &FileSenderWorker::onBytesWritten);
    connect(_codec,  &FrameCodec::frameReady,
            this,    &FileSenderWorker::onFrameReady);
    connect(_codec,  &FrameCodec::errorOccurred,
            this,    [this](gy::protocol::ErrorCode errorCode, const QString &errorMsg) {
        qWarning() << "[FileSender] 协议错误:" << errorMsg;
        _socket->disconnectFromHost();
        finish(false, errorCode, errorMsg);
    });

    // 超时定时器
    _timeoutTimer->setSingleShot(true);
    connect(_timeoutTimer, &QTimer::timeout,
            this,          &FileSenderWorker::onTimeout);
}

// 析构函数，清理传输资源
FileSenderWorker::~FileSenderWorker()
{
    cleanup();
}

// 发起文件传输：序列化文件列表、按候选顺序建立 TCP 连接、发送传输请求
void FileSenderWorker::startTransfer(const QString &host, quint16 port,
                                     const QString &path, const QString &senderDeviceId,
                                     const QString &senderName)
{
    startTransfer({{host, port}}, path, senderDeviceId, senderName);
}

// 发起文件传输：候选端点按序尝试，relayId 非空时先完成中继握手
void FileSenderWorker::startTransfer(const QList<QPair<QString, quint16>> &endpoints,
                                     const QString &path, const QString &senderDeviceId,
                                     const QString &senderName, const QString &relayId)
{
    qDebug() << "[FileSender] 开始传输流程";
    qDebug() << "[FileSender] 候选端点数量:" << endpoints.size();
    qDebug() << "[FileSender] 文件路径:" << path;

    _rootPath = path;
    _senderDeviceId = senderDeviceId;
    _senderName = senderName;
    _sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    _relayId = relayId;
    _relayReady = false;
    _relayLineBuffer.clear();

    _fileList = gy::DirSerializer::serialize(path);
    const QFileInfo rootInfo{path};
    _isDirectory = rootInfo.isDir();
    _rootName = rootInfo.fileName();
    _emptyDirectories.clear();

    for (qsizetype i = _fileList.size(); i-- > 0;) {
        // 空目录只作为请求元数据发送，不参与后续 DataChunk 发送状态机。
        if (_fileList[i].relativePath.endsWith('/')) {
            _emptyDirectories.prepend(_fileList.takeAt(i).relativePath);
        }
    }

    if (_fileList.isEmpty() && !_isDirectory) {
        qWarning() << "[FileSender] 序列化文件列表为空，没有可传输的文件";
        finish(false, gy::protocol::ErrorCode::InvalidPayload, tr("没有可传输的文件"));
        return;
    }

    // 计算总字节数
    _totalBytes = 0;
    for (const auto &item : _fileList) {
        _totalBytes += item.sizeBytes;
    }

    _bytesSent = 0;
    _currentFileIndex = 0;
    _currentFileBytesSent = 0;
    _sendChunkCount = 0;
    _transferActive = false;
    _waitingForFileAck = false;
    _sendScheduled = false;

    qDebug() << "[FileSender] 文件列表大小:" << _fileList.size();
    qDebug() << "[FileSender] 总字节数:" << _totalBytes;

    // 打开第一个文件；空文件夹没有文件数据，只发送目录元数据
    if (!_fileList.isEmpty() && !openNextFile()) {
        qWarning() << "[FileSender] 无法打开第一个文件";
        finish(false, gy::protocol::ErrorCode::InvalidFilePath, tr("无法打开文件"));
        return;
    }

    // 候选端点按序连接，全部失败才算连接失败（候选轮询：直连优先，备选兜底）
    bool connected = false;
    QString lastError;
    for (const auto &endpoint : endpoints) {
        // 阻塞等待不可中断，只能在重试之间检查取消标志（见护栏第 7 条）
        if (_cancelRequested.load()) {
            _socket->abort();
            finish(false, gy::protocol::ErrorCode::UserCancelled, tr("已取消"));
            return;
        }
        qDebug() << "[FileSender] 正在连接到" << endpoint.first << ":" << endpoint.second;
        _socket->connectToHost(endpoint.first, endpoint.second);
        // 连接建立最多等待 5 秒，避免离线设备导致发送线程长时间卡住。
        if (_socket->waitForConnected(kConnectTimeoutMs)) {
            connected = true;
            break;
        }
        lastError = _socket->errorString();
        qWarning() << "[FileSender] 候选端点连接失败:" << endpoint.first << lastError;
        _socket->abort();  // 放弃仍在进行的连接尝试，避免半开连接残留
    }

    if (!connected) {
        finish(false, gy::protocol::ErrorCode::ConnectionTimeout, tr("连接失败: %1").arg(lastError));
        return;
    }

    qDebug() << "[FileSender] 连接成功";

    // 中继模式：先发送 relay_create 握手并等待接收端加入，会话就绪后再进入 TLV 流程，
    // 否则中继会丢弃接收端加入之前到达的字节
    if (!_relayId.isEmpty() && !waitForRelayReady()) {
        return;
    }
    // relay_ready 行后可能同批到达少量 TLV 字节，补喂给解码器
    if (!_relayLineBuffer.isEmpty()) {
        _codec->feed(_relayLineBuffer);
        _relayLineBuffer.clear();
    }

    // 扩大收发缓冲到 4MB，配合分块发送降低大文件吞吐波动。
    _socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    _socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

    if (!sendTransferRequest()) {
        return;
    }

    // 启动超时定时器
    _timeoutTimer->start(_timeoutMs);
}

// 设置中继握手的访问令牌
void FileSenderWorker::setRelayToken(const QString &token)
{
    _relayToken = token;
}

// 请求取消：任意线程可调，只置位原子标志；
// 实际终结由工作线程在阻塞等待间隙检查标志后走 finish() 统一出口
void FileSenderWorker::requestCancel()
{
    _cancelRequested.store(true);
}

// 设置传输超时毫秒数
void FileSenderWorker::setTimeoutMs(int timeoutMs)
{
    if (timeoutMs > 0) {
        _timeoutMs = timeoutMs;
    }
}

// 中继握手：发送 relay_create 行并等待 relay_ready，失败时终结会话
bool FileSenderWorker::waitForRelayReady()
{
    QJsonObject hello;
    hello[gy::rendezvous::kKeyType] = gy::rendezvous::kTypeRelayCreate;
    hello[gy::rendezvous::kKeyRelayId] = _relayId;
    if (!_relayToken.isEmpty()) {
        hello[gy::rendezvous::kKeyToken] = _relayToken;
    }
    QByteArray helloLine = QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n';
    if (_socket->write(helloLine) != helloLine.size()) {
        finish(false, gy::protocol::ErrorCode::ConnectionLost,
               tr("中继握手写入失败: %1").arg(_socket->errorString()));
        return false;
    }

    qDebug() << "[FileSender] 已发送中继握手，等待接收端加入";
    QElapsedTimer elapsed;
    elapsed.start();
    while (!_relayReady) {
        // 忙等期间事件循环不跑，排队的 cancel() 不可达，靠原子标志感知取消
        if (_cancelRequested.load()) {
            _socket->abort();
            finish(false, gy::protocol::ErrorCode::UserCancelled, tr("已取消"));
            return false;
        }
        if (_socket->state() != QAbstractSocket::ConnectedState) {
            finish(false, gy::protocol::ErrorCode::ConnectionLost, tr("中继连接已断开"));
            return false;
        }
        if (elapsed.hasExpired(kRelayReadyTimeoutMs)) {
            finish(false, gy::protocol::ErrorCode::TransferTimeout, tr("等待接收端加入中继超时"));
            return false;
        }
        if (!_socket->waitForReadyRead(500)) {
            continue;
        }
        if (!processRelayControlData(_socket->readAll())) {
            return false;  // processRelayControlData 内部已终结会话
        }
    }

    qDebug() << "[FileSender] 中继会话就绪";
    return true;
}

// 解析中继控制行（relay_ready / relay_error），返回 false 表示会话已终结
bool FileSenderWorker::processRelayControlData(const QByteArray &data)
{
    _relayLineBuffer += data;

    int newlineIndex;
    while ((newlineIndex = _relayLineBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = _relayLineBuffer.left(newlineIndex);
        _relayLineBuffer.remove(0, newlineIndex + 1);

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QString type = doc.object()[QStringLiteral("type")].toString();
        if (type == gy::rendezvous::kTypeRelayReady) {
            _relayReady = true;
        } else if (type == gy::rendezvous::kTypeRelayError) {
            finish(false, gy::protocol::ErrorCode::ConnectionLost,
                   tr("中继传输失败: %1").arg(doc.object()[QStringLiteral("message")].toString()));
            return false;
        }
    }

    return true;
}

// 处理 socket 可读数据，喂入 FrameCodec 解码并重置超时
void FileSenderWorker::onReadyRead()
{
    // 中继会话就绪前到达的是控制行，不能喂给 TLV 解码器
    if (!_relayId.isEmpty() && !_relayReady) {
        processRelayControlData(_socket->readAll());
        return;
    }

    _codec->feed(_socket->readAll());

    if (_transferActive) {
        _timeoutTimer->start(_timeoutMs);
    }
}

// 处理连接断开事件，传输活跃时清理资源并通知失败
void FileSenderWorker::onDisconnected()
{
    if (_transferActive) {
        finish(false, gy::protocol::ErrorCode::ConnectionLost, tr("连接断开"));
    }
}

// 处理传输超时，清理资源并通知超时失败
void FileSenderWorker::onTimeout()
{
    if (_finished) {
        return;
    }
    // 超时覆盖两类停滞：等待响应期间（传输未激活，对端连接后不应答）与
    // 数据传输期间（已激活，对端停止确认），否则前者会无限悬挂
    qWarning() << "FileSenderWorker: 传输超时";
    finish(false, gy::protocol::ErrorCode::TransferTimeout, tr("传输超时"));
}

// 处理数据写入完成回调，写队列有空间时调度发送下一块
void FileSenderWorker::onBytesWritten(qint64)
{
    if (!_transferActive) {
        return;
    }

    _timeoutTimer->start(_timeoutMs);
    if (!_waitingForFileAck && _socket->bytesToWrite() <= kMaxQueuedBytes) {
        scheduleNextChunk();
    }
}

// 清理传输资源：停止定时器、关闭文件
void FileSenderWorker::cleanup()
{
    // 停止超时定时器
    _timeoutTimer->stop();

    // 关闭文件
    if (_file.isOpen()) {
        _file.close();
    }
    _transferActive = false;
    _sendScheduled = false;
}

// 统一终结出口：cleanup 后只发射一次 transferFinished
// 取消会主动断连、断连又触发 onDisconnected，若各自 emit 会导致上层重复持久化甚至操作已删除的 worker
void FileSenderWorker::finish(bool success, gy::protocol::ErrorCode errorCode, const QString &errorMsg)
{
    if (_finished) {
        return;
    }
    _finished = true;
    cleanup();
    emit transferFinished(success, errorCode, errorMsg);
}

// 成功终结前先把写缓冲刷到内核，否则上层会立即拆线程销毁 socket，
// 末尾的 TransferDone 可能还没发出，接收端就会一直等到超时误判失败
void FileSenderWorker::finishAfterSend()
{
    if (_finished) {
        return;
    }
    // 局域网下等待时间很短；即便超时也继续终结，可靠交付由接收端的完成确认兜底
    if (_socket && _socket->state() == QAbstractSocket::ConnectedState
        && _socket->bytesToWrite() > 0) {
        _socket->waitForBytesWritten(3000);
    }
    finish(true, gy::protocol::ErrorCode::Success, QString());
}

// 处理收到的响应帧：传输响应决定是否开始发送，块确认决定是否继续下一文件
void FileSenderWorker::onFrameReady(quint32 type, const QByteArray &payload)
{
    qDebug() << "[FileSender] 收到帧，类型:" << type;

    switch (type) {
    case gy::protocol::kTypeTransferRsp: {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject json = doc.object();

        bool accepted = json["accepted"].toBool();
        int errorCodeInt = json["error_code"].toInt(static_cast<int>(gy::protocol::ErrorCode::Success));
        auto errorCode = static_cast<gy::protocol::ErrorCode>(errorCodeInt);
        QString reason = json["reason"].toString();

        qDebug() << "[FileSender] 收到传输响应:" << (accepted ? "接受" : "拒绝");
        if (!accepted) {
            qDebug() << "[FileSender] 拒绝原因:" << reason << "错误码:" << errorCodeInt;
        }

        if (accepted) {
            _transferActive = true;
            _timeoutTimer->start(_timeoutMs);
            if (_fileList.isEmpty()) {
                if (!sendTransferDone()) {
                    return;
                }
                finishAfterSend();  // 空文件夹：等 TransferDone 落网后再终结
            } else {
                scheduleNextChunk();
            }
        } else {
            // 若响应携带了非默认错误码则优先使用，否则按 UserRejected 处理
            if (errorCode == gy::protocol::ErrorCode::Success) {
                errorCode = gy::protocol::ErrorCode::UserRejected;
            }
            // 磁盘类失败并非对方主动拒绝，标题按错误码区分避免误导排查方向
            const bool diskFailure = errorCode == gy::protocol::ErrorCode::DiskWriteFailed
                                     || errorCode == gy::protocol::ErrorCode::DiskSpaceInsufficient;
            finish(false, errorCode,
                   diskFailure ? tr("接收端写入失败（磁盘空间或权限）: %1").arg(reason)
                               : tr("请求被拒绝: %1").arg(reason));
        }
        break;
    }
    case gy::protocol::kTypeChunkAck: {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject json = doc.object();

        bool verified = json["verified"].toBool();
        int fileIndex = json["file_index"].toInt();
        int errorCodeInt = json["error_code"].toInt(static_cast<int>(gy::protocol::ErrorCode::Success));
        auto errorCode = static_cast<gy::protocol::ErrorCode>(errorCodeInt);
        QString errorMsg = json["error_msg"].toString();

        qDebug() << "[FileSender] 收到块确认，文件索引:" << fileIndex
                 << "校验结果:" << (verified ? "通过" : "失败");

        if (fileIndex != _currentFileIndex) {
            finish(false, gy::protocol::ErrorCode::InvalidPayload, tr("收到无效的文件确认"));
            return;
        }

        if (!verified) {
            qWarning() << "[FileSender] 文件确认失败:" << errorMsg << "错误码:" << errorCodeInt;
            // 优先使用响应携带的错误码
            if (errorCode == gy::protocol::ErrorCode::Success) {
                errorCode = gy::protocol::ErrorCode::Sha256Mismatch;
            }
            // 磁盘类失败不是校验失败，按错误码区分文案避免误导排查方向
            QString message;
            if (errorCode == gy::protocol::ErrorCode::DiskWriteFailed
                || errorCode == gy::protocol::ErrorCode::DiskSpaceInsufficient) {
                message = tr("文件 %1 接收端写入失败（磁盘空间或权限）: %2")
                              .arg(fileIndex).arg(errorMsg);
            } else if (errorCode == gy::protocol::ErrorCode::Sha256Mismatch) {
                message = tr("文件 %1 校验失败: %2").arg(fileIndex).arg(errorMsg);
            } else {
                message = tr("接收端报告文件 %1 错误: %2").arg(fileIndex).arg(errorMsg);
            }
            finish(false, errorCode, message);
            return;
        }

        // 当前文件校验通过，继续下一个
        _waitingForFileAck = false;
        _currentFileIndex++;
        _currentFileBytesSent = 0;

        qDebug() << "[FileSender] 文件" << fileIndex << "传输完成，准备下一个文件";

        if (_currentFileIndex < _fileList.size()) {
            // 还有文件要发
            if (!openNextFile()) {
                qWarning() << "[FileSender] 无法打开文件" << _fileList[_currentFileIndex].relativePath;
                finish(false, gy::protocol::ErrorCode::InvalidFilePath, tr("无法打开文件 %1")
                                              .arg(_fileList[_currentFileIndex].relativePath));
                return;
            }
            scheduleNextChunk();
        } else {
            // 所有文件发完
            qDebug() << "[FileSender] 所有文件传输完成";
            if (!sendTransferDone()) {
                return;
            }
            finishAfterSend();  // 等 TransferDone 落网后再终结，避免接收端收不到而误判超时
        }
        break;
    }
    case gy::protocol::kTypeCancel: {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        qDebug() << "[FileSender] 收到对端取消:"
                 << doc.object()["reason"].toString();
        // 接收端已取消并断开：必须按对端取消失败终结，
        // 否则接收端无文件而发送端显示成功，两端状态矛盾
        finish(false, gy::protocol::ErrorCode::UserCancelled, tr("对端取消了传输"));
        break;
    }
    default:
        qDebug() << "[FileSender] 未知帧类型:" << type;
        break;
    }
}

// 构建并发送传输请求帧（文件列表、总大小、协议版本等）
bool FileSenderWorker::sendTransferRequest()
{
    qDebug() << "[FileSender] 发送传输请求";

    QJsonObject json;
    json["session_id"]  = _sessionId;
    json["sender_device_id"] = _senderDeviceId;
    json["sender_name"] = _senderName;
    json["is_directory"] = _isDirectory;
    json["root_name"] = _rootName;
    json["total_files"] = _fileList.size();
    json["total_bytes"] = _totalBytes;
    json["protocol_version"] = gy::protocol::kProtocolVersion;

    QJsonArray files;
    for (int i = 0; i < _fileList.size(); ++i) {
        QJsonObject fileObj;
        fileObj["file_index"]    = i;
        fileObj["relative_path"] = _fileList[i].relativePath;
        fileObj["size_bytes"]    = _fileList[i].sizeBytes;
        fileObj["sha256"]        = _fileList[i].sha256;
        files.append(fileObj);
    }
    json["files"] = files;

    QJsonArray directories;
    for (const QString &relativePath : _emptyDirectories) {
        directories.append(relativePath);
    }
    json["empty_directories"] = directories;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (!writeControlFrame(gy::protocol::kTypeTransferReq, data, tr("发送传输请求"))) {
        return false;
    }

    qDebug() << "[FileSender] 传输请求已发送，会话ID:" << _sessionId;
    return true;
}

// 读取当前文件的数据并构建 DataChunk 帧发送，带背压控制
void FileSenderWorker::sendNextChunk()
{
    _sendScheduled = false;

    if (!_transferActive || _waitingForFileAck || !_file.isOpen()
        || _currentFileIndex >= _fileList.size()) {
        return;
    }

    const qint64 currentFileSize = _fileList[_currentFileIndex].sizeBytes;
    QByteArray chunkData = _file.read(kChunkSize);

    // 处理零字节文件：直接发送 isLastChunk=1
    if (chunkData.isEmpty() && currentFileSize == 0 && _currentFileBytesSent == 0) {
        chunkData = QByteArray();  // 空数据
        qDebug() << "[FileSender] 处理零字节文件";
    } else if (chunkData.isEmpty()) {
        qWarning() << "[FileSender] 读取文件失败";
        finish(false, gy::protocol::ErrorCode::DiskWriteFailed, tr("读取文件失败"));
        return;
    }

    // 构建 20 字节 chunk 元数据：fileIndex(4) + offset(8) + size(4) + isLast(4)。
    QByteArray metadata;
    QDataStream stream(&metadata, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 fileIndex = static_cast<quint32>(_currentFileIndex);
    quint64 chunkOffset = static_cast<quint64>(_currentFileBytesSent);
    quint32 chunkSize = static_cast<quint32>(chunkData.size());
    quint32 isLastChunk = (_currentFileBytesSent + chunkData.size() >= currentFileSize) ? 1 : 0;

    stream << fileIndex;
    stream << chunkOffset;
    stream << chunkSize;
    stream << isLastChunk;

    QByteArray payload;
    payload.reserve(20 + chunkData.size());
    payload.append(metadata);
    payload.append(chunkData);

    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeDataChunk, payload);
    if (_socket->write(frame) < 0) {
        const QString errorMsg = _socket->errorString();
        finish(false, gy::protocol::ErrorCode::ConnectionLost, tr("发送数据失败: %1").arg(errorMsg));
        return;
    }

    _bytesSent += chunkData.size();
    _currentFileBytesSent += chunkData.size();

    // 减少信号发射频率：每 4 个 chunk 发射一次（约 32MB）
    if (++_sendChunkCount % 4 == 0 || isLastChunk == 1) {
        const qint64 percent = _totalBytes > 0 ? (_bytesSent * 100 / _totalBytes) : 100;
        qDebug() << "[FileSender] 传输进度:" << _bytesSent << "/" << _totalBytes
                 << "(" << percent << "%)";
        emit progressChanged(_bytesSent, _totalBytes);
    }

    _timeoutTimer->start(_timeoutMs);

    if (isLastChunk == 1) {
        _waitingForFileAck = true;
    } else if (_socket->bytesToWrite() <= kMaxQueuedBytes) {
        scheduleNextChunk();
    }
}

// 调度下一次数据块发送，避免重复调度
void FileSenderWorker::scheduleNextChunk()
{
    if (_sendScheduled || !_transferActive || _waitingForFileAck) {
        return;
    }

    _sendScheduled = true;
    QTimer::singleShot(0, this, &FileSenderWorker::sendNextChunk);
}

// 关闭当前文件并打开下一个待发送的文件
bool FileSenderWorker::openNextFile()
{
    if (_file.isOpen()) {
        _file.close();
    }

    if (_currentFileIndex >= _fileList.size()) {
        return false;
    }

    // 构建完整路径
    QString fullPath = _rootPath;
    QFileInfo rootInfo(_rootPath);
    if (rootInfo.isDir()) {
        fullPath = _rootPath + "/" + _fileList[_currentFileIndex].relativePath;
    }

    _file.setFileName(fullPath);
    if (!_file.open(QIODevice::ReadOnly)) {
        return false;
    }

    return true;
}

// 发送传输完成帧，通知接收端所有文件已发送完毕
bool FileSenderWorker::sendTransferDone()
{
    QJsonObject json;
    json["session_id"] = _sessionId;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    return writeControlFrame(gy::protocol::kTypeTransferDone, data, tr("发送完成帧"));
}

// 发送取消传输帧并关闭连接
bool FileSenderWorker::sendCancel(const QString &reason)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["reason"] = reason;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeCancel, data);
    const qint64 written = _socket->write(frame);
    if (written != frame.size()) {
        qWarning() << "[FileSender] 取消帧写入失败:" << _socket->errorString();
    }

    _socket->disconnectFromHost();
    if (_file.isOpen()) {
        _file.close();
    }
    return written == frame.size();
}

// 写入控制帧并检查 socket 接受的字节数
bool FileSenderWorker::writeControlFrame(quint32 type, const QByteArray &payload,
                                         const QString &description,
                                         gy::protocol::ErrorCode errorCode)
{
    const QByteArray frame = FrameCodec::encode(type, payload);
    const qint64 written = _socket->write(frame);
    if (written == frame.size()) {
        return true;
    }

    const QString socketError = _socket->errorString().isEmpty()
        ? tr("socket 写入失败")
        : _socket->errorString();
    finish(false, errorCode, tr("%1失败: %2").arg(description, socketError));
    return false;
}

// 用户取消传输，发送取消帧并通知完成信号
void FileSenderWorker::cancel()
{
    if (_finished) {
        return;
    }
    _cancelRequested.store(true);
    // 先发取消帧（此时 socket 尚未关闭），再走统一终结出口。
    // finish() 内部 cleanup 会把 _transferActive 置 false，随后 disconnectFromHost
    // 触发的 onDisconnected 因此不会再次终结，配合 _finished 标志杜绝重复发射。
    sendCancel(tr("用户取消"));
    finish(false, gy::protocol::ErrorCode::UserCancelled, tr("已取消"));
}
