/**
* @file    posix_udp_socket.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   基于原生描述符的 UDP 套接字
*
* 接收侧按数据报逐条取出并暂存，保留报文边界与来源地址。
* 写侧在绑定后自动开启广播能力，支持多网卡定向发送。
*/

#pragma once

#include <QByteArray>
#include <QFlags>
#include <QList>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include "ip_address.h"

class QSocketNotifier;

namespace gy::net {

class PosixUdpSocket : public QObject {
private:
    Q_OBJECT

public:
    // 绑定选项
    enum BindFlag {
        ShareAddress = 0x1,      // 允许其他套接字绑定同一地址
        ReuseAddressHint = 0x2   // 端口复用提示
    };
    Q_DECLARE_FLAGS(BindFlags, BindFlag)

    explicit PosixUdpSocket(QObject *parent = nullptr);
    virtual ~PosixUdpSocket() override;

    PosixUdpSocket(const PosixUdpSocket &)            = delete;
    PosixUdpSocket &operator=(const PosixUdpSocket &) = delete;

    // 绑定本地地址与端口
    bool bind(const IpAddress &address, quint16 port, BindFlags flags = {});
    // 停止接收并关闭描述符
    void close();

    // 是否已绑定
    bool isBound() const;
    // 本地端口
    quint16 localPort() const;
    // 最近一次错误说明
    QString errorString() const;

    // 发送数据报
    qint64 writeDatagram(const QByteArray &datagram, const IpAddress &address, quint16 port);
    // 读取一个数据报，可选返回来源地址与端口
    qint64 readDatagram(char *data, qint64 maxSize,
                        IpAddress *address = nullptr, quint16 *port = nullptr);
    // 是否有待读数据报
    bool hasPendingDatagrams() const;
    // 下一个数据报长度
    qint64 pendingDatagramSize() const;

signals:
    // 有新数据报到达
    void readyRead();

private:
    // 待读数据报
    struct Datagram {
        QByteArray data;   // 报文内容
        IpAddress address;  // 来源地址
        quint16 port = 0;   // 来源端口
    };

    // 读事件处理
    void onReadActivated();
    // 从内核取走全部待读报文
    void pumpDatagrams(bool notify);
    // 关闭描述符
    void closeDescriptor();

private:
    int _fd = -1;                            // UDP 描述符
    QSocketNotifier *_notifier = nullptr;    // 读事件通知器
    QList<Datagram> _datagrams;              // 待读报文队列
    QString _errorText;                      // 最近一次错误说明
};

Q_DECLARE_OPERATORS_FOR_FLAGS(PosixUdpSocket::BindFlags)

}  // namespace gy::net
