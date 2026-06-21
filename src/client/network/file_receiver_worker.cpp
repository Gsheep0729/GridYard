/**
* @file    file_receiver_worker.cpp
* @version 4.16.1
* @date    2026-06-21
* @author  GridYard Team
* @brief   文件接收 Worker 实现
*
* 实现完整的文件接收流程：解析传输请求、通知 UI 确认、接收数据块、
* 写入磁盘、SHA-256 校验、发送确认帧。支持多文件/目录传输、
* 超时检测、取消操作和协议错误处理。
*
* Change Log:
* [v4.16.1] GY   2026-06-21
* * 新增 fillReceiveSession()、rootPreviewPaths() 实现
* [v4.15.1] FengChunlin   2026-06-17
* * 连接 FrameCodec::errorOccurred，协议错误时 cleanup + disconnect + emit transferFinished
* * handleDataChunk 进度节流 static 改为成员 _receiveChunkCount，acceptTransfer 时重置
* * sendTransferResponse/sendChunkAck 写入 error_code 字段，调用处传入具体 ErrorCode
* * handleTransferRequest 加强 JSON 校验：解析错误、缺字段、类型错误、负数大小、
*   total_files 不一致、total_bytes 不一致均拒绝并返回稳定 ErrorCode
* [v4.15.0] GY   2026-06-17
* * 新增 initialize() 方法，在后台线程中创建 QTimer 和连接信号
* * transferFinished 信号添加 ErrorCode 参数
* * rejectTransfer() 发射 transferFinished 信号
* * socket 父对象设为 this，随 worker 一起 moveToThread
* [v4.14.0] GY   2026-06-15
* * 提供接收完成后的实际保存路径
* [v4.13.1] FengChunlin   2026-06-15
* * 向会话层提供接收文件夹相对路径列表
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
#include <QThread>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

// 超时时间：30 秒
static constexpr int kTimeoutMs = 30000;

namespace {

// 检查相对路径是否安全（无路径穿越风险）
bool isSafeRelativePath(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return false;
    }

    const QString cleanPath = QDir::cleanPath(path);
    return cleanPath != ".." && !cleanPath.startsWith("../");
}

// 生成不与已有文件冲突的唯一目标路径
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

// 获取接收文件的相对路径列表（含空目录）
QStringList FileReceiverWorker::filePaths() const
{
    QStringList paths;
    paths.reserve(_fileList.size() + _emptyDirectories.size());
    for (const auto &item : _fileList) {
        paths.append(item.relativePath);
    }
    paths.append(_emptyDirectories);
    return paths;
}

// 获取文件实际保存路径（目录时返回根目录，单文件时返回文件路径）
QString FileReceiverWorker::savedPath() const
{
    return _isDirectory ? _destinationRoot : _singleFilePath;
}

// 构造函数，绑定 socket 并创建 SHA-256 哈希计算器
FileReceiverWorker::FileReceiverWorker(QTcpSocket *socket, QObject *parent)
    : QObject{parent}
    , _socket{socket}
    , _hash{new QCryptographicHash{QCryptographicHash::Sha256}}
{
    // socket 父对象设为 this，当 worker 被 moveToThread 时 socket 也会被移动
    _socket->setParent(this);

    // QTimer 和 FrameCodec 在 initialize() 中创建，确保在正确的线程中
}

// 在后台线程中初始化 FrameCodec、超时定时器和信号连接
void FileReceiverWorker::initialize()
{
    qDebug() << "[FileReceiver] 初始化（线程:" << QThread::currentThreadId() << ")";

    // 在当前线程中创建 FrameCodec 和 QTimer
    _codec = new FrameCodec{this};
    _timeoutTimer = new QTimer{this};

    // 连接 socket 信号
    connect(_socket, &QTcpSocket::readyRead,
            this,    &FileReceiverWorker::onReadyRead);
    connect(_socket, &QTcpSocket::disconnected,
            this,    &FileReceiverWorker::onDisconnected);

    // 连接 codec 信号
    connect(_codec,  &FrameCodec::frameReady,
            this,    &FileReceiverWorker::onFrameReady);
    connect(_codec,  &FrameCodec::errorOccurred,
            this,    [this](gy::protocol::ErrorCode errorCode, const QString &errorMsg) {
        qWarning() << "[FileReceiver] 协议错误:" << errorMsg;
        cleanup();
        _socket->disconnectFromHost();
        emit transferFinished(false, errorCode, errorMsg);
    });

    // 超时定时器
    _timeoutTimer->setSingleShot(true);
    connect(_timeoutTimer, &QTimer::timeout,
            this,          &FileReceiverWorker::onTimeout);

    qDebug() << "[FileReceiver] 初始化完成";
}

// 析构函数，释放资源并延迟删除 socket
FileReceiverWorker::~FileReceiverWorker()
{
    cleanup();
    delete _hash;
    if (_socket) {
        _socket->deleteLater();
    }
}

// 用户确认接受传输，准备接收目录和文件并发送接受响应
void FileReceiverWorker::acceptTransfer()
{
    qDebug() << "[FileReceiver] 用户接受传输";

    if (!_waitingForUserConfirm) {
        qWarning() << "[FileReceiver] 不在等待确认状态，忽略接受请求";
        return;
    }

    _waitingForUserConfirm = false;
    _transferActive = true;
    _receiveChunkCount = 0;

    const auto failPreparation = [this](gy::protocol::ErrorCode errorCode, const QString &errorMsg) {
        sendTransferResponse(false, errorCode, errorMsg);
        cleanup();
        emit transferFinished(false, errorCode, errorMsg);
        _socket->disconnectFromHost();
    };

    // 准备接收目录（路径由上层通过信号传入，此处用默认路径兜底）
    if (_receivePath.isEmpty()) {
        _receivePath = QDir::homePath() + "/GridYard/document";
    }
    if (!QDir().mkpath(_receivePath)) {
        failPreparation(gy::protocol::ErrorCode::DiskWriteFailed, tr("无法创建接收目录"));
        return;
    }

    if (_isDirectory) {
        _destinationRoot = uniqueTargetPath(_receivePath + "/" + _rootName, true);
        if (!QDir().mkpath(_destinationRoot)) {
            failPreparation(gy::protocol::ErrorCode::DiskWriteFailed, tr("无法创建接收目录"));
            return;
        }

        for (const QString &relativePath : _emptyDirectories) {
            if (!QDir().mkpath(_destinationRoot + "/" + QDir::cleanPath(relativePath))) {
                failPreparation(gy::protocol::ErrorCode::DiskWriteFailed, tr("无法创建目录: %1").arg(relativePath));
                return;
            }
        }
    } else {
        _destinationRoot = _receivePath;
        _singleFilePath = uniqueTargetPath(_receivePath + "/" + _fileName, false);
    }

    if (!_fileList.isEmpty() && !openCurrentFile()) {
        failPreparation(gy::protocol::ErrorCode::DiskWriteFailed, tr("无法创建文件: %1").arg(_file.errorString()));
        return;
    }

    qDebug() << "[FileReceiver] 发送接受响应";
    sendTransferResponse(true, gy::protocol::ErrorCode::Success);

    // 启动超时定时器
    _timeoutTimer->start(kTimeoutMs);
}

// 用户拒绝传输，发送拒绝响应并关闭连接
void FileReceiverWorker::rejectTransfer(const QString &reason)
{
    qDebug() << "[FileReceiver] 用户拒绝传输，原因:" << reason;

    if (!_waitingForUserConfirm) {
        qWarning() << "[FileReceiver] 不在等待确认状态，忽略拒绝请求";
        return;
    }

    _waitingForUserConfirm = false;

    // 发送拒绝响应
    sendTransferResponse(false, gy::protocol::ErrorCode::UserRejected, reason.isEmpty() ? tr("用户拒绝") : reason);

    // 发射传输完成信号（被拒绝）
    emit transferFinished(false, gy::protocol::ErrorCode::UserRejected, reason);

    // 关闭连接
    _socket->disconnectFromHost();
}

// 处理 socket 可读数据，喂入 FrameCodec 解码并重置超时
void FileReceiverWorker::onReadyRead()
{
    // 将收到的数据喂入 codec
    _codec->feed(_socket->readAll());

    // 重置超时定时器
    if (_transferActive) {
        _timeoutTimer->start(kTimeoutMs);
    }
}

// 处理连接断开事件，传输活跃时清理资源并通知失败
void FileReceiverWorker::onDisconnected()
{
    if (_transferActive) {
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::ConnectionLost, tr("连接断开"));
    }
}

// 处理传输超时，清理资源并通知超时失败
void FileReceiverWorker::onTimeout()
{
    if (_transferActive) {
        qWarning() << "FileReceiverWorker: 传输超时";
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::TransferTimeout, tr("传输超时"));
    }
}

// 清理传输资源：停止定时器、关闭文件、删除不完整文件、重置哈希
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

    // 重置 SHA-256 计算
    _hash->reset();

    _transferActive = false;
}

// 根据帧类型分发到对应的处理函数
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

// 解析传输请求 JSON，校验字段和版本后通知 UI 弹窗确认
void FileReceiverWorker::handleTransferRequest(const QByteArray &payload)
{
    qDebug() << "[FileReceiver] 解析传输请求";

    // 解析握手请求
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[FileReceiver] 传输请求 JSON 解析失败:" << parseError.errorString();
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("传输请求格式错误"));
        _socket->disconnectFromHost();
        return;
    }

    const QJsonObject json = doc.object();

    // 检查必需字段存在性和类型
    const auto requireString = [&json, this](const QString &key) -> QString {
        const QJsonValue val = json.value(key);
        if (!val.isString() || val.toString().isEmpty()) {
            return {};
        }
        return val.toString();
    };

    _sessionId      = requireString("session_id");
    _senderDeviceId = requireString("sender_device_id");
    _senderName     = requireString("sender_name");

    if (_sessionId.isEmpty() || _senderDeviceId.isEmpty() || _senderName.isEmpty()) {
        qWarning() << "[FileReceiver] 缺少必需字段（session_id/sender_device_id/sender_name）";
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("缺少必需字段"));
        _socket->disconnectFromHost();
        return;
    }

    if (!json.contains("total_files") || !json["total_files"].isDouble()) {
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("缺少或无效的 total_files 字段"));
        _socket->disconnectFromHost();
        return;
    }
    if (!json.contains("total_bytes") || !json["total_bytes"].isDouble()) {
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("缺少或无效的 total_bytes 字段"));
        _socket->disconnectFromHost();
        return;
    }
    if (!json.contains("files") || !json["files"].isArray()) {
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("缺少或无效的 files 字段"));
        _socket->disconnectFromHost();
        return;
    }

    _isDirectory = json["is_directory"].toBool(false);
    _rootName    = json["root_name"].toString();
    _totalFiles  = json["total_files"].toInt();
    _totalBytes  = json["total_bytes"].toVariant().toLongLong();

    // 协议版本检查：主版本不一致拒绝会话，次版本差异安全降级
    quint16 senderVersion = static_cast<quint16>(json["protocol_version"].toInt());
    if (senderVersion > 0) {
        quint8 localMajor = gy::protocol::majorVersion(gy::protocol::kProtocolVersion);
        quint8 senderMajor = gy::protocol::majorVersion(senderVersion);
        quint8 localMinor = gy::protocol::minorVersion(gy::protocol::kProtocolVersion);
        quint8 senderMinor = gy::protocol::minorVersion(senderVersion);

        if (senderMajor != localMajor) {
            // 主版本不一致，拒绝会话
            qWarning() << "[FileReceiver] 发送方主版本不匹配，本地:"
                       << localMajor << "对端:" << senderMajor;
            sendTransferResponse(false, gy::protocol::ErrorCode::ProtocolMismatch, tr("协议版本不兼容，请升级到最新版本"));
            _socket->disconnectFromHost();
            return;
        }

        if (senderMinor != localMinor) {
            // 次版本差异，安全降级（记录警告但继续）
            qWarning() << "[FileReceiver] 发送方次版本不同，本地:"
                       << localMinor << "对端:" << senderMinor << "，安全降级处理";
        }
    }

    if (_rootName.isEmpty() || QFileInfo{_rootName}.fileName() != _rootName) {
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidFileName, tr("无效的文件名称"));
        _socket->disconnectFromHost();
        return;
    }

    // 解析文件列表
    _fileList.clear();
    QJsonArray files = json["files"].toArray();
    qint64 computedTotalBytes = 0;
    for (const auto &fileVal : files) {
        QJsonObject fileObj = fileVal.toObject();
        gy::FileItem item;
        item.relativePath = fileObj["relative_path"].toString();
        item.sizeBytes    = fileObj["size_bytes"].toVariant().toLongLong();
        item.sha256       = fileObj["sha256"].toString();
        if (item.sizeBytes < 0) {
            qWarning() << "[FileReceiver] 文件大小为负数:" << item.relativePath << item.sizeBytes;
            sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("文件大小不能为负数"));
            _socket->disconnectFromHost();
            return;
        }
        if (!isSafeRelativePath(item.relativePath)) {
            sendTransferResponse(false, gy::protocol::ErrorCode::InvalidFilePath, tr("无效的文件路径"));
            _socket->disconnectFromHost();
            return;
        }
        computedTotalBytes += item.sizeBytes;
        _fileList.append(item);
    }

    _emptyDirectories.clear();
    const QJsonArray directories = json["empty_directories"].toArray();
    for (const QJsonValue &directory : directories) {
        const QString relativePath = directory.toString();
        if (!isSafeRelativePath(relativePath)) {
            sendTransferResponse(false, gy::protocol::ErrorCode::InvalidFilePath, tr("无效的目录路径"));
            _socket->disconnectFromHost();
            return;
        }
        _emptyDirectories.append(relativePath);
    }

    if (_totalFiles != _fileList.size()) {
        sendTransferResponse(false, gy::protocol::ErrorCode::FileListMismatch, tr("文件列表数量不一致"));
        _socket->disconnectFromHost();
        return;
    }

    // 校验 total_bytes 是否等于文件列表大小之和
    if (_totalBytes != computedTotalBytes) {
        qWarning() << "[FileReceiver] total_bytes 不一致，声明:" << _totalBytes << "实际:" << computedTotalBytes;
        sendTransferResponse(false, gy::protocol::ErrorCode::InvalidPayload, tr("总字节数与文件列表不一致"));
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

// 处理接收到的数据块：校验元数据、写入磁盘、增量 SHA-256、发送确认
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
        sendChunkAck(false, gy::protocol::ErrorCode::InvalidPayload, errorMsg);
        cleanup();
        emit transferFinished(false, gy::protocol::ErrorCode::InvalidPayload, errorMsg);
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
        emit transferFinished(false, gy::protocol::ErrorCode::DiskWriteFailed, tr("写入文件失败，可能是磁盘空间不足"));
        return;
    }

    // 增量更新 SHA-256
    _hash->addData(chunkData);

    _bytesReceived += chunkData.size();
    _totalBytesReceived += chunkData.size();

    // 减少信号发射频率：每 4 个 chunk 发射一次（约 32MB）
    if (++_receiveChunkCount % 4 == 0 || isLastChunk == 1) {
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

        // 使用增量计算的 SHA-256 结果
        QString computedHash = _hash->result().toHex();
        _hash->reset();
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
        sendChunkAck(verified, verified ? gy::protocol::ErrorCode::Success : gy::protocol::ErrorCode::Sha256Mismatch, errorMsg);

        if (!verified) {
            QFile::remove(_file.fileName());
            _timeoutTimer->stop();
            _transferActive = false;
            emit transferFinished(false, gy::protocol::ErrorCode::Sha256Mismatch, errorMsg);
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
                emit transferFinished(false, gy::protocol::ErrorCode::DiskWriteFailed, tr("无法创建文件: %1").arg(_file.errorString()));
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

// 处理传输完成确认，所有文件接收完毕后发射成功信号
void FileReceiverWorker::handleTransferDone()
{
    if (!_transferActive || _currentFileIndex < _fileList.size()) {
        return;
    }

    qDebug() << "[FileReceiver] 收到传输完成确认:" << _displayName;
    _timeoutTimer->stop();
    _transferActive = false;
    emit transferFinished(true, gy::protocol::ErrorCode::Success, "");
}

// 打开当前待接收的文件，目录模式下自动创建父目录
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

// 处理取消传输请求，清理资源并删除不完整文件
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

    emit transferFinished(false, gy::protocol::ErrorCode::UserCancelled, tr("传输被取消: %1").arg(reason));

    // 关闭连接
    _socket->disconnectFromHost();
}

// 构建并发送传输响应帧（接受/拒绝 + 错误码 + 原因）
void FileReceiverWorker::sendTransferResponse(bool accepted, gy::protocol::ErrorCode errorCode, const QString &reason)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["accepted"]   = accepted;
    json["error_code"] = static_cast<int>(errorCode);
    json["reason"]     = reason;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeTransferRsp, data);
    _socket->write(frame);
}

// 构建并发送数据块确认帧（校验结果 + 文件索引 + 错误信息）
void FileReceiverWorker::sendChunkAck(bool verified, gy::protocol::ErrorCode errorCode, const QString &errorMsg)
{
    QJsonObject json;
    json["session_id"] = _sessionId;
    json["file_index"] = _currentFileIndex;
    json["verified"]   = verified;
    json["error_code"] = static_cast<int>(errorCode);
    json["error_msg"]  = errorMsg;

    QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray frame = FrameCodec::encode(gy::protocol::kTypeChunkAck, data);
    _socket->write(frame);
}

// 填充接收会话信息到 QVariantMap，供 QML 展示传输记录
void FileReceiverWorker::fillReceiveSession(QVariantMap &session) const
{
    session["sessionId"]    = _sessionId;
    session["senderDeviceId"] = _senderDeviceId;
    session["senderName"]   = _senderName;
    session["fileName"]     = _displayName;
    session["fileSize"]     = _fileSize;
    session["totalFiles"]   = _totalFiles;
    session["totalBytes"]   = _totalBytes;
    session["isDirectory"]  = _isDirectory;
    // 如果是目录，填充根目录预览（只显示顶层文件和目录）
    if (_isDirectory) {
        QVariantList preview;
        QSet<QString> seen;
        for (const auto &item : _fileList) {
            const QString cleanPath = item.relativePath.endsWith('/') ? item.relativePath.chopped(1) : item.relativePath;
            const QStringList parts = cleanPath.split('/', Qt::SkipEmptyParts);
            if (parts.isEmpty()) continue;

            const bool isRootDirectory = parts.size() > 1 || item.relativePath.endsWith('/');
            const QString rootEntry = parts.first() + (isRootDirectory ? "/" : "");
            if (!seen.contains(rootEntry)) {
                seen.insert(rootEntry);
                preview.append(rootEntry);
            }
        }
        session["fileList"] = preview;
    } else {
        session["fileList"] = QVariantList{};
    }
}

// 获取文件夹根目录预览路径列表（所有文件的相对路径）
QStringList FileReceiverWorker::rootPreviewPaths() const
{
    QStringList paths;
    for (const auto &item : _fileList) {
        paths.append(item.relativePath);
    }
    return paths;
}
