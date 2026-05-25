/**
* @file    config_manager.cpp
* @date    2026-06-02
* @author  GY
* @brief   ConfigManager 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 2：初始版本
* [v0.2] GY   2026-06-03
* * 添加 localIp、refreshLocalIp、openFolder；默认路径改为 ~/GridYard/document
*/

#include "config_manager.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QSettings>
#include <QUrl>
#include <QUuid>

ConfigManager::ConfigManager(QObject *parent)
    : QObject{parent}
{
    // 检查是否有自定义配置文件路径
    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    QSettings settings(configPath.isEmpty() ? QSettings::IniFormat : QSettings::IniFormat,
                       configPath.isEmpty() ? QSettings::UserScope : QSettings::SystemScope,
                       configPath.isEmpty() ? QString() : configPath);

    // 设备名：优先使用命令行参数，否则读配置，否则用主机名
    QString envName = qEnvironmentVariable("GRIDYARD_NAME");
    _deviceName = envName.isEmpty()
        ? settings.value("device/name", QHostInfo::localHostName()).toString()
        : envName;

    // 接收路径默认为 ~/GridYard/document，不存在则自动创建
    const QString defaultPath = QDir::homePath() + "/GridYard/document";
    _receivePath = settings.value("device/receivePath", defaultPath).toString();
    QDir().mkpath(_receivePath);

    // TCP 端口：优先使用命令行参数，否则读配置
    QString envPort = qEnvironmentVariable("GRIDYARD_PORT");
    _tcpPort = envPort.isEmpty()
        ? settings.value("network/tcpPort", gy::protocol::kDefaultP2pPort).toUInt()
        : envPort.toUInt();

    // 确保设备 ID 存在（首次启动生成 UUID 并持久化）
    ensureDeviceId();

    // 初始化本机 IP
    refreshLocalIp();
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

QString ConfigManager::localIp() const
{
    return _localIp;
}

void ConfigManager::refreshLocalIp()
{
    const auto addresses = QNetworkInterface::allAddresses();
    QString newIp;

    for (const QHostAddress &addr : addresses) {
        // 取第一个非回环 IPv4 地址
        if (addr.protocol() == QAbstractSocket::IPv4Protocol
            && !addr.isLoopback()) {
            newIp = addr.toString();
            break;
        }
    }

    if (_localIp != newIp) {
        _localIp = newIp;
        emit localIpChanged();
    }
}

void ConfigManager::openFolder(const QString &path)
{
    QDir dir(path);
    if (dir.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
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

    // 确保目录存在
    QDir().mkpath(path);

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
