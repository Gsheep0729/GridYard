/**
* @file    json_line.h
* @version 7.15.16
* @date    2026-10-04
* @author  GY
* @brief   测试用 JSON 行协议收发工具
*
* 提供 QTcpSocket 上的 JSON 行读写 helper，供协调节点与中继链路
* 的协议往返断言复用；读取按事件循环驱动，不阻塞主线程。
*
* Change Log:
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
* * 从 test_relay_chain 与 test_rendezvous_coordinator 抽取公共实现
*/

#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

namespace gy::test {

// 事件循环驱动读取一行 JSON，超时返回空对象
inline QJsonObject readJsonLine(QTcpSocket *socket, int timeoutMs = 3000)
{
    QByteArray buffer;
    QElapsedTimer elapsed;
    elapsed.start();

    while (!buffer.contains('\n')) {
        if (elapsed.hasExpired(timeoutMs)) {
            return {};
        }
        buffer += socket->readAll();
        if (buffer.contains('\n')) {
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    const int newlineIndex = buffer.indexOf('\n');
    const QJsonDocument doc = QJsonDocument::fromJson(buffer.left(newlineIndex));
    return doc.object();
}

// 发送一行 JSON 并立即刷出
inline void writeJsonLine(QTcpSocket *socket, const QJsonObject &json)
{
    socket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

}
