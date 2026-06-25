/**
* @file    history_controller.cpp
* @version 6.6.2
* @date    2026-06-28
* @author  GridYard Team
* @brief   本地历史控制器实现
*
* 持有 ChatManager、TransferSessionManager 和 LocalDataBroker，
* 按设备或游标分页加载聊天历史与传输记录，向 QML 暴露
* 统一的筛选、删除和保留期限设置入口。
*
* Change Log:
* [v6.6.2] GY   2026-06-28
* * 历史查询、删除和清理统一委托 LocalDataBroker，收紧数据层封装
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.4.0] GY   2026-06-25
* * 新增本地历史视图控制器，接入聊天和传输历史的查询与删除
*/

#include "history_controller.h"

#include "chat_manager.h"
#include "config_manager.h"
#include "history_records.h"
#include "local_data_broker.h"
#include "transfer_session_manager.h"

#include <QDateTime>

// 构造函数
HistoryController::HistoryController(ChatManager *chat, TransferSessionManager *transfer,
                                     ConfigManager *config, LocalDataBroker *dataBroker,
                                     QObject *parent)
    : QObject(parent)
    , _chat(chat)
    , _transfer(transfer)
    , _config(config)
    , _dataBroker(dataBroker)
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
    if (deviceId.isEmpty() || !_dataBroker || _loading) {
        return;
    }

    MessageCursor cursor;
    cursor.peerDeviceId = deviceId;
    // 读取当前内存中已有的消息，取首条作为分页游标
    const QVariantList current = _chat ? _chat->messagesForDevice(deviceId) : QVariantList{};
    if (!current.isEmpty()) {
        const QVariantMap oldest = current.first().toMap();
        // 使用当前首条消息的时间戳和 ID 作为游标，避免分页时重复加载已显示记录。
        cursor.beforeSentAt = QDateTime::fromString(oldest.value("sentAt").toString(), Qt::ISODateWithMs);
        cursor.beforeMessageId = oldest.value("messageId").toString();
    }

    setLoading(true);
    // 聊天历史每次加载 50 条，保持翻页响应速度和内存占用可控。
    _dataBroker->loadMessages(
        this, cursor, 50,  // 每页 50 条
        [this, deviceId](const QList<MessageRecord> &records, bool succeeded) {
            if (succeeded && _chat) {
                _chat->prependHistoryMessages(deviceId, records);  // 在模型头部追加更早消息
            }
            if (!succeeded) {
                emit operationFailed(tr("加载聊天历史失败"));
            }
            // 当返回数量不足 50 条时说明已无更多历史，通知 QML 隐藏"加载更多"按钮
            emit messagesLoaded(deviceId, succeeded && records.size() == 50);
            setLoading(false);  // 重置加载状态
        });
}

// 按筛选条件查询传输历史
void HistoryController::queryTransfers(const QVariantMap &filter)
{
    if (!_dataBroker || _loading) {
        return;
    }

    setLoading(true);
    TransferQuery query;
    query.peerDeviceId = filter.value("peerDeviceId").toString();  // 可选：按设备筛选
    query.status = filter.value("status").toString();  // 可选：按状态筛选（completed/failed/cancelled）
    const QString before = filter.value("beforeStartedAt").toString();
    if (!before.isEmpty()) {
        query.beforeStartedAt = QDateTime::fromString(before, Qt::ISODateWithMs);  // 可选：时间游标分页
    }

    // 传输历史一次最多取 200 条，避免历史页打开时阻塞主线程回投。
    _dataBroker->queryTransfers(
        this, query, 200,  // 单次最多取 200 条
        [this](const QList<TransferRecord> &records, bool succeeded) {
            if (succeeded) {
                _transfers.clear();
                for (const TransferRecord &record : records) {
                    _transfers.append(transferToVariant(record));  // 逐条转换为 QML 可绑定字段
                }
                emit transfersChanged();
            } else {
                emit operationFailed(tr("查询传输历史失败"));
            }
            setLoading(false);
        });
}

// 删除单条聊天记录
void HistoryController::deleteMessage(const QString &deviceId, const QString &messageId)
{
    if (deviceId.isEmpty() || messageId.isEmpty() || !_dataBroker) {
        return;
    }

    _dataBroker->deleteMessage(
        this, messageId,
        [this, deviceId, messageId](bool succeeded) {
            if (succeeded && _chat) {
                _chat->removeMessage(deviceId, messageId);  // 同步从内存模型移除已删除消息
            }
            if (!succeeded) {
                emit operationFailed(tr("删除聊天记录失败"));
            }
        });
}

// 删除指定设备的整段聊天会话
void HistoryController::deleteConversation(const QString &deviceId)
{
    if (deviceId.isEmpty() || !_dataBroker) {
        return;
    }

    _dataBroker->deleteConversation(
        this, deviceId,
        [this, deviceId](bool succeeded) {
            if (succeeded && _chat) {
                _chat->clearMessages(deviceId);  // 清空该设备的全部运行期消息
            }
            if (!succeeded) {
                emit operationFailed(tr("清空会话失败"));
            }
        });
}

// 删除单条传输历史
void HistoryController::deleteTransfer(const QString &recordId)
{
    if (recordId.isEmpty() || !_dataBroker) {
        return;
    }

    _dataBroker->deleteTransfer(
        this, recordId,
        [this, recordId](bool succeeded) {
            if (succeeded) {
                for (int row = 0; row < _transfers.size(); ++row) {
                    if (_transfers.at(row).toMap().value("recordId").toString() == recordId) {
                        _transfers.removeAt(row);  // 本地列表同步移除已持久化删除的记录，避免重新查询整页
                        emit transfersChanged();
                        break;
                    }
                }
            } else {
                emit operationFailed(tr("删除传输历史失败"));
            }
        });
}

// 清空全部聊天记录
void HistoryController::clearAllMessages()
{
    if (!_dataBroker) {
        return;
    }

    _dataBroker->clearAllMessages(
        this,
        [this](bool succeeded) {
            if (succeeded && _chat) {
                _chat->clearMessages();
            }
            if (!succeeded) {
                emit operationFailed(tr("清空聊天记录失败"));
            }
        });
}

// 清空全部传输历史
void HistoryController::clearAllTransfers()
{
    if (!_dataBroker) {
        return;
    }

    _dataBroker->clearAllTransfers(
        this,
        [this](bool succeeded) {
            if (succeeded) {
                _transfers.clear();
                emit transfersChanged();
                if (_transfer) {
                    // 仅移除已结束会话展示项，不删除用户本地文件，操作比 removeSessionAndDeleteFile 更保守
                    _transfer->clearFinishedSessions(false);
                }
            } else {
                emit operationFailed(tr("清空传输历史失败"));
            }
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
    if (days <= 0 || !_dataBroker) {
        return;
    }

    // 保留期限以 UTC 计算，避免本地时区变化导致历史边界抖动。
    const QDateTime before = QDateTime::currentDateTimeUtc().addDays(-days);
    _dataBroker->deleteExpiredRecords(before);
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
