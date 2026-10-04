/**
* @file    transfer_session_model.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   传输会话列表模型
*
* 为 QML 提供传输会话的稳定角色与增量通知（插入、行级更新、删除），
* 模型只承担列表存储与通知，不负责网络、线程或持久化编排。
* gy::session 命名空间集中定义会话字段与状态值的具名常量，
* 模型、管理器和记录映射共用同一份字段名。
*
* Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 版本头对齐到 v7.19.0
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
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
* * 删除零调用的 takeSessionsWhere
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
* [v7.10.0] GY   2026-10-02
* * 自 TransferSessionManager 拆出会话列表模型，替代 QVariantList 全量重建
*/

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QVariantMap>

class QJSValue;

// 会话字段名与状态值的具名常量，QML 角色名与字段名保持一致
namespace gy::session {

inline constexpr const char *kSessionId         = "sessionId";
inline constexpr const char *kType              = "type";
inline constexpr const char *kDeviceId          = "deviceId";
inline constexpr const char *kPeerDeviceName    = "peerDeviceName";
inline constexpr const char *kFilePath          = "filePath";
inline constexpr const char *kFileName          = "fileName";
inline constexpr const char *kIsDirectory       = "isDirectory";
inline constexpr const char *kFileCount         = "fileCount";
inline constexpr const char *kStatus            = "status";
inline constexpr const char *kProgress          = "progress";
inline constexpr const char *kBytesTransferred  = "bytesTransferred";
inline constexpr const char *kTotalBytes        = "totalBytes";
inline constexpr const char *kCreatedAt         = "createdAt";
inline constexpr const char *kFileList          = "fileList";
inline constexpr const char *kLocalPath         = "localPath";
inline constexpr const char *kCanDeleteLocalFile = "canDeleteLocalFile";
inline constexpr const char *kSenderDeviceId    = "senderDeviceId";
inline constexpr const char *kSenderName        = "senderName";
inline constexpr const char *kErrorMsg          = "errorMsg";
inline constexpr const char *kErrorCode         = "errorCode";
inline constexpr const char *kRecordId          = "recordId";
inline constexpr const char *kRelayId           = "relayId";
inline constexpr const char *kFileSize          = "fileSize";
inline constexpr const char *kTotalFiles        = "totalFiles";

// 会话方向
inline constexpr const char *kTypeSend    = "send";
inline constexpr const char *kTypeReceive = "receive";

// 会话状态
inline constexpr const char *kStatusConnecting     = "connecting";
inline constexpr const char *kStatusWaitingConfirm = "waiting_confirm";
inline constexpr const char *kStatusAwaitingRelay  = "awaiting_relay";
inline constexpr const char *kStatusTransferring   = "transferring";
inline constexpr const char *kStatusCompleted      = "completed";
inline constexpr const char *kStatusFailed         = "failed";
inline constexpr const char *kStatusRejected       = "rejected";
inline constexpr const char *kStatusCancelled      = "cancelled";

}

class TransferSessionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        SessionIdRole = Qt::UserRole + 1,
        TypeRole,
        DeviceIdRole,
        PeerDeviceNameRole,
        FilePathRole,
        FileNameRole,
        IsDirectoryRole,
        FileCountRole,
        StatusRole,
        ProgressRole,
        BytesTransferredRole,
        TotalBytesRole,
        CreatedAtRole,
        FileListRole,
        LocalPathRole,
        CanDeleteLocalFileRole,
        SenderDeviceIdRole,
        SenderNameRole,
        ErrorMsgRole,
        ErrorCodeRole,
        RecordIdRole,
        RelayIdRole,
        FileSizeRole,
        TotalFilesRole,
    };
    Q_ENUM(Role)

    explicit TransferSessionModel(QObject *parent = nullptr);
    virtual ~TransferSessionModel() override = default;

    TransferSessionModel(const TransferSessionModel &)            = delete;
    TransferSessionModel &operator=(const TransferSessionModel &) = delete;

    // 返回模型中的会话数量
    int count() const;
    // 返回模型行数
    virtual int rowCount(const QModelIndex &parent = {}) const override;
    // 返回指定角色的会话字段
    virtual QVariant data(const QModelIndex &index, int role) const override;
    // 返回 QML delegate 使用的角色名称
    virtual QHash<int, QByteArray> roleNames() const override;

    // 追加一条会话并发出精确插入通知，返回插入行号
    int appendSession(const QVariantMap &session);
    // 就地编辑指定会话并发出该行的 dataChanged；会话不存在时返回 false
    bool updateSession(const QString &sessionId, const std::function<void(QVariantMap &)> &editor);
    // 移除指定会话并发出精确删除通知；会话不存在时返回 false
    bool removeSession(const QString &sessionId);
    // 判断会话是否存在
    bool hasSession(const QString &sessionId) const;
    // 返回指定会话的快照；不存在时返回空 map
    QVariantMap sessionById(const QString &sessionId) const;
    // 返回兼容旧调用方的会话快照列表
    QVariantList sessions() const;

signals:
    void countChanged();

private:
    // 返回角色对应会话字段名；未知角色返回空指针
    static const char *keyForRole(int role);

    QList<QVariantMap> _sessions;  // 运行期会话行，字段名即 QML 角色名
};
