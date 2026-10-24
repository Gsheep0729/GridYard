/**
* @file    chat_message.h
* @version 7.26.1
* @date    2026-10-25
* @author  GridYard Team
* @brief   在线聊天消息值类型与错误分类（客户端 DTO）
*
* 集中定义 P2P 在线文本消息的客户端内存表示与处理失败分类，供聊天
* 连接、管理器与 QML 控制器的信号契约使用。JSON 编解码已由纯 C++
* 双生实现承担，客户端经 codec_bridge.h 的包装入口调用。
*/

#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace gy {

// 在线聊天消息的内存表示
struct ChatMessage {
    QString messageId;     // 消息唯一标识
    QString fromDeviceId;  // 发送方设备标识
    QString fromName;      // 发送方显示名称
    QString content;       // UTF-8 文本内容
    QDateTime sentAt;      // 发送时间
};

// 聊天消息处理失败分类
enum class ChatMessageError : quint8 {
    None,
    InvalidInput,
    PeerOffline,
    ConnectionFailed,
    WriteFailed,
    ConnectionLost,
    InvalidPayload,
};

}  // namespace gy

Q_DECLARE_METATYPE(gy::ChatMessage)
Q_DECLARE_METATYPE(gy::ChatMessageError)
