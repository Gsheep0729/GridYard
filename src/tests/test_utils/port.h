/**
* @file    port.h
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   测试用临时端口分配工具
*
* 通过 QTcpServer 绑定 0 号端口向系统申请空闲端口后立即释放，
* 供传输与探测类测试替代硬编码或时间取模端口，消除连号占用冲突。
*/

#pragma once

#include <QAbstractSocket>
#include <QHostAddress>
#include <QTcpServer>

namespace gy::test {

// 向系统申请一个空闲 TCP 端口（申请后立即释放，存在极小的被占用窗口）
inline quint16 allocateEphemeralPort()
{
    QTcpServer allocator;
    allocator.listen(QHostAddress::LocalHost, 0);
    const quint16 port = allocator.serverPort();
    allocator.close();
    return port;
}

}
