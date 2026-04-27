/**
* @file    discovery_service.cpp
* @date    2026-06-02
* @author  GY
* @brief   DiscoveryService 实现
*
* Change Log:
* [v0.1] GY   2026-06-02
* * Stage 2：初始版本
*/

#include "discovery_service.h"
#include "config_manager.h"
#include "protocol.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>

// 心跳间隔（秒）
static constexpr int kBroadcastIntervalSec = 5;
// 节点超时时间（秒）
static constexpr int kNodeTimeoutSec = 15;
// 清理检查间隔（秒）
static constexpr int kPruneIntervalSec = 3;

DiscoveryService::DiscoveryService(ConfigManager *config, QObject *parent)
    : QObject{parent}
    , _config{config}
{
    // 初始化 UDP Socket
    _socket = new QUdpSocket(this);
    _socket->bind(QHostAddress::AnyIPv4, gy::protocol::kDefaultDiscoveryPort,
                  QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);

    connect(_socket, &QUdpSocket::readyRead,
            this,    &DiscoveryService::onDatagramReceived);

    // 初始化广播定时器
    _broadcastTimer = new QTimer(this);
    connect(_broadcastTimer, &QTimer::timeout,
            this,            &DiscoveryService::sendHelloPacket);
    _broadcastTimer->start(kBroadcastIntervalSec * 1000);

    // 初始化清理定时器
    _pruneTimer = new QTimer(this);
    connect(_pruneTimer, &QTimer::timeout,
            this,        &DiscoveryService::pruneOfflineNodes);
    _pruneTimer->start(kPruneIntervalSec * 1000);

    // 立即发送一次 Hello
    sendHelloPacket();
}

QVariantList DiscoveryService::peers() const
{
    QVariantList list;
    list.reserve(_peers.size());

    for (const PeerInfo &info : _peers) {
        list.append(QVariant::fromValue(info));
    }

    return list;
}

void DiscoveryService::sendHelloPacket()
{
    const QByteArray data = buildHelloPayload();

    // 遍历所有激活的网络接口，向每个网卡的广播地址发送
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        // 过滤：必须是激活中的、非回环的、支持广播的物理网卡
        if (!(iface.flags() & QNetworkInterface::IsUp))       continue;
        if (!(iface.flags() & QNetworkInterface::CanBroadcast)) continue;
        if (  iface.flags() & QNetworkInterface::IsLoopBack)  continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            // 只处理 IPv4 地址
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;

            // 向该网卡对应子网的广播地址精确发送
            _socket->writeDatagram(data, entry.broadcast(),
                                   gy::protocol::kDefaultDiscoveryPort);
        }
    }
}

void DiscoveryService::onDatagramReceived()
{
    while (_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(_socket->pendingDatagramSize());

        QHostAddress sender;
        quint16 senderPort = 0;

        _socket->readDatagram(datagram.data(), datagram.size(),
                              &sender, &senderPort);

        // 解析 JSON
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(datagram, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;  // 无效 JSON，丢弃
        }

        handleHelloPacket(doc.object(), sender);
    }
}

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

QByteArray DiscoveryService::buildHelloPayload() const
{
    QJsonObject json;
    json["device_id"]   = _config->deviceId();
    json["device_name"] = _config->deviceName();
    json["app_version"] = QCoreApplication::applicationVersion();
    json["tcp_port"]    = _config->tcpPort();

    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

void DiscoveryService::handleHelloPacket(const QJsonObject &json, const QHostAddress &sender)
{
    // 提取字段
    const QString deviceId   = json["device_id"].toString();
    const QString deviceName = json["device_name"].toString();
    const quint16 tcpPort    = static_cast<quint16>(json["tcp_port"].toInt());

    // 过滤无效数据
    if (deviceId.isEmpty()) return;

    // 本机过滤：忽略自己发出的广播
    if (deviceId == _config->deviceId()) return;

    // 构建 PeerInfo
    PeerInfo info;
    info.deviceId   = deviceId;
    info.deviceName = deviceName;
    info.ipAddress  = sender.toString();
    info.tcpPort    = tcpPort;
    info.isOnline   = true;
    info.lastSeen   = QDateTime::currentDateTimeUtc();

    updatePeer(deviceId, info);
}

void DiscoveryService::updatePeer(const QString &deviceId, const PeerInfo &info)
{
    const bool isNew = !_peers.contains(deviceId);

    _peers.insert(deviceId, info);

    if (isNew) {
        emit nodeDiscovered(deviceId);
    }

    notifyPeersChanged();
}

void DiscoveryService::notifyPeersChanged()
{
    emit peersChanged();
}
