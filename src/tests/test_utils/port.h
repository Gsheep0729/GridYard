/**
* @file    port.h
* @version 7.26.0
* @date 2026-11-02
* @author  GY
* @brief   测试用临时端口分配与回环套接字对工具
*
* 直接通过原生套接字绑定 0 号端口向系统申请空闲端口后立即释放，
* 供传输与探测类测试替代硬编码或时间取模端口，消除连号占用冲突。
* 另提供 socketpair 的 Windows 等价物（TCP 回环对），供 Relay 链路
* 测试造可控连接对（Windows 可行性报告 B7/N18）。
*/

#pragma once

#include <QtGlobal>

#ifdef _WIN32
// Windows 分支：Winsock2 头（报告 B7）；closesocket/htonl/ntohs 同名可用，
// SOCK_CLOEXEC 无对应概念可省。NOMINMAX 与 net_reactor.cpp 同款防护
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace gy::test {

#ifdef _WIN32

// Windows 分支：进程级一次性 WSAStartup（与 net_reactor.cpp 的
// startupWinsockOnce 同款口径：引用计数持有至进程退出）。测试工具无法
// 预知调用时机，端口分配与回环造对在此自举，不依赖 Reactor 或入口层
// 先行初始化
inline bool ensureWinsockStarted()
{
    static const bool started = [] {
        WSADATA data{};
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}

// 向系统申请一个空闲 TCP 端口（申请后立即释放，存在极小的被占用窗口）
inline quint16 allocateEphemeralPort()
{
    if (!ensureWinsockStarted()) {
        return 0;
    }

    const SOCKET descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor == INVALID_SOCKET) {
        return 0;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (::bind(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        ::closesocket(descriptor);
        return 0;
    }

    int length = static_cast<int>(sizeof(address));
    if (::getsockname(descriptor, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        ::closesocket(descriptor);
        return 0;
    }

    const quint16 port = ntohs(address.sin_port);
    ::closesocket(descriptor);
    return port;
}

// socketpair 的 Windows 等价物（报告 N18）：127.0.0.1 上 0 号端口临时监听，
// 两端经本机互连后关闭监听器，产出与 AF_UNIX socketpair 同形的 int 描述符对。
// 两端按 SOCK_NONBLOCK 语义以 FIONBIO 置非阻塞（消费方 adopt/adoptDescriptor
// 各自也会补设，先行置位保持对等造对）；int 截断口径与适配层既有 int 描述符
// 签名一致（tcp_socket_core/posix_tcp_socket 均以 int 承载 SOCKET）
inline bool makeLoopbackPair(int pairFd[2])
{
    pairFd[0] = -1;
    pairFd[1] = -1;
    if (!ensureWinsockStarted()) {
        return false;
    }

    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    int length = static_cast<int>(sizeof(address));
    if (::bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0
        || ::listen(listener, 1) != 0
        || ::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
        ::closesocket(listener);
        return false;
    }

    const SOCKET first = ::socket(AF_INET, SOCK_STREAM, 0);
    if (first == INVALID_SOCKET) {
        ::closesocket(listener);
        return false;
    }
    if (::connect(first, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        ::closesocket(first);
        ::closesocket(listener);
        return false;
    }

    const SOCKET second = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);
    if (second == INVALID_SOCKET) {
        ::closesocket(first);
        return false;
    }

    u_long nonBlocking = 1;
    if (::ioctlsocket(first, FIONBIO, &nonBlocking) != 0
        || ::ioctlsocket(second, FIONBIO, &nonBlocking) != 0) {
        ::closesocket(first);
        ::closesocket(second);
        return false;
    }

    pairFd[0] = static_cast<int>(first);
    pairFd[1] = static_cast<int>(second);
    return true;
}

#else

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

#endif

}
