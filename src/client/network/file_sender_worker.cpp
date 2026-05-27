/**
* @file    file_sender_worker.cpp
* @date    2026-06-02
* @author  GY
* @brief   FileSenderWorker 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
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

FileSenderWorker::FileSenderWorker(QObject *parent)
    : QObject{parent}
    , _socket{new QTcpSocket{this}}
    , _codec{new FrameCodec{this}}
{
    // 连接 socket 信号
    connect(_socket, &QTcpSocket::readyRead,
            this,    &FileSenderWorker::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected,
            this,    &FileSenderWorker::onDisconnected);

    // 连接 codec 信号
    connect(_codec,  &FrameCodec::frameReady,
            this,    &FileSenderWorker::onFrameReady);
}

FileSenderWorker::~FileSenderWorker()
{
    if (_file.isOpen()) {
        _file.close();
    }
}

void FileSenderWorker::startTransfer(const QString &host, quint16 port,
                                     const QString &filePath)
{
    _filePath = filePath;
    _sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 打开文件
    _file.setFileName(filePath);
    if (!_file.open(QIODevice::ReadOnly)) {
        emit transferFinished(false, tr("无法打开文件: %1").arg(_file.errorString()));
        return;
    }

    _totalBytes = _file.size();
    _bytesSent = 0;
    _chunkIndex = 0;

    // 连接到接收端
    _socket->connectToHost(host, port);

    if (!_socket->waitForConnected(5000)) {
        emit transferFinished(false, tr("连接超时: %1").arg(_socket->errorString()));
        return;
    }

    // 发送握手请求
    sendTransferRequest();
}

void FileSenderWorker::onReadyRead()
{
    // 将收到的数据喂入 codec
    _codec->feed(_socket->readAll());
}

void FileSenderWorker::onDisconnected()
{
    if (_bytesSent < _totalBytes) {
        emit transferFinished(false, tr("连接断开"));
    }
}

void FileSenderWorker::onFrameReady(quint32 type, const QByteArray &payload)
{
    switch (type) {
    case gy::protocol::kTypeTransferRsp: {
        // 解析握手响应
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject json = doc.object();

        bool accepted = json["accepted"].toBool();
        if (accepted) {
            emit requestAccepted();
            // 开始发送数据
            sendNextChunk();
        } else {
            QString reason = json["reason"].toString();
            emit requestRejected(reason);
            emit transferFinished(false, tr("请求被拒绝: %1").arg(reason));
        }
        break;
    }
    case gy::protocol::kTypeChunkAck: {
        // 单文件完成确认
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject json = doc.object();

        bool verified = json["verified"].toBool();
        if (verified) {
            emit transferFinished(true, "");
        } else {
            QString errorMsg = json["error_msg"].toString();
            emit transferFinished(false, tr("校验失败: %1").arg(errorMsg));
        }
        break;
    }
    default:
        break;
    }
}

void FileSenderWorker::sendTransferRequest()
{
    // 构建握手请求 JSON
    QJsonObject json;
    json["session_id"]  = _sessionId;
    json["sender_name"] = QHostInfo::localHostName();
    json["total_files"] = 1;
    json["total_bytes"] = _totalBytes;

    QJsonArray files;
    QJsonObject fileObj;
    fileObj["file_index"]    = 0;
    fileObj["relative_path"] = QFileInfo(_filePath).fileName();
    fileObj["size_bytes"]    = _totalBytes;
    fileObj["sha256"]        = "";  // Stage 4 实现
    files.append(fileObj);
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

    // 读取一块数据
    QByteArray chunkData = _file.read(kChunkSize);
    if (chunkData.isEmpty()) {
        emit transferFinished(false, tr("读取文件失败"));
        return;
    }

    // 构建 chunk 元数据（20 字节）
    QByteArray metadata;
    QDataStream stream(&metadata, QDataStream::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 fileIndex = 0;
    quint64 chunkOffset = static_cast<quint64>(_bytesSent);
    quint32 chunkSize = static_cast<quint32>(chunkData.size());
    quint32 isLastChunk = (_bytesSent + chunkData.size() >= _totalBytes) ? 1 : 0;

    stream << fileIndex;
    stream << chunkOffset;
    stream << chunkSize;
    stream << isLastChunk;

    // 拼接 metadata + chunkData
    QByteArray payload;
    payload.reserve(20 + chunkData.size());
    payload.append(metadata);
    payload.append(chunkData);

    // 编码并发送
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeDataChunk, payload);
    _socket->write(frame);

    // 更新进度
    _bytesSent += chunkData.size();
    _chunkIndex++;
    emit progressChanged(_bytesSent, _totalBytes);

    // 如果还有数据，继续发送
    if (_bytesSent < _totalBytes) {
        // 使用 QTimer::singleShot 避免阻塞事件循环
        QTimer::singleShot(0, this, &FileSenderWorker::sendNextChunk);
    }
}

void FileSenderWorker::sendCancel(const QString &reason)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["reason"] = reason;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeCancel, data);
    _socket->write(frame);

    // 关闭连接
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
