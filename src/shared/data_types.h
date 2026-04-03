/**
* @file    data_types.h
* @date    2026-05-24
* @author  GY
* @brief   GridYard 共享数据类型（值类型 / POD）
*
* 含 PeerInfo（局域网在线节点描述）等跨模块流通的数据类型。
* 全部使用 Q_GADGET 而非 QObject —— 这些类型是"值"，可拷贝可移动；
* QObject 的身份语义不适合。QML 端通过 Q_PROPERTY MEMBER 反射访问。
*
* Change Log:
* [v0.1] GY   2026-05-24
* * Stage 0 占位：仅 PeerInfo 骨架；FileEntry / TransferSession 待 Stage 1
*/

#pragma once

#include <QMetaType>
#include <QString>

class PeerInfo {
    Q_GADGET
    Q_PROPERTY(QString deviceId   MEMBER deviceId)
    Q_PROPERTY(QString deviceName MEMBER deviceName)
    Q_PROPERTY(QString ipAddress  MEMBER ipAddress)
    Q_PROPERTY(quint16 tcpPort    MEMBER tcpPort)

public:
    QString deviceId;       // UUID，首次启动生成
    QString deviceName;     // 用户自定义名或 hostname
    QString ipAddress;
    quint16 tcpPort = 0;
};

Q_DECLARE_METATYPE(PeerInfo)
