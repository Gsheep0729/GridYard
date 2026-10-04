/**
* @file    sqlite_device_repository.h
* @version 7.17.3
* @date 2026-10-04
* @author  GridYard Team
* @brief   SQLite 设备目录 Repository 实现
*
* 负责设备快照、最近业务活动时间和最近设备查询；纯发现心跳在代理内部
* 节流，避免 UDP 广播导致频繁磁盘写入。
*
* Change Log:
* [v7.17.3] GY   2026-10-04
* * 新增设备备注别名接口与 Step 变体声明
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 新增置顶、隐藏与删除设备历史接口声明及对应 Step 变体
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
* [v7.15.5] GY   2026-10-03
* * 接口方法内部复用 *Step（ADR-006 方案 c）
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.1.0] GY 2026-06-25
* * 新增设备目录 SQLite Repository
*/

#pragma once

#include "history_repositories.h"

#include <QHash>

#include <functional>

class QSqlDatabase;
class SqliteDatabaseBroker;

class SqliteDeviceRepository : public IDeviceRepository {
public:
    // 只执行 SQL、不自开事务的任务，供 LocalDataBroker 把多表写入组合进单个事务
    using SqlStep = std::function<bool(QSqlDatabase &, QString *)>;

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
    // 设置设备置顶状态（幂等，设备行不存在时同样返回成功）
    virtual bool setDevicePinned(const QString &deviceId, bool pinned, QString *errorMessage) override;
    // 设置设备隐藏状态（幂等，设备行不存在时同样返回成功）
    virtual bool setDeviceHidden(const QString &deviceId, bool hidden, QString *errorMessage) override;
    // 设置设备本地备注别名（空串表示清除；幂等，设备行不存在时同样返回成功）
    virtual bool setDeviceAlias(const QString &deviceId, const QString &alias,
                                QString *errorMessage) override;
    // 删除设备及其聊天与传输历史；不删除已接收的本地文件
    virtual bool deleteDeviceWithHistory(const QString &deviceId, QString *errorMessage) override;

    // 以下 *Step 返回纯 SQL 步骤，由调用方置于同一事务内组合执行（不含节流、不自开事务）
    static SqlStep upsertPeerStep(const PeerRecord &record);
    static SqlStep markChatActivityStep(const QString &deviceId, const QDateTime &time);
    static SqlStep markTransferActivityStep(const QString &deviceId, const QDateTime &time);
    static SqlStep setDevicePinnedStep(const QString &deviceId, bool pinned);
    static SqlStep setDeviceHiddenStep(const QString &deviceId, bool hidden);
    static SqlStep setDeviceAliasStep(const QString &deviceId, const QString &alias);
    static SqlStep deleteDeviceStep(const QString &deviceId);
    // 组合写入成功后刷新节流缓存，供 LocalDataBroker 在事务提交后调用
    void noteWritten(const PeerRecord &record);
    // 设备删除成功后清除节流缓存，再次发现按全新设备重新入目录
    void noteDeviceDeleted(const QString &deviceId);

private:
    SqliteDatabaseBroker *_database = nullptr;  // 数据库连接和事务入口
    QHash<QString, PeerRecord> _recentWrites;  // 最近持久化的发现快照
};
