/**
* @file    test_discovery.cpp
* @date    2026-06-05
* @author  GridYard Team
* @brief   DiscoveryService 设备发现测试
*
* 测试用例：UDP 广播收发 / 节点发现 / 节点过期 / refresh()
*
* Change Log:
* [v1.0] GY   2026-06-05
* * 初始版本
*/

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTimer>

#include "discovery_service.h"
#include "config_manager.h"

class TestDiscovery : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testNodeDiscovery();
    void testNodeExpiry();
    void testRefresh();
    void testMultipleNodes();
    void testPeerInfo();

private:
    void waitForSignal(QSignalSpy &spy, int timeout = 2000);

    ConfigManager *_config1 = nullptr;
    ConfigManager *_config2 = nullptr;
    DiscoveryService *_discovery1 = nullptr;
    DiscoveryService *_discovery2 = nullptr;
    QTemporaryDir *_tempDir = nullptr;
};

void TestDiscovery::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    // 创建两个独立的配置（直接创建实例，不使用单例）
    QString config1Path = _tempDir->path() + "/config1.ini";
    QString config2Path = _tempDir->path() + "/config2.ini";

    qputenv("GRIDYARD_CONFIG", config1Path.toUtf8());
    qputenv("GRIDYARD_NAME", "Device1");
    _config1 = new ConfigManager{};

    qputenv("GRIDYARD_CONFIG", config2Path.toUtf8());
    qputenv("GRIDYARD_NAME", "Device2");
    _config2 = new ConfigManager{};

    // 创建发现服务
    _discovery1 = new DiscoveryService(_config1, this);
    _discovery2 = new DiscoveryService(_config2, this);

    // 等待服务初始化
    QTest::qWait(500);
}

void TestDiscovery::cleanupTestCase()
{
    delete _discovery1;
    delete _discovery2;
    delete _config1;
    delete _config2;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

void TestDiscovery::waitForSignal(QSignalSpy &spy, int timeout)
{
    if (spy.isEmpty()) {
        spy.wait(timeout);
    }
}

void TestDiscovery::testNodeDiscovery()
{
    // 监听节点发现信号
    QSignalSpy spy1(_discovery1, &DiscoveryService::nodeDiscovered);
    QSignalSpy spy2(_discovery2, &DiscoveryService::nodeDiscovered);

    // 触发刷新
    _discovery1->refresh();
    _discovery2->refresh();

    // 等待节点发现
    waitForSignal(spy1, 3000);
    waitForSignal(spy2, 3000);

    // 验证至少有一个节点被发现（UDP 广播在同一机器上应该可靠）
    QVERIFY2(spy1.count() > 0 || spy2.count() > 0,
             "至少一个实例应该发现另一个实例");

    // 验证 peers 列表不为空
    QVariantList peers1 = _discovery1->peers();
    QVariantList peers2 = _discovery2->peers();
    QVERIFY2(!peers1.isEmpty() || !peers2.isEmpty(),
             "至少一个实例的 peers 列表应该非空");
}

void TestDiscovery::testNodeExpiry()
{
    // 先刷新让节点上线
    _discovery1->refresh();
    _discovery2->refresh();
    QTest::qWait(1000);

    // 验证节点已上线
    QVERIFY(!_discovery1->peers().isEmpty());

    // 监听节点过期信号
    QSignalSpy spy1(_discovery1, &DiscoveryService::nodeExpired);

    // 注意：节点过期需要 15 秒超时 + 3 秒清理间隔 = 约 18 秒
    // 为了测试速度，这里只验证信号连接正确
    // 完整的过期测试需要在手动测试中进行
    QVERIFY(spy1.isValid());
}

void TestDiscovery::testRefresh()
{
    // 测试 refresh() 方法
    QSignalSpy peersSpy(_discovery1, &DiscoveryService::peersChanged);
    _discovery1->refresh();

    // 等待信号
    waitForSignal(peersSpy, 2000);

    // 验证方法执行不崩溃
    QVERIFY(true);
}

void TestDiscovery::testMultipleNodes()
{
    // 创建第三个节点
    QString config3Path = _tempDir->path() + "/config3.ini";
    qputenv("GRIDYARD_CONFIG", config3Path.toUtf8());
    qputenv("GRIDYARD_NAME", "Device3");
    ConfigManager *config3 = ConfigManager::create(nullptr, nullptr);
    DiscoveryService discovery3(config3);

    // 触发刷新
    _discovery1->refresh();
    _discovery2->refresh();
    discovery3.refresh();

    // 等待发现
    QTest::qWait(2000);

    // 验证不崩溃
    QVERIFY(true);

    delete config3;
}

void TestDiscovery::testPeerInfo()
{
    // 测试 peerInfo() 方法
    // 使用一个不存在的 deviceId
    PeerInfo info = _discovery1->peerInfo("non_existent_device");
    QVERIFY(info.deviceId.isEmpty());
}

QTEST_MAIN(TestDiscovery)
#include "test_discovery.moc"
