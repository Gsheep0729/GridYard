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

int main(int argc, char *argv[])
{
    QCoreApplication app{argc, argv};
    app.setApplicationName(QStringLiteral("gridyard-rendezvous"));
    app.setApplicationVersion(QStringLiteral("7.3.0"));

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
    const quint16 port = parser.value(portOption).toUShort();
    const QString token = parser.value(tokenOption);

    if (mode == QStringLiteral("relay")) {
        RelayServer server{port, token};

        QObject::connect(&server, &RelayServer::serverStarted, &app, [&app](bool success, const QString &error) {
            if (!success) {
                qCritical() << "[Main] 中继服务启动失败:" << error;
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
            } else {
                qInfo() << "[Main] GridYard 中继服务已启动";
            }
        });

        server.start();
    } else {
        RendezvousServer server{QStringLiteral("0.0.0.0"), port, token};

        QObject::connect(&server, &RendezvousServer::serverStarted, &app, [&app](bool success, const QString &error) {
            if (!success) {
                qCritical() << "[Main] 协调节点服务启动失败:" << error;
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
            } else {
                qInfo() << "[Main] GridYard 协调节点服务已启动";
            }
        });

        server.start();
    }

    return app.exec();
}