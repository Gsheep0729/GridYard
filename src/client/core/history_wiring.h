/**
* @file    history_wiring.h
* @version 7.16.0
* @date    2026-10-04
* @author  GridYard Team
* @brief   本地历史持久化装配
*
* 收拢本地数据层与业务对象之间的持久化接线：发现设备、聊天消息、
* 传输记录三路落库投递，启动时恢复最近历史，以及按保留期周期清理。
* 只负责装配与投递，不关心存储实现。
*
* Change Log:
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 透传本地历史库重建标志与备份路径供界面展示
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
* [v7.11.0] GY   2026-10-02
* * 自 AppController 拆出历史持久化装配与保留期清理
*/

#pragma once

#include <QObject>
#include <QString>

class QTimer;
class ChatManager;
class DiscoveryService;
class HistoryController;
class LocalDataBroker;
class PeerDiscoveryViewModel;
class TransferSessionManager;

class HistoryWiring : public QObject {
    Q_OBJECT

public:
    explicit HistoryWiring(DiscoveryService *discovery, ChatManager *chat,
                           TransferSessionManager *transfer,
                           PeerDiscoveryViewModel *peerDiscoveryViewModel,
                           HistoryController *history, LocalDataBroker *dataBroker,
                           QObject *parent = nullptr);

    // 打开本地历史库、完成持久化接线、恢复启动数据并启动保留期清理；
    // 返回历史库是否可用（失败时保留在线收发能力）
    bool initialize();

    // 本次启动是否因库损坏重建了本地历史库（透传自 LocalDataBroker）
    bool historyDatabaseRebuilt() const;
    // 重建前损坏库的备份路径（未重建时为空）
    QString rebuiltBackupPath() const;

signals:
    // 存储任务失败，未写入本地历史（由组合根转发给 QML）
    void operationFailed();

private:
    // 连接发现、聊天、传输三路持久化信号并恢复启动数据
    void connectPersistence();
    // 在存储线程读取最近聊天记录并回投到主线程恢复模型
    void loadRecentChatHistories();
    // 在存储线程读取最近传输历史并回投到主线程恢复模型
    void loadRecentTransferHistories();

    DiscoveryService       *_discovery   = nullptr; // 查询对端端点补全落库数据
    ChatManager            *_chat        = nullptr; // 聊天消息持久化来源
    TransferSessionManager *_transfer    = nullptr; // 传输记录持久化来源
    PeerDiscoveryViewModel *_peerDiscoveryViewModel = nullptr; // 启动时加载本地设备目录
    HistoryController      *_history     = nullptr; // 过期历史清理入口
    LocalDataBroker        *_dataBroker  = nullptr; // 本地数据层代管者
    QTimer                 *_retentionTimer = nullptr;  // 周期性过期历史清理定时器
};
