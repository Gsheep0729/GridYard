/**
* @file    posix_net_error.cpp
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   errno 映射表实现
*
* 文案与既有传输历史中的错误说明保持一致，便于新旧记录直接比对。
*/

#include "posix_net_error.h"

#include <cerrno>

namespace gy::net {

// 将系统 errno 转换为套接字错误类型
SocketError socketErrorFromErrno(int errnoValue)
{
    switch (errnoValue) {
    case 0:
        return SocketError::NoError;
    case ECONNREFUSED:
        return SocketError::ConnectionRefusedError;
    case ECONNRESET:
    case EPIPE:
        return SocketError::RemoteHostClosedError;
    case ETIMEDOUT:
        return SocketError::SocketTimeoutError;
    case EHOSTUNREACH:
    case EHOSTDOWN:
        return SocketError::HostNotFoundError;
    case ENETUNREACH:
    case ENETDOWN:
        return SocketError::NetworkError;
    case EACCES:
    case EPERM:
        return SocketError::SocketAccessError;
    case EADDRINUSE:
        return SocketError::AddressInUseError;
    case EADDRNOTAVAIL:
        return SocketError::SocketAddressNotAvailableError;
    case EMFILE:
    case ENFILE:
    case ENOBUFS:
    case ENOMEM:
        return SocketError::SocketResourceError;
    case EAGAIN:
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case EINTR:
        return SocketError::NoError;
    default:
        return SocketError::UnknownSocketError;
    }
}

// 将套接字错误类型转换为中文说明
QString socketErrorMessage(SocketError error)
{
    switch (error) {
    case SocketError::NoError:
        return QStringLiteral("无错误");
    case SocketError::ConnectionRefusedError:
        return QStringLiteral("对端拒绝连接");
    case SocketError::RemoteHostClosedError:
        return QStringLiteral("远程主机关闭了连接");
    case SocketError::HostNotFoundError:
        return QStringLiteral("找不到主机");
    case SocketError::SocketTimeoutError:
        return QStringLiteral("连接超时");
    case SocketError::SocketAccessError:
        return QStringLiteral("无套接字访问权限");
    case SocketError::SocketResourceError:
        return QStringLiteral("资源不足");
    case SocketError::SocketAddressNotAvailableError:
        return QStringLiteral("地址不可用");
    case SocketError::AddressInUseError:
        return QStringLiteral("地址已被占用");
    case SocketError::NetworkError:
        return QStringLiteral("网络不可达");
    case SocketError::UnsupportedOperationError:
        return QStringLiteral("不支持的操作");
    case SocketError::UnknownSocketError:
        return QStringLiteral("未知错误");
    }
    return QStringLiteral("未知错误");
}

// 直接由 errno 得到中文说明
QString errorMessageFromErrno(int errnoValue)
{
    return socketErrorMessage(socketErrorFromErrno(errnoValue));
}

}  // namespace gy::net
