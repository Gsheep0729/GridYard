/**
* @file    port.h
* @version 7.17.5
* @date 2026-10-04
* @author  GY
* @brief   测试用临时端口分配工具
*
* 通过 QTcpServer 绑定 0 号端口向系统申请空闲端口后立即释放，
* 供传输与探测类测试替代硬编码或时间取模端口，消除连号占用冲突。
*
* Change Log:
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
* [v7.15.14] GY   2026-10-04
* * 版本头对齐到 v7.15.14
* [v7.15.13] GY   2026-10-04
* * 版本头对齐到 v7.15.13
* [v7.15.12] GY   2026-10-03
* 版本头对齐到 v7.15.12
* [v7.15.11] GY   2026-10-03
* * 版本头对齐到 v7.15.11
* [v7.15.10] GY   2026-10-03
* * 版本头对齐到 v7.15.10
* [v7.15.7] GY   2026-10-03
* * 从 test_file_transfer 与 test_endpoint_probe 的魔法端口改写中抽取
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
