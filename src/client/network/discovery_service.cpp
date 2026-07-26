/**
* @file    discovery_service.cpp
* @version 7.7.0
* @date    2026-07-21
* @author  GridYard Team
* @brief   局域网设备发现服务实现
*
* Change Log:
* [v7.7.0] GY   2026-07-21
* * 设备来源标签：broadcast（UDP广播）、rendezvous（协调节点）
* [v7.5.0] GY   2026-07-21
* * 新增 onRendezvousPeersReceived 处理协调节点返回的候选端点
* [v6.6.2] GY   2026-06-25
* * 同步文件头版本与当前主版本
* [v6.1.0] GY   2026-06-25
* * 设备首次发现或元数据变化时发射 peerUpdated 信号
* [v4.16.1] GY   2026-06-21
* * 提供 transferEndpoint() 对端快照查询，避免拆分读取节点字段
* [v4.15.0] FengChunlin   2026-06-16
* * 协议版本不兼容处理：主版本不一致标记不兼容，次版本差异安全降级
* [v4.7.1] FengChunlin   2026-06-05
* * 修复文件传输使用真实 IP 地址
* [v0.3.0] FengChunlin   2026-05-19
* * 添加 refresh() 方法实现
* [v0.2.0] FengChunlin   2026-04-27
* * Stage 2：初始版本
*/

#include "discovery_service.h"
#include "config_manager.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QNetworkProxy>

// 心跳间隔：5 秒，兼顾发现速度和局域网广播噪声
static constexpr int kBroadcastIntervalSec = 5;
// 节点超时时间：15 秒，允许丢失两次心跳后再判离线
static constexpr int kNodeTimeoutSec = 15;
// 清理检查间隔：3 秒，让离线状态不会长时间滞后
static constexpr int kPruneIntervalSec = 3;

// 构造函数，初始化 UDP socket、广播定时器和清理定时器
DiscoveryService::DiscoveryService(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
{
    // 初始化 UDP Socket
    _socket = new QUdpSocket(this);

    // 禁用代理（UDP 不支持某些代理类型）
    QNetworkProxy noProxy;
    noProxy.setType(QNetworkProxy::NoProxy);
    _socket->setProxy(noProxy);

    // ShareAddress + ReuseAddressHint 允许多进程共享同一 UDP 端口，
    // 这样同一台机器上可以同时运行多个 GridYard 实例互相发现
    bool bound = _socket->bind(QHostAddress::AnyIPv4, gy::protocol::kDefaultDiscoveryPort,
                                QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    if (!bound) {
        qWarning() << "DiscoveryService: 绑定端口" << gy::protocol::kDefaultDiscoveryPort
                    << "失败:" << _socket->errorString();
        qWarning() << "DiscoveryService: 尝试绑定到任意端口";

        // 绑定到任意可用端口
        bound = _socket->bind(QHostAddress::AnyIPv4, 0,
                              QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

        if (bound) {
            qDebug() << "DiscoveryService: 绑定到备用端口:" << _socket->localPort();
        } else {
            qCritical() << "DiscoveryService: 无法绑定 UDP socket:"
                        << _socket->errorString();
            return;
        }
    } else {
        qDebug() << "DiscoveryService: UDP socket 绑定成功，本地端口:"
                 << _socket->localPort();
    }

    connect(_socket, &QUdpSocket::readyRead,
            this,    &DiscoveryService::onDatagramReceived);

    // 设备名称变化时立即广播，让其他设备尽快更新
    connect(_config, &ConfigManager::deviceNameChanged,
            this,    &DiscoveryService::sendHelloPacket);

    _broadcastTimer = new QTimer(this);
    connect(_broadcastTimer, &QTimer::timeout,
            this,            &DiscoveryService::sendHelloPacket);
    _broadcastTimer->start(kBroadcastIntervalSec * 1000);

    _pruneTimer = new QTimer(this);
    connect(_pruneTimer, &QTimer::timeout,
            this,        &DiscoveryService::pruneOfflineNodes);
    _pruneTimer->start(kPruneIntervalSec * 1000);

    // 延迟 200ms 发送第一次 Hello，确保 socket 绑定和事件循环都已就绪。
    QTimer::singleShot(200, this, &DiscoveryService::sendHelloPacket);
}

// 获取所有在线设备列表，转换为 QVariantList 供 QML 使用
QVariantList DiscoveryService::peers() const
{
    QVariantList list;
    list.reserve(_peers.size());

    for (const PeerInfo &info : _peers) {
        list.append(QVariant::fromValue(info));
    }

    return list;
}

// 查询可用于发送传输的对端快照；离线设备返回空 map
QVariantMap DiscoveryService::transferEndpoint(const QString &deviceId) const
{
    auto it = _peers.find(deviceId);
    // 只返回仍在线的设备，避免向已超时节点发起连接
    if (it == _peers.end() || !it.value().isOnline) {
        return {};
    }

    const PeerInfo &peer = it.value();
    // 只暴露传输需要的三个字段，不泄露完整 PeerInfo（含 lastSeen 等内部时间戳）
    return {{"deviceName", peer.deviceName}, {"ipAddress", peer.ipAddress},
            {"tcpPort", peer.tcpPort}};
}

// 向所有激活网卡的广播地址发送 Hello 包
void DiscoveryService::sendHelloPacket()
{
    // 检查 socket 是否已绑定
    if (!_socket || _socket->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "DiscoveryService: socket 未绑定，跳过广播";
        return;
    }

    const QByteArray data = buildHelloPayload();
    qDebug() << "DiscoveryService: 发送广播，deviceId:" << _config->deviceId()
             << "name:" << _config->deviceName()
             << "本地端口:" << _socket->localPort();

    // 遍历所有激活的网络接口，向每个网卡的广播地址发送
    const auto interfaces = QNetworkInterface::allInterfaces();
    int sentCount = 0;
    for (const QNetworkInterface &iface : interfaces) {
        // 过滤：必须是激活中的、非回环的、支持广播的物理网卡
        if (!(iface.flags() & QNetworkInterface::IsUp))       continue;
        if (!(iface.flags() & QNetworkInterface::CanBroadcast)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack)  continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            // 只处理 IPv4 地址
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;

            // 向该网卡对应子网的广播地址精确发送
            // 发送到默认端口
            qint64 sent = _socket->writeDatagram(data, entry.broadcast(),
                                                  gy::protocol::kDefaultDiscoveryPort);
            if (sent == -1) {
                qWarning() << "DiscoveryService: 广播发送失败到"
                           << entry.broadcast().toString()
                           << ":" << _socket->errorString();
            } else {
                sentCount++;
            }

            // 备用端口用于单机多实例测试，默认端口失败时仍能互相发现。
            if (_socket->localPort() != gy::protocol::kDefaultDiscoveryPort) {
                _socket->writeDatagram(data, entry.broadcast(), _socket->localPort());
            }
        }
    }
    qDebug() << "DiscoveryService: 广播发送完成，共发送到" << sentCount << "个网卡";
}

// 处理接收到的 UDP 数据报，解析 JSON 后交给 handleHelloPacket
void DiscoveryService::onDatagramReceived()
{
    // 循环读取所有待处理数据报，避免 readyRead 在高频率广播下被合并
    while (_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(_socket->pendingDatagramSize());

        QHostAddress sender;
        quint16 senderPort = 0;

        _socket->readDatagram(datagram.data(), datagram.size(),
                              &sender, &senderPort);

        // 只处理合法 JSON 对象，其他格式静默丢弃
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(datagram, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        handleHelloPacket(doc.object(), sender);
    }
}

// 清理超时未响应的离线节点
void DiscoveryService::pruneOfflineNodes()
{
    const QDateTime threshold = QDateTime::currentDateTimeUtc().addSecs(-kNodeTimeoutSec);
    bool changed = false;

    QMutableHashIterator<QString, PeerInfo> it(_peers);
    while (it.hasNext()) {
        it.next();

        if (it.value().lastSeen < threshold) {
            // 节点超时，标记为离线
            if (it.value().isOnline) {
                it.value().isOnline = false;
                emit nodeExpired(it.key());
                changed = true;
            }
        }
    }

    if (changed) {
        notifyPeersChanged();
    }
}

// 构建 Hello 广播的 JSON 负载（设备信息 + 协议版本）
QByteArray DiscoveryService::buildHelloPayload() const
{
    QJsonObject json;
    // 委托 ConfigManager 填充设备信息（Tell, Don't Ask）
    _config->fillHelloPayload(json);
    json["app_version"] = QCoreApplication::applicationVersion();
    json["version"]     = gy::protocol::kProtocolVersion;

    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

// 解析收到的 Hello 包，进行版本兼容性检查后更新在线设备表
void DiscoveryService::handleHelloPacket(const QJsonObject &json, const QHostAddress &sender)
{
    // 提取字段
    const QString deviceId   = json["device_id"].toString();
    const QString deviceName = json["device_name"].toString();
    const quint16 tcpPort    = static_cast<quint16>(json["tcp_port"].toInt());
    const quint16 version    = static_cast<quint16>(json["version"].toInt());

    // 过滤无效数据
    if (deviceId.isEmpty()) {
        qDebug() << "DiscoveryService: 收到无效的 Hello 包（deviceId 为空）";
        return;
    }

    // 本机过滤：忽略自己发出的广播（委托 ConfigManager 判断）
    if (_config->isMyDevice(deviceId)) {
        qDebug() << "DiscoveryService: 忽略自己的广播，deviceId:" << deviceId;
        return;
    }

    // 协议版本兼容性检查：主版本不一致标记不兼容，次版本差异安全降级
    if (version > 0) {
        quint8 localMajor = gy::protocol::majorVersion(gy::protocol::kProtocolVersion);
        quint8 senderMajor = gy::protocol::majorVersion(version);
        quint8 localMinor = gy::protocol::minorVersion(gy::protocol::kProtocolVersion);
        quint8 senderMinor = gy::protocol::minorVersion(version);

        if (senderMajor != localMajor) {
            // 主版本不一致，不加入在线列表
            qWarning() << "DiscoveryService: 设备" << deviceId
                       << "主版本不兼容，本地:" << localMajor << "对端:" << senderMajor
                       << "，忽略该设备";
            return;
        }

        if (senderMinor != localMinor) {
            // 次版本差异，安全降级（记录警告但继续）
            qWarning() << "DiscoveryService: 设备" << deviceId
                       << "次版本不同，本地:" << localMinor << "对端:" << senderMinor
                       << "，安全降级处理";
        }
    }

    qDebug() << "DiscoveryService: 收到设备广播，deviceId:" << deviceId
             << "name:" << deviceName
             << "ip:" << sender.toString()
             << "tcpPort:" << tcpPort
             << "version:" << version;

    // 构建 PeerInfo
    PeerInfo info;
    info.deviceId   = deviceId;
    info.deviceName = deviceName;
    info.ipAddress  = sender.toString();
    info.tcpPort    = tcpPort;
    info.isOnline   = true;
    info.protocolVersion = version;
    info.lastSeen   = QDateTime::currentDateTimeUtc();
    info.source     = QStringLiteral("broadcast");  // UDP 广播来源

    updatePeer(deviceId, info);
}

// 更新或新增在线设备信息，新设备时发射 nodeDiscovered 信号
void DiscoveryService::updatePeer(const QString &deviceId, const PeerInfo &info)
{
    const bool isNew = !_peers.contains(deviceId);

    // 检查设备名称是否变化
    if (!isNew) {
        const PeerInfo &oldInfo = _peers.value(deviceId);
        if (oldInfo.deviceName != info.deviceName) {
            qDebug() << "DiscoveryService: 设备名称更新"
                     << "deviceId:" << deviceId
                     << "旧名称:" << oldInfo.deviceName
                     << "新名称:" << info.deviceName;
        }
    }

    _peers.insert(deviceId, info);

    // 应用层据此异步更新本地设备目录，发现服务不直接依赖 storage
    emit peerUpdated(info);

    if (isNew) {
        qDebug() << "DiscoveryService: 发现新设备" << deviceId << info.deviceName;
        emit nodeDiscovered(deviceId);
    }

    notifyPeersChanged();
}

// 通知 QML 层设备列表已变化
void DiscoveryService::notifyPeersChanged()
{
    emit peersChanged();
}

// 手动刷新：清空设备列表并重新广播发现
void DiscoveryService::refresh()
{
    qDebug() << "DiscoveryService: 手动刷新，清空设备列表并重新发现";

    // 清空所有已发现的设备
    _peers.clear();
    notifyPeersChanged();

    // 发送广播，让其他设备响应
    sendHelloPacket();
}

// 向指定地址发送定向 Hello 包，复用 buildHelloPayload() 构建内容
void DiscoveryService::sendDirectedHello(const QHostAddress &address, quint16 discoveryPort)
{
    if (!_socket || _socket->state() == QAbstractSocket::UnconnectedState) {
        qDebug() << "DiscoveryService: socket 未绑定，跳过定向 Hello";
        return;
    }

    const QByteArray data = buildHelloPayload();
    qDebug() << "DiscoveryService: 发送定向 Hello 到" << address.toString()
             << "端口" << discoveryPort;

    qint64 sent = _socket->writeDatagram(data, address, discoveryPort);
    if (sent == -1) {
        qWarning() << "DiscoveryService: 定向 Hello 发送失败:"
                   << address.toString() << ":" << _socket->errorString();
    } else {
        qDebug() << "DiscoveryService: 定向 Hello 发送成功，字节数:" << sent;
    }
}

// 添加手动端点（只在没有更高优先级在线来源时才插入）
void DiscoveryService::addManualPeer(const PeerInfo &peer)
{
    const QString deviceId = peer.deviceId;
    const QString highPrioritySource = QStringLiteral("broadcast");
    const QString midPrioritySource = QStringLiteral("directed");
    bool hasHigherPriorityOnline = _peers.contains(deviceId)
                                    && (_peers[deviceId].source == highPrioritySource
                                        || _peers[deviceId].source == midPrioritySource)
                                    && _peers[deviceId].isOnline;
    if (!hasHigherPriorityOnline) {
        _peers.insert(deviceId, peer);
    }
    qDebug() << "DiscoveryService: 添加手动端点" << deviceId << peer.ipAddress;
    notifyPeersChanged();
}

// 处理协调节点返回的候选端点，将其转换为 PeerInfo 并更新本地设备表
void DiscoveryService::onRendezvousPeersReceived(const QList<QVariantMap> &peers)
{
    for (const QVariantMap &peerData : peers) {
        const QString deviceId = peerData[QStringLiteral("deviceId")].toString();
        if (deviceId.isEmpty()) {
            continue;
        }

        // 过滤本机
        if (_config->isMyDevice(deviceId)) {
            continue;
        }

        PeerInfo info;
        info.deviceId = deviceId;
        info.deviceName = peerData[QStringLiteral("deviceName")].toString();
        info.tcpPort = static_cast<quint16>(peerData[QStringLiteral("tcpPort")].toInt());

        // 优先使用第一个地址，否则用 deviceId 作为占位
        const QVariant addressesVar = peerData[QStringLiteral("addresses")];
        if (addressesVar.canConvert<QVariantList>() && !addressesVar.toList().isEmpty()) {
            info.ipAddress = addressesVar.toList().first().toString();
        } else if (addressesVar.canConvert<QString>()) {
            info.ipAddress = addressesVar.toString();
        }

        info.isOnline = true;
        info.lastSeen = QDateTime::currentDateTime();
        info.source = QStringLiteral("rendezvous");  // 协调节点来源

        // 已有更高优先级来源（broadcast/directed）的在线设备时，保留真实直连 IP，
        // 防止协调节点返回的地址（如 NAT 映射地址）破坏 P2P 直连优先策略。
        // 只在无现有条目或现有条目优先级不高于 rendezvous 时才插入。
        const QString highPrioritySource = QStringLiteral("broadcast");
        const QString midPrioritySource = QStringLiteral("directed");
        bool isHighPriority = _peers.contains(deviceId)
                              && (_peers[deviceId].source == highPrioritySource
                                  || _peers[deviceId].source == midPrioritySource)
                              && _peers[deviceId].isOnline;
        if (!isHighPriority) {
            _peers.insert(deviceId, info);
        }
        qDebug() << "DiscoveryService: 从协调节点收到候选设备" << deviceId << info.deviceName;
    }

    notifyPeersChanged();
}
