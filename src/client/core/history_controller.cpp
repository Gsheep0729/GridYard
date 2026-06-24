/**
* @file    history_controller.cpp
* @version 6.4.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   本地历史控制器实现
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

QVariantList HistoryController::transfers() const { return _transfers; }
bool HistoryController::loading() const { return _loading; }
int HistoryController::retentionDays() const { return _config ? _config->retentionDays() : 0; }

void HistoryController::loadMoreMessages(const QString &deviceId)
{
    if (deviceId.isEmpty() || !_worker || !_messages || _loading) return;
    MessageCursor cursor;
    cursor.peerDeviceId = deviceId;
    const QVariantList current = _chat ? _chat->messagesForDevice(deviceId) : QVariantList{};
    if (!current.isEmpty()) {
        const QVariantMap oldest = current.first().toMap();
        cursor.beforeSentAt = QDateTime::fromString(oldest.value("sentAt").toString(), Qt::ISODateWithMs);
        cursor.beforeMessageId = oldest.value("messageId").toString();
    }
    setLoading(true);
    _worker->submitLoad([this, deviceId, cursor](SqliteDatabaseProxy &, QString *error) {
        const QList<MessageRecord> records = _messages->loadMessages(cursor, 50, error);
        const bool succeeded = error->isEmpty();
        QMetaObject::invokeMethod(this, [this, deviceId, records, succeeded] {
            if (succeeded && _chat) _chat->prependHistoryMessages(deviceId, records);
            if (!succeeded) emit operationFailed(tr("加载聊天历史失败"));
            emit messagesLoaded(deviceId, succeeded && records.size() == 50);
            setLoading(false);
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::queryTransfers(const QVariantMap &filter)
{
    if (!_worker || !_transferHistory || _loading) return;
    setLoading(true);
    _worker->submitLoad([this, filter](SqliteDatabaseProxy &, QString *error) {
        TransferQuery query;
        query.peerDeviceId = filter.value("peerDeviceId").toString();
        query.status = filter.value("status").toString();
        const QString before = filter.value("beforeStartedAt").toString();
        if (!before.isEmpty()) query.beforeStartedAt = QDateTime::fromString(before, Qt::ISODateWithMs);
        const QList<TransferRecord> records = _transferHistory->queryTransfers(query, 200, error);
        const bool succeeded = error->isEmpty();
        QMetaObject::invokeMethod(this, [this, records, succeeded] {
            if (succeeded) {
                _transfers.clear();
                for (const TransferRecord &record : records) _transfers.append(transferToVariant(record));
                emit transfersChanged();
            } else {
                emit operationFailed(tr("查询传输历史失败"));
            }
            setLoading(false);
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::deleteMessage(const QString &deviceId, const QString &messageId)
{
    if (deviceId.isEmpty() || messageId.isEmpty() || !_worker || !_messages) return;
    _worker->submitDelete([this, deviceId, messageId](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _messages->deleteMessage(messageId, error);
        QMetaObject::invokeMethod(this, [this, deviceId, messageId, succeeded] {
            if (succeeded && _chat) _chat->removeMessage(deviceId, messageId);
            if (!succeeded) emit operationFailed(tr("删除聊天记录失败"));
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::deleteConversation(const QString &deviceId)
{
    if (deviceId.isEmpty() || !_worker || !_messages) return;
    _worker->submitDelete([this, deviceId](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _messages->deleteConversation(deviceId, error);
        QMetaObject::invokeMethod(this, [this, deviceId, succeeded] {
            if (succeeded && _chat) _chat->clearMessages(deviceId);
            if (!succeeded) emit operationFailed(tr("清空会话失败"));
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::deleteTransfer(const QString &recordId)
{
    if (recordId.isEmpty() || !_worker || !_transferHistory) return;
    _worker->submitDelete([this, recordId](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _transferHistory->deleteTransfer(recordId, error);
        QMetaObject::invokeMethod(this, [this, recordId, succeeded] {
            if (succeeded) {
                for (int row = 0; row < _transfers.size(); ++row) {
                    if (_transfers.at(row).toMap().value("recordId").toString() == recordId) {
                        _transfers.removeAt(row);
                        emit transfersChanged();
                        break;
                    }
                }
            } else emit operationFailed(tr("删除传输历史失败"));
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::clearAllMessages()
{
    if (!_worker || !_messages) return;
    _worker->submitDelete([this](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _messages->clearAllMessages(error);
        QMetaObject::invokeMethod(this, [this, succeeded] {
            if (succeeded && _chat) _chat->clearMessages();
            if (!succeeded) emit operationFailed(tr("清空聊天记录失败"));
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::clearAllTransfers()
{
    if (!_worker || !_transferHistory) return;
    _worker->submitDelete([this](SqliteDatabaseProxy &, QString *error) {
        const bool succeeded = _transferHistory->clearAllTransfers(error);
        QMetaObject::invokeMethod(this, [this, succeeded] {
            if (succeeded) {
                _transfers.clear();
                emit transfersChanged();
                if (_transfer) _transfer->clearFinishedSessions(false);
            } else emit operationFailed(tr("清空传输历史失败"));
        }, Qt::QueuedConnection);
        return succeeded;
    });
}

void HistoryController::setRetentionDays(int days)
{
    if (_config) _config->setRetentionDays(days);
    cleanupExpiredRecords();
}

void HistoryController::cleanupExpiredRecords()
{
    const int days = retentionDays();
    if (days <= 0 || !_worker || !_messages || !_transferHistory) return;
    const QDateTime before = QDateTime::currentDateTimeUtc().addDays(-days);
    _worker->submitDelete([this, before](SqliteDatabaseProxy &, QString *error) {
        return _messages->deleteExpiredMessages(before, error)
               && _transferHistory->deleteExpiredTransfers(before, error);
    });
}

void HistoryController::setLoading(bool value)
{
    if (_loading == value) return;
    _loading = value;
    emit loadingChanged();
}

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
