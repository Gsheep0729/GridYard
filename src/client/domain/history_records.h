/**
* @file    history_records.h
* @version 6.6.2
* @date    2026-06-25
* @author  GY
* @brief   本地历史持久化领域记录
*
* 应用层与存储层之间使用的纯值类型，不含 Qt Sql 或 QML 类型。
*
* Change Log:
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.0.0] GY 2026-06-25
* * 新增本地历史领域记录和查询游标
*/

#pragma once

#include <QDateTime>
#include <QString>

enum class RecordDirection {
    Incoming = 0,  // 接收自对端的记录
    Outgoing = 1,  // 发送给对端的记录
};

struct PeerRecord {
    QString deviceId;         // 对端稳定设备标识
    QString deviceName;       // 最近一次发现的设备名称
    QString lastIpAddress;    // 最近一次发现的网络地址
    quint16 lastTcpPort = 0;  // 最近一次发现的 TCP 端口
    QDateTime firstSeenAt;    // 首次写入设备目录的 UTC 时间
    QDateTime lastSeenAt;     // 最近一次发现设备的 UTC 时间
    QDateTime lastChatAt;     // 最近一次聊天活动的 UTC 时间
    QDateTime lastTransferAt; // 最近一次传输活动的 UTC 时间
};

struct MessageRecord {
    QString messageId;       // 消息 UUID，也是幂等写入键
    QString peerDeviceId;    // 对端稳定设备标识
    RecordDirection direction = RecordDirection::Incoming;  // 收发方向
    QString senderDeviceId;  // 实际发送方设备标识
    QString senderName;      // 发送时的设备名称快照
    QString content;         // 文本消息内容
    QDateTime sentAt;        // 协议消息时间
    int localStatus = 0;     // 本地发送状态快照
    QDateTime createdAt;     // 写入本地历史的 UTC 时间
};

struct TransferRecord {
    QString recordId;        // 历史记录 UUID
    QString sessionId;       // 传输会话 UUID
    QString peerDeviceId;    // 对端稳定设备标识
    QString peerName;        // 传输结束时的对端名称快照
    RecordDirection direction = RecordDirection::Incoming;  // 收发方向
    QString displayName;     // 文件或目录的展示名称
    bool isDirectory = false; // 是否为目录传输
    int fileCount = 0;       // 文件总数
    qint64 totalBytes = 0;   // 内容总字节数
    QString status;          // 完成、失败、拒绝或取消状态
    QDateTime startedAt;     // 会话开始 UTC 时间
    QDateTime finishedAt;    // 会话结束 UTC 时间
    int errorCode = 0;       // 最终失败错误码
    QString errorMessage;    // 可展示的错误说明
};

struct MessageCursor {
    QString peerDeviceId;      // 当前分页所属会话
    QDateTime beforeSentAt;    // 下一页仅查询早于该时间的消息
    QString beforeMessageId;   // 同一时间下保证稳定排序的消息标识
};

struct TransferQuery {
    QString peerDeviceId;      // 可选的设备筛选条件
    QString status;            // 可选的最终状态筛选条件
    QDateTime beforeStartedAt; // 下一页仅查询早于该时间的记录
};
