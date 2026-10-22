/**
* @file    port.h
* @version 7.26.0
* @date 2026-10-22
* @author  GY
* @brief   测试用临时端口分配工具
*
* 直接通过原生套接字绑定 0 号端口向系统申请空闲端口后立即释放，
* 供传输与探测类测试替代硬编码或时间取模端口，消除连号占用冲突。
*/

#pragma once

#include <QtGlobal>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace gy::test {

// 向系统申请一个空闲 TCP 端口（申请后立即释放，存在极小的被占用窗口）
inline quint16 allocateEphemeralPort()
{
    const int descriptor = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (descriptor < 0) {
        return 0;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        ::close(descriptor);
        return 0;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(descriptor, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        ::close(descriptor);
        return 0;
    }

    const quint16 port = ntohs(address.sin_port);
    ::close(descriptor);
    return port;
}

}
