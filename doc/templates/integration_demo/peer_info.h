/**
 * @file    peer_info.h
 * @date    2026-05-24
 * @author  GY
 * @brief   局域网在线节点的值类型
 *
 * 选用 Q_GADGET 而非 QObject 的原因：节点信息本质是数据记录，
 * 可拷贝、可移动、无身份。QObject 的拷贝禁用语义与"批量传值"
 * 场景天然冲突，因此走 Q_GADGET 路线。
 * Q_PROPERTY 用 MEMBER 形式直接绑定到 public 字段，QML 中可
 * 通过点记法访问。
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

#pragma once

#include <QMetaType>
#include <QString>
#include <QtQml/qqmlregistration.h>

class PeerInfo {
    Q_GADGET
    QML_VALUE_TYPE(peerInfo)
    Q_PROPERTY(QString deviceId   MEMBER deviceId)
    Q_PROPERTY(QString deviceName MEMBER deviceName)
    Q_PROPERTY(QString ipAddress  MEMBER ipAddress)
    Q_PROPERTY(bool    isOnline   MEMBER isOnline)

public:
    QString deviceId;
    QString deviceName;
    QString ipAddress;
    bool    isOnline = false;
};

Q_DECLARE_METATYPE(PeerInfo)
