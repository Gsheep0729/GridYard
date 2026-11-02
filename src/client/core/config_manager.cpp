/**
* @file    config_manager.cpp
* @version 8.3.0
* @date    2026-11-03
* @author  GridYard Team
* @brief   应用配置管理器实现
*
* 实现配置的读取、写入和持久化。内部经纯 C++ 的 ConfigStore 读写 INI
* 文件（语义与 QSettings(IniFormat) 逐字节兼容，旧配置直接可读），对外
* 保持 Qt 属性接口不变。支持环境变量覆盖（GRIDYARD_CONFIG、
* GRIDYARD_NAME、GRIDYARD_PORT、GRIDYARD_INSTANCE），便于单机多实例测试。
*/

#include "config_manager.h"
#include "application_paths.h"
#include "protocol.h"

#include "core/config/config_store.h"
#include "net_interfaces.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

#ifdef _WIN32
// Windows 头链把 interface 定义成宏，展开会击穿下方以 interface 命名的循环变量
#undef interface
#endif

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

// 打开当前配置文件的纯 C++ 存取器，读写点各自短暂加载
static gy::config::ConfigStore openConfigStore()
{
    return gy::config::ConfigStore{resolveConfigPath().toStdString()};
}

// 读取字符串配置，键不存在回落默认值
static QString readConfigString(gy::config::ConfigStore &store, const char *group,
                                const char *key, const QString &defaultValue)
{
    const auto value = store.get(group, key);
    return value ? QString::fromStdString(*value) : defaultValue;
}

// 读取整数配置，键不存在或非数字回落默认值
static int readConfigInt(gy::config::ConfigStore &store, const char *group,
                         const char *key, int defaultValue)
{
    return static_cast<int>(store.getInt(group, key).value_or(defaultValue));
}

// 读取布尔配置，键不存在或无法识别回落默认值
static bool readConfigBool(gy::config::ConfigStore &store, const char *group,
                           const char *key, bool defaultValue)
{
    return store.getBool(group, key).value_or(defaultValue);
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

// 构造函数：从配置文件加载配置，支持环境变量覆盖
ConfigManager::ConfigManager(QObject *parent)
    : QObject{parent}
{
    // 配置文件统一存放在系统配置目录
    gy::config::ConfigStore store = openConfigStore();

    // 设备名：优先使用命令行参数，否则读配置，否则用主机名
    QString envName = qEnvironmentVariable("GRIDYARD_NAME");
    _deviceName = envName.isEmpty()
        ? readConfigString(store, "device", "name", QSysInfo::machineHostName())
        : envName;

    // 接收路径默认为 ~/GridYard/document，不存在则自动创建
    const QString defaultPath = QDir::homePath() + "/GridYard/document";
    _receivePath = readConfigString(store, "device", "receivePath", defaultPath);
    QDir().mkpath(_receivePath);
    _autoAcceptFiles = readConfigBool(store, "device", "autoAcceptFiles", false);
    // 勿扰接收：默认关闭，开启期间新传输请求走自动拒绝
    _doNotDisturb = readConfigBool(store, "device", "doNotDisturb", false);
    _retentionDays = std::max(0, readConfigInt(store, "history", "retentionDays", 0));

    // 开发者模式实例号：正常模式为 0，仅用于本机资源隔离（目录后缀与端口偏移）
    _instanceNumber = ApplicationPaths::instanceNumber();

    // TCP 端口：优先使用命令行参数，否则读配置；
    // 开发者实例的默认端口按实例号确定性偏移（+10N），已保存的配置仍然优先
    QString envPort = qEnvironmentVariable("GRIDYARD_PORT");
    _tcpPort = envPort.isEmpty()
        ? static_cast<quint16>(readConfigInt(store, "network", "tcpPort",
                                             gy::protocol::instanceP2pPort(_instanceNumber)))
        : envPort.toUInt();
    // 身份播报端口在启动期定型：P2P 监听器只在启动时按配置端口绑定一次，
    // 运行期保存的新端口要等重启才真正生效，播报必须与监听器同源同刻，
    // 否则保存后每 5 秒的 Hello 都在向全网宣告一个无人监听的端口
    _announcedTcpPort = _tcpPort;

    // Reachability 配置：协调服务器
    _rendezvousEnabled = readConfigBool(store, "reachability", "rendezvousEnabled", false);
    _rendezvousHost = readConfigString(store, "reachability", "rendezvousHost", QStringLiteral("127.0.0.1"));
    _rendezvousPort = readConfigInt(store, "reachability", "rendezvousPort",
                                    gy::protocol::kDefaultRendezvousPort);
    _rendezvousToken = readConfigString(store, "reachability", "rendezvousToken", QString());
    _relayMode = static_cast<RelayMode>(readConfigInt(
        store, "reachability", "relayMode", static_cast<int>(RelayMode::AskBeforeRelay)));

    // 关窗行为：默认每次关窗弹确认窗，勾选"记住我的选择"后写入选中动作
    _closeWindowAction = static_cast<CloseWindowAction>(readConfigInt(
        store, "window", "closeWindowAction", static_cast<int>(CloseWindowAction::Ask)));

    // 开发者模式启动入口开关：默认关闭，仅控制设置页"启动新实例"入口可见性，
    // 不改变当前实例的任何运行属性
    _developerLaunchEntryEnabled = readConfigBool(store, "developer", "launchEntryEnabled", false);

    // 开机自启意图：仅记录用户选择，桌面项的写/删随开关动作落盘
    _autostartEnabled = readConfigBool(store, "startup", "autostartEnabled", false);

    // 确保设备 ID 存在（首次启动生成 UUID 并持久化）
    ensureDeviceId();

    // 初始化本机 IP
    refreshLocalIp();

    // 开机自启对账：意图为开时修复缺失/陈旧桌面项
    reconcileAutostartEntry();
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

// 获取身份播报用 TCP 端口（启动期定型快照）
quint16 ConfigManager::announcedTcpPort() const
{
    return _announcedTcpPort;
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

// 备份恢复身份：仅写入配置文件，运行期保持当前身份不变，重启后生效
void ConfigManager::restoreDeviceIdentity(const QString &deviceId, const QString &deviceName)
{
    gy::config::ConfigStore store = openConfigStore();
    store.set("device", "id", deviceId.toStdString());
    if (!deviceName.isEmpty()) {
        store.set("device", "name", deviceName.toStdString());
    }
}

// 刷新本机 IP 地址，优先取默认路由接口，避免多网卡或 VPN 环境选错出口
void ConfigManager::refreshLocalIp()
{
    QString newIp;
    const QString routeInterface = defaultRouteInterface();

    // 与默认路由同接口的地址才是真实出口网段
    if (!routeInterface.isEmpty()) {
        const QList<gy::net::NetInterface> interfaces = gy::net::listInterfaces();
        for (const gy::net::NetInterface &interface : interfaces) {
            if (interface.name != routeInterface || !interface.isUp || interface.isLoopback) {
                continue;
            }
            if (interface.address.isValid()) {
                newIp = interface.address.toString();
                break;
            }
        }
    }

    // 兜底：没有默认路由信息时退回第一个非回环 IPv4
    if (newIp.isEmpty()) {
        const QList<gy::net::IpAddress> addresses = gy::net::listIpv4Addresses();
        for (const gy::net::IpAddress &address : addresses) {
            if (!address.isNull() && !address.isLoopback()) {
                newIp = address.toString();
                break;
            }
        }
    }

    if (_localIp != newIp) {
        _localIp = newIp;
        emit localIpChanged();
    }
}

// 打开路径所在位置（使用系统默认文件管理器）：目录直接打开，文件打开其所在目录
void ConfigManager::openFolder(const QString &path)
{
    const QFileInfo info(path);
    if (info.isDir()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    } else if (info.isFile()) {
        // 单文件接收的落盘路径指向文件本身，定位到其所在目录
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
    }
}

// 设置设备名称并持久化
void ConfigManager::setDeviceName(const QString &name)
{
    // 值未变化时跳过
    if (_deviceName == name) return;

    _deviceName = name;

    // 持久化到配置文件
    openConfigStore().set("device", "name", name.toStdString());

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

    openConfigStore().set("device", "receivePath", path.toStdString());

    emit receivePathChanged();
}

// 校验接收路径是否可用：可创建/已存在且可写返回空串，否则返回可直接展示的
// 原因；供设置页保存前行内反馈，避免无效路径静默写入配置、延后到接受传输时
// 才以「无法创建接收目录」失败（自动接收模式下用户甚至不在场）
QString ConfigManager::receivePathError(const QString &path) const
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return tr("接收路径不能为空");
    }

    // 路径已被普通文件占用：mkpath 必失败，提前给出可读原因
    if (QFileInfo::exists(trimmed) && !QFileInfo(trimmed).isDir()) {
        const QString reason = tr("该路径已被文件占用，无法用作接收目录");
        qWarning() << "ConfigManager: 接收路径校验失败:" << trimmed << "-" << reason;
        return reason;
    }

    // 与 setReceivePath 的落盘前准备一致：尝试创建目录（已存在时 mkpath 直接成功）
    if (!QDir().mkpath(trimmed)) {
        const QString reason = tr("无法创建接收目录，请检查路径是否正确");
        qWarning() << "ConfigManager: 接收路径校验失败:" << trimmed << "-" << reason;
        return reason;
    }

    // 目录已存在但不可写（如只读挂载、权限收紧）：接收时必然写失败
    if (!QFileInfo(trimmed).isWritable()) {
        const QString reason = tr("接收目录不可写，请检查目录权限");
        qWarning() << "ConfigManager: 接收路径校验失败:" << trimmed << "-" << reason;
        return reason;
    }

    return QString();
}

// 设置自动接收文件开关并持久化
void ConfigManager::setAutoAcceptFiles(bool enabled)
{
    if (_autoAcceptFiles == enabled) return;

    _autoAcceptFiles = enabled;

    openConfigStore().setBool("device", "autoAcceptFiles", enabled);

    emit autoAcceptFilesChanged();
}

// 获取勿扰接收配置
bool ConfigManager::doNotDisturb() const
{
    return _doNotDisturb;
}

// 设置勿扰接收开关并持久化
void ConfigManager::setDoNotDisturb(bool enabled)
{
    if (_doNotDisturb == enabled) return;

    _doNotDisturb = enabled;

    openConfigStore().setBool("device", "doNotDisturb", enabled);

    emit doNotDisturbChanged();
}

// 设置 TCP 端口并持久化
void ConfigManager::setTcpPort(quint16 port)
{
    if (_tcpPort == port) return;

    _tcpPort = port;

    openConfigStore().setInt("network", "tcpPort", port);

    emit tcpPortChanged();
}

// 设置历史保留天数并持久化
void ConfigManager::setRetentionDays(int days)
{
    days = std::max(0, days);
    if (_retentionDays == days) return;

    _retentionDays = days;
    openConfigStore().setInt("history", "retentionDays", days);
    emit retentionDaysChanged();
}

// 确保设备 ID 存在（首次启动生成 UUID 并持久化）
void ConfigManager::ensureDeviceId()
{
    gy::config::ConfigStore store = openConfigStore();
    const auto savedId = store.get("device", "id");
    _deviceId = savedId ? QString::fromStdString(*savedId) : QString();

    // 首次运行时生成 UUID 并持久化，确保设备标识跨会话稳定
    if (_deviceId.isEmpty()) {
        _deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        store.set("device", "id", _deviceId.toStdString());
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
    // 播报启动期定型的端口：监听器未随保存切换前，宣告新端口即宣告无人监听
    json["tcp_port"]    = _announcedTcpPort;
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

// 获取关窗行为配置
CloseWindowAction ConfigManager::closeWindowAction() const
{
    return _closeWindowAction;
}

// 获取开发者模式实例号（0 = 正常模式）
int ConfigManager::instanceNumber() const
{
    return _instanceNumber;
}

// 获取开发者模式启动入口开关
bool ConfigManager::developerLaunchEntryEnabled() const
{
    return _developerLaunchEntryEnabled;
}

// 设置协调服务器启用状态并持久化
void ConfigManager::setRendezvousEnabled(bool enabled)
{
    if (_rendezvousEnabled == enabled) return;
    _rendezvousEnabled = enabled;
    openConfigStore().setBool("reachability", "rendezvousEnabled", enabled);
    emit rendezvousEnabledChanged();
}

// 设置协调服务器地址并持久化
void ConfigManager::setRendezvousHost(const QString &host)
{
    if (_rendezvousHost == host) return;
    _rendezvousHost = host;
    openConfigStore().set("reachability", "rendezvousHost", host.toStdString());
    emit rendezvousHostChanged();
}

// 设置协调服务器端口并持久化
void ConfigManager::setRendezvousPort(int port)
{
    port = std::max(1, std::min(65535, port));
    if (_rendezvousPort == port) return;
    _rendezvousPort = port;
    openConfigStore().setInt("reachability", "rendezvousPort", port);
    emit rendezvousPortChanged();
}

// 设置协调服务器访问令牌并持久化
void ConfigManager::setRendezvousToken(const QString &token)
{
    if (_rendezvousToken == token) return;
    _rendezvousToken = token;
    openConfigStore().set("reachability", "rendezvousToken", token.toStdString());
    emit rendezvousTokenChanged();
}

// 设置 Relay 策略并持久化
void ConfigManager::setRelayMode(RelayMode mode)
{
    if (_relayMode == mode) return;
    _relayMode = mode;
    openConfigStore().setInt("reachability", "relayMode", static_cast<int>(mode));
    emit relayModeChanged();
}

// 设置关窗行为并持久化
void ConfigManager::setCloseWindowAction(CloseWindowAction action)
{
    if (_closeWindowAction == action) return;
    _closeWindowAction = action;
    openConfigStore().setInt("window", "closeWindowAction", static_cast<int>(action));
    emit closeWindowActionChanged();
}

// 设置开发者模式启动入口开关并持久化
void ConfigManager::setDeveloperLaunchEntryEnabled(bool enabled)
{
    if (_developerLaunchEntryEnabled == enabled) return;
    _developerLaunchEntryEnabled = enabled;
    openConfigStore().setBool("developer", "launchEntryEnabled", enabled);
    emit developerLaunchEntryEnabledChanged();
}

// 获取开机自启开关（持久化意图）
bool ConfigManager::autostartEnabled() const
{
    return _autostartEnabled;
}

// 应用开机自启开关：先落桌面项，成功才持久化意图并广播 NOTIFY；
// 失败返回可直接展示的原因，配置意图保持原值（禁静默假成功）
QString ConfigManager::setAutostartEnabled(bool enabled)
{
    if (_autostartEnabled == enabled) {
        return QString();
    }

    const QString error = applyAutostartEntry(enabled);
    if (!error.isEmpty()) {
        return error;  // 写失败：意图保持原值，原因交由设置页行内展示
    }

    _autostartEnabled = enabled;
    openConfigStore().setBool("startup", "autostartEnabled", enabled);
    emit autostartEnabledChanged();
    qDebug() << "ConfigManager: 开机自启已" << (enabled ? "开启" : "关闭");
    return QString();
}

// 桌面项文件名：实例 0 为 gridyard.desktop，开发者实例独立命名，
// 两个实例的自启项互不覆盖（Exec 各自指向同一二进制但实例号不同）
QString ConfigManager::autostartEntryFileName()
{
    const int instance = qBound(0, ApplicationPaths::instanceNumber(),
                                gy::protocol::kMaxInstanceNumber);
    if (instance <= 0) {
        return QStringLiteral("gridyard.desktop");
    }
    return QStringLiteral("gridyard-dev%1.desktop").arg(instance);
}

// 桌面项目录：XDG autostart 标准位置，尊重 XDG_CONFIG_HOME（测试重定向依赖此点）
QString ConfigManager::autostartDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty()) {
        return QDir::home().filePath(".config/autostart");
    }
    return base + "/autostart";
}

// 桌面项完整路径
QString ConfigManager::autostartEntryPath()
{
    return autostartDirectory() + '/' + autostartEntryFileName();
}

// 桌面项内容：以 dist/AppDir/gridyard.desktop 为模板，Exec 回填 applicationFilePath
// 绝对路径（拍板备注：安装位置漂移时自启不得失效），开发者实例追加 --instance=N
QString ConfigManager::autostartEntryContent()
{
    const int instance = qBound(0, ApplicationPaths::instanceNumber(),
                                gy::protocol::kMaxInstanceNumber);
    QString exec = QCoreApplication::applicationFilePath();
    // Desktop Entry 规范：参数可双引号包裹，内嵌引号与反斜杠须转义（路径含空格也安全）
    exec.replace('\\', QStringLiteral("\\\\"));
    exec.replace('"', QStringLiteral("\\\""));
    QString execLine = QStringLiteral("Exec=\"%1\"").arg(exec);
    if (instance > 0) {
        execLine += QStringLiteral(" --instance=%1").arg(instance);
    }

    return QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=GridYard\n"
        "GenericName=LAN P2P File Transfer\n"
        "Comment=GridYard 局域网 P2P 文件传输、在线聊天与本地历史\n"
        "Comment[zh_CN]=GridYard 局域网 P2P 文件传输、在线聊天与本地历史\n"
        "%1\n"
        "Icon=gridyard\n"
        "Categories=Network;FileTransfer;P2P;\n"
        "Terminal=false\n"
        "StartupWMClass=GridYard\n"
        "Keywords=gridyard;p2p;lan;file;chat;\n").arg(execLine);
}

// 写入/移除桌面项（不碰配置意图）：成功返回空串，失败返回可直接展示的原因
QString ConfigManager::applyAutostartEntry(bool present)
{
    const QString path = autostartEntryPath();

    if (!present) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            const QString reason = QStringLiteral("无法删除自启项 %1，请检查文件权限").arg(path);
            qWarning() << "ConfigManager:" << reason;
            return reason;
        }
        return QString();
    }

    // 目录不存在则创建（首次开启时 ~/.config/autostart 可能还没有）
    const QString dir = autostartDirectory();
    if (!QDir().mkpath(dir)) {
        const QString reason = QStringLiteral("无法创建自启目录 %1，请检查目录权限").arg(dir);
        qWarning() << "ConfigManager:" << reason;
        return reason;
    }

    // QSaveFile 临时文件加原子改名：中断不会留下半截桌面项冒充可用自启
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)
        || file.write(autostartEntryContent().toUtf8()) < 0
        || !file.commit()) {
        const QString reason = QStringLiteral("无法写入自启项 %1，请检查目录权限").arg(path);
        qWarning() << "ConfigManager:" << reason << "-" << file.errorString();
        return reason;
    }
    return QString();
}

// 启动对账：意图为开时确保桌面项存在且内容与当前可执行文件一致——
// AppImage/deb 更新或安装位置变化后，陈旧 Exec 会在登录时拉起已失效路径；
// 意图为关时启动期不删除（桌面项可能由外部工具维护），关闭只在开关动作时落盘
void ConfigManager::reconcileAutostartEntry()
{
    if (!_autostartEnabled) {
        return;
    }

    const QString path = autostartEntryPath();
    QString current;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text)) {
        current = QString::fromUtf8(existing.readAll());
    }
    if (current == autostartEntryContent()) {
        return;  // 桌面项已是当前形态，避免每次启动无谓重写
    }

    const QString error = applyAutostartEntry(true);
    if (!error.isEmpty()) {
        qWarning() << "ConfigManager: 开机自启桌面项对账失败，自启可能失效:" << path
                   << "-" << error;
    }
}

// 关窗动作决策：ask 弹确认窗，hide 直接隐藏，exit 直接退出；
// 记住退出后仍有活动传输时返回 confirm，由表现层弹警示确认窗拦截
QString ConfigManager::resolveWindowCloseAction(CloseWindowAction action, int activeSessionCount)
{
    switch (action) {
    case CloseWindowAction::Hide:
        return QStringLiteral("hide");
    case CloseWindowAction::Exit:
        // 退出防护（A6）不随记忆豁免：有活动传输时仍需一次警示确认
        return activeSessionCount > 0 ? QStringLiteral("confirm") : QStringLiteral("exit");
    case CloseWindowAction::Ask:
    default:
        return QStringLiteral("ask");
    }
}

// "启动新实例"的实例号分配：当前实例号 + 1 确定性递增；
// 实例的目录后缀与端口偏移都由实例号唯一推导，选递增而非"最小空闲探测"，
// 是为了不在启动路径里做端口试探（有竞态且依赖本机绑定权限），同号重复拉起
// 由既有 bind 回退路径兜底；到达上限 9 返回 -1，供表现层给出行内提示
int ConfigManager::nextLaunchInstance(int currentInstance)
{
    const int current = qBound(0, currentInstance, gy::protocol::kMaxInstanceNumber);
    if (current >= gy::protocol::kMaxInstanceNumber) {
        return -1;
    }
    return current + 1;
}

// 持久化窗口几何：window 组键随写随原子落盘（ConfigStore 每次 set 即刷盘），
// QML 侧去抖后调用，SIGTERM/托盘退出/关窗退出等任一退出路径都不丢
void ConfigManager::saveWindowGeometry(int x, int y, int width, int height, bool maximized)
{
    gy::config::ConfigStore store = openConfigStore();
    store.setInt("window", "x", x);
    store.setInt("window", "y", y);
    store.setInt("window", "width", width);
    store.setInt("window", "height", height);
    store.setBool("window", "maximized", maximized);
}

// 窗口几何夹紧：尺寸抬到最小尺寸、压到所有屏幕可用区能容纳的最大宽高；
// 位置与任一屏幕可用区相交达窗口面积三成才算可见（阈值保证标题栏与主体
// 在屏，只露一条边仍判为屏外），不满足即放弃位置交由窗口管理器自选；
// 可用区列表为空（无 GUI 环境）时不做夹紧原样返回，位置以保存为准
QVariantMap ConfigManager::clampWindowGeometry(const QVariantMap &saved,
                                               int defaultWidth, int defaultHeight,
                                               int minimumWidth, int minimumHeight,
                                               const QList<QRect> &availableScreens)
{
    QVariantMap result;
    result.insert(QStringLiteral("maximized"),
                  saved.value(QStringLiteral("maximized")).toBool());

    QList<QRect> screens;
    for (const QRect &rect : availableScreens) {
        if (rect.isValid()) {
            screens.append(rect);
        }
    }

    const int savedWidth = saved.value(QStringLiteral("width")).toInt();
    const int savedHeight = saved.value(QStringLiteral("height")).toInt();

    // 无有效保存尺寸（首次启动或配置缺键）：默认尺寸且不指定位置
    if (savedWidth <= 0 || savedHeight <= 0) {
        result.insert(QStringLiteral("width"), defaultWidth);
        result.insert(QStringLiteral("height"), defaultHeight);
        result.insert(QStringLiteral("hasPosition"), false);
        return result;
    }

    // 屏幕信息不可得（无 GUI 环境）：不做夹紧原样返回，交由上层处置
    if (screens.isEmpty()) {
        result.insert(QStringLiteral("width"), savedWidth);
        result.insert(QStringLiteral("height"), savedHeight);
        const bool hasSavedPosition = saved.contains(QStringLiteral("x"))
                && saved.contains(QStringLiteral("y"));
        if (hasSavedPosition) {
            result.insert(QStringLiteral("x"), saved.value(QStringLiteral("x")));
            result.insert(QStringLiteral("y"), saved.value(QStringLiteral("y")));
        }
        result.insert(QStringLiteral("hasPosition"), hasSavedPosition);
        return result;
    }

    // 尺寸夹紧：不小于最小尺寸，不大于所有屏幕可用区的最大可容纳宽高
    int maxWidth = minimumWidth;
    int maxHeight = minimumHeight;
    for (const QRect &screen : screens) {
        maxWidth = qMax(maxWidth, screen.width());
        maxHeight = qMax(maxHeight, screen.height());
    }
    const int width = qBound(minimumWidth, savedWidth, maxWidth);
    const int height = qBound(minimumHeight, savedHeight, maxHeight);
    result.insert(QStringLiteral("width"), width);
    result.insert(QStringLiteral("height"), height);

    // 位置可用性：保存矩形与某屏幕可用区的相交面积须达窗口面积三成，
    // 否则视为落屏外（多显示器拔插的典型残留），不给出位置键
    const bool hasSavedPosition = saved.contains(QStringLiteral("x"))
            && saved.contains(QStringLiteral("y"));
    bool hasPosition = false;
    if (hasSavedPosition) {
        const QRect savedRect(saved.value(QStringLiteral("x")).toInt(),
                              saved.value(QStringLiteral("y")).toInt(), width, height);
        const qint64 windowArea = qint64(width) * height;
        qint64 bestOverlap = 0;
        for (const QRect &screen : screens) {
            const QRect intersection = savedRect.intersected(screen);
            bestOverlap = qMax(bestOverlap,
                               qint64(intersection.width()) * intersection.height());
        }
        hasPosition = bestOverlap * 10 >= windowArea * 3;
    }
    if (hasPosition) {
        result.insert(QStringLiteral("x"), saved.value(QStringLiteral("x")));
        result.insert(QStringLiteral("y"), saved.value(QStringLiteral("y")));
    }
    result.insert(QStringLiteral("hasPosition"), hasPosition);
    return result;
}

// 读取保存的窗口几何并按屏幕可用区夹紧：availableScreens 缺省为空列表时取
// 当前全部屏幕的可用区（QGuiApplication 环境；纯 Core 测试环境屏幕列表为空，
// 夹紧自动退化为原样返回），传入非空列表则以传入为准（测试确定性）
QVariantMap ConfigManager::restoreWindowGeometry(int defaultWidth, int defaultHeight,
                                                 int minimumWidth, int minimumHeight,
                                                 const QVariantList &availableScreens)
{
    gy::config::ConfigStore store = openConfigStore();
    QVariantMap saved;
    const auto readInt = [&store, &saved](const char *key) {
        if (const auto value = store.getInt("window", key)) {
            saved.insert(QLatin1String(key), static_cast<int>(*value));
        }
    };
    readInt("x");
    readInt("y");
    readInt("width");
    readInt("height");
    if (const auto value = store.getBool("window", "maximized")) {
        saved.insert(QStringLiteral("maximized"), *value);
    }

    QList<QRect> screens;
    if (availableScreens.isEmpty()) {
        const QList<QScreen *> screenList = QGuiApplication::screens();
        for (const QScreen *screen : screenList) {
            if (screen) {
                screens.append(screen->availableGeometry());
            }
        }
    } else {
        for (const QVariant &entry : availableScreens) {
            screens.append(entry.toRect());
        }
    }

    return clampWindowGeometry(saved, defaultWidth, defaultHeight,
                               minimumWidth, minimumHeight, screens);
}
