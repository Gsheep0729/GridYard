/**
* @file    history_controller.h
* @version 6.4.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   本地聊天与传输历史的 QML 应用层入口
*/

#pragma once

#include <QObject>
#include <QVariantList>

#include "history_records.h"

class ChatManager;
class ConfigManager;
class DatabaseWorker;
class IMessageRepository;
class ITransferHistoryRepository;
class TransferSessionManager;

class HistoryController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList transfers READ transfers NOTIFY transfersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int retentionDays READ retentionDays NOTIFY retentionDaysChanged)

public:
    explicit HistoryController(ChatManager *chat, TransferSessionManager *transfer,
                               ConfigManager *config, DatabaseWorker *worker,
                               IMessageRepository *messages,
                               ITransferHistoryRepository *transfers,
                               QObject *parent = nullptr);

    QVariantList transfers() const;
    bool loading() const;
    int retentionDays() const;

    Q_INVOKABLE void loadMoreMessages(const QString &deviceId);
    Q_INVOKABLE void queryTransfers(const QVariantMap &filter = {});
    Q_INVOKABLE void deleteMessage(const QString &deviceId, const QString &messageId);
    Q_INVOKABLE void deleteConversation(const QString &deviceId);
    Q_INVOKABLE void deleteTransfer(const QString &recordId);
    Q_INVOKABLE void clearAllMessages();
    Q_INVOKABLE void clearAllTransfers();
    Q_INVOKABLE void setRetentionDays(int days);
    void cleanupExpiredRecords();

signals:
    void transfersChanged();
    void loadingChanged();
    void messagesLoaded(const QString &deviceId, bool hasMore);
    void operationFailed(const QString &message);
    void retentionDaysChanged();

private:
    void setLoading(bool value);
    static QVariantMap transferToVariant(const TransferRecord &record);

    ChatManager *_chat = nullptr;
    TransferSessionManager *_transfer = nullptr;
    ConfigManager *_config = nullptr;
    DatabaseWorker *_worker = nullptr;
    IMessageRepository *_messages = nullptr;
    ITransferHistoryRepository *_transferHistory = nullptr;
    QVariantList _transfers;
    bool _loading = false;
};
