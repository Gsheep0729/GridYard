/**
* @file    config_manager.cpp
* @date    2026-06-02
* @author  GY
* @brief   ConfigManager 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 2：初始版本
*/

#include "config_manager.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostInfo>
#include <QSettings>
#include <QUuid>

ConfigManager::ConfigManager(QObject *parent)
    : QObject{parent}
{
    // 从持久化存储读取配置，首次运行时使用默认值
    QSettings settings;

    // 设备名默认使用系统主机名
    _deviceName  = settings.value("device/name", QHostInfo::localHostName()).toString();
    // 接收路径默认在用户主目录下创建 GridYard 文件夹
    _receivePath = settings.value("device/receivePath", QDir::homePath() + "/GridYard").toString();
    // TCP 端口默认使用协议定义的端口
    _tcpPort     = settings.value("network/tcpPort", gy::protocol::kDefaultP2pPort).toUInt();

    // 确保设备 ID 存在（首次启动生成 UUID 并持久化）
    ensureDeviceId();
}

ConfigManager *ConfigManager::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    return new ConfigManager{};
}

QString ConfigManager::deviceId() const
{
    return _deviceId;
}

QString ConfigManager::deviceName() const
{
    return _deviceName;
}

QString ConfigManager::receivePath() const
{
    return _receivePath;
}

quint16 ConfigManager::tcpPort() const
{
    return _tcpPort;
}

void ConfigManager::setDeviceName(const QString &name)
{
    // 值未变化时跳过
    if (_deviceName == name) return;

    _deviceName = name;

    // 持久化到 QSettings
    QSettings settings;
    settings.setValue("device/name", name);

    emit deviceNameChanged();
}

void ConfigManager::setReceivePath(const QString &path)
{
    if (_receivePath == path) return;

    _receivePath = path;

    QSettings settings;
    settings.setValue("device/receivePath", path);

    emit receivePathChanged();
}

void ConfigManager::setTcpPort(quint16 port)
{
    if (_tcpPort == port) return;

    _tcpPort = port;

    QSettings settings;
    settings.setValue("network/tcpPort", port);

    emit tcpPortChanged();
}

void ConfigManager::ensureDeviceId()
{
    QSettings settings;
    _deviceId = settings.value("device/id").toString();

    // 首次运行时生成 UUID 并持久化，确保设备标识跨会话稳定
    if (_deviceId.isEmpty()) {
        _deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("device/id", _deviceId);
    }
}
