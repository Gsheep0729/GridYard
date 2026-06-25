/**
* @file    data_types.h
* @version 4.16.1
* @date    2026-06-21
* @author  GridYard Team
* @brief   跨模块共享数据类型定义（值类型 / POD）
*
* 含 PeerInfo（局域网在线节点描述）、FileEntry（文件元数据）、
* TransferSession（传输会话状态）等跨模块流通的数据类型。
* 全部使用 Q_GADGET 而非 QObject——这些类型是"值"，可拷贝可移动；
* QObject 的身份语义不适合。QML 端通过 Q_PROPERTY MEMBER 反射访问。
*
* Change Log:
* [v4.16.1] GY   2026-06-21
* * 优化封装性，补充 Tell, Don't Ask 语义化方法注释
* [v0.2.0] FengChunlin   2026-04-24
* * Stage 2 准备：PeerInfo 补充 isOnline / lastSeen 字段
* [v0.1.0] GY   2026-04-20
* * Stage 1：补充 FileEntry / TransferSession 数据结构
* [v0.0.1] GY   2026-04-06
* * Stage 0 占位：仅 PeerInfo 骨架
*/

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

// 局域网在线节点描述
class PeerInfo {
    Q_GADGET
    Q_PROPERTY(QString  deviceId   MEMBER deviceId)
    Q_PROPERTY(QString  deviceName MEMBER deviceName)
    Q_PROPERTY(QString  ipAddress  MEMBER ipAddress)
    Q_PROPERTY(quint16  tcpPort    MEMBER tcpPort)
    Q_PROPERTY(bool     isOnline   MEMBER isOnline)
    Q_PROPERTY(QString  lastSeen   READ lastSeenStr)
    Q_PROPERTY(quint16  protocolVersion MEMBER protocolVersion)

public:
    QString  deviceId;      // UUID，首次启动生成
    QString  deviceName;    // 用户自定义名或 hostname
    QString  ipAddress;     // 对端最近一次广播来源地址
    quint16  tcpPort = 0;   // 对端 TCP 监听端口
    bool     isOnline = false;  // 是否仍处于发现心跳有效期内
    quint16  protocolVersion = 0;  // 对端协议版本
    QDateTime lastSeen;     // 最后心跳时间

    QString lastSeenStr() const { return lastSeen.toString("HH:mm:ss"); }
};

Q_DECLARE_METATYPE(PeerInfo)

// 文件元数据（传输请求用）
class FileEntry {
    Q_GADGET
    Q_PROPERTY(QString relativePath MEMBER relativePath)
    Q_PROPERTY(qint64  fileSize     MEMBER fileSize)
    Q_PROPERTY(QString sha256       MEMBER sha256)

public:
    QString relativePath;   // 相对路径（含目录结构）
    qint64  fileSize = 0;   // 文件字节数
    QString sha256;         // 文件哈希（传输完成后校验用）
};

Q_DECLARE_METATYPE(FileEntry)

// 传输会话状态
class TransferSession {
    Q_GADGET
    Q_PROPERTY(QString sessionId    MEMBER sessionId)
    Q_PROPERTY(QString peerDeviceId MEMBER peerDeviceId)
    Q_PROPERTY(int     fileCount    MEMBER fileCount)
    Q_PROPERTY(qint64  totalBytes   MEMBER totalBytes)
    Q_PROPERTY(qint64  sentBytes    MEMBER sentBytes)
    Q_PROPERTY(bool    isActive     MEMBER isActive)

public:
    QString sessionId;      // 会话唯一 ID
    QString peerDeviceId;   // 对端设备 ID
    int     fileCount = 0;  // 会话包含的真实文件数量
    qint64  totalBytes = 0; // 会话总字节数
    qint64  sentBytes = 0;  // 已发送或已接收字节数
    bool    isActive = false;  // 会话是否仍在传输中
};

Q_DECLARE_METATYPE(TransferSession)
