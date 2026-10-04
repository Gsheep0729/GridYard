/**
* @file    test_config_manager.cpp
* @version 7.17.1
* @date    2026-10-04
* @author  GY
* @brief   ConfigManager 配置管理器测试
*
* 测试用例：配置读写 / 默认值 / 信号发射 / 持久化
*
* Change Log:
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

#include "config_manager.h"

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

QTEST_MAIN(TestConfigManager)
#include "test_config_manager.moc"
