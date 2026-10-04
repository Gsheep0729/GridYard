/**
* @file    history_controller.cpp
* @version 7.18.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   本地历史控制器实现
*
* 持有 ChatManager、TransferSessionManager 和 LocalDataBroker，
* 按设备或游标分页加载聊天历史与传输记录，向 QML 暴露
* 统一的筛选、删除和保留期限设置入口。
*
* Change Log:
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 新增传输历史翻页 loadMoreTransfers 与 hasMoreTransfers 属性
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
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

// 获取传输历史是否还有更早一页
bool HistoryController::hasMoreTransfers() const
{
    return _hasMoreTransfers;
}

// 设置传输历史是否还有下一页并通知 QML
void HistoryController::setHasMoreTransfers(bool value)
{
    if (_hasMoreTransfers == value) {
        return;
    }
    _hasMoreTransfers = value;
    emit hasMoreTransfersChanged();
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
    _dataBroker->loadMessages(
        this, cursor, kChatPageSize,
        [this, deviceId](const QList<MessageRecord> &records, bool succeeded) {
            if (succeeded && _chat) {
                _chat->prependHistoryMessages(deviceId, records);  // 在模型头部追加更早消息
            }
            if (!succeeded) {
                emit operationFailed(tr("加载聊天历史失败"));
            }
            // 返回数量不足一页时说明已无更多历史，通知 QML 隐藏"加载更多"按钮
            emit messagesLoaded(deviceId, succeeded && records.size() == kChatPageSize);
            setLoading(false);  // 重置加载状态
        });
}

// 按筛选条件查询传输历史第一页
void HistoryController::queryTransfers(const QVariantMap &filter)
{
    if (!_dataBroker || _loading) {
        return;
    }

    setLoading(true);
    TransferQuery query;
    query.peerDeviceId = filter.value("peerDeviceId").toString();  // 可选：按设备筛选
    query.status = filter.value("status").toString();  // 可选：按状态筛选（completed/failed/cancelled）

    _dataBroker->queryTransfers(
        this, query, kTransferPageSize,
        [this](const QList<TransferRecord> &records, bool succeeded) {
            if (succeeded) {
                _transfers.clear();
                for (const TransferRecord &record : records) {
                    _transfers.append(transferToVariant(record));  // 逐条转换为 QML 可绑定字段
                }
                emit transfersChanged();
                // 取满一页才认为可能还有更早记录，不足一页即已到底
                setHasMoreTransfers(records.size() == kTransferPageSize);
            } else {
                emit operationFailed(tr("查询传输历史失败"));
            }
            setLoading(false);
        });
}

// 翻页加载更早的传输历史：游标取当前列表最后一条，新记录追加到尾部
void HistoryController::loadMoreTransfers(const QVariantMap &filter)
{
    if (!_dataBroker || _loading || !_hasMoreTransfers || _transfers.isEmpty()) {
        return;
    }

    // 次键游标取本页最后一条的记录 ID，与排序键 (started_at, record_id) 对齐避免同毫秒翻页漏重
    const QVariantMap pageEnd = _transfers.last().toMap();
    TransferQuery query;
    query.peerDeviceId = filter.value("peerDeviceId").toString();
    query.status = filter.value("status").toString();
    query.beforeStartedAt = QDateTime::fromString(pageEnd.value("startedAt").toString(),
                                                  Qt::ISODateWithMs);
    query.beforeRecordId = pageEnd.value("recordId").toString();

    setLoading(true);
    _dataBroker->queryTransfers(
        this, query, kTransferPageSize,
        [this](const QList<TransferRecord> &records, bool succeeded) {
            if (succeeded) {
                for (const TransferRecord &record : records) {
                    _transfers.append(transferToVariant(record));  // 追加到列表尾部而非替换
                }
                emit transfersChanged();
                setHasMoreTransfers(records.size() == kTransferPageSize);
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
                setHasMoreTransfers(false);  // 列表已清空，翻页状态一并复位
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
