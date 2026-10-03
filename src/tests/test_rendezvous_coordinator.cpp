/**
* @file    test_rendezvous_coordinator.cpp
* @version 7.15.6
* @date    2026-10-03
* @author  GY
* @brief   RendezvousCoordinator 协调编排测试
*
* 测试用例：默认停用不连接、启用后注册并拉取候选、停用后不再自动重连、
* 修改端口即时切换服务器（修复"改配置需重启"）。
*
* Change Log:
* [v7.15.6] GY   2026-10-03
* * 版本头对齐到 v7.15.6
*/

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpSocket>

#include "config_manager.h"
#include "data_types.h"
#include "discovery_service.h"
#include "p2p_server.h"
#include "rendezvous_client.h"
#include "rendezvous_coordinator.h"
#include "rendezvous_server.h"

namespace {

// 等待读取一行 JSON（事件循环驱动）
QJsonObject readJsonLine(QTcpSocket *socket, int timeoutMs = 3000)
{
    QByteArray buffer;
    QElapsedTimer elapsed;
    elapsed.start();

    while (!buffer.contains('\n')) {
        if (elapsed.hasExpired(timeoutMs)) {
            return {};
        }
        buffer += socket->readAll();
        if (buffer.contains('\n')) {
            break;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    const int newlineIndex = buffer.indexOf('\n');
    const QJsonDocument doc = QJsonDocument::fromJson(buffer.left(newlineIndex));
    return doc.object();
}

// 发送一行 JSON
void writeJsonLine(QTcpSocket *socket, const QJsonObject &json)
{
    socket->write(QJsonDocument(json).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}

}

class TestRendezvousCoordinator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testDisabledByDefault();
    void testEnableConnectsAndFetchesPeers();
    void testDisableStaysDisconnected();
    void testPortChangeReconnects();

private:
    // 在指定协调服务器上注册一台辅助设备
    QTcpSocket *registerHelperDevice(RendezvousServer *server, const QString &deviceId);
    // 判断发现服务是否已收录指定设备
    bool discoveryContains(const QString &deviceId) const;

    QTemporaryDir *_tempDir = nullptr;
    ConfigManager *_config = nullptr;
    DiscoveryService *_discovery = nullptr;
    P2pServer *_p2pServer = nullptr;
    RendezvousClient *_client = nullptr;
    RendezvousCoordinator *_coordinator = nullptr;
};

void TestRendezvousCoordinator::initTestCase()
{
    _tempDir = new QTemporaryDir();
    QVERIFY(_tempDir->isValid());

    qputenv("GRIDYARD_CONFIG", (_tempDir->path() + "/config.ini").toUtf8());
    qputenv("GRIDYARD_NAME", "CoordinatorTestDevice");

    _config = ConfigManager::create(nullptr, nullptr);
    _config->setRendezvousEnabled(false);
    _config->setRendezvousHost(QStringLiteral("127.0.0.1"));

    _discovery = new DiscoveryService(_config, this);
    _p2pServer = new P2pServer(_config, this);
    _client = new RendezvousClient(this);
    _coordinator = new RendezvousCoordinator(_config, _discovery, _p2pServer,
                                            _client, this);
    _coordinator->applyConfig();
}

void TestRendezvousCoordinator::cleanupTestCase()
{
    delete _coordinator;
    delete _client;
    delete _p2pServer;
    delete _discovery;
    delete _config;
    delete _tempDir;

    qunsetenv("GRIDYARD_CONFIG");
    qunsetenv("GRIDYARD_NAME");
}

// 在指定协调服务器上注册一台辅助设备
QTcpSocket *TestRendezvousCoordinator::registerHelperDevice(RendezvousServer *server,
                                                            const QString &deviceId)
{
    auto *socket = new QTcpSocket(this);
    socket->connectToHost(QHostAddress::LocalHost, server->serverPort());

    // 辅助函数返回指针，不能使用会展开成 return 的 QVERIFY 宏
    QElapsedTimer elapsed;
    elapsed.start();
    while (socket->state() != QAbstractSocket::ConnectedState
           && !elapsed.hasExpired(3000)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (socket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "辅助设备连接协调服务器超时";
        return nullptr;
    }

    QJsonObject registerRequest;
    registerRequest[QStringLiteral("type")] = QStringLiteral("register");
    registerRequest[QStringLiteral("room")] = QStringLiteral("default");
    registerRequest[QStringLiteral("device_id")] = deviceId;
    registerRequest[QStringLiteral("device_name")] = deviceId;
    registerRequest[QStringLiteral("addresses")] = QJsonArray{QStringLiteral("127.0.0.1")};
    registerRequest[QStringLiteral("tcp_port")] = 1;
    writeJsonLine(socket, registerRequest);
    const QString responseType = readJsonLine(socket)[QStringLiteral("type")].toString();
    if (responseType != QStringLiteral("register_ack")) {
        qWarning() << "辅助设备注册失败:" << responseType;
        return nullptr;
    }
    return socket;
}

// 判断发现服务是否已收录指定设备
bool TestRendezvousCoordinator::discoveryContains(const QString &deviceId) const
{
    const QVariantList peers = _discovery->peers();
    for (const QVariant &entry : peers) {
        if (entry.value<PeerInfo>().deviceId == deviceId) {
            return true;
        }
    }
    return false;
}

// 默认停用：不建立连接，不拉取候选
void TestRendezvousCoordinator::testDisabledByDefault()
{
    QVERIFY(!_config->rendezvousEnabled());
    _coordinator->applyConfig();

    QTest::qWait(200);
    QVERIFY(!_client->isConnected());
    QCOMPARE(_discovery->peers().size(), 0);
}

// 启用后连接注册并拉取候选：协调服务器上的辅助设备进入发现列表
void TestRendezvousCoordinator::testEnableConnectsAndFetchesPeers()
{
    RendezvousServer server{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(server.start());
    _config->setRendezvousPort(static_cast<int>(server.serverPort()));

    QTcpSocket *helper = registerHelperDevice(&server, QStringLiteral("device-b"));
    QVERIFY(helper);

    _config->setRendezvousEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(_client->isConnected(), 3000);

    // 编排器注册 device-a 并拉取候选，device-b 应进入发现列表
    QTRY_VERIFY_WITH_TIMEOUT(discoveryContains(QStringLiteral("device-b")), 3000);
    QVERIFY(!discoveryContains(_config->deviceId()));  // 不收录本机

    helper->close();
    helper->deleteLater();
}

// 停用后立即断开，且不会在重连延迟后自行恢复连接
void TestRendezvousCoordinator::testDisableStaysDisconnected()
{
    _config->setRendezvousEnabled(false);
    QTRY_VERIFY_WITH_TIMEOUT(!_client->isConnected(), 3000);

    // 覆盖客户端 3 秒重连延迟，确认停用后不再自动恢复
    QTest::qWait(3300);
    QVERIFY(!_client->isConnected());
}

// 修改端口后即时切换到新服务器并完成注册与拉取
void TestRendezvousCoordinator::testPortChangeReconnects()
{
    RendezvousServer firstServer{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(firstServer.start());
    RendezvousServer secondServer{QStringLiteral("127.0.0.1"), 0, QString()};
    QVERIFY(secondServer.start());

    _config->setRendezvousPort(static_cast<int>(firstServer.serverPort()));
    _config->setRendezvousEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(_client->isConnected(), 3000);

    QTcpSocket *helper = registerHelperDevice(&secondServer, QStringLiteral("device-c"));
    QVERIFY(helper);
    _config->setRendezvousPort(static_cast<int>(secondServer.serverPort()));

    // 无需重启：编排器应重连到新端口并拉取到新服务器上的设备
    QTRY_VERIFY_WITH_TIMEOUT(discoveryContains(QStringLiteral("device-c")), 5000);

    helper->close();
    helper->deleteLater();
    _config->setRendezvousEnabled(false);
}

QTEST_MAIN(TestRendezvousCoordinator)
#include "test_rendezvous_coordinator.moc"
