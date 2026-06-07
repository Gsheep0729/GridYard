/**
* @file    test_config_manager.cpp
* @date    2026-06-05
* @author  GY
* @brief   ConfigManager 配置管理器测试
*
* 测试用例：配置读写 / 默认值 / 信号发射 / 持久化
*
* Change Log:
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
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
    delete _config;
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

void TestConfigManager::testDeviceIdPersistence()
{
    // 保存当前 ID
    QString originalId = _config->deviceId();
    QVERIFY(!originalId.isEmpty());

    // 创建新的 ConfigManager 实例
    ConfigManager *newConfig = ConfigManager::create(nullptr, nullptr);
    QCOMPARE(newConfig->deviceId(), originalId);
    delete newConfig;
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
}

void TestConfigManager::testLocalIp()
{
    // 本地 IP 可能为空（无网络）或有值
    QString ip = _config->localIp();
    // 不检查具体值，只验证不崩溃
    Q_UNUSED(ip);

    // 测试刷新方法
    QSignalSpy spy(_config, &ConfigManager::localIpChanged);
    _config->refreshLocalIp();
    // 信号可能发射也可能不发射（取决于 IP 是否变化）
    Q_UNUSED(spy);
}

QTEST_MAIN(TestConfigManager)
#include "test_config_manager.moc"
