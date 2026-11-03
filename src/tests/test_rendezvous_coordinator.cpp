/**
* @file    test_rendezvous_coordinator.cpp
* @version 8.3.0
* @date    2026-11-03
* @author  GY
* @brief   RendezvousCoordinator 协调编排测试
*
* 测试用例：默认停用不连接、启用后注册并拉取候选、注册上报全量地址（C-02，
* 虚拟网卡剔除）、停用后不再自动重连、修改端口即时切换服务器（修复"改配置
* 需重启"）、本机 IP 变化后重注册。
*/

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "config_manager.h"
#include "data_types.h"
#include "discovery_service.h"
#include "ip_address.h"
#include "net_interfaces.h"
#include "p2p_server.h"
#include "protocol.h"
#include "rendezvous_client.h"
#include "rendezvous_coordinator.h"
#include "rendezvous_server.h"
#include "posix_tcp_socket.h"

#include "test_utils/json_line.h"
#include "test_utils/wait.h"

#ifdef _WIN32
// Windows 头链把 interface 定义成宏，展开会击穿下方以 interface 命名的循环变量
#undef interface
#endif

// JSON 行读写复用测试公共工具
using gy::test::readJsonLine;
using gy::test::writeJsonLine;

// 测试侧独立虚拟网卡名单（与 C-04 拍板名单一致，不引用实现函数，
// 保持断言对实现的独立性）：docker 网桥/veth 对/libvirt/VMware/VirtualBox
static bool isVirtualNicName(const QString &name)
{
    static const QStringList kVirtualPrefixes = {
        QStringLiteral("docker"), QStringLiteral("br-"), QStringLiteral("veth"),
        QStringLiteral("virbr"), QStringLiteral("vmnet"), QStringLiteral("vboxnet")};
    for (const QString &prefix : kVirtualPrefixes) {
        if (name.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

// 本机应上报的候选地址全集：非回环、已启用、非虚拟网卡的 IPv4
static QStringList expectedAnnounceAddresses()
{
    QStringList expected;
    const QList<gy::net::NetInterface> interfaces = gy::net::listInterfaces();
    for (const gy::net::NetInterface &interface : interfaces) {
        if (!interface.isUp || interface.isLoopback || !interface.address.isValid()) {
            continue;
        }
        if (isVirtualNicName(interface.name)) {
            continue;
        }
        if (!expected.contains(interface.address.toString())) {
            expected.append(interface.address.toString());
        }
    }
    return expected;
}

class TestRendezvousCoordinator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testDisabledByDefault();
    void testEnableConnectsAndFetchesPeers();
    void testRegisterAnnouncesAllAddresses();
    void testDisableStaysDisconnected();
    void testPortChangeReconnects();
    void testLocalIpChangeReregisters();

private:
    // 在指定协调服务器上注册一台辅助设备
    gy::net::PosixTcpSocket *registerHelperDevice(RendezvousServer *server, const QString &deviceId);
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
gy::net::PosixTcpSocket *TestRendezvousCoordinator::registerHelperDevice(RendezvousServer *server,
                                                            const QString &deviceId)
{
    auto *socket = new gy::net::PosixTcpSocket(this);
    socket->connectToHost(gy::net::IpAddress::localHost(), server->serverPort());

    // 辅助函数返回指针，不能使用会展开成 return 的 QVERIFY 宏
    if (!gy::test::waitConnected(socket)) {
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
    RendezvousServer server{0, {}};
    QVERIFY(server.start());
    _config->setRendezvousPort(static_cast<int>(server.serverPort()));

    gy::net::PosixTcpSocket *helper = registerHelperDevice(&server, QStringLiteral("device-b"));
    QVERIFY(helper);

    _config->setRendezvousEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(_client->isConnected(), 3000);

    // 编排器注册 device-a 并拉取候选，device-b 应进入发现列表
    QTRY_VERIFY_WITH_TIMEOUT(discoveryContains(QStringLiteral("device-b")), 3000);
    QVERIFY(!discoveryContains(_config->deviceId()));  // 不收录本机

    helper->close();
    helper->deleteLater();
}

// 注册上报全量地址（C-02）：协调注册的 addresses 应包含本机全部非回环 IPv4，
// 且不含 docker/网桥类虚拟网卡地址（C-04 名单随本项同批落地）。本机存在
// UP 状态的 docker 自定义网桥（br-）与 VPN TUN 设备，旧行为只上报 localIp
// 单地址，多网卡/VPN 场景下本用例对旧行为为红
void TestRendezvousCoordinator::testRegisterAnnouncesAllAddresses()
{
    RendezvousServer server{0, {}};
    QVERIFY(server.start());
    _config->setRendezvousPort(static_cast<int>(server.serverPort()));
    _config->setRendezvousEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(_client->isConnected(), 3000);

    gy::net::PosixTcpSocket *helper = registerHelperDevice(&server, QStringLiteral("device-b"));
    QVERIFY(helper);

    // helper 视角拉取候选，读本机（device-a）注册上报的 addresses 全集；
    // 注册与 list_peers 存在先后，未读到本机条目时退避重试
    QStringList announced;
    QElapsedTimer deadline;
    deadline.start();
    while (announced.isEmpty() && !deadline.hasExpired(3000)) {
        QJsonObject listPeers;
        listPeers[QStringLiteral("type")] = QStringLiteral("list_peers");
        listPeers[QStringLiteral("room")] = QStringLiteral("default");
        listPeers[QStringLiteral("device_id")] = QStringLiteral("device-b");
        writeJsonLine(helper, listPeers);

        const QJsonObject response = readJsonLine(helper);
        const QJsonArray items = response[QStringLiteral("items")].toArray();
        for (const QJsonValue &item : items) {
            if (item[QStringLiteral("device_id")].toString() == _config->deviceId()) {
                const QJsonArray addresses = item[QStringLiteral("addresses")].toArray();
                for (const QJsonValue &address : addresses) {
                    announced.append(address.toString());
                }
                break;
            }
        }
        if (announced.isEmpty()) {
            QTest::qWait(100);
        }
    }
    QVERIFY2(!announced.isEmpty(), "未从协调服务器读到本机注册条目");

    // 全量上报：本机全部非回环 IPv4 都在注册载荷里（旧行为只报 localIp，此断言为红）
    const QStringList expected = expectedAnnounceAddresses();
    QVERIFY2(!expected.isEmpty(), "本机无非回环 IPv4，用例前提不成立");
    for (const QString &address : expected) {
        QVERIFY2(announced.contains(address),
                 qPrintable(QStringLiteral("注册载荷缺少本机地址: %1（载荷: %2）")
                                .arg(address, announced.join(QLatin1Char(',')))));
    }

    // 虚拟网卡剔除：docker/网桥地址不得混入候选
    const QList<gy::net::NetInterface> interfaces = gy::net::listInterfaces();
    for (const gy::net::NetInterface &interface : interfaces) {
        if (!isVirtualNicName(interface.name) || !interface.address.isValid()) {
            continue;
        }
        QVERIFY2(!announced.contains(interface.address.toString()),
                 qPrintable(QStringLiteral("注册载荷混入虚拟网卡地址: %1 (%2)")
                                .arg(interface.address.toString(), interface.name)));
    }

    // 默认路由地址置首，保持对端"首地址即直连端点"的既有语义
    if (!_config->localIp().isEmpty()) {
        QCOMPARE(announced.value(0), _config->localIp());
    }

    helper->close();
    helper->deleteLater();
    _config->setRendezvousEnabled(false);
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
    RendezvousServer firstServer{0, {}};
    QVERIFY(firstServer.start());
    RendezvousServer secondServer{0, {}};
    QVERIFY(secondServer.start());

    _config->setRendezvousPort(static_cast<int>(firstServer.serverPort()));
    _config->setRendezvousEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(_client->isConnected(), 3000);

    gy::net::PosixTcpSocket *helper = registerHelperDevice(&secondServer, QStringLiteral("device-c"));
    QVERIFY(helper);
    _config->setRendezvousPort(static_cast<int>(secondServer.serverPort()));

    // 无需重启：编排器应重连到新端口并拉取到新服务器上的设备
    QTRY_VERIFY_WITH_TIMEOUT(discoveryContains(QStringLiteral("device-c")), 5000);

    helper->close();
    helper->deleteLater();
    _config->setRendezvousEnabled(false);
}

// 换网/IP 变化后重注册：本机地址实际变化（refreshLocalIp 检出并发 localIpChanged）
// 时，已连接的编排器应立即以新地址重注册，不必等断线重连才恢复正确候选地址。
// 网络变化监听与邀请码生成前强制刷新两路最终都汇入 refreshLocalIp → localIpChanged，
// 这里直接发该信号验证重注册接线。可观测面选 peersReceived：registerSelf 在注册后
// 必拉一次候选，而客户端 5 秒心跳只重注册加中继轮询、不拉候选，不会污染计数
void TestRendezvousCoordinator::testLocalIpChangeReregisters()
{
    RendezvousServer server{0, {}};
    QVERIFY(server.start());
    _config->setRendezvousPort(static_cast<int>(server.serverPort()));
    _config->setRendezvousEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(_client->isConnected(), 3000);

    QSignalSpy peersSpy(_client, &RendezvousClient::peersReceived);
    QVERIFY(peersSpy.isValid());
    // 先手动拉取一次建立基线，避免初次注册的应答落在建 spy 前后造成计数歧义
    _client->listPeers(gy::protocol::kDefaultRendezvousRoom);
    QTRY_VERIFY_WITH_TIMEOUT(peersSpy.count() >= 1, 3000);
    const int baseline = peersSpy.count();

    emit _config->localIpChanged();
    QTRY_VERIFY_WITH_TIMEOUT(peersSpy.count() > baseline, 3000);

    _config->setRendezvousEnabled(false);
}

QTEST_MAIN(TestRendezvousCoordinator)
#include "test_rendezvous_coordinator.moc"
