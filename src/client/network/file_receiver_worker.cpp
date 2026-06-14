/**
* @file    file_receiver_worker.cpp
* @version 4.12.1
* @date    2026-06-14
* @author  GridYard Team
* @brief   FileReceiverWorker 实现
*
* Change Log:
* [v4.12.1] FengChunlin   2026-06-14
* * 校验数据块、修正文件夹累计进度，并等待最终完成确认
* [v4.11.0] FengChunlin   2026-06-13
* * 文件夹接收保留顶层目录，校验路径并避免覆盖
* [v4.8.3] FengChunlin   2026-06-13
* * 传输请求中使用发送方设备别名
* [v4.4.2] FengChunlin   2026-06-04
* * Stage 4.4：添加超时检测机制
* [v4.3.4] FengChunlin   2026-06-04
* * Stage 4.3：SHA-256 校验实现，多文件接收支持
* [v4.3.1] FengChunlin   2026-06-04
* * Stage 4.3：解析文件列表（含 sha256），支持多文件接收
* [v0.3.0] FengChunlin   2026-06-03
* * 接收路径改用 _receivePath 成员，支持外部配置
* [v0.2.0] FengChunlin   2026-06-02
* * Stage 3：初始版本
*/

#include "file_receiver_worker.h"
#include "config_manager.h"
#include "frame_codec.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// 超时时间：30 秒
static constexpr int kTimeoutMs = 30000;

namespace {

bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return false;
    }

    const QString cleanPath = QDir::cleanPath(path);
    return cleanPath != ".." && !cleanPath.startsWith("../");
}

QString uniqueTargetPath(const QString &path, bool directory)
{
    if (!QFileInfo::exists(path)) {
        return path;
    }

    const QFileInfo info{path};
    const QString parentPath = info.path();
    const QString suffix = directory || info.suffix().isEmpty()
        ? QString()
        : "." + info.suffix();
    const QString baseName = directory || info.suffix().isEmpty()
        ? info.fileName()
        : info.completeBaseName();

    for (int index = 1; ; ++index) {
        const QString candidate = QString("%1/%2 (%3)%4")
                                      .arg(parentPath, baseName)
                                      .arg(index)
                                      .arg(suffix);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
}

}

FileReceiverWorker::FileReceiverWorker(QTcpSocket *socket, QObject *parent)
    : QObject{parent}
    , _socket{socket}
    , _codec{new FrameCodec{this}}
    , _timeoutTimer{new QTimer{this}}
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

    // 超时定时器
    _timeoutTimer->setSingleShot(true);
    connect(_timeoutTimer, &QTimer::timeout,
            this,          &FileReceiverWorker::onTimeout);
}

FileReceiverWorker::~FileReceiverWorker()
{
    cleanup();
    if (_socket) {
        _socket->deleteLater();
    }
}

void FileReceiverWorker::acceptTransfer()
{
    qDebug() << "[FileReceiver] 用户接受传输";

    if (!_waitingForUserConfirm) {
        qWarning() << "[FileReceiver] 不在等待确认状态，忽略接受请求";
        return;
    }

    _waitingForUserConfirm = false;
    _transferActive = true;

    const auto failPreparation = [this](const QString &errorMsg) {
        sendTransferResponse(false, errorMsg);
        cleanup();
        emit transferFinished(false, errorMsg);
        _socket->disconnectFromHost();
    };

    // 准备接收目录（路径由上层通过信号传入，此处用默认路径兜底）
    if (_receivePath.isEmpty()) {
        _receivePath = QDir::homePath() + "/GridYard/document";
    }
    if (!QDir().mkpath(_receivePath)) {
        failPreparation(tr("无法创建接收目录"));
        return;
    }

    if (_isDirectory) {
        _destinationRoot = uniqueTargetPath(_receivePath + "/" + _rootName, true);
        if (!QDir().mkpath(_destinationRoot)) {
            failPreparation(tr("无法创建接收目录"));
            return;
        }

        for (const QString &relativePath : _emptyDirectories) {
            if (!QDir().mkpath(_destinationRoot + "/" + QDir::cleanPath(relativePath))) {
                failPreparation(tr("无法创建目录: %1").arg(relativePath));
                return;
            }
        }
    } else {
        _destinationRoot = _receivePath;
        _singleFilePath = uniqueTargetPath(_receivePath + "/" + _fileName, false);
    }

    if (!_fileList.isEmpty() && !openCurrentFile()) {
        failPreparation(tr("无法创建文件: %1").arg(_file.errorString()));
        return;
    }

    qDebug() << "[FileReceiver] 发送接受响应";
    sendTransferResponse(true);

    // 启动超时定时器
    _timeoutTimer->start(kTimeoutMs);
}

void FileReceiverWorker::rejectTransfer(const QString &reason)
{
    qDebug() << "[FileReceiver] 用户拒绝传输，原因:" << reason;

    if (!_waitingForUserConfirm) {
        qWarning() << "[FileReceiver] 不在等待确认状态，忽略拒绝请求";
        return;
    }

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

    // 重置超时定时器
    if (_transferActive) {
        _timeoutTimer->start(kTimeoutMs);
    }
}

void FileReceiverWorker::onDisconnected()
{
    if (_transferActive) {
        cleanup();
        emit transferFinished(false, tr("连接断开"));
    }
}

void FileReceiverWorker::onTimeout()
{
    if (_transferActive) {
        qWarning() << "FileReceiverWorker: 传输超时";
        cleanup();
        emit transferFinished(false, tr("传输超时"));
    }
}

void FileReceiverWorker::cleanup()
{
    // 停止超时定时器
    _timeoutTimer->stop();

    // 关闭文件并删除不完整文件
    if (_file.isOpen()) {
        QString filePath = _file.fileName();
        _file.close();
        QFile::remove(filePath);
        qDebug() << "FileReceiverWorker: 已删除不完整文件" << filePath;
    }

    _transferActive = false;
}

void FileReceiverWorker::onFrameReady(quint32 type, const QByteArray &payload)
{
    qDebug() << "[FileReceiver] 收到帧，类型:" << type;

    switch (type) {
    case gy::protocol::kTypeTransferReq:
        qDebug() << "[FileReceiver] 处理传输请求";
        handleTransferRequest(payload);
        break;
    case gy::protocol::kTypeDataChunk:
        handleDataChunk(payload);
        break;
    case gy::protocol::kTypeCancel:
        qDebug() << "[FileReceiver] 处理取消请求";
        handleCancel(payload);
        break;
    case gy::protocol::kTypeTransferDone:
        handleTransferDone();
        break;
    default:
        qDebug() << "[FileReceiver] 未知帧类型:" << type;
        break;
    }
}

void FileReceiverWorker::handleTransferRequest(const QByteArray &payload)
{
    qDebug() << "[FileReceiver] 解析传输请求";

    // 解析握手请求
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    QJsonObject json = doc.object();

    _sessionId  = json["session_id"].toString();
    _senderDeviceId = json["sender_device_id"].toString();
    _senderName = json["sender_name"].toString();
    _isDirectory = json["is_directory"].toBool(false);
    _rootName = json["root_name"].toString();
    _totalFiles = json["total_files"].toInt();
    _totalBytes = json["total_bytes"].toVariant().toLongLong();

    if (_rootName.isEmpty() || QFileInfo{_rootName}.fileName() != _rootName) {
        sendTransferResponse(false, tr("无效的文件名称"));
        _socket->disconnectFromHost();
        return;
    }

    // 解析文件列表
    _fileList.clear();
    QJsonArray files = json["files"].toArray();
    for (const auto &fileVal : files) {
        QJsonObject fileObj = fileVal.toObject();
        gy::FileItem item;
        item.relativePath = fileObj["relative_path"].toString();
        item.sizeBytes    = fileObj["size_bytes"].toVariant().toLongLong();
        item.sha256       = fileObj["sha256"].toString();
        if (!isSafeRelativePath(item.relativePath)) {
            sendTransferResponse(false, tr("无效的文件路径"));
            _socket->disconnectFromHost();
            return;
        }
        _fileList.append(item);
    }

    _emptyDirectories.clear();
    const QJsonArray directories = json["empty_directories"].toArray();
    for (const QJsonValue &directory : directories) {
        const QString relativePath = directory.toString();
        if (!isSafeRelativePath(relativePath)) {
            sendTransferResponse(false, tr("无效的目录路径"));
            _socket->disconnectFromHost();
            return;
        }
        _emptyDirectories.append(relativePath);
    }

    if (_totalFiles != _fileList.size()) {
        sendTransferResponse(false, tr("文件列表数量不一致"));
        _socket->disconnectFromHost();
        return;
    }

    // 设置第一个文件信息
    if (!_fileList.isEmpty()) {
        _currentFileIndex = 0;
        _fileName = _fileList[0].relativePath;
        _fileSize = _fileList[0].sizeBytes;
    }
    _bytesReceived = 0;
    _totalBytesReceived = 0;
    _displayName = _isDirectory ? _rootName : _fileName;

    _waitingForUserConfirm = true;

    qDebug() << "[FileReceiver] 传输请求详情:";
    qDebug() << "  会话ID:" << _sessionId;
    qDebug() << "  发送方设备ID:" << _senderDeviceId;
    qDebug() << "  发送方:" << _senderName;
    qDebug() << "  文件数:" << _totalFiles;
    qDebug() << "  总大小:" << _totalBytes;
    qDebug() << "  显示名称:" << _displayName;

    // 通知 UI 弹窗确认
    emit transferRequestReceived(_senderDeviceId, _senderName, _displayName, _fileSize,
                                 _totalFiles, _totalBytes);

    qDebug() << "[FileReceiver] 已通知 UI 弹窗确认";
}

void FileReceiverWorker::handleDataChunk(const QByteArray &payload)
{
    if (!_transferActive || !_file.isOpen()) {
        qDebug() << "[FileReceiver] 忽略数据块，传输未激活或文件未打开";
        return;
    }

    // 解析 20 字节元数据
    if (payload.size() < 20) {
        qWarning() << "[FileReceiver] 数据块过小:" << payload.size() << "字节";
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

    if (fileIndex != static_cast<quint32>(_currentFileIndex)
        || chunkOffset != static_cast<quint64>(_bytesReceived)
        || chunkSize != static_cast<quint32>(chunkData.size())
        || _bytesReceived + chunkData.size() > _fileSize) {
        const QString errorMsg = tr("收到无效的文件数据块");
        qWarning() << "[FileReceiver]" << errorMsg
                   << "文件索引:" << fileIndex
                   << "偏移:" << chunkOffset
                   << "声明长度:" << chunkSize
                   << "实际长度:" << chunkData.size();
        sendChunkAck(false, errorMsg);
        cleanup();
        emit transferFinished(false, errorMsg);
        _socket->disconnectFromHost();
        return;
    }

    // 写入文件
    if (!_file.seek(chunkOffset)) {
        qWarning() << "[FileReceiver] 文件 seek 失败，偏移量:" << chunkOffset;
        return;
    }

    qint64 written = _file.write(chunkData);
    if (written != chunkData.size()) {
        qWarning() << "[FileReceiver] 写入文件失败，可能是磁盘空间不足";
        cleanup();
        emit transferFinished(false, tr("写入文件失败，可能是磁盘空间不足"));
        return;
    }

    _bytesReceived += chunkData.size();
    _totalBytesReceived += chunkData.size();

    // 减少信号发射频率：每 4 个 chunk 发射一次（约 32MB）
    static int chunkCount = 0;
    if (++chunkCount % 4 == 0 || isLastChunk == 1) {
        const qint64 percent = _totalBytes > 0
            ? (_totalBytesReceived * 100 / _totalBytes)
            : 100;
        qDebug() << "[FileReceiver] 接收进度:" << _totalBytesReceived << "/" << _totalBytes
                 << "(" << percent << "%)";
        emit progressChanged(_totalBytesReceived, _totalBytes);
    }

    // 检查是否是最后一个块
    if (isLastChunk == 1) {
        qDebug() << "[FileReceiver] 文件" << _fileName << "接收完成，开始校验";
        _file.close();

        // 计算 SHA-256 校验
        QString computedHash = gy::DirSerializer::computeSha256(_file.fileName());
        bool verified = true;
        QString errorMsg;

        if (_currentFileIndex < _fileList.size()) {
            QString expectedHash = _fileList[_currentFileIndex].sha256;
            if (!expectedHash.isEmpty() && computedHash != expectedHash) {
                verified = false;
                errorMsg = tr("SHA-256 校验失败");
                qWarning() << "[FileReceiver] SHA-256 不匹配"
                           << "期望" << expectedHash
                           << "实际" << computedHash;
            }
        }

        // 发送块确认
        sendChunkAck(verified, errorMsg);

        if (!verified) {
            QFile::remove(_file.fileName());
            _timeoutTimer->stop();
            _transferActive = false;
            emit transferFinished(false, errorMsg);
            return;
        }

        // 当前文件校验通过
        qDebug() << "[FileReceiver] 文件接收完成:" << _fileName;

        // 切换到下一个文件
        _currentFileIndex++;
        if (_currentFileIndex < _fileList.size()) {
            // 还有文件要接收
            _fileName = _fileList[_currentFileIndex].relativePath;
            _fileSize = _fileList[_currentFileIndex].sizeBytes;
            _bytesReceived = 0;

            if (!openCurrentFile()) {
                _transferActive = false;
                emit transferFinished(false, tr("无法创建文件: %1").arg(_file.errorString()));
                return;
            }

            qDebug() << "[FileReceiver] 开始接收下一个文件:" << _fileName;
        } else {
            // 保持连接直到收到 TransferDone，确保最后一个 ACK 已写入网络。
            qDebug() << "[FileReceiver] 所有文件接收完成，等待传输完成确认";
            _timeoutTimer->start(kTimeoutMs);
        }
    }
}

void FileReceiverWorker::handleTransferDone()
{
    if (!_transferActive || _currentFileIndex < _fileList.size()) {
        return;
    }

    qDebug() << "[FileReceiver] 收到传输完成确认:" << _displayName;
    _timeoutTimer->stop();
    _transferActive = false;
    emit transferFinished(true, "");
}

bool FileReceiverWorker::openCurrentFile()
{
    QString filePath = _singleFilePath;
    if (_isDirectory) {
        filePath = _destinationRoot + "/" + _fileName;
        if (!QDir().mkpath(QFileInfo{filePath}.path())) {
            return false;
        }
    }

    _file.setFileName(filePath);
    if (!_file.open(QIODevice::WriteOnly)) {
        qWarning() << "[FileReceiver] 无法创建文件:" << _file.errorString();
        return false;
    }

    qDebug() << "[FileReceiver] 开始接收文件:" << filePath;
    return true;
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
