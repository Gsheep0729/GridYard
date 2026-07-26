/**
* @file    transfer_session_manager.cpp
* @version 7.8.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   传输会话管理器实现
*
* Change Log:
* [v7.8.0] GY   2026-07-21
* * 传输连接增加重试机制和候选端点超时
* [v6.6.2] GY   2026-06-25
* * 支持按当前设备清空已结束传输记录
* [v6.3.0] GY   2026-06-25
* * 生成结束态传输快照并支持恢复历史记录
* [v4.16.1] FengChunlin   2026-06-21
* * 接收请求和完成结果改为跨线程值传递，删除 Worker 状态读取函数
* [v4.15.0] FengChunlin   2026-06-16
* * transferFinished 信号适配 ErrorCode 参数
* * 会话模型新增 errorCode 字段
* [v4.14.0] GY   2026-06-15
* * 支持清理传输记录并删除已接收的本地文件
* [v4.13.3] DuRuoxian   2026-06-15
* * 仅在可见进度变化时通知会话列表，减少文件夹传输任务闪烁
* [v4.13.2] DuRuoxian   2026-06-15
* * 统一生成文件夹根目录预览并传递给接收确认弹窗
* [v4.13.1] DuRuoxian   2026-06-15
* * 添加 isDirectory 和 fileList 字段到会话
* [v4.11.0] FengChunlin   2026-06-13
* * 支持按配置自动接受并保存接收文件
* [v4.10.1] DuRuoxian   2026-06-11
* * 补齐接收会话字段，保证 QML 模型角色一致
* [v4.10.0] GY   2026-06-13
* * 新增 removeSession() 方法，添加 createdAt 时间戳
* [v4.8.3] FengChunlin   2026-06-10
* * 文件传输使用发送方设备别名
* [v4.7.1] GY   2026-06-07
* * 移除 QML_SINGLETON，改为通过 AppController 暴露
* [v4.3.4] GY   2026-05-27
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.3.1] GY   2026-05-21
* * Stage 3.10：实现 cancelSession()；保存/清理发送方 worker
* [v0.3.0] FengChunlin   2026-05-19
* * 接收会话连接 transferFinished 信号；设置接收路径
* [v0.2.0] GY   2026-05-08
* * Stage 3：初始版本
*/

#include "transfer_session_manager.h"
#include "config_manager.h"
#include "dir_serializer.h"
#include "discovery_service.h"
#include "file_receiver_worker.h"
#include "file_sender_worker.h"
#include "p2p_server.h"

// 确保 QVariant::fromValue 能处理 FileReceiverWorker*
Q_DECLARE_METATYPE(FileReceiverWorker*)

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QUuid>
#include <QDateTime>

#include <algorithm>
#include <utility>

namespace {

// 判断会话状态是否为已结束（完成、失败、拒绝、取消）
bool isFinishedStatus(const QString &status)
{
    return status == "completed" || status == "failed"
           || status == "rejected" || status == "cancelled";
}

// 根据 Worker 结果归一化最终状态，避免取消和拒绝被错误折叠成 failed
QString normalizedFinalStatus(bool success, gy::protocol::ErrorCode errorCode,
                              const QString &currentStatus)
{
    if (currentStatus == "cancelled") {
        return "cancelled";
    }
    if (currentStatus == "rejected") {
        return "rejected";
    }
    if (success) {
        return "completed";
    }
    if (errorCode == gy::protocol::ErrorCode::UserRejected) {
        return "rejected";
    }
    if (errorCode == gy::protocol::ErrorCode::UserCancelled) {
        return "cancelled";
    }
    return "failed";
}

// 统计发送任务中的真实文件数和总字节数，为早失败场景保留完整历史快照
QPair<int, qint64> transferStatsForPath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {0, 0};
    }
    if (!info.isDir()) {
        return {1, info.size()};
    }

    const auto items = gy::DirSerializer::serializeNoHash(path);
    int fileCount = 0;
    qint64 totalBytes = 0;
    for (const auto &item : items) {
        // 目录占位项只用于恢复层级，不计入实际文件数量和传输字节数。
        if (item.relativePath.endsWith('/')) {
            continue;
        }
        ++fileCount;
        totalBytes += item.sizeBytes;
    }
    return {fileCount, totalBytes};
}

// 从会话字段读取 UTC 时间
QDateTime sessionTime(const QVariantMap &session, const QString &key)
{
    return QDateTime::fromString(session.value(key).toString(), Qt::ISODateWithMs);
}

// 构建文件夹根目录预览（只显示顶层文件和目录）
QVariantList buildRootPreview(const QStringList &paths)
{
    QVariantList result;
    QSet<QString> seen;

    for (const QString &path : paths) {
        // 只展示根层条目，深层文件夹折叠为它所属的顶层目录。
        const QString cleanPath = path.endsWith('/') ? path.chopped(1) : path;
        const QStringList parts = cleanPath.split('/', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            continue;
        }

        const bool isRootDirectory = parts.size() > 1 || path.endsWith('/');
        const QString rootEntry = parts.first() + (isRootDirectory ? "/" : "");
        if (!seen.contains(rootEntry)) {
            seen.insert(rootEntry);
            result.append(rootEntry);
        }
    }

    return result;
}

}

// 构造函数
TransferSessionManager::TransferSessionManager(QObject *parent)
    : QObject{parent}
{
}

// 获取会话列表（供 QML 绑定）
QVariantList TransferSessionManager::sessions() const
{
    QVariantList list;
    for (const QVariantMap &session : _sessions) {
        list.append(session);
    }
    return list;
}

// 初始化：绑定配置、发现服务和 P2P 服务器
void TransferSessionManager::init(ConfigManager *config, DiscoveryService *discovery,
                                   P2pServer *p2pServer)
{
    _config = config;
    _discovery = discovery;
    _p2pServer = p2pServer;

    // 连接 P2pServer 的传输请求信号
    connect(_p2pServer, &P2pServer::transferRequestReceived,
            this,       &TransferSessionManager::onTransferRequestReceived);
}

// 创建发送会话（建立 TCP 连接并启动文件传输）
void TransferSessionManager::createSendSession(const QString &deviceId, const QString &filePath)
{
    qDebug() << "[TransferSession] 创建发送会话";
    qDebug() << "[TransferSession] 目标设备ID:" << deviceId;
    qDebug() << "[TransferSession] 文件路径:" << filePath;

    // 获取用于发送的对端快照
    const QVariantMap endpoint = _discovery->transferEndpoint(deviceId);
    if (endpoint.isEmpty()) {
        qWarning() << "[TransferSession] 目标设备不存在或已离线:" << deviceId;
        emit errorOccurred(tr("目标设备不存在或已离线"));
        return;
    }

    const QString host = endpoint["ipAddress"].toString();
    quint16 port = static_cast<quint16>(endpoint["tcpPort"].toUInt());
    if (port == 0) {
        port = _config->tcpPort();
    }
    const QString peerName = endpoint["deviceName"].toString();
    const auto [fileCount, totalBytes] = transferStatsForPath(filePath);

    qDebug() << "[TransferSession] 目标设备信息:";
    qDebug() << "  设备名:" << peerName;
    qDebug() << "  IP 地址:" << host;
    qDebug() << "  端口:" << port;

    // 创建发送会话
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QVariantMap session;
    session["sessionId"] = sessionId;
    session["type"]      = "send";
    session["deviceId"]  = deviceId;
    session["peerDeviceName"] = peerName;
    session["filePath"]  = filePath;
    session["fileName"]  = QFileInfo{filePath}.fileName();
    session["isDirectory"] = QFileInfo{filePath}.isDir();
    session["fileCount"] = fileCount;
    session["status"]    = "connecting";
    session["progress"]  = 0;
    session["bytesTransferred"] = 0;
    session["totalBytes"] = totalBytes;
    session["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    session["fileList"]  = QVariantList{};
    session["localPath"] = "";
    session["canDeleteLocalFile"] = false;

    // 委托 ConfigManager 填充发送方信息（Tell, Don't Ask）
    _config->fillSenderInfo(session);

    // 如果是文件夹，获取文件列表
    if (QFileInfo{filePath}.isDir()) {
        auto fileList = gy::DirSerializer::serializeNoHash(filePath);
        QStringList paths;
        for (const auto &item : fileList) {
            paths.append(item.relativePath);
        }
        session["fileList"] = buildRootPreview(paths);
    }

    _sessions.append(session);
    emit sessionsChanged();

    qDebug() << "[TransferSession] 会话已创建，ID:" << sessionId;

    // 创建 FileSenderWorker 并在工作线程中运行
    auto *worker = new FileSenderWorker{};
    auto *thread = new QThread{this};

    worker->moveToThread(thread);

    // 线程结束后按 Qt 惯用法销毁 worker 和线程：worker 在自己的事件循环里被 deleteLater，
    // 线程随后自删。不能用 quit()+wait()+deleteLater()——wait 后事件循环已停，
    // DeferredDelete 无人处理会导致 worker 连同 socket 永久泄漏，wait 本身还会阻塞 UI 线程。
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // 保存 worker 引用
    _sendWorkers[sessionId] = worker;

    // 从 session 中获取发送方信息（Tell, Don't Ask）
    const QString senderDeviceId = session["senderDeviceId"].toString();
    const QString senderName = session["senderName"].toString();

    // 连接信号
    connect(thread, &QThread::started, worker,
            [worker, host, port, filePath, senderDeviceId, senderName]() {
        worker->startTransfer(host, port, filePath, senderDeviceId, senderName);  // 在工作线程中启动传输
    });

    connect(worker, &FileSenderWorker::progressChanged,
            this, [this, sessionId](qint64 bytesSent, qint64 totalBytes) {
        // 遍历会话列表找到匹配的发送会话并更新进度
        for (int i = 0; i < _sessions.size(); ++i) {
            if (_sessions[i]["sessionId"].toString() == sessionId) {
                const QString previousStatus = _sessions[i]["status"].toString();
                const int previousProgress = _sessions[i]["progress"].toInt();
                const qint64 previousTotalBytes = _sessions[i]["totalBytes"].toLongLong();
                const int progress = totalBytes > 0 ? (bytesSent * 100 / totalBytes) : 0;  // 百分比计算

                _sessions[i]["status"] = "transferring";
                _sessions[i]["progress"] = progress;
                _sessions[i]["bytesTransferred"] = bytesSent;
                _sessions[i]["totalBytes"] = totalBytes;
                // 仅在状态、进度或总字节数实际变化时通知 QML，避免文件夹传输任务频繁闪烁
                if (previousStatus != "transferring"
                    || previousProgress != progress
                    || previousTotalBytes != totalBytes) {
                    emit sessionsChanged();
                }
                break;
            }
        }
    });

    connect(worker, &FileSenderWorker::transferFinished,
            this, [this, sessionId, thread, worker](bool success, gy::protocol::ErrorCode errorCode, const QString &errorMsg) {
        // 先读取当前会话状态，用户手动取消/拒绝的状态不应被 Worker 结果覆盖
        QString currentStatus = "failed";
        for (const QVariantMap &session : std::as_const(_sessions)) {
            if (session["sessionId"].toString() == sessionId) {
                currentStatus = session["status"].toString();
                break;
            }
        }
        const QString finalStatus = normalizedFinalStatus(success, errorCode, currentStatus);  // 归一化最终状态
        finalizeSession(sessionId, finalStatus, errorCode, errorMsg);  // 生成快照并通知持久化

        // 清理 worker 引用
        _sendWorkers.remove(sessionId);

        // 只请求线程退出，销毁由上面的 finished→deleteLater 链接完成，不在 UI 线程 wait
        thread->quit();
    });

    // 启动线程
    thread->start();

    qDebug() << "TransferSessionManager: 创建发送会话" << sessionId
             << "目标" << deviceId << "文件" << filePath;
}

// 接收会话（用户确认接收文件）
void TransferSessionManager::acceptReceiveSession(const QString &sessionId)
{
    // 查找会话对应的 worker
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId &&
            _sessions[i]["type"].toString() == "receive") {

            if (isFinishedStatus(_sessions[i]["status"].toString())) {
                break;  // 会话已结束，worker 已释放，忽略迟到的接受
            }
            FileReceiverWorker *worker = _sessions[i]["worker"].value<FileReceiverWorker*>();
            if (worker) {
                // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
                QMetaObject::invokeMethod(worker, [worker]() {
                    worker->acceptTransfer();
                }, Qt::QueuedConnection);
                _sessions[i]["status"] = "transferring";
                emit sessionsChanged();
            }
            break;
        }
    }
}

// 拒绝接收会话
void TransferSessionManager::rejectReceiveSession(const QString &sessionId)
{
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId &&
            _sessions[i]["type"].toString() == "receive") {

            if (isFinishedStatus(_sessions[i]["status"].toString())) {
                break;  // 会话已结束，worker 已释放，忽略迟到的拒绝
            }
            FileReceiverWorker *worker = _sessions[i]["worker"].value<FileReceiverWorker*>();
            if (worker) {
                // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
                QMetaObject::invokeMethod(worker, [worker]() {
                    worker->rejectTransfer();
                }, Qt::QueuedConnection);
                _sessions[i]["status"] = "rejected";
                emit sessionsChanged();
            }
            break;
        }
    }
}

// 取消传输会话
void TransferSessionManager::cancelSession(const QString &sessionId)
{
    // 查找会话
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId) {
            if (isFinishedStatus(_sessions[i]["status"].toString())) {
                break;  // 已结束会话不能再取消，其 worker 可能已释放
            }
            QString type = _sessions[i]["type"].toString();

            if (type == "send") {
                // 发送方：通过 worker 发送 Cancel 帧
                FileSenderWorker *worker = _sendWorkers.value(sessionId);
                if (worker) {
                    QMetaObject::invokeMethod(worker, "cancel");
                }
            } else if (type == "receive") {
                // 接收方：拒绝传输（会触发对方超时或连接断开）
                FileReceiverWorker *worker = _sessions[i]["worker"].value<FileReceiverWorker*>();
                if (worker) {
                    // 使用 QMetaObject::invokeMethod 在 worker 的线程中调用
                    QMetaObject::invokeMethod(worker, [worker]() {
                        worker->rejectTransfer(tr("用户取消"));
                    }, Qt::QueuedConnection);
                }
            }

            // 更新状态
            _sessions[i]["status"] = "cancelled";
            emit sessionsChanged();

            qDebug() << "TransferSessionManager: 取消会话" << sessionId;
            break;
        }
    }
}

// 移除已结束的传输记录
void TransferSessionManager::removeSession(const QString &sessionId)
{
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId) {
            const QString status = _sessions[i]["status"].toString();

            // 只允许移除已完成、失败、取消的会话
            if (isFinishedStatus(status)) {
                const QString recordId = _sessions[i]["recordId"].toString();
                _sessions.removeAt(i);
                emit sessionsChanged();
                if (!recordId.isEmpty()) {
                    emit transferHistoryDeleteRequested({recordId});
                }
                qDebug() << "TransferSessionManager: 移除会话" << sessionId;
            } else {
                qWarning() << "TransferSessionManager: 无法移除进行中的会话" << sessionId;
            }
            break;
        }
    }
}

// 移除传输记录并删除已接收的本地文件
void TransferSessionManager::removeSessionAndDeleteFile(const QString &sessionId)
{
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() != sessionId) {
            continue;
        }

        if (!deleteReceivedFile(_sessions[i])) {
            return;
        }

        const QString recordId = _sessions[i]["recordId"].toString();
        _sessions.removeAt(i);
        emit sessionsChanged();
        if (!recordId.isEmpty()) {
            emit transferHistoryDeleteRequested({recordId});
        }
        emit messageOccurred(tr("已删除本地文件并移除传输记录"));
        return;
    }
}

// 清空所有已结束的传输记录（可选删除已接收文件）
void TransferSessionManager::clearFinishedSessions(bool deleteReceivedFiles, const QString &deviceId)
{
    int removedCount = 0;
    int deletedCount = 0;
    QStringList recordIds;

    // 倒序移除，避免删除元素后改变后续索引
    for (int i = _sessions.size() - 1; i >= 0; --i) {
        if (!isFinishedStatus(_sessions[i]["status"].toString())) {
            continue;
        }
        if (!deviceId.isEmpty() && _sessions[i]["deviceId"].toString() != deviceId) {
            continue;
        }

        if (deleteReceivedFiles && _sessions[i]["canDeleteLocalFile"].toBool()) {
            if (!deleteReceivedFile(_sessions[i])) {
                continue;
            }
            ++deletedCount;
        }

        const QString recordId = _sessions[i]["recordId"].toString();
        if (!recordId.isEmpty()) {
            recordIds.append(recordId);
        }
        _sessions.removeAt(i);
        ++removedCount;
    }

    if (removedCount > 0) {
        emit sessionsChanged();
        if (!recordIds.isEmpty()) {
            emit transferHistoryDeleteRequested(recordIds);
        }
        emit messageOccurred(deleteReceivedFiles
                             ? tr("已清理 %1 条记录并删除 %2 个本地项目")
                                   .arg(removedCount).arg(deletedCount)
                             : tr("已清理 %1 条传输记录").arg(removedCount));
    }
}

// 删除已接收的本地文件（仅允许接收成功的记录）
bool TransferSessionManager::deleteReceivedFile(const QVariantMap &session)
{
    if (!session["canDeleteLocalFile"].toBool()
        || session["type"].toString() != "receive"
        || session["status"].toString() != "completed") {
        emit errorOccurred(tr("该记录没有可删除的已接收文件"));
        return false;
    }

    const QString localPath = QDir::cleanPath(session["localPath"].toString());
    const QFileInfo info{localPath};
    if (!info.isAbsolute() || localPath == QDir::rootPath() || !info.exists()) {
        emit errorOccurred(tr("本地保存路径无效或文件不存在，未移除记录"));
        return false;
    }

    // 符号链接按文件删除，避免递归进入链接目标
    const bool removed = info.isDir() && !info.isSymLink()
                         ? QDir{localPath}.removeRecursively()
                         : QFile::remove(localPath);
    if (!removed) {
        emit errorOccurred(tr("无法删除本地文件：%1").arg(localPath));
        return false;
    }
    return true;
}

// 处理新的传输请求（创建接收会话，通知 UI 弹窗确认）
void TransferSessionManager::onTransferRequestReceived(FileReceiverWorker *worker,
                                                        const QVariantMap &request)
{
    const QString sessionId = request["sessionId"].toString();
    const QString senderDeviceId = request["senderDeviceId"].toString();
    const QString senderName = request["senderName"].toString();
    const QString fileName = request["fileName"].toString();
    const qint64 fileSize = request["fileSize"].toLongLong();
    const int totalFiles = request["totalFiles"].toInt();
    const qint64 totalBytes = request["totalBytes"].toLongLong();
    qDebug() << "[TransferSession] 收到传输请求";
    qDebug() << "[TransferSession] 发送方设备ID:" << senderDeviceId;
    qDebug() << "[TransferSession] 发送方:" << senderName;
    qDebug() << "[TransferSession] 文件名:" << fileName;
    qDebug() << "[TransferSession] 文件大小:" << fileSize;
    qDebug() << "[TransferSession] 总文件数:" << totalFiles;
    qDebug() << "[TransferSession] 总大小:" << totalBytes;

    // 设置接收路径（使用 QMetaObject::invokeMethod 在 worker 的线程中调用）
    if (_config) {
        QMetaObject::invokeMethod(worker, [worker, config = _config]() {
            worker->setReceivePath(config->receivePath());
        }, Qt::QueuedConnection);
        qDebug() << "[TransferSession] 接收路径:" << _config->receivePath();
    }

    QVariantMap session = request;
    session["type"]      = "receive";
    session["deviceId"]  = senderDeviceId;
    session["peerDeviceName"] = senderName;
    session["senderName"] = senderName;
    session["filePath"]  = "";
    session["status"]    = "waiting_confirm";
    session["progress"]  = 0;
    session["bytesTransferred"] = 0;
    session["worker"]    = QVariant::fromValue(worker);
    session["createdAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    session["fileCount"] = totalFiles;
    session["localPath"] = "";
    session["canDeleteLocalFile"] = false;

    _sessions.append(session);
    emit sessionsChanged();

    qDebug() << "[TransferSession] 接收会话已创建，ID:" << sessionId;

    // 连接接收进度信号（通过 QMetaObject 跨线程投递回主线程）
    connect(worker, &FileReceiverWorker::progressChanged,
            this, [this, sessionId](qint64 bytesReceived, qint64 totalBytes) {
        for (int i = 0; i < _sessions.size(); ++i) {
            if (_sessions[i]["sessionId"].toString() == sessionId) {
                const QString previousStatus = _sessions[i]["status"].toString();
                const int previousProgress = _sessions[i]["progress"].toInt();
                const qint64 previousTotalBytes = _sessions[i]["totalBytes"].toLongLong();
                const int progress = totalBytes > 0 ? (bytesReceived * 100 / totalBytes) : 0;

                _sessions[i]["status"] = "transferring";
                _sessions[i]["progress"] = progress;
                _sessions[i]["bytesTransferred"] = bytesReceived;
                _sessions[i]["totalBytes"] = totalBytes;
                // 仅在可见字段变化时通知 QML，减少高频进度更新导致的不必要刷新
                if (previousStatus != "transferring"
                    || previousProgress != progress
                    || previousTotalBytes != totalBytes) {
                    emit sessionsChanged();
                }
                break;
            }
        }
    });

    // 连接接收完成信号，生成最终快照并触发持久化
    connect(worker, &FileReceiverWorker::transferFinished,
            this, [this, sessionId, worker](bool success, gy::protocol::ErrorCode errorCode,
                                            const QString &errorMsg, const QString &savedPath) {
        qDebug() << "[TransferSession] 接收传输完成，成功:" << success << "错误:" << errorMsg;
        QString currentStatus = "failed";
        for (const QVariantMap &session : std::as_const(_sessions)) {
            if (session["sessionId"].toString() == sessionId) {
                currentStatus = session["status"].toString();
                break;
            }
        }
        const QString finalStatus = normalizedFinalStatus(success, errorCode, currentStatus);
        finalizeSession(sessionId, finalStatus, errorCode, errorMsg, savedPath);

        worker->deleteLater();
    });

    if (_config && _config->autoAcceptFiles()) {
        qDebug() << "[TransferSession] 自动接受接收请求:" << sessionId;
        acceptReceiveSession(sessionId);
        emit messageOccurred(tr("已自动接受 \"%1\"，正在保存").arg(fileName));
    } else {
        // 通知 QML 弹窗确认
        const QVariantList previewFiles = _sessions.last()["fileList"].toList();
        emit receiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                    fileSize, totalFiles, totalBytes,
                                    _sessions.last()["isDirectory"].toBool(), previewFiles);
    }

    qDebug() << "TransferSessionManager: 收到接收请求" << sessionId
             << "来自" << senderName << "文件" << fileName;
}

// 将已完成传输历史恢复到会话列表
void TransferSessionManager::restoreFinishedTransfers(const QList<TransferRecord> &records)
{
    bool changed = false;
    for (auto it = records.crbegin(); it != records.crend(); ++it) {
        const bool exists = std::any_of(_sessions.cbegin(), _sessions.cend(),
                                        [&it](const QVariantMap &session) {
                                            return session["sessionId"].toString() == it->sessionId;
                                        });
        if (exists) {
            continue;
        }
        _sessions.append(sessionFromRecord(*it));
        changed = true;
    }

    if (changed) {
        emit sessionsChanged();
    }
}

// 将运行期会话收敛为可持久化的最终快照
void TransferSessionManager::finalizeSession(const QString &sessionId, const QString &finalStatus,
                                             gy::protocol::ErrorCode errorCode,
                                             const QString &errorMessage,
                                             const QString &savedPath)
{
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() != sessionId) {
            continue;
        }

        _sessions[i]["status"] = finalStatus;
        _sessions[i]["progress"] = finalStatus == "completed" ? 100 : _sessions[i]["progress"].toInt();
        _sessions[i]["errorMsg"] = errorMessage;
        _sessions[i]["errorCode"] = static_cast<quint16>(errorCode);
        // 会话结束后接收 worker 即将 deleteLater，清空指针避免后续 cancel/accept 操作已释放对象
        _sessions[i].remove("worker");
        if (finalStatus == "completed") {
            _sessions[i]["bytesTransferred"] = _sessions[i]["totalBytes"];
        }
        if (!savedPath.isEmpty()) {
            _sessions[i]["localPath"] = savedPath;
            _sessions[i]["canDeleteLocalFile"] = finalStatus == "completed";  // 仅接收成功时允许删除本地文件
        }

        QString recordId = _sessions[i]["recordId"].toString();
        if (recordId.isEmpty()) {
            recordId = QUuid::createUuid().toString(QUuid::WithoutBraces);  // 首次完成时生成持久化记录 ID
            _sessions[i]["recordId"] = recordId;
        }

        emit sessionsChanged();

        if (finalStatus == "completed") {
            if (_sessions[i]["type"].toString() == "receive" && _config) {
                emit transferCompleted(sessionId, _sessions[i]["fileName"].toString(),
                                       _config->receivePath());
            }
            emit messageOccurred(_sessions[i]["type"].toString() == "send"
                                     ? tr("文件 \"%1\" 发送成功").arg(_sessions[i]["fileName"].toString())
                                     : tr("文件 \"%1\" 接收成功").arg(_sessions[i]["fileName"].toString()));
        } else if (_sessions[i]["type"].toString() == "send") {
            emit errorOccurred(tr("发送失败：%1").arg(errorMessage));
        } else {
            emit errorOccurred(tr("接收失败：%1").arg(errorMessage));
        }

        TransferRecord record;
        record.recordId = recordId;
        record.sessionId = _sessions[i]["sessionId"].toString();
        record.peerDeviceId = _sessions[i]["deviceId"].toString();
        record.peerName = _sessions[i]["peerDeviceName"].toString();
        record.direction = _sessions[i]["type"].toString() == "send"
                               ? RecordDirection::Outgoing
                               : RecordDirection::Incoming;
        record.displayName = _sessions[i]["fileName"].toString();
        record.isDirectory = _sessions[i]["isDirectory"].toBool();
        record.fileCount = _sessions[i]["fileCount"].toInt();
        record.totalBytes = _sessions[i]["totalBytes"].toLongLong();
        record.status = finalStatus;
        record.startedAt = sessionTime(_sessions[i], "createdAt");
        record.finishedAt = QDateTime::currentDateTimeUtc();
        record.errorCode = finalStatus == "completed" ? 0 : static_cast<int>(errorCode);
        record.errorMessage = finalStatus == "completed" ? QString{} : errorMessage;
        emit transferToPersist(record);
        return;
    }
}

// 将一条历史记录恢复成 QML 可消费的会话项
QVariantMap TransferSessionManager::sessionFromRecord(const TransferRecord &record) const
{
    QVariantMap session;
    session["recordId"] = record.recordId;
    session["sessionId"] = record.sessionId;
    session["type"] = record.direction == RecordDirection::Outgoing ? "send" : "receive";
    session["deviceId"] = record.peerDeviceId;
    session["peerDeviceName"] = record.peerName;
    session["filePath"] = "";
    session["fileName"] = record.displayName;
    session["isDirectory"] = record.isDirectory;
    session["fileCount"] = record.fileCount;
    session["status"] = record.status;
    session["progress"] = record.status == "completed" ? 100 : 0;
    session["bytesTransferred"] = record.status == "completed" ? record.totalBytes : 0;
    session["totalBytes"] = record.totalBytes;
    session["createdAt"] = record.startedAt.toUTC().toString(Qt::ISODateWithMs);
    session["fileList"] = QVariantList{};
    session["localPath"] = "";
    session["canDeleteLocalFile"] = false;
    session["errorCode"] = record.errorCode;
    session["errorMsg"] = record.errorMessage;
    return session;
}
