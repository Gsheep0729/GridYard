/**
* @file    history_repositories.h
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   本地历史持久化端口
*
* 应用层通过这些接口使用历史数据，不依赖 SQLite 实现细节。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增三个 Repository 接口
*/

#pragma once

#include "history_records.h"

#include <QList>

class IDeviceRepository {
public:
    // 析构函数
    virtual ~IDeviceRepository() = default;

    // 新增或更新设备目录快照
    virtual bool upsertPeer(const PeerRecord &record, QString *errorMessage) = 0;
    // 更新设备最近聊天活动时间
    virtual bool markChatActivity(const QString &deviceId, const QDateTime &time,
                                  QString *errorMessage) = 0;
    // 更新设备最近传输活动时间
    virtual bool markTransferActivity(const QString &deviceId, const QDateTime &time,
                                      QString *errorMessage) = 0;
    // 获取按最近活动排序的设备目录
    virtual QList<PeerRecord> recentPeers(int limit, QString *errorMessage) const = 0;
};

class IMessageRepository {
public:
    // 析构函数
    virtual ~IMessageRepository() = default;

    // 按消息标识幂等保存聊天记录
    virtual bool saveMessage(const MessageRecord &record, QString *errorMessage) = 0;
    // 按游标加载一页聊天记录
    virtual QList<MessageRecord> loadMessages(const MessageCursor &cursor, int limit,
                                              QString *errorMessage) const = 0;
    // 删除单条聊天消息
    virtual bool deleteMessage(const QString &messageId, QString *errorMessage) = 0;
    // 删除一个会话的所有聊天记录
    virtual bool deleteConversation(const QString &deviceId, QString *errorMessage) = 0;
    // 删除早于指定时间的聊天记录
    virtual bool deleteExpiredMessages(const QDateTime &before, QString *errorMessage) = 0;
    // 清空全部聊天消息
    virtual bool clearAllMessages(QString *errorMessage) = 0;
};

class ITransferHistoryRepository {
public:
    // 析构函数
    virtual ~ITransferHistoryRepository() = default;

    // 保存结束态传输记录
    virtual bool upsertFinishedTransfer(const TransferRecord &record, QString *errorMessage) = 0;
    // 按筛选条件查询传输历史
    virtual QList<TransferRecord> queryTransfers(const TransferQuery &query, int limit,
                                                 QString *errorMessage) const = 0;
    // 删除单条传输历史
    virtual bool deleteTransfer(const QString &recordId, QString *errorMessage) = 0;
    // 删除早于指定时间的传输历史
    virtual bool deleteExpiredTransfers(const QDateTime &before, QString *errorMessage) = 0;
    // 清空全部传输历史
    virtual bool clearAllTransfers(QString *errorMessage) = 0;
};
