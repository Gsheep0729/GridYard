/**
* @file    test_config_manager.cpp
* @version 7.19.0
* @date 2026-10-05
* @author  GY
* @brief   ConfigManager 配置管理器测试
*
* 测试用例：配置读写 / 默认值 / 信号发射 / 持久化
*
* Change Log:
 * [v7.19.0] GY   2026-10-05
 * * 新增实例端口偏移、实例号解析、目录后缀推导与实例默认端口用例
 * [v7.18.0] GY   2026-10-05
 * * 新增关窗行为配置读写与持久化用例、关窗动作决策分支用例（含活动传输拦截）
 * [v7.17.5] GY   2026-10-04
 * * 版本头对齐到 v7.17.5
* [v7.17.4] GY   2026-10-04
* * 版本头对齐到 v7.17.4
* [v7.17.3] GY   2026-10-04
* * 版本头对齐到 v7.17.3
* [v7.17.2] GY   2026-10-04
* * 版本头对齐到 v7.17.2
* [v7.17.1] GY   2026-10-04
* * 版本头对齐到 v7.17.1
* [v7.17.0] GY   2026-10-04
* * 版本头对齐到 v7.17.0
* [v7.16.0] GY   2026-10-04
* * 版本头对齐到 v7.16.0
* [v7.15.19] GY   2026-10-04
* * 版本头对齐到 v7.15.19
* [v7.15.18] GY   2026-10-04
* * 版本头对齐到 v7.15.18
* [v7.15.17] GY   2026-10-04
* * 版本头对齐到 v7.15.17
* [v7.15.16] GY   2026-10-04
* * 版本头对齐到 v7.15.16
* [v7.15.15] GY   2026-10-04
* * 版本头对齐到 v7.15.15
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
* [v7.13.0] GY   2026-10-02
* * 本机 IP 用例改为校验刷新结果属于真实接口地址
* [v4.11.0] GY   2026-06-13
* * 新增自动接收文件配置测试
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QNetworkInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QSettings>
#include <QHostInfo>

#include "application_paths.h"
#include "config_manager.h"
#include "protocol.h"

class TestConfigManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testDefaultValue();
    void testDeviceName();
    void testReceivePath();
    void testAutoAcceptFiles();
    void testTcpPort();
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
    _config->setDeviceName(QHostInfo::localHostName());
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
    _config->setDeviceName(QHostInfo::localHostName());

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
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().toString() == ip) {
                belongsToInterface = true;
                break;
            }
        }
        if (belongsToInterface) {
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

QTEST_MAIN(TestConfigManager)
#include "test_config_manager.moc"
