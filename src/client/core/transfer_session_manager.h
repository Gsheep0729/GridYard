/**
* @file    transfer_session_manager.h
* @version 7.25.0
* @date 2026-10-08
* @author  GridYard Team
* @brief   传输会话管理器
*
* 编排传输会话的建立、确认、取消与终结，并将运行期会话行委托给
* TransferSessionModel 存储与增量通知，记录映射委托给 TransferSessionMapper。
* 发送与接收 worker 分别以独立映射管理生命周期。
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
    // 多选群发：对每台目标各建一个独立 1:1 发送会话，同一份内容的序列化与
    // SHA-256 只计算一次，N 个发送 worker 复用同一份文件清单
    void createMultiSendSessions(const QStringList &deviceIds, const QString &filePath);
    // 返回上次多选群发勾选的目标集合（QSettings 持久化，多选弹窗默认勾选）
    Q_INVOKABLE QStringList lastMultiTargets() const;
    // 记录本次多选群发的目标集合
    Q_INVOKABLE void saveMultiTargets(const QStringList &deviceIds);
    // 返回上次群发的内容路径（多选弹窗预填，记住上次选择的一部分）
    Q_INVOKABLE QString lastMultiPath() const;
    // 记录本次群发的内容路径
    Q_INVOKABLE void saveMultiPath(const QString &filePath);
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
    // 群发序列化完成：各目标会话已建立，携带台数供界面提示
    void multiSendStarted(int targetCount);

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
    // 序列化完成后逐台建立群发会话（UI 线程回调）
    void finishMultiSendStart(const QList<gy::FileItem> &fileList);
    void startSendWorker(const QVariantMap &session,
                         const QList<QPair<QString, quint16>> &endpoints,
                         const QString &relayId, bool allowRelayFallback,
                         const QList<gy::FileItem> &sharedFileList = {});
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

    // 多选群发的暂存上下文：序列化在后台线程完成后由 UI 线程消费
    struct MultiTarget
    {
        QString deviceId;  // 目标设备 ID
        QString peerName;  // 目标设备名快照
        QList<QPair<QString, quint16>> endpoints;  // 候选端点（直连优先）
    };
    QList<MultiTarget> _multiTargets;  // 待建会话的目标清单
    QString _multiFilePath;  // 群发源路径
    bool _multiIsDirectory = false;  // 源是否为目录
    QString _multiRootName;  // 源名称
    int _multiFileCount = 0;  // 文件总数
    qint64 _multiTotalBytes = 0;  // 内容总字节数
    int _multiOfflineCount = 0;  // 已剔除的离线目标数
    QHash<QString, FileSenderWorker*> _sendWorkers;      // 发送方 worker 映射（sessionId -> worker）
    QHash<QString, FileReceiverWorker*> _receiveWorkers; // 接收方 worker 映射（sessionId -> worker）
    QHash<QString, QTimer*> _relayDecisionTimers;        // awaiting_relay 决策超时（sessionId -> timer）
    QHash<QString, QString> _pendingRelayInvites;        // 等待受理的中继邀请（relayId -> sessionId）
    QHash<QString, QTimer*> _relayInviteTimers;          // 中继邀请受理超时（relayId -> timer）
    int _activeSessionCount = 0;                          // 最近一次统计的活动会话数，变化时发 NOTIFY
};
