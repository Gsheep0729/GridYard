/**
* @file    history_controller.cpp
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   本地历史控制器实现
*
* 持有 ChatManager、TransferSessionManager 和 DatabaseWorker，
* 按设备或游标分页加载聊天历史与传输记录，向 QML 暴露
* 统一的筛选、删除和保留期限设置入口。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.4.0] GY   2026-06-25
* * 新增本地历史视图控制器，接入聊天和传输历史的查询与删除
*/

#include "history_controller.h"

#include "chat_manager.h"
#include "config_manager.h"
#include "database_worker.h"
#include "history_repositories.h"
#include "history_records.h"
#include "transfer_session_manager.h"

#include <QDateTime>
#include <QMetaObject>

// 构造函数
HistoryController::HistoryController(ChatManager *chat, TransferSessionManager *transfer,
                                     ConfigManager *config, DatabaseWorker *worker,
                                     IMessageRepository *messages,
                                     ITransferHistoryRepository *transfers, QObject *parent)
    : QObject(parent)
    , _chat(chat)
    , _transfer(transfer)
    , _config(config)
    , _worker(worker)
    , _messages(messages)
    , _transferHistory(transfers)
{
    if (_config) {
        connect(_config, &ConfigManager::retentionDaysChanged,
                this, &HistoryController::retentionDaysChanged);
    }
}

// 获取当前传输历史列表
QVariantList HistoryController::transfers() const
{
    return _transfers;
}

// 获取历史查询加载状态
bool HistoryController::loading() const
{
    return _loading;
}

// 获取历史保留天数
int HistoryController::retentionDays() const
{
    return _config ? _config->retentionDays() : 0;
}

// 加载指定设备更早的一页聊天记录
void HistoryController::loadMoreMessages(const QString &deviceId)
{
    if (deviceId.isEmpty() || !_worker || !_messages || _loading) {
        return;
    }

    MessageCursor cursor;
    cursor.peerDeviceId = deviceId;
    const QVariantList current = _chat ? _chat->messagesForDevice(deviceId) : QVariantList{};
    if (!current.isEmpty()) {
        // 使用当前首条消息作为游标，避免分页时重复加载已显示记录。
        const QVariantMap oldest = current.first().toMap();
        cursor.beforeSentAt = QDateTime::fromString(oldest.value("sentAt").toString(), Qt::ISODateWithMs);
        cursor.beforeMessageId = oldest.value("messageId").toString();
    }

    setLoading(true);
    _worker->submitLoad([this, deviceId, cursor](SqliteDatabaseProxy &, QString *error) {
        // 聊天历史每次加载 50 条，保持翻页响应速度和内存占用可控。
        const QList<MessageRecord> records = _messages->loadMessages(cursor, 50, error);
        const bool succeeded = error->isEmpty();
        QMetaObject::invokeMethod(this, [this, deviceId, records, succeeded] {
            if (succeeded && _chat) {
                _chat->prependHistoryMessages(deviceId, records);
            }
            if (!succeeded) {
                emit operationFailed(tr("加载聊天历史失败"));
            }
            emit messagesLoaded(deviceId, succeeded && records.size() == 50);
            setLoading(false);
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 按筛选条件查询传输历史
void HistoryController::queryTransfers(const QVariantMap &filter)
{
    if (!_worker || !_transferHistory || _loading) {
        return;
    }

    setLoading(true);
    _worker->submitLoad([this, filter](SqliteDatabaseProxy &, QString *error) {
        TransferQuery query;
        query.peerDeviceId = filter.value("peerDeviceId").toString();
        query.status = filter.value("status").toString();
        const QString before = filter.value("beforeStartedAt").toString();
        if (!before.isEmpty()) {
            query.beforeStartedAt = QDateTime::fromString(before, Qt::ISODateWithMs);
        }
        // 传输历史一次最多取 200 条，避免历史页打开时阻塞主线程回投。
        const QList<TransferRecord> records = _transferHistory->queryTransfers(query, 200, error);
        const bool succeeded = error->isEmpty();
        QMetaObject::invokeMethod(this, [this, records, succeeded] {
            if (succeeded) {
                _transfers.clear();
                for (const TransferRecord &record : records) {
                    _transfers.append(transferToVariant(record));
                }
                emit transfersChanged();
            } else {
                emit operationFailed(tr("查询传输历史失败"));
            }
            setLoading(false);
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 删除单条聊天记录
void HistoryController::deleteMessage(const QString &deviceId, const QString &messageId)
{
    if (deviceId.isEmpty() || messageId.isEmpty() || !_worker || !_messages) {
        return;
    }

    _worker->submitDelete([this, deviceId, messageId](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _messages->deleteMessage(messageId, error);
        QMetaObject::invokeMethod(this, [this, deviceId, messageId, succeeded] {
            if (succeeded && _chat) {
                _chat->removeMessage(deviceId, messageId);
            }
            if (!succeeded) {
                emit operationFailed(tr("删除聊天记录失败"));
            }
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 删除指定设备的整段聊天会话
void HistoryController::deleteConversation(const QString &deviceId)
{
    if (deviceId.isEmpty() || !_worker || !_messages) {
        return;
    }

    _worker->submitDelete([this, deviceId](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _messages->deleteConversation(deviceId, error);
        QMetaObject::invokeMethod(this, [this, deviceId, succeeded] {
            if (succeeded && _chat) {
                _chat->clearMessages(deviceId);
            }
            if (!succeeded) {
                emit operationFailed(tr("清空会话失败"));
            }
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 删除单条传输历史
void HistoryController::deleteTransfer(const QString &recordId)
{
    if (recordId.isEmpty() || !_worker || !_transferHistory) {
        return;
    }

    _worker->submitDelete([this, recordId](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _transferHistory->deleteTransfer(recordId, error);
        QMetaObject::invokeMethod(this, [this, recordId, succeeded] {
            if (succeeded) {
                for (int row = 0; row < _transfers.size(); ++row) {
                    // 本地列表同步删除已持久化删除的记录，避免重新查询整页。
                    if (_transfers.at(row).toMap().value("recordId").toString() == recordId) {
                        _transfers.removeAt(row);
                        emit transfersChanged();
                        break;
                    }
                }
            } else {
                emit operationFailed(tr("删除传输历史失败"));
            }
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 清空全部聊天记录
void HistoryController::clearAllMessages()
{
    if (!_worker || !_messages) {
        return;
    }

    _worker->submitDelete([this](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _messages->clearAllMessages(error);
        QMetaObject::invokeMethod(this, [this, succeeded] {
            if (succeeded && _chat) {
                _chat->clearMessages();
            }
            if (!succeeded) {
                emit operationFailed(tr("清空聊天记录失败"));
            }
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 清空全部传输历史
void HistoryController::clearAllTransfers()
{
    if (!_worker || !_transferHistory) {
        return;
    }

    _worker->submitDelete([this](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _transferHistory->clearAllTransfers(error);
        QMetaObject::invokeMethod(this, [this, succeeded] {
            if (succeeded) {
                _transfers.clear();
                emit transfersChanged();
                if (_transfer) {
                    // 清空历史只移除已结束会话展示项，不删除用户本地文件。
                    _transfer->clearFinishedSessions(false);
                }
            } else {
                emit operationFailed(tr("清空传输历史失败"));
            }
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

// 设置历史保留天数并立即执行一次清理
void HistoryController::setRetentionDays(int days)
{
    if (_config) {
        _config->setRetentionDays(days);
    }
    cleanupExpiredRecords();
}

// 按当前保留期限清理过期历史
void HistoryController::cleanupExpiredRecords()
{
    const int days = retentionDays();
    if (days <= 0 || !_worker || !_messages || !_transferHistory) {
        return;
    }

    // 保留期限以 UTC 计算，避免本地时区变化导致历史边界抖动。
    const QDateTime before = QDateTime::currentDateTimeUtc().addDays(-days);
    _worker->submitDelete([this, before](SqliteDatabaseProxy &, QString *error) {
        return _messages->deleteExpiredMessages(before, error)
               && _transferHistory->deleteExpiredTransfers(before, error);
    });
}

// 设置加载状态并通知 QML
void HistoryController::setLoading(bool value)
{
    if (_loading == value) {
        return;
    }
    _loading = value;
    emit loadingChanged();
}

// 将传输历史记录转换为 QML 可绑定的字段集合
QVariantMap HistoryController::transferToVariant(const TransferRecord &record)
{
    return {{"recordId", record.recordId}, {"peerDeviceId", record.peerDeviceId},
            {"peerName", record.peerName}, {"direction", static_cast<int>(record.direction)},
            {"displayName", record.displayName}, {"isDirectory", record.isDirectory},
            {"fileCount", record.fileCount}, {"totalBytes", record.totalBytes},
            {"status", record.status},
            {"startedAt", record.startedAt.toUTC().toString(Qt::ISODateWithMs)},
            {"finishedAt", record.finishedAt.toUTC().toString(Qt::ISODateWithMs)},
            {"errorCode", record.errorCode}, {"errorMessage", record.errorMessage}};
}
