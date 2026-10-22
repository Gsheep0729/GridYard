/**
* @file    json_line.h
* @version 7.26.0
* @date 2026-10-22
* @author  GY
* @brief   测试用 JSON 行协议收发工具
*
* 提供原生套接字上的 JSON 行读写 helper，供协调节点与中继链路
* 的协议往返断言复用；读取按事件循环驱动，不阻塞主线程。
*/

#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>

#include "posix_tcp_socket.h"

namespace gy::test {

// 事件循环驱动读取一行 JSON，超时返回空对象
inline QJsonObject readJsonLine(gy::net::PosixTcpSocket *socket, int timeoutMs = 3000)
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
inline void writeJsonLine(gy::net::PosixTcpSocket *socket, const QJsonObject &json)
{
    socket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

}
