/**
* @file    file_sender_worker.cpp
* @version 4.15.0
* @date    2026-06-17
* @author  GridYard Team
* @brief   FileSenderWorker 实现
*
* Change Log:
* [v4.15.1] FengChunlin   2026-06-17
* * 连接 FrameCodec::errorOccurred，协议错误时 cleanup + disconnect + emit transferFinished
* * sendNextChunk 进度节流 static 改为成员 _sendChunkCount，startTransfer 时重置
* * kTypeTransferRsp 分支优先读 error_code，非默认值直接作为 errorCode 传递
* * kTypeChunkAck 分支优先读 error_code，失败时按响应携带的错误码传递
* [v4.15.0] GY   2026-06-17
* * transferFinished 信号添加 ErrorCode 参数
* [v4.12.1] FengChunlin   2026-06-14
* * 修复多文件最后一块重复读取并限制大型文件写队列
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
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 3：初始版本
*/

#include "file_sender_worker.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QTimer>
#include <QUuid>

// 超时时间：30 秒
static constexpr int kTimeoutMs = 30000;

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
        cleanup();
        _socket->disconnectFromHost();
        emit transferFinished(false, errorCode, errorMsg);
    });

    // 超时定时器
    _timeoutTimer->setSingleShot(true);
    connect(_timeoutTimer, &QTimer::timeout,
            this,          &FileSenderWorker::onTimeout);
}

FileSenderWorker::~FileSenderWorker()
{
    cleanup();
}

void FileSenderWorker::startTransfer(const QString &host, quint16 port,
                                     const QString &path, const QString &senderDeviceId,
                                     const QString &senderName)
{
    qDebug() << "[FileSender] 开始传输流程";
    qDebug() << "[FileSender] 目标地址:" << host << ":" << port;
    qDebug() << "[FileSender] 文件路径:" << path;

    _rootPath = path;
    _senderDeviceId = senderDeviceId;
    _senderName = senderName;
    _sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 序列化文件列表
    _fileList = gy::DirSerializer::serialize(path);
    const QFileInfo rootInfo{path};
    _isDirectory = rootInfo.isDir();
    _rootName = rootInfo.fileName();
    _emptyDirectories.clear();

    for (qsizetype i = _fileList.size(); i-- > 0;) {
        if (_fileList[i].relativePath.endsWith('/')) {
            _emptyDirectories.prepend(_fileList.takeAt(i).relativePath);
        }
    }

    if (_fileList.isEmpty() && !_isDirectory) {
        qWarning() << "[FileSender] 序列化文件列表为空，没有可传输的文件";
        emit transferFinished(false, gy::protocol::ErrorCode::InvalidPayload, tr("没有可传输的文件"));
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
        emit transferFinished(false, gy::protocol::ErrorCode::InvalidFilePath, tr("无法打开文件"));
        return;
    }

    // 连接到接收端
    qDebug() << "[FileSender] 正在连接到" << host << ":" << port;
    _socket->connectToHost(host, port);
    if (!_socket->waitForConnected(5000)) {
        qWarning() << "[FileSender] 连接失败:" << _socket->errorString();
        emit transferFinished(false, gy::protocol::ErrorCode::ConnectionTimeout, tr("连接超时: %1").arg(_socket->errorString()));
        return;
    }

    qDebug() << "[FileSender] 连接成功";

    // 优化 socket buffer
    _socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 4 * 1024 * 1024);
    _socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);

    sendTransferRequest();

    // 启动超时定时器
    _timeoutTimer->start(kTimeoutMs);
}

void FileSenderWorker::onReadyRead()
{
    _codec->feed(_socket->readAll());

    if (_transferActive) {
        _timeoutTimer->start(kTimeoutMs);
    }
}

void FileSenderWorker::onDisconnected()
{
    if (_transferActive) {
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::ConnectionLost, tr("连接断开"));
    }
}

void FileSenderWorker::onTimeout()
{
    if (_transferActive) {
        qWarning() << "FileSenderWorker: 传输超时";
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::TransferTimeout, tr("传输超时"));
    }
}

void FileSenderWorker::onBytesWritten(qint64)
{
    if (!_transferActive) {
        return;
    }

    _timeoutTimer->start(kTimeoutMs);
    if (!_waitingForFileAck && _socket->bytesToWrite() <= kMaxQueuedBytes) {
        scheduleNextChunk();
    }
}

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
            emit requestAccepted();
            _transferActive = true;
            _timeoutTimer->start(kTimeoutMs);
            if (_fileList.isEmpty()) {
                sendTransferDone();
                cleanup();
                emit transferFinished(true, gy::protocol::ErrorCode::Success, "");
            } else {
                scheduleNextChunk();
            }
        } else {
            cleanup();
            emit requestRejected(reason);
            // 若响应携带了非默认错误码则优先使用，否则按 UserRejected 处理
            if (errorCode == gy::protocol::ErrorCode::Success) {
                errorCode = gy::protocol::ErrorCode::UserRejected;
            }
            emit transferFinished(false, errorCode, tr("请求被拒绝: %1").arg(reason));
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
            cleanup();
            emit transferFinished(false, gy::protocol::ErrorCode::InvalidPayload, tr("收到无效的文件确认"));
            return;
        }

        if (!verified) {
            qWarning() << "[FileSender] 文件校验失败:" << errorMsg;
            cleanup();
            // 优先使用响应携带的错误码
            if (errorCode == gy::protocol::ErrorCode::Success) {
                errorCode = gy::protocol::ErrorCode::Sha256Mismatch;
            }
            emit transferFinished(false, errorCode, tr("文件 %1 校验失败: %2")
                                          .arg(fileIndex).arg(errorMsg));
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
                cleanup();
                emit transferFinished(false, gy::protocol::ErrorCode::InvalidFilePath, tr("无法打开文件 %1")
                                              .arg(_fileList[_currentFileIndex].relativePath));
                return;
            }
            scheduleNextChunk();
        } else {
            // 所有文件发完
            qDebug() << "[FileSender] 所有文件传输完成";
            sendTransferDone();
            cleanup();
            emit transferFinished(true, gy::protocol::ErrorCode::Success, "");
        }
        break;
    }
    default:
        qDebug() << "[FileSender] 未知帧类型:" << type;
        break;
    }
}

void FileSenderWorker::sendTransferRequest()
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
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
    _socket->write(frame);

    qDebug() << "[FileSender] 传输请求已发送，会话ID:" << _sessionId;
}

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
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::DiskWriteFailed, tr("读取文件失败"));
        return;
    }

    // 构建 chunk 元数据（20 字节）
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
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::ConnectionLost, tr("发送数据失败: %1").arg(errorMsg));
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

    _timeoutTimer->start(kTimeoutMs);

    if (isLastChunk == 1) {
        _waitingForFileAck = true;
    } else if (_socket->bytesToWrite() <= kMaxQueuedBytes) {
        scheduleNextChunk();
    }
}

void FileSenderWorker::scheduleNextChunk()
{
    if (_sendScheduled || !_transferActive || _waitingForFileAck) {
        return;
    }

    _sendScheduled = true;
    QTimer::singleShot(0, this, &FileSenderWorker::sendNextChunk);
}

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

void FileSenderWorker::sendTransferDone()
{
    QJsonObject json;
    json["session_id"] = _sessionId;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferDone, data);
    _socket->write(frame);
}

void FileSenderWorker::sendCancel(const QString &reason)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["reason"] = reason;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeCancel, data);
    _socket->write(frame);

    _socket->disconnectFromHost();
    if (_file.isOpen()) {
        _file.close();
    }
}

void FileSenderWorker::cancel()
{
    sendCancel(tr("用户取消"));
    emit transferFinished(false, gy::protocol::ErrorCode::UserCancelled, tr("已取消"));
}
