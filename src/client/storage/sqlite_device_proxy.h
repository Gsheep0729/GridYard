/**
* @file    sqlite_device_proxy.h
* @version 6.1.0
* @date    2026-06-25
* @author  GridYard Team
* @brief   SQLite 设备目录 Repository 实现
*
* 负责设备快照、最近业务活动时间和最近设备查询；纯发现心跳在代理内部
* 节流，避免 UDP 广播导致频繁磁盘写入。
*
* Change Log:
* [v6.1.0] GY 2026-06-25
* * 新增设备目录 SQLite Proxy
*/

#pragma once

#include "history_repositories.h"

#include <QHash>

class SqliteDatabaseProxy;

class SqliteDeviceProxy : public IDeviceRepository {
public:
    explicit SqliteDeviceProxy(SqliteDatabaseProxy *database);

    bool upsertPeer(const PeerRecord &record, QString *errorMessage) override;
    bool markChatActivity(const QString &deviceId, const QDateTime &time,
                          QString *errorMessage) override;
    bool markTransferActivity(const QString &deviceId, const QDateTime &time,
                              QString *errorMessage) override;
    QList<PeerRecord> recentPeers(int limit, QString *errorMessage) const override;

private:
    SqliteDatabaseProxy *_database = nullptr;  // 数据库连接和事务入口
    QHash<QString, PeerRecord> _recentWrites;  // 最近持久化的发现快照
};
