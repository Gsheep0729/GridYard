/**
* @file    relay_server.h
* @version 7.15.18
* @date    2026-10-04
* @author  GridYard Team
* @brief   流式中继服务器
*
* 处理最严格的校园网隔离场景，两个客户端可都无法直连时通过中继转发。
* 中继只转发在线字节流，不落盘文件内容。
* 两端齐备后向发送端回一行 relay_ready 门控传输开始，任一端离开立即
* 通知并关闭对端，未完成会话超时回收。
* 握手等待复用 LineSession（行上限 + 超时），转发带写队列背压，
* 会话数与积压均有上限。
*
* Change Log:
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
* [v7.14.0] GY   2026-10-03
* * 激活握手令牌校验，独立模式不再无认证
* [v7.13.4] GY   2026-10-03
* * 会话关闭判等改对象指针，消除 relay_id 复用窗口的误删；积压回滚补断信号
* * 会话等待时长与邀请 TTL 等常量迁入 rendezvous_limits.h 并支持测试调小
* [v7.12.0] GY   2026-10-02
* * 握手等待迁移到 LineSession 基类，补齐会话数与积压上限
* * 转发增加写队列背压与读缓冲上限，消除大文件转发内存无界增长
* [v7.9.0] GY   2026-07-26
* * 会话就绪通知与对端关闭传播，支持同端口复用时的外部连接接入
* * 首行握手改异步读取，未完成会话增加超时回收
* [v7.3.0] GY   2026-07-21
* * Stage 7.3：新增流式中继服务器
*/

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QString>
#include <QTimer>

#include "line_session.h"
#include "rendezvous_limits.h"

class RelaySession : public QObject {
    Q_OBJECT

public:
    explicit RelaySession(const QString &relayId,
                          int waitMs = gy::rendezvous::kRelaySessionWaitMs,
                          QObject *parent = nullptr);
    virtual ~RelaySession() override;

    QString relayId() const { return _relayId; }
    bool isComplete() const;  // 两端都连接完成
    bool addSender(QTcpSocket *socket, const QByteArray &pendingData = {});
    bool addReceiver(QTcpSocket *socket, const QByteArray &pendingData = {});
    // 两端齐备后向发送端写 relay_ready，门控其开始 TLV 传输
    void notifyReady();
    // 向两端广播错误消息
    void broadcastError(const QString &code, const QString &message);
    // 主动结束会话：向两端广播错误并关闭连接
    void abort(const QString &code, const QString &message);

signals:
    void sessionClosed();

private slots:
    void onSenderReadyRead();
    void onReceiverReadyRead();
    void onSenderDisconnected();
    void onReceiverDisconnected();

private:
    // 把 from 缓冲里的字节向 to 转发；对端写队列超限时暂停，等待排空续传
    void pump(QTcpSocket *from, QTcpSocket *to);
    // 会话未齐备时缓存积压字节，超限返回 false
    bool appendBacklog(QByteArray &backlog, const QByteArray &data);

    QString _relayId;
    QTcpSocket *_sender = nullptr;
    QTcpSocket *_receiver = nullptr;
    QByteArray _senderBacklog;   // 会话未齐备前缓存的发送端字节
    QByteArray _receiverBacklog; // 会话未齐备前缓存的接收端字节
    QTimer *_idleTimer = nullptr; // 会话未齐备时的超时回收定时器
};

class RelayServer : public QObject {
    Q_OBJECT

public:
    explicit RelayServer(quint16 port, const QString &token, QObject *parent = nullptr);
    virtual ~RelayServer() override;

    bool start();
    void stop();
    quint16 serverPort() const;
    int sessionCount() const;
    // 调整并发中继会话上限（对新建会话生效）
    void setMaxSessions(int maxSessions);
    // 调整会话等待对端加入的超时（测试可调小；对新建会话生效）
    void setSessionWaitMs(int waitMs);

    // 接纳已由其他监听方完成首行握手的连接（协调节点同端口复用场景）
    void adoptConnection(QTcpSocket *socket, const QJsonObject &hello, const QByteArray &pendingData);

signals:
    void serverStarted(bool success, const QString &error);
    void sessionCreated(const QString &relayId);
    void sessionClosed(const QString &relayId);

private slots:
    void onNewConnection();
    void onSessionClosed();

private:
    // 解析中继握手并按角色加入会话
    void handleRelayHello(QTcpSocket *socket, const QJsonObject &json, const QByteArray &pendingData);
    // 丢弃未完成握手的连接
    void dropConnection(QTcpSocket *socket, const QString &reason);

    QTcpServer *_server = nullptr;
    quint16 _port = 0;
    QString _token;
    QMap<QString, RelaySession *> _sessions;
    QSet<LineSession *> _pendingHellos;  // 等待握手首行的连接
    int _maxSessions = 0;
    int _sessionWaitMs = gy::rendezvous::kRelaySessionWaitMs;  // 新建会话等待对端的超时
};
