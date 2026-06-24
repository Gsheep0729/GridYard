/**
* @file    sqlite_device_repository.h
* @version 6.6.2
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 设备目录 Repository 实现
*
* 负责设备快照、最近业务活动时间和最近设备查询；纯发现心跳在代理内部
* 节流，避免 UDP 广播导致频繁磁盘写入。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.1.0] GY 2026-06-25
* * 新增设备目录 SQLite Repository
*/

#pragma once

#include "history_repositories.h"

#include <QHash>

class SqliteDatabaseBroker;

class SqliteDeviceRepository : public IDeviceRepository {
public:
    // 构造设备目录 Repository
    explicit SqliteDeviceRepository(SqliteDatabaseBroker *database);

    // 新增或更新设备目录快照
    virtual bool upsertPeer(const PeerRecord &record, QString *errorMessage) override;
    // 更新设备最近聊天活动时间
    virtual bool markChatActivity(const QString &deviceId, const QDateTime &time, QString *errorMessage) override;
    // 更新设备最近传输活动时间
    virtual bool markTransferActivity(const QString &deviceId, const QDateTime &time, QString *errorMessage) override;
    // 获取最近活跃设备列表
    virtual QList<PeerRecord> recentPeers(int limit, QString *errorMessage) const override;

private:
    SqliteDatabaseBroker *_database = nullptr;  // 数据库连接和事务入口
    QHash<QString, PeerRecord> _recentWrites;  // 最近持久化的发现快照
};
