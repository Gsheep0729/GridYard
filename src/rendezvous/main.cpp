/**
* @file    main.cpp
* @version 7.2.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   协调节点服务入口
*
* 监听 TCP 连接，提供设备注册和候选端点拉取服务。
* 不保存文件内容、聊天内容和历史记录。
*
* Change Log:
* [v7.2.0] GY   2026-07-21
* * Stage 7.2：新增协调节点服务
*/

#include "rendezvous_server.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication app{argc, argv};
    app.setApplicationName(QStringLiteral("gridyard-rendezvous"));
    app.setApplicationVersion(QStringLiteral("7.2.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("GridYard 协调节点服务"));
    parser.addHelpOption();
    parser.addVersionOption();

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

    const quint16 port = parser.value(portOption).toUShort();
    const QString token = parser.value(tokenOption);

    RendezvousServer server{QStringLiteral("0.0.0.0"), port, token};

    QObject::connect(&server, &RendezvousServer::serverStarted, &app, [&app](bool success, const QString &error) {
        if (!success) {
            qCritical() << "[Main] 服务器启动失败:" << error;
            QTimer::singleShot(0, &app, [] { QCoreApplication::exit(1); });
        } else {
            qInfo() << "[Main] GridYard 协调节点服务已启动";
        }
    });

    server.start();

    return app.exec();
}