/**
* @file    config_manager.cpp
* @version 4.11.0
* @date    2026-06-13
* @author  GridYard Team
* @brief   ConfigManager 实现
*
* Change Log:
* [v4.11.0] GY   2026-06-13
* * 新增自动接收并保存文件配置
* [v4.8.1] GY   2026-06-08
* * 单例模式实现，修复设备名称更新问题
* [v0.3.0] GY   2026-06-03
* * 添加 localIp、refreshLocalIp、openFolder
* [v0.2.0] GY   2026-06-02
* * Stage 2：初始版本
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

// 静态成员变量定义
QPointer<ConfigManager> ConfigManager::s_instance;

ConfigManager::ConfigManager(QObject *parent)
    : QObject{parent}
{
    // 检查是否有自定义配置文件路径
    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");

    // 创建 QSettings 对象
    // 如果指定了配置文件路径，直接使用该文件；否则使用系统默认路径
    QSettings settings(configPath.isEmpty() ? QSettings() : QSettings(configPath, QSettings::IniFormat));

    // 设备名：优先使用命令行参数，否则读配置，否则用主机名
    QString envName = qEnvironmentVariable("GRIDYARD_NAME");
    _deviceName = envName.isEmpty()
        ? settings.value("device/name", QHostInfo::localHostName()).toString()
        : envName;

    // 接收路径默认为 ~/GridYard/document，不存在则自动创建
    const QString defaultPath = QDir::homePath() + "/GridYard/document";
    _receivePath = settings.value("device/receivePath", defaultPath).toString();
    QDir().mkpath(_receivePath);
    _autoAcceptFiles = settings.value("device/autoAcceptFiles", false).toBool();

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

ConfigManager::~ConfigManager()
{
    // 清除静态实例指针
    if (s_instance == this) {
        s_instance = nullptr;
        qDebug() << "ConfigManager: 全局实例已销毁";
    }
}

ConfigManager *ConfigManager::create(QQmlEngine *engine, QJSEngine *)
{
    Q_UNUSED(engine);
    // 使用静态变量确保全局只有一个实例
    if (!s_instance) {
        s_instance = new ConfigManager{};
        qDebug() << "ConfigManager: 创建全局实例";
    }
    return s_instance;
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

bool ConfigManager::autoAcceptFiles() const
{
    return _autoAcceptFiles;
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
    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    QSettings settings(configPath.isEmpty() ? QSettings() : QSettings(configPath, QSettings::IniFormat));
    settings.setValue("device/name", name);

    // 清除环境变量影响，确保下次启动时使用 QSettings 中的值
    qunsetenv("GRIDYARD_NAME");

    qDebug() << "ConfigManager: 设备名称已更新为:" << name;

    emit deviceNameChanged();
}

void ConfigManager::setReceivePath(const QString &path)
{
    if (_receivePath == path) return;

    _receivePath = path;

    // 确保目录存在
    QDir().mkpath(path);

    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    QSettings settings(configPath.isEmpty() ? QSettings() : QSettings(configPath, QSettings::IniFormat));
    settings.setValue("device/receivePath", path);

    emit receivePathChanged();
}

void ConfigManager::setAutoAcceptFiles(bool enabled)
{
    if (_autoAcceptFiles == enabled) return;

    _autoAcceptFiles = enabled;

    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    QSettings settings(configPath.isEmpty() ? QSettings() : QSettings(configPath, QSettings::IniFormat));
    settings.setValue("device/autoAcceptFiles", enabled);

    emit autoAcceptFilesChanged();
}

void ConfigManager::setTcpPort(quint16 port)
{
    if (_tcpPort == port) return;

    _tcpPort = port;

    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    QSettings settings(configPath.isEmpty() ? QSettings() : QSettings(configPath, QSettings::IniFormat));
    settings.setValue("network/tcpPort", port);

    emit tcpPortChanged();
}

void ConfigManager::ensureDeviceId()
{
    QString configPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    QSettings settings(configPath.isEmpty() ? QSettings() : QSettings(configPath, QSettings::IniFormat));
    _deviceId = settings.value("device/id").toString();

    // 首次运行时生成 UUID 并持久化，确保设备标识跨会话稳定
    if (_deviceId.isEmpty()) {
        _deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        settings.setValue("device/id", _deviceId);
        qDebug() << "ConfigManager: 生成新的 deviceId:" << _deviceId;
    } else {
        qDebug() << "ConfigManager: 使用已有的 deviceId:" << _deviceId;
    }
}
