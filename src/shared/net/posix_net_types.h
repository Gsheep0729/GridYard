/**
* @file    posix_net_types.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   原生套接字状态与错误类型
*
* 枚举成员沿用 Qt 套接字类型的命名，业务层迁移时只需替换类型限定名。
* 错误类型不携带平台细节，调用方无需直接接触 errno。
*/

#pragma once

#include <QMetaType>

namespace gy::net {

// 套接字生命周期状态
enum class SocketState {
    UnconnectedState,  // 未连接
    HostLookupState,   // 主机名解析中
    ConnectingState,   // 正在建立连接
    ConnectedState,    // 连接已建立
    BoundState,        // 已绑定本地地址
    ListeningState,    // 正在监听
    ClosingState       // 正在关闭
};

// 套接字错误类型
enum class SocketError {
    NoError,                         // 无错误
    ConnectionRefusedError,          // 对端拒绝连接
    RemoteHostClosedError,           // 远程主机关闭连接
    HostNotFoundError,               // 主机不可达
    SocketTimeoutError,              // 操作超时
    SocketAccessError,               // 权限不足
    SocketResourceError,             // 资源不足
    SocketAddressNotAvailableError,  // 地址不可用
    AddressInUseError,               // 地址已被占用
    NetworkError,                    // 网络不可达
    UnsupportedOperationError,       // 不支持的操作
    UnknownSocketError               // 未知错误
};

}  // namespace gy::net

Q_DECLARE_METATYPE(gy::net::SocketError)
