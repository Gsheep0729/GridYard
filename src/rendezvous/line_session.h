/**
* @file    line_session.h
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   带上限的按行分帧会话基类
*
* 承载"按 \n 分帧的控制行"这一共同模式：单行长度上限、首行握手
* 超时、逐行刷新的空闲超时与断开清理，供协调会话和中继握手等待复用。
* 会话持有 socket 的归属，终结时统一回收。
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
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
* [v7.12.0] GY   2026-10-02
* * 自协调会话与中继握手的共同逻辑抽出分帧会话基类
*/

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QTcpSocket>

class LineSession : public QObject {
    Q_OBJECT

public:
    // 会话接管 socket 归属；maxLineBytes 为单行长度上限
    explicit LineSession(QTcpSocket *socket, qsizetype maxLineBytes, QObject *parent = nullptr);
    virtual ~LineSession() override;

    LineSession(const LineSession &)            = delete;
    LineSession &operator=(const LineSession &) = delete;

    // 启动首行握手超时：期限内未收到完整控制行则断开
    void startHandshakeTimeout(int ms);
    // 启动空闲超时：每收到一行自动刷新
    void startIdleTimeout(int ms);
    // 服务端主动以协议错误终结会话（如握手内容非法）
    void finishWithError(const QString &reason);

    // 会话持有的连接；已移交或终结后返回 nullptr
    QTcpSocket *socket() const { return _socket; }
    // 握手行之后已到达的剩余字节（中继移交时一并转交）
    QByteArray pendingBytes() const;
    // 剥离 socket 归属并返回（连接移交场景）；此后会话不再触达该 socket
    QTcpSocket *takeSocket();
    // 会话是否已终结（终结后不再发任何信号）
    bool isFinished() const { return _finished; }

signals:
    // 收到一条完整控制行
    void lineReady(const QByteArray &line);
    // 行超长、超时或 socket 错误，会话将断开
    void protocolError(const QString &reason);
    // 会话终结（保证只发射一次），监听方据此回收会话对象
    void closed();

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    // 统一终结出口：断开 socket 并只发射一次 closed
    void finish();

    QTcpSocket *_socket = nullptr;  // 会话持有的连接
    QTimer *_timeoutTimer = nullptr; // 握手/空闲共用计时器
    QString _timeoutReason;          // 当前计时器到期的原因描述
    QByteArray _buffer;              // 半行缓冲
    qsizetype _maxLineBytes = 0;     // 单行长度上限
    bool _handshakePhase = false;    // 是否仍处于首行握手阶段
    bool _finished = false;          // 是否已终结
    bool _detached = false;          // socket 是否已移交（移交后停止一切处理）
};
