#include "qt_network_system.h"

#include <algorithm>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QNetworkInterface>

namespace
{
constexpr quint16 DISCOVERY_PORT = 18082;
constexpr int DISCOVERY_INTERVAL_MS = 500;
constexpr int PEER_STALE_MS = 2500;
constexpr int PEER_SETTLE_MS = 1200;
constexpr int PAIRING_TIMEOUT_MS = 8000;
constexpr int CLIENT_DISCOVERY_INTERVAL_MS = 60;
constexpr int CLIENT_DISCOVERY_TIMEOUT_MS = 650;
constexpr int CLIENT_DISCOVERY_RETRY_MS = 1000;
constexpr int CLIENT_DISCOVERY_BATCH_SIZE = 8;
constexpr int CLIENT_DISCOVERY_MAX_HOSTS = 1024;
constexpr auto DISCOVERY_MAGIC = "PocketWalkerPeer/1";
}

QtNetworkSystem::QtNetworkSystem(PocketWalker& emulator, const Mode mode,
                                 const QString& host, const quint16 port,
                                 const int packet_timeout_ms, const QString& peer_id,
                                 QObject* parent)
    : QObject(parent)
      , emulator(emulator)
      , mode(mode)
      , host(host)
      , configured_client_host(host.trimmed())
      , port(port)
      , packet_timeout_ms(packet_timeout_ms)
      , peer_id(peer_id)
{
}

void QtNetworkSystem::start()
{
    accumulator_timer = new QTimer(this);
    accumulator_timer->setSingleShot(true);
    connect(accumulator_timer, &QTimer::timeout, this, &QtNetworkSystem::flushPacket);

    reconnect_timer = new QTimer(this);
    reconnect_timer->setInterval(1000);
    connect(reconnect_timer, &QTimer::timeout, this, &QtNetworkSystem::tryConnect);

    client_discovery_timer = new QTimer(this);
    client_discovery_timer->setInterval(CLIENT_DISCOVERY_INTERVAL_MS);
    connect(client_discovery_timer, &QTimer::timeout,
            this, &QtNetworkSystem::launchClientDiscoveryBatch);

    emulator.OnTransmitIR([this](const uint8_t byte)
    {
        QMetaObject::invokeMethod(this, [this, byte]()
        {
            tx_buffer.push_back(byte);
            accumulator_timer->start(packet_timeout_ms);
        }, Qt::QueuedConnection);
    });

    switch (mode)
    {
    case Mode::Server:
        startServer();
        break;
    case Mode::Client:
        if (configured_client_host != QHostAddress(QHostAddress::LocalHost).toString())
        {
            host = QHostAddress(QHostAddress::LocalHost).toString();
            probing_loopback = true;
        }
        tryConnect();
        break;
    case Mode::AutoPeer:
        startAutoPeer();
        break;
    }
}

void QtNetworkSystem::flushPacket()
{
    if (tx_buffer.empty())
        return;

    if (socket && socket->state() == QAbstractSocket::ConnectedState)
    {
        const QByteArray bytes(reinterpret_cast<const char*>(tx_buffer.data()),
            static_cast<qsizetype>(tx_buffer.size()));
        socket->write(bytes);
    }

    tx_buffer.clear();
}

void QtNetworkSystem::attachSocket(QTcpSocket* new_socket, const bool reconnect_client)
{
    if (socket && socket != new_socket)
    {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }

    socket = new_socket;
    socket->setParent(this);
    socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(socket, &QTcpSocket::readyRead, this, &QtNetworkSystem::onReadyRead);
    connect(socket, &QTcpSocket::disconnected, this, [this, new_socket, reconnect_client]()
    {
        if (socket != new_socket)
            return;

        Log::Info("IR network peer disconnected");
        socket->deleteLater();
        socket = nullptr;

        if (mode == Mode::AutoPeer)
            resetAutoPairing("Peer disconnected - searching again");
        else if (reconnect_client)
        {
            emit statusChanged(QString("Disconnected - reconnecting to %1:%2").arg(host).arg(port));
            reconnect_timer->start();
        }
        else
            emit statusChanged(QString("Waiting for melonDS at %1:%2").arg(localAddressText()).arg(port));
    });
}

void QtNetworkSystem::tryConnect()
{
    if (mode == Mode::AutoPeer)
    {
        if (auto_state != AutoState::Pairing || peer_id < pairing_peer_id ||
            pairing_peer_address.isNull())
            return;
    }

    if (socket)
    {
        const auto state = socket->state();
        if (state == QAbstractSocket::ConnectedState ||
            state == QAbstractSocket::ConnectingState ||
            state == QAbstractSocket::HostLookupState)
            return;
    }

    auto* client = new QTcpSocket(this);
    attachSocket(client, mode == Mode::Client);

    connect(client, &QTcpSocket::connected, this, [this, client]()
    {
        if (socket != client)
            return;

        reconnect_timer->stop();
        client_connection_failures = 0;
        if (mode == Mode::AutoPeer)
            markAutoConnected();
        else
        {
            Log::Info("Connected to {}:{}", host.toStdString(), port);
            emit statusChanged(QString("Connected to melonDS at %1:%2").arg(host).arg(port));
        }
    });

    connect(client, &QTcpSocket::errorOccurred, this,
        [this, client](const QAbstractSocket::SocketError err)
        {
            if (socket != client || err == QAbstractSocket::RemoteHostClosedError)
                return;

            client->abort();
            if (mode == Mode::Client)
            {
                if (probing_loopback)
                {
                    client->disconnect(this);
                    client->deleteLater();
                    if (socket == client)
                        socket = nullptr;

                    probing_loopback = false;
                    client_connection_failures = 0;
                    host = configured_client_host;
                    if (host.isEmpty())
                        startClientDiscovery();
                    else
                        QTimer::singleShot(0, this, &QtNetworkSystem::tryConnect);
                    return;
                }

                ++client_connection_failures;
                if (client_connection_failures >= 3)
                {
                    client->disconnect(this);
                    client->abort();
                    client->deleteLater();
                    if (socket == client)
                        socket = nullptr;
                    host.clear();
                    reconnect_timer->stop();
                    startClientDiscovery();
                }
                else
                {
                    emit statusChanged(QString("Waiting for melonDS server at %1:%2")
                        .arg(host).arg(port));
                    reconnect_timer->start();
                }
            }
        });

    if (mode == Mode::Client && host.trimmed().isEmpty())
    {
        client->disconnect(this);
        client->deleteLater();
        if (socket == client)
            socket = nullptr;
        startClientDiscovery();
    }
    else if (mode == Mode::AutoPeer)
    {
        emit statusChanged("Found another PocketWalker - connecting...");
        client->connectToHost(pairing_peer_address, pairing_peer_port);
    }
    else
    {
        emit statusChanged(QString("Connecting to %1:%2...").arg(host).arg(port));
        client->connectToHost(host, port);
    }
}

void QtNetworkSystem::startClientDiscovery()
{
    if (mode != Mode::Client || !host.trimmed().isEmpty() ||
        client_discovery_retry_pending ||
        (client_discovery_timer && client_discovery_timer->isActive()) ||
        !client_discovery_probes.isEmpty())
        return;

    client_discovery_candidates.clear();
    client_discovery_index = 0;
    QSet<quint32> seen;

    const QHostAddress loopback(QHostAddress::LocalHost);
    client_discovery_candidates.push_back(loopback);
    seen.insert(loopback.toIPv4Address());

    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces())
    {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry& entry : interface.addressEntries())
        {
            const QHostAddress local_address = entry.ip();
            if (local_address.protocol() != QAbstractSocket::IPv4Protocol ||
                local_address.isLoopback() || local_address.isLinkLocal())
                continue;
            if (!local_address.isInSubnet(QHostAddress("10.0.0.0"), 8) &&
                !local_address.isInSubnet(QHostAddress("172.16.0.0"), 12) &&
                !local_address.isInSubnet(QHostAddress("192.168.0.0"), 16))
                continue;

            const quint32 local = local_address.toIPv4Address();
            quint32 mask = entry.netmask().toIPv4Address();
            quint32 network = local & mask;
            quint32 broadcast = network | ~mask;
            const quint64 host_count = static_cast<quint64>(broadcast) - network - 1;
            if (host_count == 0 || host_count > CLIENT_DISCOVERY_MAX_HOSTS)
            {
                mask = 0xFFFFFF00u;
                network = local & mask;
                broadcast = network | ~mask;
            }

            for (quint64 address = static_cast<quint64>(network) + 1;
                 address < broadcast &&
                 client_discovery_candidates.size() < CLIENT_DISCOVERY_MAX_HOSTS;
                 ++address)
            {
                const quint32 candidate = static_cast<quint32>(address);
                if (candidate == local || seen.contains(candidate))
                    continue;
                seen.insert(candidate);
                client_discovery_candidates.push_back(QHostAddress(candidate));
            }
        }
    }

    if (client_discovery_candidates.isEmpty())
    {
        emit statusChanged("Could not detect a local Wi-Fi network");
        scheduleClientDiscoveryRetry();
        return;
    }

    emit statusChanged("Searching this network for melonDS...");
    client_discovery_timer->start();
    launchClientDiscoveryBatch();
}

void QtNetworkSystem::launchClientDiscoveryBatch()
{
    if (mode != Mode::Client || !host.trimmed().isEmpty())
    {
        client_discovery_timer->stop();
        return;
    }

    int launched = 0;
    while (client_discovery_index < client_discovery_candidates.size() &&
           launched < CLIENT_DISCOVERY_BATCH_SIZE)
    {
        const QHostAddress candidate = client_discovery_candidates[client_discovery_index++];
        auto* probe = new QTcpSocket(this);
        client_discovery_probes.insert(probe);

        connect(probe, &QTcpSocket::connected, this, [this, probe, candidate]()
        {
            if (mode != Mode::Client || !host.trimmed().isEmpty())
            {
                finishClientProbe(probe);
                return;
            }

            host = candidate.toString();
            client_connection_failures = 0;
            client_discovery_retry_pending = false;
            client_discovery_timer->stop();
            client_discovery_probes.remove(probe);

            const auto remaining_probes = client_discovery_probes.values();
            client_discovery_probes.clear();
            for (QTcpSocket* other : remaining_probes)
            {
                other->disconnect(this);
                other->abort();
                other->deleteLater();
            }

            probe->disconnect(this);
            attachSocket(probe, true);
            if (!candidate.isLoopback())
                emit hostDiscovered(host);
            emit statusChanged(QString("Connected to melonDS at %1:%2").arg(host).arg(port));
        });
        connect(probe, &QTcpSocket::errorOccurred, this,
                [this, probe](QAbstractSocket::SocketError) { finishClientProbe(probe); });
        probe->connectToHost(candidate, port);
        QTimer::singleShot(CLIENT_DISCOVERY_TIMEOUT_MS, probe,
                           [this, probe]() { finishClientProbe(probe); });
        ++launched;
    }

    if (client_discovery_index >= client_discovery_candidates.size())
    {
        client_discovery_timer->stop();
        if (client_discovery_probes.isEmpty())
            scheduleClientDiscoveryRetry();
    }
}

void QtNetworkSystem::finishClientProbe(QTcpSocket* probe)
{
    if (!client_discovery_probes.remove(probe))
        return;

    probe->disconnect(this);
    probe->abort();
    probe->deleteLater();
    if (client_discovery_index >= client_discovery_candidates.size() &&
        client_discovery_probes.isEmpty())
        scheduleClientDiscoveryRetry();
}

void QtNetworkSystem::scheduleClientDiscoveryRetry()
{
    if (client_discovery_retry_pending || mode != Mode::Client ||
        !host.trimmed().isEmpty())
        return;

    client_discovery_retry_pending = true;
    QTimer::singleShot(CLIENT_DISCOVERY_RETRY_MS, this, [this]()
    {
        client_discovery_retry_pending = false;
        startClientDiscovery();
    });
}

void QtNetworkSystem::startServer()
{
    if (server)
        return;

    server = new QTcpServer(this);
    connect(server, &QTcpServer::newConnection, this, [this]()
    {
        while (server->hasPendingConnections())
        {
            QTcpSocket* candidate = server->nextPendingConnection();
            const bool already_connected = socket &&
                socket->state() != QAbstractSocket::UnconnectedState;
            const bool allowed_auto_peer = mode != Mode::AutoPeer ||
                (auto_state == AutoState::Pairing &&
                 peer_id < pairing_peer_id &&
                 isExpectedAutoPeer(candidate->peerAddress()));

            if (already_connected || !allowed_auto_peer)
            {
                rejectSocket(candidate);
                continue;
            }

            Log::Info("IR client connected from {}",
                candidate->peerAddress().toString().toStdString());
            attachSocket(candidate, false);

            if (mode == Mode::AutoPeer)
                markAutoConnected();
            else
                emit statusChanged(QString("melonDS connected from %1")
                    .arg(candidate->peerAddress().toString()));
        }
    });

    if (!server->listen(QHostAddress::AnyIPv4, port))
    {
        Log::Info("Failed to start server: {}", server->errorString().toStdString());
        emit statusChanged(QString("Could not listen on port %1").arg(port));
    }
    else if (mode == Mode::Server)
    {
        Log::Info("Listening on port {}", port);
        emit statusChanged(QString("Waiting for melonDS at %1:%2")
            .arg(localAddressText()).arg(port));
    }
}

void QtNetworkSystem::rejectSocket(QTcpSocket* rejected_socket)
{
    rejected_socket->abort();
    rejected_socket->deleteLater();
}

void QtNetworkSystem::onReadyRead()
{
    if (!socket)
        return;

    const QByteArray data = socket->readAll();
    for (const uint8_t byte : data)
        emulator.ReceiveIR(byte);
}

void QtNetworkSystem::startAutoPeer()
{
    if (peer_id.isEmpty())
    {
        emit statusChanged("Automatic Peer Play unavailable: missing device identity");
        return;
    }

    auto_clock.start();
    startServer();

    discovery_socket = new QUdpSocket(this);
    const auto bind_flags = QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint;
    if (!discovery_socket->bind(QHostAddress::AnyIPv4, DISCOVERY_PORT, bind_flags))
    {
        emit statusChanged("Automatic Peer Play unavailable on this network");
        return;
    }
    connect(discovery_socket, &QUdpSocket::readyRead,
            this, &QtNetworkSystem::readDiscoveryDatagrams);

    discovery_timer = new QTimer(this);
    discovery_timer->setInterval(DISCOVERY_INTERVAL_MS);
    connect(discovery_timer, &QTimer::timeout, this, &QtNetworkSystem::broadcastPresence);
    discovery_timer->start();

    peer_evaluation_timer = new QTimer(this);
    peer_evaluation_timer->setInterval(200);
    connect(peer_evaluation_timer, &QTimer::timeout,
            this, &QtNetworkSystem::evaluateAutoPeers);
    peer_evaluation_timer->start();

    pairing_timeout_timer = new QTimer(this);
    pairing_timeout_timer->setSingleShot(true);
    pairing_timeout_timer->setInterval(PAIRING_TIMEOUT_MS);
    connect(pairing_timeout_timer, &QTimer::timeout, this, [this]()
    {
        resetAutoPairing("Could not connect - searching again");
    });

    emit statusChanged("Searching for another PocketWalker...");
    broadcastPresence();
}

QString QtNetworkSystem::autoStateName() const
{
    switch (auto_state)
    {
    case AutoState::Available: return "available";
    case AutoState::Pairing: return "pairing";
    case AutoState::Busy: return "busy";
    }
    return "available";
}

void QtNetworkSystem::broadcastPresence()
{
    if (!discovery_socket)
        return;

    const QJsonObject message{
        {"magic", DISCOVERY_MAGIC},
        {"id", peer_id},
        {"port", static_cast<int>(port)},
        {"state", autoStateName()},
        {"target", pairing_peer_id}
    };
    const QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);

    discovery_socket->writeDatagram(bytes, QHostAddress::Broadcast, DISCOVERY_PORT);
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces())
    {
        if (!(interface.flags() & QNetworkInterface::IsUp) ||
            !(interface.flags() & QNetworkInterface::IsRunning) ||
            (interface.flags() & QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry& entry : interface.addressEntries())
        {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol &&
                !entry.broadcast().isNull())
                discovery_socket->writeDatagram(bytes, entry.broadcast(), DISCOVERY_PORT);
        }
    }
}

void QtNetworkSystem::readDiscoveryDatagrams()
{
    while (discovery_socket && discovery_socket->hasPendingDatagrams())
    {
        const QNetworkDatagram datagram = discovery_socket->receiveDatagram();
        const QJsonDocument document = QJsonDocument::fromJson(datagram.data());
        if (!document.isObject())
            continue;

        const QJsonObject message = document.object();
        if (message.value("magic").toString() != DISCOVERY_MAGIC)
            continue;

        const QString id = message.value("id").toString();
        const QString state = message.value("state").toString();
        const int advertised_port = message.value("port").toInt();
        if (id.isEmpty() || id == peer_id ||
            (state != "available" && state != "pairing" && state != "busy") ||
            advertised_port < 1 || advertised_port > 65535)
            continue;

        PeerPresence presence;
        presence.address = datagram.senderAddress();
        presence.port = static_cast<quint16>(advertised_port);
        presence.state = state;
        presence.target_id = message.value("target").toString();
        presence.last_seen_ms = auto_clock.elapsed();
        peers.insert(id, presence);

        if (auto_state == AutoState::Available && state == "pairing" &&
            presence.target_id == peer_id)
            beginAutoPairing(id, presence);
    }
}

void QtNetworkSystem::evaluateAutoPeers()
{
    if (auto_state != AutoState::Available)
        return;

    const qint64 now = auto_clock.elapsed();
    for (auto it = peers.begin(); it != peers.end();)
    {
        if (now - it->last_seen_ms > PEER_STALE_MS)
            it = peers.erase(it);
        else
            ++it;
    }

    QStringList available_ids{peer_id};
    for (auto it = peers.cbegin(); it != peers.cend(); ++it)
    {
        if (it->state == "available")
            available_ids.push_back(it.key());
    }
    std::sort(available_ids.begin(), available_ids.end());

    const QString peer_set = available_ids.join('|');
    if (peer_set != stable_peer_set)
    {
        stable_peer_set = peer_set;
        stable_peer_set_since_ms = now;
        return;
    }

    if (available_ids.size() < 2 || now - stable_peer_set_since_ms < PEER_SETTLE_MS)
        return;

    const QString first = available_ids[0];
    const QString second = available_ids[1];
    if (peer_id != first && peer_id != second)
    {
        emit statusChanged("Two other PocketWalkers are already pairing");
        return;
    }

    const QString target_id = peer_id == first ? second : first;
    const auto target = peers.constFind(target_id);
    if (target == peers.cend())
        return;

    beginAutoPairing(target_id, target.value());
}

void QtNetworkSystem::beginAutoPairing(const QString& target_id, const PeerPresence& peer)
{
    auto_state = AutoState::Pairing;
    pairing_peer_id = target_id;
    pairing_peer_address = peer.address;
    pairing_peer_port = peer.port;
    pairing_timeout_timer->start();
    broadcastPresence();

    emit statusChanged("Found another PocketWalker - pairing...");
    if (peer_id > pairing_peer_id)
        tryConnect();
}

void QtNetworkSystem::resetAutoPairing(const QString& reason)
{
    if (pairing_timeout_timer)
        pairing_timeout_timer->stop();
    reconnect_timer->stop();

    if (socket)
    {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        socket = nullptr;
    }

    auto_state = AutoState::Available;
    pairing_peer_id.clear();
    pairing_peer_address.clear();
    pairing_peer_port = 0;
    stable_peer_set.clear();
    stable_peer_set_since_ms = auto_clock.elapsed();
    broadcastPresence();
    emit statusChanged(reason.isEmpty() ?
        "Searching for another PocketWalker..." : reason);
}

void QtNetworkSystem::markAutoConnected()
{
    auto_state = AutoState::Busy;
    if (pairing_timeout_timer)
        pairing_timeout_timer->stop();
    broadcastPresence();
    emit statusChanged("Peer Play connected to another PocketWalker");
}

bool QtNetworkSystem::isExpectedAutoPeer(const QHostAddress& address) const
{
    if (pairing_peer_address.isNull())
        return false;
    return address == pairing_peer_address ||
        address.toIPv4Address() == pairing_peer_address.toIPv4Address();
}

QString QtNetworkSystem::localAddressText() const
{
    QString fallback = "this phone";
    for (const QHostAddress& address : QNetworkInterface::allAddresses())
    {
        if (address.protocol() != QAbstractSocket::IPv4Protocol ||
            address.isLoopback() || address.isLinkLocal())
            continue;
        if (address.isInSubnet(QHostAddress("10.0.0.0"), 8) ||
            address.isInSubnet(QHostAddress("172.16.0.0"), 12) ||
            address.isInSubnet(QHostAddress("192.168.0.0"), 16))
            return address.toString();
        fallback = address.toString();
    }
    return fallback;
}
