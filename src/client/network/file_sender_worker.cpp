/**
* @file    file_sender_worker.cpp
* @date    2026-06-02
* @author  GY
* @brief   FileSenderWorker 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
* [v0.2] GY   2026-06-04
* * Stage 4：支持多文件/目录传输，SHA-256 校验
* [v0.3] GY   2026-06-04
* * Stage 4.4：添加超时检测机制
*/

#include "file_sender_worker.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
    connect(_socket, &QTcpSocket::readyRead,
            this,    &FileSenderWorker::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected,
            this,    &FileSenderWorker::onDisconnected);
    connect(_codec,  &FrameCodec::frameReady,
            this,    &FileSenderWorker::onFrameReady);

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
                                     const QString &path)
{
    _rootPath = path;
    _sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 序列化文件列表
    _fileList = gy::DirSerializer::serialize(path);
    if (_fileList.isEmpty()) {
        emit transferFinished(false, tr("没有可传输的文件"));
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

    // 打开第一个文件
    if (!openNextFile()) {
        emit transferFinished(false, tr("无法打开文件"));
        return;
    }

    // 连接到接收端
    _socket->connectToHost(host, port);
    if (!_socket->waitForConnected(5000)) {
        emit transferFinished(false, tr("连接超时: %1").arg(_socket->errorString()));
        return;
    }

    sendTransferRequest();

    // 启动超时定时器
    _timeoutTimer->start(kTimeoutMs);
}

void FileSenderWorker::onReadyRead()
{
    _codec->feed(_socket->readAll());

    // 重置超时定时器
    if (_bytesSent < _totalBytes) {
        _timeoutTimer->start(kTimeoutMs);
    }
}

void FileSenderWorker::onDisconnected()
{
    if (_bytesSent < _totalBytes) {
        cleanup();
        emit transferFinished(false, tr("连接断开"));
    }
}

void FileSenderWorker::onTimeout()
{
    if (_bytesSent < _totalBytes) {
        qWarning() << "FileSenderWorker: 传输超时";
        cleanup();
        emit transferFinished(false, tr("传输超时"));
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
}

void FileSenderWorker::onFrameReady(quint32 type, const QByteArray &payload)
{
    switch (type) {
    case gy::protocol::kTypeTransferRsp: {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject json = doc.object();

        bool accepted = json["accepted"].toBool();
        if (accepted) {
            emit requestAccepted();
            sendNextChunk();
        } else {
            QString reason = json["reason"].toString();
            emit requestRejected(reason);
            emit transferFinished(false, tr("请求被拒绝: %1").arg(reason));
        }
        break;
    }
    case gy::protocol::kTypeChunkAck: {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject json = doc.object();

        bool verified = json["verified"].toBool();
        int fileIndex = json["file_index"].toInt();

        if (!verified) {
            QString errorMsg = json["error_msg"].toString();
            emit transferFinished(false, tr("文件 %1 校验失败: %2")
                                          .arg(fileIndex).arg(errorMsg));
            return;
        }

        // 当前文件校验通过，继续下一个
        _currentFileIndex++;
        _currentFileBytesSent = 0;

        if (_currentFileIndex < _fileList.size()) {
            // 还有文件要发
            if (!openNextFile()) {
                emit transferFinished(false, tr("无法打开文件 %1")
                                              .arg(_fileList[_currentFileIndex].relativePath));
                return;
            }
            sendNextChunk();
        } else {
            // 所有文件发完
            sendTransferDone();
            emit transferFinished(true, "");
        }
        break;
    }
    default:
        break;
    }
}

void FileSenderWorker::sendTransferRequest()
{
    QJsonObject json;
    json["session_id"]  = _sessionId;
    json["sender_name"] = QHostInfo::localHostName();
    json["total_files"] = _fileList.size();
    json["total_bytes"] = _totalBytes;

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

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferReq, data);
    _socket->write(frame);
}

void FileSenderWorker::sendNextChunk()
{
    if (!_file.isOpen() || _bytesSent >= _totalBytes) {
        return;
    }

    QByteArray chunkData = _file.read(kChunkSize);
    if (chunkData.isEmpty()) {
        emit transferFinished(false, tr("读取文件失败"));
        return;
    }

    // 构建 chunk 元数据（20 字节）
    QByteArray metadata;
    QDataStream stream(&metadata, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 fileIndex = static_cast<quint32>(_currentFileIndex);
    quint64 chunkOffset = static_cast<quint64>(_currentFileBytesSent);
    quint32 chunkSize = static_cast<quint32>(chunkData.size());
    quint32 isLastChunk = (_currentFileBytesSent + chunkData.size()
                           >= _fileList[_currentFileIndex].sizeBytes) ? 1 : 0;

    stream << fileIndex;
    stream << chunkOffset;
    stream << chunkSize;
    stream << isLastChunk;

    QByteArray payload;
    payload.reserve(20 + chunkData.size());
    payload.append(metadata);
    payload.append(chunkData);

    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeDataChunk, payload);
    _socket->write(frame);

    _bytesSent += chunkData.size();
    _currentFileBytesSent += chunkData.size();
    emit progressChanged(_bytesSent, _totalBytes);

    if (_bytesSent < _totalBytes) {
        QTimer::singleShot(0, this, &FileSenderWorker::sendNextChunk);
    }
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
    emit transferFinished(false, tr("已取消"));
}
