/**
* @file    test_config_manager.cpp
* @version 8.3.0
* @date 2026-11-03
* @author  GY
* @brief   ConfigManager 配置管理器测试
*
* 测试用例：配置读写 / 默认值 / 信号发射 / 持久化
*/

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>
#include <QSysInfo>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

#ifndef _WIN32
// POSIX 专有头：仅 geteuid() 的 root 判定消费（Windows 无 root/权限位语义）
#include <sys/types.h>
#include <unistd.h>
#endif

#include "application_paths.h"
#include "config_manager.h"
#include "net_interfaces.h"
#include "protocol.h"

#ifdef _WIN32
// Windows 头链把 interface 定义成宏，展开会击穿下方以 interface 命名的循环变量
#undef interface
#endif

class TestConfigManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testDefaultValue();
    void testDeviceName();
    void testReceivePath();
    void testReceivePathError();
    void testAutoAcceptFiles();
    void testDoNotDisturb();
    void testTcpPort();
    void testAnnouncedTcpPortFrozenAtStartup();
    void testDeviceIdPersistence();
    void testSignalEmission();
    void testLocalIp();
    void testCloseWindowAction();
    void testResolveWindowCloseAction();
    void testInstancePortOffset();
    void testInstanceNumberParsing();
    void testInstanceDirectoryName();
    void testApplyInstanceSuffix();
    void testInstanceTcpPortDefault();
    void testDeveloperLaunchEntry();
    void testNextLaunchInstance();
    void testAutostartEntryLifecycle();
    void testAutostartReconcileOnStartup();
    void testAutostartDevInstanceEntry();
    void testWindowGeometryPersistence();
    void testWindowGeometryClamp();

private:
    ConfigManager *_config = nullptr;
    QTemporaryDir *_tempDir = nullptr;
};

void TestConfigManager::initTestCase()
{
    // 使用临时目录作为配置文件路径
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    // 设置环境变量指向临时配置文件
    QString configPath = _tempDir->path() + "/test_config.ini";
    qputenv("GRIDYARD_CONFIG", configPath.toUtf8());

    // 创建 ConfigManager 实例
    _config = ConfigManager::create(nullptr, nullptr);
    QVERIFY(_config != nullptr);
}

void TestConfigManager::cleanupTestCase()
{
    // 不删除 _config，因为它是全局单例
    // delete _config;
    _config = nullptr;

    delete _tempDir;
    _tempDir = nullptr;

    // 清理环境变量
    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
    qunsetenv("GRIDYARD_PORT");
}

void TestConfigManager::testDefaultValue()
{
    // 验证默认值
    QVERIFY(!_config->deviceId().isEmpty());
    QVERIFY(!_config->deviceName().isEmpty());
    QVERIFY(!_config->receivePath().isEmpty());
    QVERIFY(_config->tcpPort() > 0);
    QVERIFY(!_config->autoAcceptFiles());
    // 关窗行为默认每次询问
    QVERIFY(_config->closeWindowAction() == CloseWindowAction::Ask);
}

void TestConfigManager::testDeviceName()
{
    QSignalSpy spy(_config, &ConfigManager::deviceNameChanged);

    // 设置新名称
    QString newName = "TestDevice";
    _config->setDeviceName(newName);
    QCOMPARE(_config->deviceName(), newName);
    QCOMPARE(spy.count(), 1);

    // 设置相同名称不应触发信号
    _config->setDeviceName(newName);
    QCOMPARE(spy.count(), 1);

    // 恢复原名称
    _config->setDeviceName(QSysInfo::machineHostName());
}

void TestConfigManager::testReceivePath()
{
    QSignalSpy spy(_config, &ConfigManager::receivePathChanged);

    // 创建临时目录
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 设置新路径
    _config->setReceivePath(dir.path());
    QCOMPARE(_config->receivePath(), dir.path());
    QCOMPARE(spy.count(), 1);

    // 设置相同路径不应触发信号
    _config->setReceivePath(dir.path());
    QCOMPARE(spy.count(), 1);
}

// O-11 回归：接收路径保存前校验存在/可写，失败必须给出原因供行内反馈，
// 禁止无效路径静默写入配置、延后到接受传输时才失败
void TestConfigManager::testReceivePathError()
{
    // 空路径（含纯空白）：直接拒绝
    QVERIFY(!_config->receivePathError(QString()).isEmpty());
    QVERIFY(!_config->receivePathError("   ").isEmpty());

    QTemporaryDir base;
    QVERIFY(base.isValid());

    // 不存在的新路径：可创建即通过，且目录被实际创建（与 setReceivePath 的 mkpath 对齐）
    const QString nested = base.path() + "/auto/recv";
    QVERIFY(_config->receivePathError(nested).isEmpty());
    QVERIFY(QFileInfo(nested).isDir());

    // 已存在的目录：通过
    QVERIFY(_config->receivePathError(base.path()).isEmpty());

    // 路径已被普通文件占用：mkpath 必失败，必须报错
    const QString occupied = base.path() + "/occupied";
    QFile file(occupied);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY(!_config->receivePathError(occupied).isEmpty());

    // 不可写目录：必须报错（root 下权限位不生效，跳过该例）
#ifdef _WIN32
    // Windows 分支：目录只读属性不约束子项创建（CreateDirectory 不受父目录
    // 只读位限制），不可写目录子用例判定口径不成立，跳过（与 root 分支同口径）
    qWarning("Windows 下跳过不可写目录用例（目录只读属性不约束子项创建）");
#else
    if (geteuid() != 0) {
        const QString roDir = base.path() + "/readonly";
        QVERIFY(QDir().mkpath(roDir));
        QVERIFY(QFile::setPermissions(roDir, QFile::ReadUser | QFile::ExeUser));
        QVERIFY(!QFileInfo(roDir).isWritable());
        QVERIFY(!_config->receivePathError(roDir).isEmpty());
        // 恢复权限，保证 QTemporaryDir 析构可清理
        QFile::setPermissions(roDir, QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
    } else {
        qWarning("以 root 运行，跳过不可写目录用例（权限位对 root 不生效）");
    }
#endif
}

void TestConfigManager::testTcpPort()
{
    QSignalSpy spy(_config, &ConfigManager::tcpPortChanged);

    // 设置新端口
    quint16 newPort = 12345;
    _config->setTcpPort(newPort);
    QCOMPARE(_config->tcpPort(), newPort);
    QCOMPARE(spy.count(), 1);

    // 设置相同端口不应触发信号
    _config->setTcpPort(newPort);
    QCOMPARE(spy.count(), 1);
}

// 身份播报端口应在启动期定型：运行期保存的新端口不得立即进入 Hello 载荷，
// 否则监听器仍在旧端口而播报已切换，保存后到重启前对端按新端口连入必失败
void TestConfigManager::testAnnouncedTcpPortFrozenAtStartup()
{
    // 快照取构造期定型的播报端口（前序用例可能已改动 tcpPort 运行值）
    const quint16 frozenPort = _config->announcedTcpPort();
    const quint16 portBefore = _config->tcpPort();

    // 运行期改端口：配置值立即更新（重启生效），播报快照保持启动值
    quint16 runtimePort = static_cast<quint16>(frozenPort + 100 > 65535
                                                    ? frozenPort - 100 : frozenPort + 100);
    if (runtimePort == portBefore) {
        runtimePort = static_cast<quint16>(frozenPort + 200 > 65535
                                               ? frozenPort - 200 : frozenPort + 200);
    }
    _config->setTcpPort(runtimePort);
    QCOMPARE(_config->tcpPort(), runtimePort);
    QCOMPARE(_config->announcedTcpPort(), frozenPort);

    // Hello 载荷播报的是监听器真正使用的启动端口
    QJsonObject payload;
    _config->fillHelloPayload(payload);
    QCOMPARE(payload["tcp_port"].toInt(), static_cast<int>(frozenPort));

    // 恢复进入本用例前的端口值，避免影响后续用例
    _config->setTcpPort(portBefore);
}

void TestConfigManager::testAutoAcceptFiles()
{
    QSignalSpy spy(_config, &ConfigManager::autoAcceptFilesChanged);

    _config->setAutoAcceptFiles(true);
    QVERIFY(_config->autoAcceptFiles());
    QCOMPARE(spy.count(), 1);

    _config->setAutoAcceptFiles(true);
    QCOMPARE(spy.count(), 1);

    _config->setAutoAcceptFiles(false);
    QVERIFY(!_config->autoAcceptFiles());
    QCOMPARE(spy.count(), 2);
}

// 勿扰接收开关：setter 翻转运行值并发 NOTIFY，同值重写不重发；
// 键持久化进 device 组，重建实例（重启语义）后回读不丢
void TestConfigManager::testDoNotDisturb()
{
    const bool saved = _config->doNotDisturb();

    QSignalSpy spy(_config, &ConfigManager::doNotDisturbChanged);
    _config->setDoNotDisturb(true);
    QVERIFY(_config->doNotDisturb());
    QCOMPARE(spy.count(), 1);

    _config->setDoNotDisturb(true);  // 同值重写：不重发 NOTIFY
    QCOMPARE(spy.count(), 1);

    // 键已落配置文件（device/doNotDisturb），重建实例验证持久化回读
    ConfigManager *reloaded = ConfigManager::create(nullptr, nullptr);
    QVERIFY(reloaded->doNotDisturb());

    _config->setDoNotDisturb(false);
    QVERIFY(!_config->doNotDisturb());
    QCOMPARE(spy.count(), 2);
    _config->setDoNotDisturb(saved);  // 还原进入本用例前的值
}

void TestConfigManager::testDeviceIdPersistence()
{
    // 保存当前 ID
    QString originalId = _config->deviceId();
    QVERIFY(!originalId.isEmpty());

    // 获取 ConfigManager 实例（应该是同一个实例）
    ConfigManager *newConfig = ConfigManager::create(nullptr, nullptr);
    QCOMPARE(newConfig->deviceId(), originalId);
    // 不删除 newConfig，因为它是全局单例
}

void TestConfigManager::testSignalEmission()
{
    // 测试 deviceNameChanged 信号
    QSignalSpy nameSpy(_config, &ConfigManager::deviceNameChanged);
    _config->setDeviceName("SignalTest");
    QCOMPARE(nameSpy.count(), 1);
    _config->setDeviceName(QSysInfo::machineHostName());

    // 测试 receivePathChanged 信号
    QSignalSpy pathSpy(_config, &ConfigManager::receivePathChanged);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    _config->setReceivePath(dir.path());
    QCOMPARE(pathSpy.count(), 1);

    // 测试 tcpPortChanged 信号
    QSignalSpy portSpy(_config, &ConfigManager::tcpPortChanged);
    _config->setTcpPort(9999);
    QCOMPARE(portSpy.count(), 1);

    QSignalSpy autoAcceptSpy(_config, &ConfigManager::autoAcceptFilesChanged);
    _config->setAutoAcceptFiles(true);
    QCOMPARE(autoAcceptSpy.count(), 1);
    _config->setAutoAcceptFiles(false);
}

void TestConfigManager::testLocalIp()
{
    _config->refreshLocalIp();

    // 无网络环境允许为空；取到值时必须是真实接口上的地址
    const QString ip = _config->localIp();
    if (ip.isEmpty()) {
        return;
    }

    bool belongsToInterface = false;
    const QList<gy::net::NetInterface> interfaces = gy::net::listInterfaces();
    for (const gy::net::NetInterface &interface : interfaces) {
        if (interface.address.toString() == ip) {
            belongsToInterface = true;
            break;
        }
    }
    QVERIFY2(belongsToInterface, "localIp 应是本机接口上的地址");
}

void TestConfigManager::testCloseWindowAction()
{
    QSignalSpy spy(_config, &ConfigManager::closeWindowActionChanged);

    // 记住"隐藏到后台"
    _config->setCloseWindowAction(CloseWindowAction::Hide);
    QCOMPARE(static_cast<int>(_config->closeWindowAction()),
             static_cast<int>(CloseWindowAction::Hide));
    QCOMPARE(spy.count(), 1);

    // 重复写入相同值不应触发信号
    _config->setCloseWindowAction(CloseWindowAction::Hide);
    QCOMPARE(spy.count(), 1);

    // 记住"完全退出"
    _config->setCloseWindowAction(CloseWindowAction::Exit);
    QCOMPARE(static_cast<int>(_config->closeWindowAction()),
             static_cast<int>(CloseWindowAction::Exit));
    QCOMPARE(spy.count(), 2);

    // 设置页恢复入口：改回"每次询问"
    _config->setCloseWindowAction(CloseWindowAction::Ask);
    QCOMPARE(static_cast<int>(_config->closeWindowAction()),
             static_cast<int>(CloseWindowAction::Ask));
    QCOMPARE(spy.count(), 3);

    // setter 走 openSettings() 即时落盘，配置文件应已写入所选动作
    QSettings settings(_tempDir->path() + "/test_config.ini", QSettings::IniFormat);
    QCOMPARE(settings.value("window/closeWindowAction").toInt(),
             static_cast<int>(CloseWindowAction::Ask));
}

void TestConfigManager::testResolveWindowCloseAction()
{
    // 每次询问：无论是否有活动传输都弹确认窗
    QCOMPARE(ConfigManager::resolveWindowCloseAction(CloseWindowAction::Ask, 0),
             QStringLiteral("ask"));
    QCOMPARE(ConfigManager::resolveWindowCloseAction(CloseWindowAction::Ask, 3),
             QStringLiteral("ask"));

    // 记住隐藏到后台：直接隐藏，活动传输后台继续
    QCOMPARE(ConfigManager::resolveWindowCloseAction(CloseWindowAction::Hide, 0),
             QStringLiteral("hide"));
    QCOMPARE(ConfigManager::resolveWindowCloseAction(CloseWindowAction::Hide, 2),
             QStringLiteral("hide"));

    // 记住完全退出且无活动传输：直接退出
    QCOMPARE(ConfigManager::resolveWindowCloseAction(CloseWindowAction::Exit, 0),
             QStringLiteral("exit"));

    // 记住完全退出但仍有活动传输：拦截为警示确认，A6 防护不随记忆豁免
    QCOMPARE(ConfigManager::resolveWindowCloseAction(CloseWindowAction::Exit, 1),
             QStringLiteral("confirm"));
}

void TestConfigManager::testInstancePortOffset()
{
    // 偏移公式：实例 N 的发现端口 = 45678 + 10N，TCP 端口 = 35100 + 10N
    QCOMPARE(gy::protocol::instanceDiscoveryPort(0), quint16(45678));
    QCOMPARE(gy::protocol::instanceDiscoveryPort(1), quint16(45688));
    QCOMPARE(gy::protocol::instanceDiscoveryPort(9), quint16(45768));
    QCOMPARE(gy::protocol::instanceP2pPort(0), quint16(35100));
    QCOMPARE(gy::protocol::instanceP2pPort(2), quint16(35120));
    QCOMPARE(gy::protocol::instanceP2pPort(9), quint16(35190));

    // 范围夹紧：越界实例号按边界取值，端口恒在合法范围内
    QCOMPARE(gy::protocol::instanceDiscoveryPort(-1), quint16(45678));
    QCOMPARE(gy::protocol::instanceDiscoveryPort(10), quint16(45768));
    QCOMPARE(gy::protocol::instanceP2pPort(-3), quint16(35100));
    QCOMPARE(gy::protocol::instanceP2pPort(99), quint16(35190));

    // 冲突规避：全部实例端口不撞协调节点/中继端口，发现与 TCP 两序列互不重叠
    for (int n = 0; n <= gy::protocol::kMaxInstanceNumber; ++n) {
        const quint16 discoveryPort = gy::protocol::instanceDiscoveryPort(n);
        const quint16 tcpPort = gy::protocol::instanceP2pPort(n);
        QVERIFY2(discoveryPort != gy::protocol::kDefaultRendezvousPort,
                 "发现端口不得与协调节点端口冲突");
        QVERIFY2(discoveryPort != gy::protocol::kDefaultRelayPort,
                 "发现端口不得与中继端口冲突");
        QVERIFY(discoveryPort >= 1024 && discoveryPort <= 65535);
        QVERIFY(tcpPort >= 1024 && tcpPort <= 65535);
        QVERIFY(discoveryPort != tcpPort);
    }
}

void TestConfigManager::testInstanceNumberParsing()
{
    // 未设置环境变量：正常模式实例 0
    qunsetenv("GRIDYARD_INSTANCE");
    QCOMPARE(ApplicationPaths::instanceNumber(), 0);

    // 合法数值原样解析
    qputenv("GRIDYARD_INSTANCE", "2");
    QCOMPARE(ApplicationPaths::instanceNumber(), 2);

    // 非法字符串按正常模式处理
    qputenv("GRIDYARD_INSTANCE", "abc");
    QCOMPARE(ApplicationPaths::instanceNumber(), 0);

    // 越界数值夹紧到合法范围
    qputenv("GRIDYARD_INSTANCE", "15");
    QCOMPARE(ApplicationPaths::instanceNumber(), 9);
    qputenv("GRIDYARD_INSTANCE", "-3");
    QCOMPARE(ApplicationPaths::instanceNumber(), 0);

    qunsetenv("GRIDYARD_INSTANCE");
}

void TestConfigManager::testInstanceDirectoryName()
{
    // 实例 0 沿用默认名，实例 N 返回确定性后缀名，越界夹紧
    QCOMPARE(ApplicationPaths::instanceDirectoryName(0), QStringLiteral("GridYard"));
    QCOMPARE(ApplicationPaths::instanceDirectoryName(3), QStringLiteral("GridYard-dev3"));
    QCOMPARE(ApplicationPaths::instanceDirectoryName(12), QStringLiteral("GridYard-dev9"));
    QCOMPARE(ApplicationPaths::instanceDirectoryName(-1), QStringLiteral("GridYard"));
}

void TestConfigManager::testApplyInstanceSuffix()
{
    const QString systemDir = QStringLiteral("/home/user/.local/share/CQNU-SED/GridYard");

    // 实例 0：逐字符原样返回，正常模式路径推导零变化
    QCOMPARE(ApplicationPaths::applyInstanceSuffix(systemDir, 0), systemDir);

    // 实例 N：叶目录替换为实例目录名，上级目录保持不变
    QCOMPARE(ApplicationPaths::applyInstanceSuffix(systemDir, 2),
             QStringLiteral("/home/user/.local/share/CQNU-SED/GridYard-dev2"));

    // 越界实例号夹紧
    QCOMPARE(ApplicationPaths::applyInstanceSuffix(systemDir, 20),
             QStringLiteral("/home/user/.local/share/CQNU-SED/GridYard-dev9"));
}

void TestConfigManager::testInstanceTcpPortDefault()
{
    // 恢复干净环境：实例端口默认值不受命令行端口与既有配置影响
    qunsetenv("GRIDYARD_PORT");
    const QString savedConfig = qEnvironmentVariable("GRIDYARD_CONFIG");

    // 实例 0（未设置实例号）：默认 TCP 端口保持 35100 不变
    qunsetenv("GRIDYARD_INSTANCE");
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/instance0.ini").toUtf8());
    {
        ConfigManager normal;
        QCOMPARE(normal.instanceNumber(), 0);
        QCOMPARE(normal.tcpPort(), quint16(35100));
    }

    // 实例 3：默认 TCP 端口确定性偏移为 35130
    qputenv("GRIDYARD_INSTANCE", "3");
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/instance3.ini").toUtf8());
    {
        ConfigManager dev;
        QCOMPARE(dev.instanceNumber(), 3);
        QCOMPARE(dev.tcpPort(), quint16(35130));
    }

    qunsetenv("GRIDYARD_INSTANCE");
    if (!savedConfig.isEmpty()) {
        qputenv("GRIDYARD_CONFIG", savedConfig.toUtf8());
    } else {
        qunsetenv("GRIDYARD_CONFIG");
    }
}

void TestConfigManager::testDeveloperLaunchEntry()
{
    // 默认关闭：干净配置下开关不启用
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/fresh_dev_entry.ini").toUtf8());
    {
        ConfigManager fresh;
        QVERIFY(!fresh.developerLaunchEntryEnabled());
    }

    // 恢复主配置路径，后续单例的读写都落在 test_config.ini
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/test_config.ini").toUtf8());

    QSignalSpy spy(_config, &ConfigManager::developerLaunchEntryEnabledChanged);

    // 开启开关并持久化到配置文件
    _config->setDeveloperLaunchEntryEnabled(true);
    QVERIFY(_config->developerLaunchEntryEnabled());
    QCOMPARE(spy.count(), 1);

    // 重复写入相同值不应触发信号
    _config->setDeveloperLaunchEntryEnabled(true);
    QCOMPARE(spy.count(), 1);

    // setter 走 openSettings() 即时落盘，配置文件应已写入开启状态
    QSettings settings(_tempDir->path() + "/test_config.ini", QSettings::IniFormat);
    QCOMPARE(settings.value("developer/launchEntryEnabled").toBool(), true);

    // 重启后保持：重新构造 ConfigManager 从配置文件读回开启状态
    {
        ConfigManager reopened;
        QVERIFY(reopened.developerLaunchEntryEnabled());
    }

    // 关闭开关恢复默认
    _config->setDeveloperLaunchEntryEnabled(false);
    QVERIFY(!_config->developerLaunchEntryEnabled());
    QCOMPARE(spy.count(), 2);
    // 配置改经纯 C++ 存取器落盘，QSettings 实例须先 sync 才能看到外部改写
    settings.sync();
    QCOMPARE(settings.value("developer/launchEntryEnabled").toBool(), false);
}

void TestConfigManager::testNextLaunchInstance()
{
    // 正常实例（0）拉起实例 1，开发者实例 N 拉起 N+1
    QCOMPARE(ConfigManager::nextLaunchInstance(0), 1);
    QCOMPARE(ConfigManager::nextLaunchInstance(3), 4);
    QCOMPARE(ConfigManager::nextLaunchInstance(8), 9);

    // 到达上限 9：返回 -1 供表现层给出行内提示，不再递增
    QCOMPARE(ConfigManager::nextLaunchInstance(9), -1);

    // 越界输入先夹紧到合法范围再分配
    QCOMPARE(ConfigManager::nextLaunchInstance(15), -1);
    QCOMPARE(ConfigManager::nextLaunchInstance(-2), 1);
}

// —— O-06 开机自启 ——

// 开机自启开关生命周期：开启写 XDG autostart 桌面项（Exec 为 applicationFilePath
// 绝对路径，禁相对名），同值重写不重复落盘/广播，关闭移除桌面项；写失败必须
// 返回原因且意图保持原值，禁静默假成功。测试经 XDG_CONFIG_HOME 重定向，
// 绝不触碰真实 ~/.config/autostart
void TestConfigManager::testAutostartEntryLifecycle()
{
#ifdef _WIN32
    // Windows 分支：自启隔离依赖 XDG_CONFIG_HOME 重定向，Windows 无 XDG 语义
    // （autostartDirectory 落 QStandardPaths GenericConfigLocation，config_manager
    // 的自启实现仍为 XDG 桌面项，注册表化属可行性报告 P1-P5 未落位），隔离失效
    // 会触碰真实用户 autostart 目录，整体跳过
    QSKIP("XDG autostart 用例仅适用 POSIX（Windows 注册表化待 P1-P5）");
#else
    QTemporaryDir xdgDir;
    QVERIFY(xdgDir.isValid());
    const QByteArray savedXdg = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", xdgDir.path().toUtf8());

    // 干净配置：默认关闭，且不产生任何桌面项
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/autostart_fresh.ini").toUtf8());
    {
        ConfigManager fresh;
        QVERIFY(!fresh.autostartEnabled());
        QVERIFY(!QFileInfo::exists(ConfigManager::autostartEntryPath()));
    }

    QSignalSpy spy(_config, &ConfigManager::autostartEnabledChanged);

    // 开启：先落桌面项，成功才持久化意图并广播 NOTIFY
    const QString enableError = _config->setAutostartEnabled(true);
    QVERIFY2(enableError.isEmpty(), qPrintable(enableError));
    QVERIFY(_config->autostartEnabled());
    QCOMPARE(spy.count(), 1);

    // 桌面项写到 XDG autostart 标准位置，Exec 回填当前可执行文件绝对路径
    QVERIFY(ConfigManager::autostartEntryFileName()
                == QLatin1String("gridyard.desktop"));
    const QString entryPath = ConfigManager::autostartEntryPath();
    QVERIFY(QFileInfo(entryPath).isFile());
    QFile entry(entryPath);
    QVERIFY(entry.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(entry.readAll());
    entry.close();
    QVERIFY2(content.contains(QStringLiteral("Exec=\"%1\"\n")
                                  .arg(QCoreApplication::applicationFilePath())),
             qPrintable(content));
    QVERIFY(content.contains(QLatin1String("Type=Application")));
    QVERIFY(content.contains(QLatin1String("Terminal=false")));

    // 同值重写：不重复广播，桌面项保持存在
    QVERIFY(_config->setAutostartEnabled(true).isEmpty());
    QCOMPARE(spy.count(), 1);
    QVERIFY(QFileInfo::exists(entryPath));

    // 意图持久化：重建实例（重启语义）回读开启状态
    {
        ConfigManager reloaded;
        QVERIFY(reloaded.autostartEnabled());
        QVERIFY(QFileInfo::exists(ConfigManager::autostartEntryPath()));
    }

    // 关闭：桌面项删除，意图持久化为关
    const QString disableError = _config->setAutostartEnabled(false);
    QVERIFY2(disableError.isEmpty(), qPrintable(disableError));
    QVERIFY(!_config->autostartEnabled());
    QCOMPARE(spy.count(), 2);
    QVERIFY(!QFileInfo::exists(entryPath));

    // 写失败必须反馈原因且不得写入意图（root 下权限位不生效，跳过该例）
    if (geteuid() != 0) {
        const QString autoDir = ConfigManager::autostartDirectory();
        QVERIFY(QFile::setPermissions(autoDir, QFile::ReadUser | QFile::ExeUser));
        const QString roError = _config->setAutostartEnabled(true);
        QVERIFY2(!roError.isEmpty(), "只读自启目录下开启自启应失败");
        QVERIFY(!_config->autostartEnabled());
        QVERIFY(!QFileInfo::exists(ConfigManager::autostartEntryPath()));
        // 恢复权限，保证 QTemporaryDir 析构可清理
        QFile::setPermissions(autoDir, QFile::ReadUser | QFile::WriteUser | QFile::ExeUser);
    } else {
        qWarning("以 root 运行，跳过只读目录用例（权限位对 root 不生效）");
    }

    if (savedXdg.isEmpty()) {
        qunsetenv("XDG_CONFIG_HOME");
    } else {
        qputenv("XDG_CONFIG_HOME", savedXdg);
    }
    // 恢复主配置路径，后续用例的读写都落在 test_config.ini
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/test_config.ini").toUtf8());
#endif
}

// 启动对账：意图为开但桌面项缺失或 Exec 指向陈旧路径（AppImage/deb 更新、
// 安装位置漂移）时，构造期以当前 applicationFilePath 重写，登录不再拉起失效路径
void TestConfigManager::testAutostartReconcileOnStartup()
{
#ifdef _WIN32
    // Windows 分支：与 testAutostartEntryLifecycle 同口径（XDG 隔离失效），整体跳过
    QSKIP("XDG autostart 用例仅适用 POSIX（Windows 注册表化待 P1-P5）");
#else
    QTemporaryDir xdgDir;
    QVERIFY(xdgDir.isValid());
    const QByteArray savedXdg = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", xdgDir.path().toUtf8());

    // 种一个开启自启的配置，再手放一个 Exec 已失效的陈旧桌面项
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/autostart_reconcile.ini").toUtf8());
    QSettings seed(_tempDir->path() + "/autostart_reconcile.ini", QSettings::IniFormat);
    seed.setValue("startup/autostartEnabled", true);
    seed.sync();

    const QString entryPath = ConfigManager::autostartEntryPath();
    QVERIFY(QDir().mkpath(QFileInfo(entryPath).absolutePath()));
    {
        QFile stale(entryPath);
        QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Text));
        stale.write("[Desktop Entry]\nExec=/nonexistent/gridyard-old\nTerminal=false\n");
    }

    {
        ConfigManager reloaded;
        QVERIFY(reloaded.autostartEnabled());
        QFile entry(entryPath);
        QVERIFY(entry.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(entry.readAll());
        entry.close();
        QVERIFY2(content.contains(QStringLiteral("Exec=\"%1\"\n")
                                      .arg(QCoreApplication::applicationFilePath())),
                 qPrintable(content));
        QVERIFY(!content.contains(QLatin1String("/nonexistent/gridyard-old")));
    }

    // 意图为关：启动对账不删除既有桌面项（可能由外部工具维护），关闭只在开关动作时落盘
    QSettings seedOff(_tempDir->path() + "/autostart_off.ini", QSettings::IniFormat);
    seedOff.setValue("startup/autostartEnabled", false);
    seedOff.sync();
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/autostart_off.ini").toUtf8());
    {
        ConfigManager reloaded;
        QVERIFY(!reloaded.autostartEnabled());
        QVERIFY(QFileInfo::exists(entryPath));
    }

    if (savedXdg.isEmpty()) {
        qunsetenv("XDG_CONFIG_HOME");
    } else {
        qputenv("XDG_CONFIG_HOME", savedXdg);
    }
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/test_config.ini").toUtf8());
#endif
}

// 开发者实例的桌面项独立命名（gridyard-devN.desktop），Exec 追加 --instance=N：
// 登录拉起的是同一开发者实例，且不与正常实例的自启项互踩
void TestConfigManager::testAutostartDevInstanceEntry()
{
#ifdef _WIN32
    // Windows 分支：与 testAutostartEntryLifecycle 同口径（XDG 隔离失效），整体跳过
    QSKIP("XDG autostart 用例仅适用 POSIX（Windows 注册表化待 P1-P5）");
#else
    QTemporaryDir xdgDir;
    QVERIFY(xdgDir.isValid());
    const QByteArray savedXdg = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", xdgDir.path().toUtf8());

    qputenv("GRIDYARD_INSTANCE", "2");
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/autostart_dev2.ini").toUtf8());
    {
        ConfigManager dev;
        QCOMPARE(dev.instanceNumber(), 2);
        QVERIFY(ConfigManager::autostartEntryPath()
                    .endsWith(QLatin1String("gridyard-dev2.desktop")));

        const QString error = dev.setAutostartEnabled(true);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(dev.autostartEnabled());

        QFile entry(ConfigManager::autostartEntryPath());
        QVERIFY(entry.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(entry.readAll());
        entry.close();
        QVERIFY2(content.contains(QStringLiteral("Exec=\"%1\" --instance=2\n")
                                      .arg(QCoreApplication::applicationFilePath())),
                 qPrintable(content));

        QVERIFY(dev.setAutostartEnabled(false).isEmpty());
        QVERIFY(!QFileInfo::exists(ConfigManager::autostartEntryPath()));
    }

    qunsetenv("GRIDYARD_INSTANCE");
    if (savedXdg.isEmpty()) {
        qunsetenv("XDG_CONFIG_HOME");
    } else {
        qputenv("XDG_CONFIG_HOME", savedXdg);
    }
    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/test_config.ini").toUtf8());
#endif
}

void TestConfigManager::testWindowGeometryPersistence()
{
    // 独立配置文件，先写后读模拟跨启动；不依赖运行环境真实屏幕，恢复一律
    // 显式传入屏幕可用区，结果确定性
    const QString geometryPath = _tempDir->path() + "/window_geometry.ini";
    QFile::remove(geometryPath);
    qputenv("GRIDYARD_CONFIG", geometryPath.toUtf8());

    {
        ConfigManager writer;
        writer.saveWindowGeometry(112, 84, 1234, 768, true);
    }

    // 写入即落盘：window 组键逐键可查（QSettings 与 ConfigStore 文件语义兼容）
    QSettings ini(geometryPath, QSettings::IniFormat);
    QCOMPARE(ini.value("window/x").toInt(), 112);
    QCOMPARE(ini.value("window/y").toInt(), 84);
    QCOMPARE(ini.value("window/width").toInt(), 1234);
    QCOMPARE(ini.value("window/height").toInt(), 768);
    QCOMPARE(ini.value("window/maximized").toBool(), true);

    const QVariantList screens{QVariant::fromValue(QRect(0, 0, 1920, 1040))};
    {
        ConfigManager reloaded;
        const QVariantMap geo = reloaded.restoreWindowGeometry(980, 725, 860, 620, screens);
        QCOMPARE(geo.value("width").toInt(), 1234);
        QCOMPARE(geo.value("height").toInt(), 768);
        QCOMPARE(geo.value("x").toInt(), 112);
        QCOMPARE(geo.value("y").toInt(), 84);
        QCOMPARE(geo.value("maximized").toBool(), true);
        QCOMPARE(geo.value("hasPosition").toBool(), true);
    }

    // 首次启动无保存几何：回落默认尺寸且不指定位置（交窗口管理器自选）
    const QString freshPath = _tempDir->path() + "/window_geometry_fresh.ini";
    QFile::remove(freshPath);
    qputenv("GRIDYARD_CONFIG", freshPath.toUtf8());
    {
        ConfigManager fresh;
        const QVariantMap geo = fresh.restoreWindowGeometry(980, 725, 860, 620, screens);
        QCOMPARE(geo.value("width").toInt(), 980);
        QCOMPARE(geo.value("height").toInt(), 725);
        QCOMPARE(geo.value("hasPosition").toBool(), false);
        QCOMPARE(geo.value("maximized").toBool(), false);
    }

    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/test_config.ini").toUtf8());
}

void TestConfigManager::testWindowGeometryClamp()
{
    const QList<QRect> dualScreens = {QRect(0, 0, 1920, 1040), QRect(1920, 0, 1366, 768)};

    // 屏内几何原样保留
    QVariantMap geo = ConfigManager::clampWindowGeometry(
        {{QStringLiteral("x"), 100}, {QStringLiteral("y"), 50},
         {QStringLiteral("width"), 1200}, {QStringLiteral("height"), 800},
         {QStringLiteral("maximized"), false}},
        980, 725, 860, 620, dualScreens);
    QCOMPARE(geo.value("hasPosition").toBool(), true);
    QCOMPARE(geo.value("x").toInt(), 100);
    QCOMPARE(geo.value("y").toInt(), 50);
    QCOMPARE(geo.value("width").toInt(), 1200);
    QCOMPARE(geo.value("height").toInt(), 800);

    // 超宽超大（如多显示器撤掉一台后残留）：尺寸夹到最大屏幕可用区，
    // 缩小后仍与屏相交，位置保留
    geo = ConfigManager::clampWindowGeometry(
        {{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
         {QStringLiteral("width"), 4000}, {QStringLiteral("height"), 2000},
         {QStringLiteral("maximized"), false}},
        980, 725, 860, 620, dualScreens);
    QCOMPARE(geo.value("width").toInt(), 1920);
    QCOMPARE(geo.value("height").toInt(), 1040);
    QCOMPARE(geo.value("hasPosition").toBool(), true);

    // 保存尺寸小于最小尺寸：抬到最小
    geo = ConfigManager::clampWindowGeometry(
        {{QStringLiteral("x"), 10}, {QStringLiteral("y"), 10},
         {QStringLiteral("width"), 400}, {QStringLiteral("height"), 300},
         {QStringLiteral("maximized"), false}},
        980, 725, 860, 620, dualScreens);
    QCOMPARE(geo.value("width").toInt(), 860);
    QCOMPARE(geo.value("height").toInt(), 620);

    // 整窗落屏外（多显示器拔插的典型残留）：放弃位置，尺寸仍夹紧保留
    geo = ConfigManager::clampWindowGeometry(
        {{QStringLiteral("x"), 5000}, {QStringLiteral("y"), 3000},
         {QStringLiteral("width"), 1200}, {QStringLiteral("height"), 800},
         {QStringLiteral("maximized"), false}},
        980, 725, 860, 620, dualScreens);
    QCOMPARE(geo.value("hasPosition").toBool(), false);
    QVERIFY(!geo.contains(QStringLiteral("x")));
    QVERIFY(!geo.contains(QStringLiteral("y")));
    QCOMPARE(geo.value("width").toInt(), 1200);
    QCOMPARE(geo.value("height").toInt(), 800);

    // 大部分悬出屏幕（相交不足窗口面积三成，仅露一条边）：同样判为不可见
    geo = ConfigManager::clampWindowGeometry(
        {{QStringLiteral("x"), 1700}, {QStringLiteral("y"), 900},
         {QStringLiteral("width"), 1200}, {QStringLiteral("height"), 800},
         {QStringLiteral("maximized"), false}},
        980, 725, 860, 620, dualScreens);
    QCOMPARE(geo.value("hasPosition").toBool(), false);

    // 屏幕信息不可得（无 GUI 环境）：不做夹紧原样返回，位置以保存为准
    geo = ConfigManager::clampWindowGeometry(
        {{QStringLiteral("x"), 5000}, {QStringLiteral("y"), 3000},
         {QStringLiteral("width"), 4000}, {QStringLiteral("height"), 2000},
         {QStringLiteral("maximized"), true}},
        980, 725, 860, 620, {});
    QCOMPARE(geo.value("width").toInt(), 4000);
    QCOMPARE(geo.value("height").toInt(), 2000);
    QCOMPARE(geo.value("hasPosition").toBool(), true);
    QCOMPARE(geo.value("x").toInt(), 5000);
    QCOMPARE(geo.value("maximized").toBool(), true);

    // 无保存尺寸（首次启动）：默认尺寸，不指定位置
    geo = ConfigManager::clampWindowGeometry({}, 980, 725, 860, 620, dualScreens);
    QCOMPARE(geo.value("width").toInt(), 980);
    QCOMPARE(geo.value("height").toInt(), 725);
    QCOMPARE(geo.value("hasPosition").toBool(), false);
    QCOMPARE(geo.value("maximized").toBool(), false);
}

QTEST_MAIN(TestConfigManager)
#include "test_config_manager.moc"
