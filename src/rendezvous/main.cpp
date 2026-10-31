/**
* @file    main.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GridYard Team
* @brief   协调节点和中继服务入口
*
* 支持两种模式：
* - 协调节点模式（默认）：同端口提供设备注册、候选端点拉取、中继邀请信令
*   和流式中继转发（按握手类型分流）
* - 中继模式：仅提供流式中继转发服务
*/

#include "rendezvous_server.h"
#include "relay_server.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <memory>

int main(int argc, char *argv[])
{
    QCoreApplication app{argc, argv};
    app.setApplicationName(QStringLiteral("gridyard-rendezvous"));
    app.setApplicationVersion(QStringLiteral("7.14.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("GridYard 协调节点/中继服务"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption modeOption(
        QStringList() << QStringLiteral("m") << QStringLiteral("mode"),
        QStringLiteral("运行模式：rendezvous=协调节点，relay=中继"),
        QStringLiteral("mode"),
        QStringLiteral("rendezvous")
    );
    parser.addOption(modeOption);

    QCommandLineOption portOption(
        QStringList() << QStringLiteral("p") << QStringLiteral("port"),
        QStringLiteral("监听端口"),
        QStringLiteral("port"),
        QStringLiteral("45679")
    );
    parser.addOption(portOption);

    QCommandLineOption tokenOption(
        QStringList() << QStringLiteral("t") << QStringLiteral("token"),
        QStringLiteral("访问口令"),
        QStringLiteral("token"),
        QString()
    );
    parser.addOption(tokenOption);

    parser.process(app);

    const QString mode = parser.value(modeOption);
    const QString token = parser.value(tokenOption);

    // 端口必须能解析为 1~65535，否则 toUShort 返回 0 会让 OS 分配随机端口，
    // 用户以为服务在指定端口实则监听在别处
    bool portOk = false;
    const quint16 port = parser.value(portOption).toUShort(&portOk);
    if (!portOk || port == 0) {
        qCritical() << "[Main] 无效的端口参数:" << parser.value(portOption);
        return 1;
    }

    // 只接受 rendezvous / relay 两种模式，拼写错误不再静默回退
    if (mode != QStringLiteral("rendezvous") && mode != QStringLiteral("relay")) {
        qCritical() << "[Main] 无效的运行模式:" << mode << "（可选 rendezvous 或 relay）";
        return 1;
    }

    // server 必须活过 app.exec()，用 unique_ptr 持有到 main 作用域，
    // 不能声明在 if/else 块内——那样离开块即析构，事件循环会跑在没有监听套接字的空壳上
    std::unique_ptr<QObject> server;

    if (mode == QStringLiteral("relay")) {
        auto *relay = new RelayServer{port, token};
        server.reset(relay);

        QObject::connect(relay, &RelayServer::serverStarted, &app, [&app](bool success, const QString &error) {
            if (!success) {
                qCritical() << "[Main] 中继服务启动失败:" << error;
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
            } else {
                qInfo() << "[Main] GridYard 中继服务已启动";
            }
        });

        relay->start();
    } else {
        auto *rendezvous = new RendezvousServer{port, token};
        server.reset(rendezvous);

        // 协调节点模式同端口内置中继：分流出的 relay_create/relay_join 连接
        // 移交给该实例（不独立监听，仅承接），校园网只需开放一个端口
        auto *relay = new RelayServer{port, token, rendezvous};
        QObject::connect(rendezvous, &RendezvousServer::relayPipeRequested,
                         relay,    &RelayServer::adoptConnection);

        QObject::connect(rendezvous, &RendezvousServer::serverStarted, &app, [&app](bool success, const QString &error) {
            if (!success) {
                qCritical() << "[Main] 协调节点服务启动失败:" << error;
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
            } else {
                qInfo() << "[Main] GridYard 协调节点服务已启动（同端口内置中继）";
            }
        });

        rendezvous->start();
    }

    return app.exec();
}
