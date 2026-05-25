/**
* @file    file_receiver_worker.cpp
* @date    2026-06-02
* @author  GY
* @brief   FileReceiverWorker 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
* [v0.2] GY   2026-06-03
* * 接收路径改用 _receivePath 成员，支持外部配置
* [v0.3] GY   2026-06-04
* * Stage 4.3：解析文件列表（含 sha256），支持多文件接收
*/

#include "file_receiver_worker.h"
#include "config_manager.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

FileReceiverWorker::FileReceiverWorker(QTcpSocket *socket, QObject *parent)
    : QObject{parent}
    , _socket{socket}
    , _codec{new FrameCodec{this}}
{
    // socket 父对象设为 nullptr，避免自动删除
    _socket->setParent(nullptr);

    // 连接 socket 信号
    connect(_socket, &QTcpSocket::readyRead,
            this,    &FileReceiverWorker::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected,
            this,    &FileReceiverWorker::onDisconnected);

    // 连接 codec 信号
    connect(_codec,  &FrameCodec::frameReady,
            this,    &FileReceiverWorker::onFrameReady);
}

FileReceiverWorker::~FileReceiverWorker()
{
    if (_file.isOpen()) {
        _file.close();
    }
    if (_socket) {
        _socket->deleteLater();
    }
}

void FileReceiverWorker::acceptTransfer()
{
    if (!_waitingForUserConfirm) return;

    _waitingForUserConfirm = false;
    _transferActive = true;

    // 发送接受响应
    sendTransferResponse(true);

    // 打开文件准备接收（路径由上层通过信号传入，此处用默认路径兜底）
    if (_receivePath.isEmpty()) {
        _receivePath = QDir::homePath() + "/GridYard/document";
    }
    QDir().mkpath(_receivePath);

    QString filePath = _receivePath + "/" + _fileName;
    _file.setFileName(filePath);

    if (!_file.open(QIODevice::WriteOnly)) {
        emit transferFinished(false, tr("无法创建文件: %1").arg(_file.errorString()));
        return;
    }

    qDebug() << "FileReceiverWorker: 开始接收文件" << filePath;
}

void FileReceiverWorker::rejectTransfer(const QString &reason)
{
    if (!_waitingForUserConfirm) return;

    _waitingForUserConfirm = false;

    // 发送拒绝响应
    sendTransferResponse(false, reason.isEmpty() ? tr("用户拒绝") : reason);

    // 关闭连接
    _socket->disconnectFromHost();
}

void FileReceiverWorker::onReadyRead()
{
    // 将收到的数据喂入 codec
    _codec->feed(_socket->readAll());
}

void FileReceiverWorker::onDisconnected()
{
    if (_transferActive && _bytesReceived < _fileSize) {
        emit transferFinished(false, tr("连接断开"));
    }
}

void FileReceiverWorker::onFrameReady(quint32 type, const QByteArray &payload)
{
    switch (type) {
    case gy::protocol::kTypeTransferReq:
        handleTransferRequest(payload);
        break;
    case gy::protocol::kTypeDataChunk:
        handleDataChunk(payload);
        break;
    case gy::protocol::kTypeCancel:
        handleCancel(payload);
        break;
    default:
        break;
    }
}

void FileReceiverWorker::handleTransferRequest(const QByteArray &payload)
{
    // 解析握手请求
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    QJsonObject json = doc.object();

    _sessionId  = json["session_id"].toString();
    _senderName = json["sender_name"].toString();
    _totalFiles = json["total_files"].toInt();
    _totalBytes = json["total_bytes"].toVariant().toLongLong();

    // 解析文件列表
    _fileList.clear();
    QJsonArray files = json["files"].toArray();
    for (const auto &fileVal : files) {
        QJsonObject fileObj = fileVal.toObject();
        gy::FileItem item;
        item.relativePath = fileObj["relative_path"].toString();
        item.sizeBytes    = fileObj["size_bytes"].toVariant().toLongLong();
        item.sha256       = fileObj["sha256"].toString();
        _fileList.append(item);
    }

    // 设置第一个文件信息
    if (!_fileList.isEmpty()) {
        _currentFileIndex = 0;
        _fileName = _fileList[0].relativePath;
        _fileSize = _fileList[0].sizeBytes;
    }

    _waitingForUserConfirm = true;

    // 通知 UI 弹窗确认
    emit transferRequestReceived(_senderName, _fileName, _fileSize);

    qDebug() << "FileReceiverWorker: 收到传输请求"
             << "来自" << _senderName
             << "文件数" << _totalFiles
             << "总大小" << _totalBytes;
}

void FileReceiverWorker::handleDataChunk(const QByteArray &payload)
{
    if (!_transferActive || !_file.isOpen()) return;

    // 解析 20 字节元数据
    if (payload.size() < 20) {
        qWarning() << "FileReceiverWorker: 数据块过小";
        return;
    }

    QDataStream stream(payload.left(20));
    stream.setByteOrder(QDataStream::BigEndian);

    quint32 fileIndex, chunkSize, isLastChunk;
    quint64 chunkOffset;

    stream >> fileIndex;
    stream >> chunkOffset;
    stream >> chunkSize;
    stream >> isLastChunk;

    // 提取文件数据
    QByteArray chunkData = payload.mid(20);

    // 写入文件
    if (!_file.seek(chunkOffset)) {
        qWarning() << "FileReceiverWorker: 文件 seek 失败";
        return;
    }

    qint64 written = _file.write(chunkData);
    if (written != chunkData.size()) {
        qWarning() << "FileReceiverWorker: 写入文件失败";
        return;
    }

    _bytesReceived += chunkData.size();
    emit progressChanged(_bytesReceived, _fileSize);

    // 检查是否是最后一个块
    if (isLastChunk == 1) {
        _file.close();
        _transferActive = false;

        // 发送块确认
        sendChunkAck(true);

        emit transferFinished(true, "");

        qDebug() << "FileReceiverWorker: 文件接收完成" << _fileName;
    }
}

void FileReceiverWorker::handleCancel(const QByteArray &payload)
{
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    QJsonObject json = doc.object();

    QString reason = json["reason"].toString();

    // 清理资源
    if (_file.isOpen()) {
        _file.close();
        // 删除不完整的文件
        QFile::remove(_file.fileName());
    }

    _transferActive = false;

    emit transferFinished(false, tr("传输被取消: %1").arg(reason));

    // 关闭连接
    _socket->disconnectFromHost();
}

void FileReceiverWorker::sendTransferResponse(bool accepted, const QString &reason)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["accepted"]   = accepted;
    json["reason"]     = reason;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferRsp, data);
    _socket->write(frame);
}

void FileReceiverWorker::sendChunkAck(bool verified, const QString &errorMsg)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["file_index"] = _currentFileIndex;
    json["verified"]   = verified;
    json["error_msg"]  = errorMsg;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeChunkAck, data);
    _socket->write(frame);
}
