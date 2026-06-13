/**
* @file    transfer_session_manager.cpp
* @version 4.11.0
* @date    2026-06-13
* @author  GridYard Team
* @brief   TransferSessionManager 实现
*
* Change Log:
* [v4.11.0] GY   2026-06-13
* * 支持按配置自动接受并保存接收文件
* [v4.10.1] GY   2026-06-13
* * 补齐接收会话字段，保证 QML 模型角色一致
* [v4.10.0] GY   2026-06-13
* * 新增 removeSession() 方法，添加 createdAt 时间戳
* [v4.8.3] GY   2026-06-13
* * 文件传输使用发送方设备别名
* [v4.7.1] GY   2026-06-05
* * 移除 QML_SINGLETON，改为通过 AppController 暴露
* [v4.3.4] GY   2026-06-04
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.3.1] GY   2026-06-03
* * Stage 3.10：实现 cancelSession()；保存/清理发送方 worker
* [v0.3.0] GY   2026-06-03
* * 接收会话连接 transferFinished 信号；设置接收路径
* [v0.2.0] GY   2026-06-02
* * Stage 3：初始版本
*/

#include "transfer_session_manager.h"
#include "config_manager.h"
#include "discovery_service.h"
#include "file_receiver_worker.h"
#include "file_sender_worker.h"
#include "p2p_server.h"

// 确保 QVariant::fromValue 能处理 FileReceiverWorker*
Q_DECLARE_METATYPE(FileReceiverWorker*)

#include <QDebug>
#include <QFileInfo>
#include <QThread>
#include <QUuid>
#include <QDateTime>

TransferSessionManager::TransferSessionManager(QObject *parent)
    : QObject{parent}
{
}

QVariantList TransferSessionManager::sessions() const
{
    QVariantList list;
    for (const QVariantMap &session : _sessions) {
        list.append(session);
    }
    return list;
}

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

void TransferSessionManager::createSendSession(const QString &deviceId, const QString &filePath)
{
    qDebug() << "[TransferSession] 创建发送会话";
    qDebug() << "[TransferSession] 目标设备ID:" << deviceId;
    qDebug() << "[TransferSession] 文件路径:" << filePath;

    // 从 DiscoveryService 获取目标设备信息
    PeerInfo peer = _discovery->peerInfo(deviceId);
    if (peer.deviceId.isEmpty()) {
        qWarning() << "[TransferSession] 目标设备不存在:" << deviceId;
        emit errorOccurred(tr("目标设备不存在"));
        return;
    }
    if (!peer.isOnline) {
        qWarning() << "[TransferSession] 目标设备已离线:" << peer.deviceName;
        emit errorOccurred(tr("目标设备 \"%1\" 已离线").arg(peer.deviceName));
        return;
    }

    QString host = peer.ipAddress;
    quint16 port = peer.tcpPort > 0 ? peer.tcpPort : _config->tcpPort();
    const QString senderDeviceId = _config->deviceId();
    const QString senderName = _config->deviceName();

    qDebug() << "[TransferSession] 目标设备信息:";
    qDebug() << "  设备名:" << peer.deviceName;
    qDebug() << "  IP 地址:" << host;
    qDebug() << "  端口:" << port;

    // 创建发送会话
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QVariantMap session;
    session["sessionId"] = sessionId;
    session["type"]      = "send";
    session["deviceId"]  = deviceId;
    session["peerDeviceName"] = peer.deviceName;
    session["filePath"]  = filePath;
    session["fileName"]  = QFileInfo{filePath}.fileName();
    session["status"]    = "connecting";
    session["progress"]  = 0;
    session["bytesTransferred"] = 0;
    session["totalBytes"] = 0;
    session["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    _sessions.append(session);
    emit sessionsChanged();

    qDebug() << "[TransferSession] 会话已创建，ID:" << sessionId;

    // 创建 FileSenderWorker 并在工作线程中运行
    auto *worker = new FileSenderWorker{};
    auto *thread = new QThread{this};

    worker->moveToThread(thread);

    // 保存 worker 引用
    _sendWorkers[sessionId] = worker;

    // 连接信号
    connect(thread, &QThread::started, worker,
            [worker, host, port, filePath, senderDeviceId, senderName]() {
        worker->startTransfer(host, port, filePath, senderDeviceId, senderName);
    });

    connect(worker, &FileSenderWorker::progressChanged,
            this, [this, sessionId](qint64 bytesSent, qint64 totalBytes) {
        // 更新会话进度
        for (int i = 0; i < _sessions.size(); ++i) {
            if (_sessions[i]["sessionId"].toString() == sessionId) {
                _sessions[i]["status"] = "transferring";
                _sessions[i]["progress"] = totalBytes > 0 ? (bytesSent * 100 / totalBytes) : 0;
                _sessions[i]["bytesTransferred"] = bytesSent;
                _sessions[i]["totalBytes"] = totalBytes;
                emit sessionsChanged();
                break;
            }
        }
    });

    connect(worker, &FileSenderWorker::transferFinished,
            this, [this, sessionId, thread, worker](bool success, const QString &errorMsg) {
        // 更新会话状态
        for (int i = 0; i < _sessions.size(); ++i) {
            if (_sessions[i]["sessionId"].toString() == sessionId) {
                _sessions[i]["status"] = success ? "completed" : "failed";
                _sessions[i]["progress"] = success ? 100 : _sessions[i]["progress"].toInt();
                _sessions[i]["errorMsg"] = errorMsg;
                emit sessionsChanged();

                // 通知用户传输结果
                if (success) {
                    QString fileName = _sessions[i]["filePath"].toString();
                    QFileInfo info(fileName);
                    emit messageOccurred(tr("文件 \"%1\" 发送成功").arg(info.fileName()));
                } else {
                    emit errorOccurred(tr("发送失败：%1").arg(errorMsg));
                }
                break;
            }
        }

        // 清理 worker 引用
        _sendWorkers.remove(sessionId);

        // 清理线程
        thread->quit();
        thread->wait();
        worker->deleteLater();
        thread->deleteLater();
    });

    // 启动线程
    thread->start();

    qDebug() << "TransferSessionManager: 创建发送会话" << sessionId
             << "目标" << deviceId << "文件" << filePath;
}

void TransferSessionManager::acceptReceiveSession(const QString &sessionId)
{
    // 查找会话对应的 worker
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId &&
            _sessions[i]["type"].toString() == "receive") {

            FileReceiverWorker *worker = _sessions[i]["worker"].value<FileReceiverWorker*>();
            if (worker) {
                worker->acceptTransfer();
                _sessions[i]["status"] = "transferring";
                emit sessionsChanged();
            }
            break;
        }
    }
}

void TransferSessionManager::rejectReceiveSession(const QString &sessionId)
{
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId &&
            _sessions[i]["type"].toString() == "receive") {

            FileReceiverWorker *worker = _sessions[i]["worker"].value<FileReceiverWorker*>();
            if (worker) {
                worker->rejectTransfer();
                _sessions[i]["status"] = "rejected";
                emit sessionsChanged();
            }
            break;
        }
    }
}

void TransferSessionManager::cancelSession(const QString &sessionId)
{
    // 查找会话
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId) {
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
                    worker->rejectTransfer(tr("用户取消"));
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

void TransferSessionManager::removeSession(const QString &sessionId)
{
    for (int i = 0; i < _sessions.size(); ++i) {
        if (_sessions[i]["sessionId"].toString() == sessionId) {
            QString status = _sessions[i]["status"].toString();

            // 只允许移除已完成、失败、取消的会话
            if (status == "completed" || status == "failed" ||
                status == "rejected" || status == "cancelled") {
                _sessions.removeAt(i);
                emit sessionsChanged();
                qDebug() << "TransferSessionManager: 移除会话" << sessionId;
            } else {
                qWarning() << "TransferSessionManager: 无法移除进行中的会话" << sessionId;
            }
            break;
        }
    }
}

void TransferSessionManager::onTransferRequestReceived(FileReceiverWorker *worker,
                                                        const QString &senderDeviceId,
                                                        const QString &senderName,
                                                        const QString &fileName,
                                                        qint64 fileSize,
                                                        int totalFiles,
                                                        qint64 totalBytes)
{
    qDebug() << "[TransferSession] 收到传输请求";
    qDebug() << "[TransferSession] 发送方设备ID:" << senderDeviceId;
    qDebug() << "[TransferSession] 发送方:" << senderName;
    qDebug() << "[TransferSession] 文件名:" << fileName;
    qDebug() << "[TransferSession] 文件大小:" << fileSize;
    qDebug() << "[TransferSession] 总文件数:" << totalFiles;
    qDebug() << "[TransferSession] 总大小:" << totalBytes;

    // 创建接收会话
    QString sessionId = worker->sessionId();

    // 设置接收路径
    if (_config) {
        worker->setReceivePath(_config->receivePath());
        qDebug() << "[TransferSession] 接收路径:" << _config->receivePath();
    }

    QVariantMap session;
    session["sessionId"] = sessionId;
    session["type"]      = "receive";
    session["deviceId"]  = senderDeviceId;
    session["peerDeviceName"] = senderName;
    session["senderName"] = senderName;
    session["filePath"]  = "";
    session["fileName"]  = fileName;
    session["fileSize"]  = fileSize;
    session["totalFiles"] = totalFiles;
    session["totalBytes"] = totalBytes;
    session["status"]    = "waiting_confirm";
    session["progress"]  = 0;
    session["bytesTransferred"] = 0;
    session["worker"]    = QVariant::fromValue(worker);
    session["createdAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    _sessions.append(session);
    emit sessionsChanged();

    qDebug() << "[TransferSession] 接收会话已创建，ID:" << sessionId;

    // 连接接收完成信号
    connect(worker, &FileReceiverWorker::transferFinished,
            this, [this, sessionId, worker](bool success, const QString &errorMsg) {
        qDebug() << "[TransferSession] 接收传输完成，成功:" << success << "错误:" << errorMsg;

        for (int i = 0; i < _sessions.size(); ++i) {
            if (_sessions[i]["sessionId"].toString() == sessionId) {
                // 如果已经是 cancelled 状态，不覆盖
                if (_sessions[i]["status"].toString() == "cancelled") {
                    qDebug() << "[TransferSession] 会话已取消，跳过状态更新";
                    break;
                }

                _sessions[i]["status"] = success ? "completed" : "failed";
                _sessions[i]["progress"] = success ? 100 : _sessions[i]["progress"].toInt();
                _sessions[i]["errorMsg"] = errorMsg;
                emit sessionsChanged();

                if (success) {
                    // 接收成功时通知 UI 打开文件夹
                    if (_config) {
                        emit transferCompleted(sessionId, worker->fileName(),
                                               _config->receivePath());
                    }
                    emit messageOccurred(tr("文件 \"%1\" 接收成功").arg(worker->fileName()));
                } else {
                    emit errorOccurred(tr("接收失败：%1").arg(errorMsg));
                }
                break;
            }
        }

        worker->deleteLater();
    });

    if (_config && _config->autoAcceptFiles()) {
        qDebug() << "[TransferSession] 自动接受接收请求:" << sessionId;
        acceptReceiveSession(sessionId);
        emit messageOccurred(tr("已自动接受 \"%1\"，正在保存").arg(fileName));
    } else {
        // 通知 QML 弹窗确认
        emit receiveRequestReceived(sessionId, senderDeviceId, senderName, fileName,
                                    fileSize, totalFiles, totalBytes);
    }

    qDebug() << "TransferSessionManager: 收到接收请求" << sessionId
             << "来自" << senderName << "文件" << fileName;
}
