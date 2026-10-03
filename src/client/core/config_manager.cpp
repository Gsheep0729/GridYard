/**
* @file    config_manager.cpp
* @version 7.15.14
* @date    2026-10-04
* @author  GridYard Team
* @brief   应用配置管理器实现
*
* 实现配置的读取、写入和持久化。使用 QSettings 存储设备名、
* 接收路径、TCP 端口等配置项。支持环境变量覆盖（GRIDYARD_CONFIG、
* GRIDYARD_NAME、GRIDYARD_PORT），便于单机多实例测试。
*
* Change Log:
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
* * 新增协调服务器访问令牌配置项
* [v7.13.0] GY   2026-10-02
* * 收敛各 setter 重复的 QSettings 打开逻辑，refreshLocalIp 优先取默认路由接口地址
* [v7.4.0] GY   2026-07-21
* * 新增 Reachability 配置分组：rendezvousEnabled、rendezvousHost、rendezvousPort、relayMode
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v4.16.1] GY   2026-06-21
* * 新增 isMyDevice()、fillHelloPayload()、fillSenderInfo() 实现
* [v4.11.0] FengChunlin   2026-06-13
* * 新增自动接收并保存文件配置
* [v4.8.1] GY   2026-06-09
* * 单例模式实现，修复设备名称更新问题
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 localIp、refreshLocalIp、openFolder
* [v0.2.0] GY   2026-04-26
* * Stage 2：初始版本
*/

#include "config_manager.h"
#include "application_paths.h"
#include "protocol.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHostInfo>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QUuid>

// 静态成员变量定义
QPointer<ConfigManager> ConfigManager::s_instance;

// 获取当前生效的配置文件路径：优先 GRIDYARD_CONFIG 环境变量，其次应用数据目录
static QString resolveConfigPath()
{
    const QString envPath = qEnvironmentVariable("GRIDYARD_CONFIG");
    if (!envPath.isEmpty()) {
        return envPath;
    }
    return ApplicationPaths::configDir() + "/gridyard.ini";
}

// 打开当前配置文件，读写点各自短暂打开，避免多处拼装 QSettings
static QSettings openSettings()
{
    return QSettings(resolveConfigPath(), QSettings::IniFormat);
}

// 查询默认路由所在的网络接口名；非 Linux 或查询失败返回空
static QString defaultRouteInterface()
{
    QProcess process;
    process.start(QStringLiteral("ip"), {QStringLiteral("route"), QStringLiteral("show"), QStringLiteral("default")});
    if (!process.waitForFinished(1000)) {
        return QString();
    }

    // 输出形如 "default via 10.10.24.1 dev wlan0 proto dhcp ..."，取 dev 后的接口名
    static const QRegularExpression pattern(QStringLiteral("\\bdev\\s+(\\S+)"));
    const QRegularExpressionMatch match = pattern.match(
        QString::fromUtf8(process.readAllStandardOutput()));
    return match.hasMatch() ? match.captured(1) : QString();
}

// 构造函数：从 QSettings 加载配置，支持环境变量覆盖
ConfigManager::ConfigManager(QObject *parent)
    : QObject{parent}
{
    // 配置文件统一存放在系统配置目录
    QSettings settings(openSettings());

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
    _retentionDays = std::max(0, settings.value("history/retentionDays", 0).toInt());

    // TCP 端口：优先使用命令行参数，否则读配置
    QString envPort = qEnvironmentVariable("GRIDYARD_PORT");
    _tcpPort = envPort.isEmpty()
        ? settings.value("network/tcpPort", gy::protocol::kDefaultP2pPort).toUInt()
        : envPort.toUInt();

    // Reachability 配置：协调服务器
    _rendezvousEnabled = settings.value("reachability/rendezvousEnabled", false).toBool();
    _rendezvousHost = settings.value("reachability/rendezvousHost", "127.0.0.1").toString();
    _rendezvousPort = settings.value("reachability/rendezvousPort", gy::protocol::kDefaultRendezvousPort).toInt();
    _rendezvousToken = settings.value("reachability/rendezvousToken").toString();
    int relayModeInt = settings.value("reachability/relayMode", static_cast<int>(RelayMode::AskBeforeRelay)).toInt();
    _relayMode = static_cast<RelayMode>(relayModeInt);

    // 确保设备 ID 存在（首次启动生成 UUID 并持久化）
    ensureDeviceId();

    // 初始化本机 IP
    refreshLocalIp();
}

// 析构函数：清除静态实例指针
ConfigManager::~ConfigManager()
{
    // 清除静态实例指针
    if (s_instance == this) {
        s_instance = nullptr;
        qDebug() << "ConfigManager: 全局实例已销毁";
    }
}

// QML_SINGLETON 工厂方法，确保全局只有一个实例
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

// 获取设备 UUID
QString ConfigManager::deviceId() const
{
    return _deviceId;
}

// 获取设备名称
QString ConfigManager::deviceName() const
{
    return _deviceName;
}

// 获取文件接收路径
QString ConfigManager::receivePath() const
{
    return _receivePath;
}

// 获取自动接收文件配置
bool ConfigManager::autoAcceptFiles() const
{
    return _autoAcceptFiles;
}

// 获取 TCP 端口
quint16 ConfigManager::tcpPort() const
{
    return _tcpPort;
}

// 获取历史保留天数
int ConfigManager::retentionDays() const
{
    return _retentionDays;
}

// 获取本机 IP 地址
QString ConfigManager::localIp() const
{
    return _localIp;
}

// 刷新本机 IP 地址，优先取默认路由接口，避免多网卡或 VPN 环境选错出口
void ConfigManager::refreshLocalIp()
{
    QString newIp;
    const QString routeInterface = defaultRouteInterface();

    // 与默认路由同接口的地址才是真实出口网段
    if (!routeInterface.isEmpty()) {
        const auto interfaces = QNetworkInterface::allInterfaces();
        for (const QNetworkInterface &iface : interfaces) {
            if (iface.name() != routeInterface
                    || !(iface.flags() & QNetworkInterface::IsUp)) {
                continue;
            }
            for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol
                        && !entry.ip().isLoopback()) {
                    newIp = entry.ip().toString();
                    break;
                }
            }
            if (!newIp.isEmpty()) {
                break;
            }
        }
    }

    // 兜底：没有默认路由信息时退回第一个非回环 IPv4
    if (newIp.isEmpty()) {
        const auto addresses = QNetworkInterface::allAddresses();
        for (const QHostAddress &addr : addresses) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol
                && !addr.isLoopback()) {
                newIp = addr.toString();
                break;
            }
        }
    }

    if (_localIp != newIp) {
        _localIp = newIp;
        emit localIpChanged();
    }
}

// 打开文件夹（使用系统默认文件管理器）
void ConfigManager::openFolder(const QString &path)
{
    QDir dir(path);
    if (dir.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

// 设置设备名称并持久化
void ConfigManager::setDeviceName(const QString &name)
{
    // 值未变化时跳过
    if (_deviceName == name) return;

    _deviceName = name;

    // 持久化到 QSettings
    openSettings().setValue("device/name", name);

    // 清除环境变量影响，确保下次启动时使用 QSettings 中的值
    qunsetenv("GRIDYARD_NAME");

    qDebug() << "ConfigManager: 设备名称已更新为:" << name;

    emit deviceNameChanged();
}

// 设置文件接收路径并持久化
void ConfigManager::setReceivePath(const QString &path)
{
    if (_receivePath == path) return;

    _receivePath = path;

    // 确保目录存在
    QDir().mkpath(path);

    openSettings().setValue("device/receivePath", path);

    emit receivePathChanged();
}

// 设置自动接收文件开关并持久化
void ConfigManager::setAutoAcceptFiles(bool enabled)
{
    if (_autoAcceptFiles == enabled) return;

    _autoAcceptFiles = enabled;

    openSettings().setValue("device/autoAcceptFiles", enabled);

    emit autoAcceptFilesChanged();
}

// 设置 TCP 端口并持久化
void ConfigManager::setTcpPort(quint16 port)
{
    if (_tcpPort == port) return;

    _tcpPort = port;

    openSettings().setValue("network/tcpPort", port);

    emit tcpPortChanged();
}

// 设置历史保留天数并持久化
void ConfigManager::setRetentionDays(int days)
{
    days = std::max(0, days);
    if (_retentionDays == days) return;

    _retentionDays = days;
    openSettings().setValue("history/retentionDays", days);
    emit retentionDaysChanged();
}

// 确保设备 ID 存在（首次启动生成 UUID 并持久化）
void ConfigManager::ensureDeviceId()
{
    QSettings settings(openSettings());
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

// 判断是否是本机设备 ID
bool ConfigManager::isMyDevice(const QString &deviceId) const
{
    return _deviceId == deviceId;
}

// 填充 Hello 包数据（设备 ID、名称、端口）
void ConfigManager::fillHelloPayload(QJsonObject &json) const
{
    json["device_id"]   = _deviceId;
    json["device_name"] = _deviceName;
    json["tcp_port"]    = _tcpPort;
}

// 填充发送方信息到会话（设备 ID、名称）
void ConfigManager::fillSenderInfo(QVariantMap &session) const
{
    session["senderDeviceId"] = _deviceId;
    session["senderName"]     = _deviceName;
}

// 获取协调服务器启用状态
bool ConfigManager::rendezvousEnabled() const
{
    return _rendezvousEnabled;
}

// 获取协调服务器地址
QString ConfigManager::rendezvousHost() const
{
    return _rendezvousHost;
}

// 获取协调服务器端口
int ConfigManager::rendezvousPort() const
{
    return _rendezvousPort;
}

// 获取协调服务器访问令牌
QString ConfigManager::rendezvousToken() const
{
    return _rendezvousToken;
}

// 获取 Relay 策略
RelayMode ConfigManager::relayMode() const
{
    return _relayMode;
}

// 设置协调服务器启用状态并持久化
void ConfigManager::setRendezvousEnabled(bool enabled)
{
    if (_rendezvousEnabled == enabled) return;
    _rendezvousEnabled = enabled;
    openSettings().setValue("reachability/rendezvousEnabled", enabled);
    emit rendezvousEnabledChanged();
}

// 设置协调服务器地址并持久化
void ConfigManager::setRendezvousHost(const QString &host)
{
    if (_rendezvousHost == host) return;
    _rendezvousHost = host;
    openSettings().setValue("reachability/rendezvousHost", host);
    emit rendezvousHostChanged();
}

// 设置协调服务器端口并持久化
void ConfigManager::setRendezvousPort(int port)
{
    port = std::max(1, std::min(65535, port));
    if (_rendezvousPort == port) return;
    _rendezvousPort = port;
    openSettings().setValue("reachability/rendezvousPort", port);
    emit rendezvousPortChanged();
}

// 设置协调服务器访问令牌并持久化
void ConfigManager::setRendezvousToken(const QString &token)
{
    if (_rendezvousToken == token) return;
    _rendezvousToken = token;
    openSettings().setValue("reachability/rendezvousToken", token);
    emit rendezvousTokenChanged();
}

// 设置 Relay 策略并持久化
void ConfigManager::setRelayMode(RelayMode mode)
{
    if (_relayMode == mode) return;
    _relayMode = mode;
    openSettings().setValue("reachability/relayMode", static_cast<int>(mode));
    emit relayModeChanged();
}
