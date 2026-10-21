/**
* @file    posix_net_error.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   errno 与套接字错误类型、中文文案的映射
*
* 网络层对外只暴露自研错误类型与稳定中文文案，避免平台 errno 扩散到业务层。
* EAGAIN、EWOULDBLOCK 与 EINTR 属于可重试状态，映射为无错误。
*/

#pragma once

#include <QString>

#include "posix_net_types.h"

namespace gy::net {

// 将系统 errno 转换为套接字错误类型
SocketError socketErrorFromErrno(int errnoValue);
// 将套接字错误类型转换为中文说明
QString socketErrorMessage(SocketError error);
// 直接由 errno 得到中文说明
QString errorMessageFromErrno(int errnoValue);

}  // namespace gy::net
