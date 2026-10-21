/**
* @file    posix_tcp_socket.h
* @version 7.26.0
* @date 2026-10-22
* @author  GridYard Team
* @brief   基于原生描述符的 TCP 套接字
*
* 对外接口覆盖业务层当前使用的 Qt 套接字子集，读写缓冲与信号语义保持一致。
* 写入先尝试系统调用，余量进入用户态队列，返回值恒等于入参长度，避免短写
* 被调用方误判为发送失败。
*/

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QtGlobal>

#include <atomic>

#include "ip_address.h"
#include "posix_net_types.h"

class QSocketNotifier;

namespace gy::net {

class PosixTcpSocket : public QObject {
private:
    Q_OBJECT

public:
    // 套接字选项
    enum SocketOption {
        SendBufferSizeOption,     // 发送缓冲大小
        ReceiveBufferSizeOption,  // 接收缓冲大小
        LowDelayOption            // 低延迟（TCP_NODELAY）
    };

    explicit PosixTcpSocket(QObject *parent = nullptr);
    virtual ~PosixTcpSocket() override;

    PosixTcpSocket(const PosixTcpSocket &)            = delete;
    PosixTcpSocket &operator=(const PosixTcpSocket &) = delete;

    // 接管已建立的连接描述符
    bool adoptDescriptor(int descriptor);
    // 移交前停用通知器
    void prepareThreadMove();
    // 在目标线程重新启用通知器
    void finishThreadMove();
    // 是否处于线程移交窗口
    bool isThreadMovePending() const;

    // 按主机名或地址文本建立连接
    void connectToHost(const QString &host, quint16 port);
    // 按地址值建立连接
    void connectToHost(const IpAddress &address, quint16 port);

    // 当前连接状态
    SocketState state() const;
    // 最近一次错误类型
    SocketError error() const;
    // 最近一次错误说明
    QString errorString() const;

    // 可读字节数（用户态缓冲与内核缓冲之和）
    qint64 bytesAvailable() const;
    // 待写字节数（仅用户态缓冲）
    qint64 bytesToWrite() const;

    // 读取指定长度到调用方缓冲
    qint64 read(char *data, qint64 maxSize);
    // 读取指定长度并返回字节数组
    QByteArray read(qint64 maxSize);
    // 读取全部可读内容
    QByteArray readAll();
    // 窥探指定长度但不消费
    QByteArray peek(qint64 maxSize);
    // 读取一行（包含换行符）
    QByteArray readLine(qint64 maxSize = 0);

    // 写入数据，返回入参长度；套接字不可写时返回 -1
    qint64 write(const QByteArray &data);
    // 写入原始数据
    qint64 write(const char *data, qint64 size);
    // 尽力把用户态写缓冲送入内核
    void flush();

    // 阻塞等待连接建立
    bool waitForConnected(int msecs = 30000);
    // 阻塞等待可读数据
    bool waitForReadyRead(int msecs = 30000);
    // 阻塞等待写缓冲排空
    bool waitForBytesWritten(int msecs = 30000);
    // 阻塞等待连接断开
    bool waitForDisconnected(int msecs = 30000);

    // 优雅关闭：先刷出待写数据再关闭；waitForPeer 为真时等待对端关闭并排空到达数据
    void disconnectFromHost(bool waitForPeer = false, int waitMs = 500);
    // 立即关闭并丢弃待写数据
    void abort();
    // 关闭连接
    void close();

    // 设置套接字选项
    bool setSocketOption(SocketOption option, const QVariant &value);
    // 设置应用层读缓冲上限，0 表示不限制
    void setReadBufferSize(qint64 size);

    // 本地端口
    quint16 localPort() const;
    // 对端端口
    quint16 peerPort() const;
    // 本地地址
    IpAddress localAddress() const;
    // 对端地址
    IpAddress peerAddress() const;
    // 原生描述符
    int socketDescriptor() const;

signals:
    // 连接建立
    void connected();
    // 连接断开
    void disconnected();
    // 有新数据可读
    void readyRead();
    // 数据已写入内核
    void bytesWritten(qint64 bytes);
    // 发生套接字错误
    void errorOccurred(gy::net::SocketError error);

private:
    // 读事件处理
    void onReadActivated();
    // 写事件处理
    void onWriteActivated();

private:
    // 创建事件通知器
    void ensureNotifiers();
    // 销毁事件通知器
    void teardownNotifiers();
    // 记录错误并通知
    void setError(SocketError error, const QString &message);
    // 关闭描述符
    void closeDescriptor();
    // 从内核读取数据到用户态缓冲
    void pumpReadBuffer(bool notify);
    // 把用户态写缓冲送入内核
    void pumpWriteBuffer();
    // 完成连接建立
    void finishConnect();
    // 刷新读通知器开关
    void syncReadNotifier();
    // 刷新写通知器开关
    void syncWriteNotifier();
    // 完成优雅关闭
    void finishGracefulClose();
    // 用户态缓冲未读字节数
    qint64 bufferedBytes() const;
    // 压缩已消费的读缓冲
    void compactReadBuffer();

    int _fd = -1;                              // 原生描述符
    QSocketNotifier *_readNotifier = nullptr;  // 读事件通知器
    QSocketNotifier *_writeNotifier = nullptr; // 写事件通知器
    QByteArray _rxBuf;                         // 用户态读缓冲
    qint64 _rxOffset = 0;                      // 读缓冲消费偏移
    QByteArray _txBuf;                         // 用户态写缓冲
    qint64 _txOffset = 0;                      // 写缓冲发送偏移
    SocketState _state = SocketState::UnconnectedState;  // 当前状态
    SocketError _lastError = SocketError::NoError;        // 最近错误类型
    QString _errorText;                        // 最近错误说明
    qint64 _readBufferLimit = 0;               // 读缓冲上限，0 表示不限
    std::atomic_bool _movedPending{false};     // 线程移交窗口标志，跨线程可见
    bool _pumpingWrite = false;                // 写泵重入保护
    bool _pumpingRead = false;                 // 读泵重入保护，保证回调用尽缓冲后再发断开
    bool _gracefulWaitPending = false;         // 写缓冲未排空时记录的等待对端关闭请求
    int _gracefulWaitMs = 0;                   // 写缓冲排空后等待对端关闭的时长
    bool _peerEofSeen = false;                 // 重入排空期间已看到对端关闭，交由外层读泵收尾
};

}  // namespace gy::net
