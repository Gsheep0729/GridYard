/**
* @file    invite_codec.cpp
* @version 7.1.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   邀请连接文本编解码器实现
*
* 实现邀请文本的 URL 编码解析和生成。
*
* Change Log:
* [v7.1.0] GY   2026-07-21
* * Stage 7.1：新增邀请连接编解码器，支持文本格式的设备交换
*/

#include "invite_codec.h"

#include <QHostAddress>
#include <QUrl>
#include <QUrlQuery>

QString InviteCodec::encode(const Invite &invite)
{
    QUrl url(QStringLiteral("gridyard://invite"));
    QUrlQuery query;

    query.addQueryItem(QStringLiteral("deviceId"), invite.deviceId);
    query.addQueryItem(QStringLiteral("name"), invite.deviceName);
    query.addQueryItem(QStringLiteral("ip"), invite.ipAddress);
    query.addQueryItem(QStringLiteral("tcpPort"), QString::number(invite.tcpPort));
    query.addQueryItem(QStringLiteral("discoveryPort"), QString::number(invite.discoveryPort));

    url.setQuery(query);
    return url.toString();
}

InviteCodec::Invite InviteCodec::parse(const QString &text, Error *error)
{
    Invite result;
    *error = Error::None;

    if (text.isEmpty()) {
        *error = Error::InvalidSchema;
        return result;
    }

    QUrl url(text);
    if (!url.isValid() || url.scheme() != QStringLiteral("gridyard") ||
        url.host() != QStringLiteral("invite")) {
        *error = Error::InvalidSchema;
        return result;
    }

    QUrlQuery query(url.query());

    const QString deviceId = query.queryItemValue(QStringLiteral("deviceId"));
    if (deviceId.isEmpty()) {
        *error = Error::MissingDeviceId;
        return result;
    }
    result.deviceId = deviceId;
    result.deviceName = query.queryItemValue(QStringLiteral("name"));

    const QString ip = query.queryItemValue(QStringLiteral("ip"));
    QHostAddress addr;
    if (ip.isEmpty() || !addr.setAddress(ip)) {
        *error = Error::InvalidIp;
        return result;
    }
    result.ipAddress = ip;

    bool ok = false;
    const QString tcpPortStr = query.queryItemValue(QStringLiteral("tcpPort"));
    const int tcpPort = tcpPortStr.toInt(&ok);
    if (!ok || tcpPort < 1 || tcpPort > 65535) {
        *error = Error::InvalidTcpPort;
        return result;
    }
    result.tcpPort = static_cast<quint16>(tcpPort);

    const QString discoveryPortStr = query.queryItemValue(QStringLiteral("discoveryPort"));
    if (!discoveryPortStr.isEmpty()) {
        const int discoveryPort = discoveryPortStr.toInt(&ok);
        if (ok && discoveryPort >= 1 && discoveryPort <= 65535) {
            result.discoveryPort = static_cast<quint16>(discoveryPort);
        }
    }

    return result;
}

QString InviteCodec::errorString(Error error)
{
    switch (error) {
    case Error::None:
        return QStringLiteral("无错误");
    case Error::InvalidSchema:
        return QStringLiteral("无效的协议格式，请检查邀请文本是否正确");
    case Error::MissingDeviceId:
        return QStringLiteral("邀请文本缺少设备标识");
    case Error::InvalidIp:
        return QStringLiteral("IP 地址格式错误");
    case Error::InvalidTcpPort:
        return QStringLiteral("TCP 端口必须为 1-65535 之间的数字");
    case Error::InvalidDiscoveryPort:
        return QStringLiteral("发现端口必须为 1-65535 之间的数字");
    }
    return QStringLiteral("未知错误");
}