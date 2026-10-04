/**
* @file    transfer_session_manager.h
* @version 7.17.1
* @date    2026-10-04
* @author  GridYard Team
* @brief   传输会话管理器
*
* 编排传输会话的建立、确认、取消与终结，并将运行期会话行委托给
* TransferSessionModel 存储与增量通知，记录映射委托给 TransferSessionMapper。
* 发送与接收 worker 分别以独立映射管理生命周期。
*
* Change Log:
* [v7.17.1] GY   2026-10-04
* * 新增 incomingTransferRequested 入站请求入口信号
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 新增 activeSessionCount 只读属性：当前活动（未终态）会话数量，
*   创建与终态迁移时发 NOTIFY，供退出前警示提示
* [v7.15.17] GY   2026-10-04
* * 新增 waitingConfirmReceiveSessions 快照：按到达序返回全部等待确认的接收会话，
*   供确认弹窗串行队列化
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
* [v7.15.1] GY   2026-10-03
* * 新增 sessionStale 信号：waiting_confirm 会话被后端终结时通知弹窗关闭
* [v7.15.0] GY   2026-10-03
* * relayModeRequested 更名 relayConfirmRequested：三档策略在 C++ 分流，
*   仅 AskBeforeRelay 档向 QML 请求弹窗确认
* [v7.10.0] GY   2026-10-02
* * 拆分 God Object：会话存储与通知移入 TransferSessionModel，
*   记录映射与文件清理策略移入 TransferSessionMapper，
*   接收 worker 改为独立映射管理（根治会话行内悬空指针），
*   魔法字段名收敛为 gy::session 具名常量
* [v7.9.0] GY   2026-07-26
* * Relay 降级链路落地：直连候选轮询失败后按策略进入 awaiting_relay，
*   经协调服务器邀请建立中继传输（retryViaRelay）
* [v7.8.0] GY   2026-07-21
* * 新增 Relay 降级策略支持（询问后中继 / 自动中继 / 从不中继）
* [v6.6.2] GY   2026-06-25
* * 移除 QML 属性和 Q_INVOKABLE 标记，QML 通过 TransferController 访问
* [v6.6.2] GY   2026-06-25
* * 支持按当前设备清空已结束传输记录
* [v6.3.0] GY   2026-06-25
* * 发射结束态传输快照并支持启动恢复历史记录
* [v4.16.1] FengChunlin   2026-06-21
* * 接收请求和完成结果改为跨线程值传递，删除 Worker 状态读取函数
* [v4.14.0] GY   2026-06-15
* * 支持清理传输记录并删除已接收的本地文件
* [v4.13.2] DuRuoxian   2026-06-15
* * 接收确认信号增加文件夹标记和根目录预览
* [v4.11.0] GY   2026-06-13
* * 支持按配置自动接受并保存接收文件
* [v4.10.0] GY   2026-06-13
* * 新增 removeSession() 方法
* [v4.8.3] FengChunlin   2026-06-10
* * 文件传输使用发送方设备别名
* [v4.7.1] GY   2026-06-07
* * 移除 QML_SINGLETON，改为通过 AppController 暴露
* [v4.3.4] GY   2026-05-27
* * Stage 4.3：信号签名添加 totalFiles/totalBytes 参数
* [v0.3.1] GY   2026-05-21
* * Stage 3.10：实现 cancelSession()；保存发送方 worker 引用
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 transferCompleted 信号；接收完成通知
* [v0.2.0] GY   2026-05-08
* * Stage 3：初始版本
*/

#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "history_records.h"
#include "file_receiver_worker.h"

class QQmlEngine;
class QJSEngine;
class ConfigManager;
class DiscoveryService;
class FileSenderWorker;
class P2pServer;
class QTimer;
class RendezvousClient;
class TransferSessionModel;

class TransferSessionManager : public QObject {
private:
    Q_OBJECT
    Q_PROPERTY(int activeSessionCount READ activeSessionCount NOTIFY activeSessionCountChanged)

public:
    // 判断会话状态是否已结束（完成、失败、拒绝、取消），供控制器层复用同一口径
    static bool isFinishedStatus(const QString &status);
    // 返回会话快照列表（兼容 QML 拉取式消费与测试）
    QVariantList sessions() const;
    // 返回当前全部等待确认的接收会话快照（按到达序），供确认弹窗串行展示
    QVariantList waitingConfirmReceiveSessions() const;
    // 返回当前活动（未终态）会话数量
    int activeSessionCount() const;
    // 返回承载会话行的增量通知模型（所有权归本管理器）
    TransferSessionModel *sessionModel() const;

    // 初始化（由 AppController 调用；协调客户端用于中继降级信令，可为空）
    void init(ConfigManager *config, DiscoveryService *discovery, P2pServer *p2pServer,
              RendezvousClient *rendezvousClient = nullptr);

    // 传输会话命令
    void createSendSession(const QString &deviceId, const QString &filePath);
    void acceptReceiveSession(const QString &sessionId);
    void rejectReceiveSession(const QString &sessionId);
    void cancelSession(const QString &sessionId);
    void removeSession(const QString &sessionId);
    // 删除本地文件只允许接收成功记录，避免误删发送源文件
    void removeSessionAndDeleteFile(const QString &sessionId);
    void clearFinishedSessions(bool deleteReceivedFiles = false,
                               const QString &deviceId = {});
    // 启动阶段恢复已结束的历史记录
    void restoreFinishedTransfers(const QList<TransferRecord> &records);
    // 用户确认后经中继通道重新建立发送（直连失败降级入口）
    void retryViaRelay(const QString &sessionId);

signals:
    void sessionsChanged();
    // 入站传输请求的处理入口通知（含自动接受路径），隐藏设备的自动恢复显示据此触发
    void incomingTransferRequested(const QString &senderDeviceId);
    // 活动会话数量变化（会话创建或迁移到终态），退出前警示据此刷新
    void activeSessionCountChanged();
    // 直连失败后的中继确认请求（AskBeforeRelay 档触发，QML 只负责弹窗）
    void relayConfirmRequested(const QString &sessionId, const QString &deviceId);
    // 新的接收请求（需要弹窗确认）
    void receiveRequestReceived(const QString &sessionId,
                                const QString &senderDeviceId,
                                const QString &senderName,
                                const QString &fileName,
                                qint64 fileSize,
                                int totalFiles,
                                qint64 totalBytes,
                                bool isDirectory,
                                const QVariantList &fileList);
    // 等待确认的接收会话在用户操作前被终结（对方取消/超时/断连），弹窗应关闭
    void sessionStale(const QString &sessionId);
    // 传输完成通知（接收方用于提示打开文件夹）
    void transferCompleted(const QString &sessionId,
                           const QString &fileName,
                           const QString &filePath);
    // 错误提示（显示给用户）
    void errorOccurred(const QString &message);
    // 成功提示（显示给用户）
    void messageOccurred(const QString &message);
    // 最终状态快照，供应用层异步持久化
    void transferToPersist(const TransferRecord &record);
    // 用户移除历史记录后同步删除持久化行
    void transferHistoryDeleteRequested(const QStringList &recordIds);

private slots:
    // 处理新的传输请求
    void onTransferRequestReceived(FileReceiverWorker *worker, const QVariantMap &request);

public:
    explicit TransferSessionManager(QObject *parent = nullptr);
    TransferSessionManager(const TransferSessionManager &)            = delete;
    TransferSessionManager &operator=(const TransferSessionManager &) = delete;

private:
    // 将运行期会话收敛为可持久化的最终快照
    void finalizeSession(const QString &sessionId, const QString &finalStatus,
                         gy::protocol::ErrorCode errorCode, const QString &errorMessage,
                         const QString &savedPath = {});
    // 创建发送 worker 并在工作线程中运行；allowRelayFallback 标记直连失败后可降级
    void startSendWorker(const QVariantMap &session,
                         const QList<QPair<QString, quint16>> &endpoints,
                         const QString &relayId, bool allowRelayFallback);
    // 直连失败后进入等待中继决策状态，并启动决策超时保护
    void enterAwaitingRelay(const QString &sessionId);
    // 判断当前是否具备中继降级条件：策略允许且协调服务器在线
    bool relayDegradationAvailable() const;
    // 中继邀请已被协调服务器受理，建立中继发送
    void onRelayInviteAck(const QString &relayId);
    // 中继邀请未在期限内得到协调服务器受理
    void onRelayInviteTimeout(const QString &relayId);
    // 清理会话关联的中继决策与邀请等待状态
    void clearRelayPendingState(const QString &sessionId);
    // 重新统计活动会话数，数量变化时发 NOTIFY
    void refreshActiveSessionCount();

    ConfigManager    *_config    = nullptr; // 本机配置和接收路径来源
    DiscoveryService *_discovery = nullptr; // 在线设备与发送端点查询服务
    P2pServer        *_p2pServer = nullptr; // 入站传输请求来源
    RendezvousClient *_rendezvous = nullptr; // 协调节点客户端（中继降级信令）
    TransferSessionModel *_model = nullptr; // 会话行存储与 QML 增量通知

    QHash<QString, FileSenderWorker*> _sendWorkers;      // 发送方 worker 映射（sessionId -> worker）
    QHash<QString, FileReceiverWorker*> _receiveWorkers; // 接收方 worker 映射（sessionId -> worker）
    QHash<QString, QTimer*> _relayDecisionTimers;        // awaiting_relay 决策超时（sessionId -> timer）
    QHash<QString, QString> _pendingRelayInvites;        // 等待受理的中继邀请（relayId -> sessionId）
    QHash<QString, QTimer*> _relayInviteTimers;          // 中继邀请受理超时（relayId -> timer）
    int _activeSessionCount = 0;                          // 最近一次统计的活动会话数，变化时发 NOTIFY
};
