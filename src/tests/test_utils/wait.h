/**
* @file    wait.h
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   测试用事件循环等待工具
*
* 统一各测试里 processEvents 轮询等待的写法（等谓词、等信号、
* 等 socket 状态），语义与 QTRY 一致：条件满足立即返回，超时返回失败。
*
* 两条实测教训（新增等待时必须规避）：
* 1. QSignalSpy::wait() 只等待"新"信号（从当前计数起算），被测函数
*    在 wait 之前同步 emit 的信号不会唤醒等待——此时应直接断言
*    spy 非空，不要先 wait；waitSignal() 已封装该判断。
* 2. QTcpServer::waitForNewConnection() 的阻塞等待不得嵌在 QTRY
*    断言宏里，实际耗时远超声明超时；改用 hasPendingConnections()
*    轮询（waitFor 谓词即可表达）。
*
* Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 版本头对齐到 v7.19.0
 * [v7.18.0] GY   2026-10-05
 * * 版本头对齐到 v7.18.0
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
* * 抽取 test_relay_chain / test_session_manager / test_rendezvous_coordinator 的三种轮询写法
*/

#pragma once

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTcpSocket>

namespace gy::test {

// 事件循环驱动的谓词等待：条件满足返回 true，超时返回 false
template <typename Predicate>
inline bool waitFor(Predicate &&predicate, int timeoutMs = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate()) {
        if (elapsed.hasExpired(timeoutMs)) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return true;
}

// 等待 spy 记录到信号；wait 之前同步 emit 的场景直接判非空（见文件头教训 1）
inline bool waitSignal(QSignalSpy &spy, int timeoutMs = 3000)
{
    if (!spy.isEmpty()) {
        return true;
    }
    return spy.wait(timeoutMs);
}

// 等待 socket 建立连接（事件循环驱动，保证 QTcpServer 能并行处理新连接）
inline bool waitConnected(QTcpSocket *socket, int timeoutMs = 3000)
{
    return waitFor([socket]() {
        return socket->state() == QAbstractSocket::ConnectedState;
    }, timeoutMs);
}

// 等待 socket 读到至少 expected 字节并返回已读数据（超时返回已读部分）
inline QByteArray waitBytes(QTcpSocket *socket, int expected, int timeoutMs = 3000)
{
    QByteArray data;
    waitFor([&data, socket, expected]() {
        data += socket->readAll();
        return data.size() >= expected;
    }, timeoutMs);
    return data;
}

}
