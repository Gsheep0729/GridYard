/**
* @file    conversation_timeline_model.h
* @version 7.15.19
* @date    2026-10-04
* @author  GridYard Team
* @brief   聊天消息与传输会话的统一时间线模型
*
* 订阅当前设备的聊天消息模型和全局传输会话模型，把两类行按时间戳
* 合并为单一展示列表，并把来源模型的行级变更换算成自己的增量通知，
* 避免 QML 在每次进度或消息变化时全量重建时间线。
*
* Change Log:
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
* [v7.13.0] GY   2026-10-02
* * 新增统一时间线合并模型，替代 QML 侧 JS 数组全量重建
*/

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class ChatController;
class ChatMessageModel;
class TransferSessionModel;

class ConversationTimelineModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY deviceIdChanged)
    Q_PROPERTY(QObject *chatController READ chatController WRITE setChatController CONSTANT)
    Q_PROPERTY(QObject *transferSessions READ transferSessions WRITE setTransferSessions CONSTANT)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        ShowAvatarRole,
        MessageIdRole,
        SenderNameRole,
        ContentRole,
        SentAtRole,
        IsOutgoingRole,
        StatusRole,
        SessionIdRole,
        TypeRole,
        FileNameRole,
        ProgressRole,
        BytesTransferredRole,
        TotalBytesRole,
        CreatedAtRole,
        PeerDeviceNameRole,
        IsDirectoryRole,
        FileListRole,
        CanDeleteLocalFileRole,
        ErrorMsgRole,
    };
    Q_ENUM(Role)

    explicit ConversationTimelineModel(QObject *parent = nullptr);
    virtual ~ConversationTimelineModel() override = default;

    ConversationTimelineModel(const ConversationTimelineModel &)            = delete;
    ConversationTimelineModel &operator=(const ConversationTimelineModel &) = delete;

    // 返回时间线条目数量
    int count() const;
    // 返回模型行数
    virtual int rowCount(const QModelIndex &parent = {}) const override;
    // 返回指定角色的行数据
    virtual QVariant data(const QModelIndex &index, int role) const override;
    // 返回 QML delegate 使用的角色名称
    virtual QHash<int, QByteArray> roleNames() const override;

    // 返回时间线归属的设备
    QString deviceId() const;
    // 返回注入的聊天控制器
    QObject *chatController() const;
    // 返回注入的传输会话模型
    QObject *transferSessions() const;

public slots:
    // 切换时间线归属的设备并重建合并列表
    void setDeviceId(const QString &deviceId);
    // 注入聊天控制器，用于解析设备对应的消息模型
    void setChatController(QObject *controller);
    // 注入全局传输会话模型
    void setTransferSessions(QObject *source);

signals:
    void countChanged();
    void deviceIdChanged();

private:
    struct TimelineEntry {
        bool isMessage = false;  // true 表示聊天消息行，false 表示传输任务行
        int sourceRow = -1;      // 来源模型中的行号（消息行随来源增删维护）
        qint64 sortTime = 0;     // 排序时间戳（毫秒）
        bool showAvatar = false; // 消息行是否显示头像
        QVariantMap data;        // 行数据快照，字段名即角色名
    };

    // 按当前设备重新解析消息模型并全量重建
    void refreshSources();
    // 从两个来源模型重建合并列表并发出重置通知
    void rebuildFromSources();
    // 处理消息来源的插入通知
    void onMessagesInserted(const QModelIndex &parent, int first, int last);
    // 处理消息来源的删除通知
    void onMessagesRemoved(const QModelIndex &parent, int first, int last);
    // 处理消息来源的行更新通知
    void onMessagesDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                               const QList<int> &roles);
    // 处理会话来源的插入通知
    void onSessionsInserted(const QModelIndex &parent, int first, int last);
    // 处理会话来源的删除通知
    void onSessionsRemoved(const QModelIndex &parent, int first, int last);
    // 处理会话来源的行更新通知（进度刷新只更新对应行）
    void onSessionsDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                               const QList<int> &roles);
    // 构造消息来源指定行的合并条目
    TimelineEntry makeMessageEntry(int row) const;
    // 读取消息来源指定行的数据快照
    QVariantMap readMessageRow(int row) const;
    // 读取会话来源指定行的数据快照
    QVariantMap readSessionRow(int row) const;
    // 判断会话是否属于时间线当前设备
    bool sessionBelongsToDevice(const QVariantMap &session) const;
    // 返回按时间与稳定顺序的插入位置
    int insertionPosition(qint64 sortTime, bool isMessage, int sourceRow) const;
    // 返回指定来源行号对应的合并行；不存在返回 -1
    int mergedRowForMessage(int sourceRow) const;
    // 返回指定会话标识对应的合并行；不存在返回 -1
    int mergedRowForSession(const QString &sessionId) const;
    // 从 fromRow 起重算消息头像标记，notify 为真时对变化的行发出通知
    void applyAvatarFlags(int fromRow, bool notify);
    // 将 ISO 时间字符串转为毫秒时间戳，解析失败返回 0
    static qint64 timestampOf(const QString &isoTime);

    ChatController *_chatController = nullptr;        // 聊天控制器（解析设备消息模型）
    ChatMessageModel *_messageSource = nullptr;       // 当前设备的聊天消息模型
    TransferSessionModel *_transferSource = nullptr;  // 全局传输会话模型
    QString _deviceId;                                // 时间线归属设备
    QList<TimelineEntry> _entries;                    // 合并后的时间线行
};
