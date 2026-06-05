/**
* @file    transfer_session_manager.cpp
* @date    2026-06-02
* @author  GY
* @brief   TransferSessionManager 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 3：初始版本
* [v0.2] GY   2026-06-03
* * 接收会话连接 transferFinished 信号；设置接收路径；触发完成通知
* [v0.3] GY   2026-06-03
* * Stage 3.10：实现 cancelSession()；保存/清理发送方 worker
* [v0.4] GY   2026-06-04
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
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
#include <QThread>
#include <QUuid>

TransferSessionManager::TransferSessionManager(QObject *parent)
    : QObject{parent}
{
}

TransferSessionManager *TransferSessionManager::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    return new TransferSessionManager{};
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
    // 从 DiscoveryService 获取目标设备信息
    PeerInfo peer = _discovery->peerInfo(deviceId);
    if (peer.deviceId.isEmpty()) {
        emit errorOccurred(tr("目标设备不存在"));
        return;
    }
    if (!peer.isOnline) {
        emit errorOccurred(tr("目标设备 \"%1\" 已离线").arg(peer.deviceName));
        return;
    }

    QString host = peer.ipAddress;
    quint16 port = peer.tcpPort > 0 ? peer.tcpPort : _config->tcpPort();

    // 创建发送会话
    QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QVariantMap session;
    session["sessionId"] = sessionId;
    session["type"]      = "send";
    session["deviceId"]  = deviceId;
    session["filePath"]  = filePath;
    session["status"]    = "connecting";
    session["progress"]  = 0;
    session["bytesTransferred"] = 0;
    session["totalBytes"] = 0;

    _sessions.append(session);
    emit sessionsChanged();

    // 创建 FileSenderWorker 并在工作线程中运行
    auto *worker = new FileSenderWorker{};
    auto *thread = new QThread{this};

    worker->moveToThread(thread);

    // 保存 worker 引用
    _sendWorkers[sessionId] = worker;

    // 连接信号
    connect(thread, &QThread::started, worker, [worker, host, port, filePath]() {
        worker->startTransfer(host, port, filePath);
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

void TransferSessionManager::onTransferRequestReceived(FileReceiverWorker *worker,
                                                        const QString &senderName,
                                                        const QString &fileName,
                                                        qint64 fileSize,
                                                        int totalFiles,
                                                        qint64 totalBytes)
{
    // 创建接收会话
    QString sessionId = worker->sessionId();

    // 设置接收路径
    if (_config) {
        worker->setReceivePath(_config->receivePath());
    }

    QVariantMap session;
    session["sessionId"] = sessionId;
    session["type"]      = "receive";
    session["senderName"] = senderName;
    session["fileName"]  = fileName;
    session["fileSize"]  = fileSize;
    session["totalFiles"] = totalFiles;
    session["totalBytes"] = totalBytes;
    session["status"]    = "waiting_confirm";
    session["progress"]  = 0;
    session["bytesTransferred"] = 0;
    session["worker"]    = QVariant::fromValue(worker);

    _sessions.append(session);
    emit sessionsChanged();

    // 连接接收完成信号
    connect(worker, &FileReceiverWorker::transferFinished,
            this, [this, sessionId, worker](bool success, const QString &errorMsg) {
        for (int i = 0; i < _sessions.size(); ++i) {
            if (_sessions[i]["sessionId"].toString() == sessionId) {
                // 如果已经是 cancelled 状态，不覆盖
                if (_sessions[i]["status"].toString() == "cancelled") {
                    break;
                }

                _sessions[i]["status"] = success ? "completed" : "failed";
                _sessions[i]["progress"] = success ? 100 : _sessions[i]["progress"].toInt();
                _sessions[i]["errorMsg"] = errorMsg;
                emit sessionsChanged();

                // 接收成功时通知 UI 打开文件夹
                if (success && _config) {
                    emit transferCompleted(sessionId, worker->fileName(),
                                           _config->receivePath());
                }
                break;
            }
        }

        worker->deleteLater();
    });

    // 通知 QML 弹窗确认
    emit receiveRequestReceived(sessionId, senderName, fileName,
                                fileSize, totalFiles, totalBytes);

    qDebug() << "TransferSessionManager: 收到接收请求" << sessionId
             << "来自" << senderName << "文件" << fileName;
}
