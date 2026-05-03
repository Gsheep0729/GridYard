/**
* @file    config_manager.h
* @date    2026-06-02
* @author  GY
* @brief   应用配置管理器（QML 单例）
*
* 使用 QSettings 管理设备名、接收路径、TCP 端口等配置。
* 首次启动生成 UUID 并持久化，确保设备标识跨会话稳定。
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 2：初始版本
*/

#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

class ConfigManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString  deviceId    READ deviceId    NOTIFY deviceIdChanged)
    Q_PROPERTY(QString  deviceName  READ deviceName  WRITE setDeviceName  NOTIFY deviceNameChanged)
    Q_PROPERTY(QString  receivePath READ receivePath WRITE setReceivePath NOTIFY receivePathChanged)
    Q_PROPERTY(quint16  tcpPort     READ tcpPort     WRITE setTcpPort     NOTIFY tcpPortChanged)

public:
    static ConfigManager *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    QString  deviceId()    const;
    QString  deviceName()  const;
    QString  receivePath() const;
    quint16  tcpPort()     const;

    void setDeviceName(const QString &name);
    void setReceivePath(const QString &path);
    void setTcpPort(quint16 port);

signals:
    void deviceIdChanged();
    void deviceNameChanged();
    void receivePathChanged();
    void tcpPortChanged();

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ConfigManager(const ConfigManager &)            = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    void ensureDeviceId();

    QString _deviceId;
    QString _deviceName;
    QString _receivePath;
    quint16 _tcpPort = 0;
};
