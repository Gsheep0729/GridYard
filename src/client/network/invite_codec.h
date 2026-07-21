/**
* @file    invite_codec.h
* @version 7.1.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   邀请连接文本编解码器
*
* 提供邀请文本的编码与解析功能。邀请文本格式为：
* gridyard://invite?deviceId=...&name=...&ip=...&tcpPort=35100&discoveryPort=45678
*
* Change Log:
* [v7.1.0] GY   2026-07-21
* * Stage 7.1：新增邀请连接编解码器，支持文本格式的设备交换
*/

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

class InviteCodec {
public:
    // 邀请数据
    struct Invite {
        QString deviceId;       // 设备唯一标识（非空）
        QString deviceName;     // 设备名称（仅作显示）
        QString ipAddress;      // IP 地址
        quint16 tcpPort = 0;    // TCP 端口
        quint16 discoveryPort = 0; // 发现端口
    };

    // 解析错误类型
    enum class Error {
        None,
        InvalidSchema,     // 非 gridyard:// 协议
        MissingDeviceId,   // 缺少 deviceId
        InvalidIp,         // IP 地址格式错误
        InvalidTcpPort,    // TCP 端口非法
        InvalidDiscoveryPort // 发现端口非法
    };

    // 编码邀请数据为文本
    static QString encode(const Invite &invite);

    // 解析邀请文本，错误时 result 为空，error 指示错误类型
    static Invite parse(const QString &text, Error *error);

    // 获取错误描述
    static QString errorString(Error error);
};