/**
 * @file    discovery_stub.cpp
 * @date    2026-05-24
 * @author  GY
 * @brief   DiscoveryStub 实现
 *
 * Change Log:
 * [v1.0] GY   2026-05-24
 * * Initial creation
 */

#include "discovery_stub.h"

#include <QMutexLocker>
#include <QUuid>

DiscoveryStub::DiscoveryStub(QObject *parent)
    : QObject{parent}
{
}

QVariantList DiscoveryStub::peers() const
{
    QMutexLocker locker(&_mutex);
    QVariantList out;
    out.reserve(_peers.size());
    for (const PeerInfo &p : _peers) {
        out.append(QVariant::fromValue(p));
    }
    return out;
}

void DiscoveryStub::addPeer(const QString &deviceName, const QString &ipAddress)
{
    PeerInfo info;
    info.deviceId   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    info.deviceName = deviceName;
    info.ipAddress  = ipAddress;
    info.isOnline   = true;

    {
        QMutexLocker locker(&_mutex);
        _peers.append(info);
    }

    emit nodeDiscovered(info.deviceId);
    emit peersChanged();
}

void DiscoveryStub::removeAllPeers()
{
    QList<QString> expiredIds;
    {
        QMutexLocker locker(&_mutex);
        for (const PeerInfo &p : _peers) {
            expiredIds.append(p.deviceId);
        }
        _peers.clear();
    }

    for (const QString &id : expiredIds) {
        emit nodeExpired(id);
    }
    emit peersChanged();
}

void DiscoveryStub::markOffline(const QString &deviceId)
{
    bool found = false;
    {
        QMutexLocker locker(&_mutex);
        for (PeerInfo &p : _peers) {
            if (p.deviceId == deviceId) {
                p.isOnline = false;
                found = true;
                break;
            }
        }
    }
    if (found) {
        emit peersChanged();
    }
}
