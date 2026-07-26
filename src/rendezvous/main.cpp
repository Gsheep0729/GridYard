/**
* @file    main.cpp
* @version 7.3.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点和中继服务入口
*
* 支持两种模式：
* - 协调节点模式（默认）：提供设备注册和候选端点拉取服务
* - 中继模式：提供流式中继转发服务
*
* Change Log:
* [v7.8.1] GY   2026-07-26
* * 修复服务器对象声明在 if/else 块内导致离开作用域即析构、事件循环空转不监听的问题
* * 补充端口和运行模式参数校验
* [v7.3.0] GY   2026-07-21
* * Stage 7.3：新增流式中继服务支持
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点服务
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
    app.setApplicationVersion(QStringLiteral("7.8.1"));

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
        auto *rendezvous = new RendezvousServer{QStringLiteral("0.0.0.0"), port, token};
        server.reset(rendezvous);

        QObject::connect(rendezvous, &RendezvousServer::serverStarted, &app, [&app](bool success, const QString &error) {
            if (!success) {
                qCritical() << "[Main] 协调节点服务启动失败:" << error;
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
            } else {
                qInfo() << "[Main] GridYard 协调节点服务已启动";
            }
        });

        rendezvous->start();
    }

    return app.exec();
}