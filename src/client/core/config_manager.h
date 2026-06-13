/**
* @file    config_manager.h
* @version 4.11.0
* @date    2026-06-13
* @author  GY
* @brief   应用配置管理器（QML 单例）
*
* 使用 QSettings 管理设备名、接收路径、TCP 端口等配置。
* 首次启动生成 UUID 并持久化，确保设备标识跨会话稳定。
*
* Change Log:
* [v4.11.0] GY   2026-06-13
* * 新增自动接收并保存文件配置
* [v4.8.1] GY   2026-06-08
* * 修复设备名称更新不及时问题，单例模式实现
* [v0.3.0] GY   2026-06-03
* * 添加 localIp 属性、refreshLocalIp()、openFolder() 方法
* [v0.2.0] GY   2026-06-02
* * Stage 2：初始版本
*/

#pragma once

#include <QObject>
#include <QPointer>
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
    Q_PROPERTY(QString  localIp     READ localIp     NOTIFY localIpChanged)
    Q_PROPERTY(QString  receivePath READ receivePath WRITE setReceivePath NOTIFY receivePathChanged)
    Q_PROPERTY(bool     autoAcceptFiles READ autoAcceptFiles WRITE setAutoAcceptFiles NOTIFY autoAcceptFilesChanged)
    Q_PROPERTY(quint16  tcpPort     READ tcpPort     WRITE setTcpPort     NOTIFY tcpPortChanged)

public:
    static ConfigManager *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    ~ConfigManager() override;

    QString  deviceId()    const;
    QString  deviceName()  const;
    QString  localIp()     const;
    QString  receivePath() const;
    bool     autoAcceptFiles() const;
    quint16  tcpPort()     const;

    void setDeviceName(const QString &name);
    void setReceivePath(const QString &path);
    void setAutoAcceptFiles(bool enabled);
    void setTcpPort(quint16 port);

    Q_INVOKABLE void refreshLocalIp();
    Q_INVOKABLE void openFolder(const QString &path);

signals:
    void deviceIdChanged();
    void deviceNameChanged();
    void localIpChanged();
    void receivePathChanged();
    void autoAcceptFilesChanged();
    void tcpPortChanged();

private:
    explicit ConfigManager(QObject *parent = nullptr);
    ConfigManager(const ConfigManager &)            = delete;
    ConfigManager &operator=(const ConfigManager &) = delete;

    // 允许测试代码访问私有构造函数
    friend class TestDiscovery;
    friend class TestIntegration;

    void ensureDeviceId();

    // 全局实例指针（用于单例模式）
    static QPointer<ConfigManager> s_instance;

    QString _deviceId;
    QString _deviceName;
    QString _localIp;
    QString _receivePath;
    bool    _autoAcceptFiles = false;
    quint16 _tcpPort = 0;
};
