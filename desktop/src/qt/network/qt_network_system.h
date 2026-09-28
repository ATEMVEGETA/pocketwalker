#pragma once

#include <vector>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTimer>
#include <QThread>
#include <QHostAddress>
#include <QUdpSocket>
#include <QVector>

#include "core/pokewalker/pocketwalker.h"
#include "core/utils/logger.h"

class QtNetworkSystem : public QObject
{
    Q_OBJECT

public:
    enum class Mode
    {
        Client,
        Server,
        AutoPeer
    };

    explicit QtNetworkSystem(PocketWalker& emulator, Mode mode,
                             const QString& host, quint16 port,
                             int packet_timeout_ms, const QString& peer_id = {},
                             QObject* parent = nullptr);

signals:
    void statusChanged(const QString& status);
    void hostDiscovered(const QString& host);

public slots:
    void start();

private slots:
    void flushPacket();
    void tryConnect();
    void startServer();
    void onReadyRead();
    void broadcastPresence();
    void readDiscoveryDatagrams();
    void evaluateAutoPeers();
    void launchClientDiscoveryBatch();

private:
    struct PeerPresence
    {
        QHostAddress address;
        quint16 port = 0;
        QString state;
        QString target_id;
        qint64 last_seen_ms = 0;
    };

    enum class AutoState
    {
        Available,
        Pairing,
        Busy
    };

    void attachSocket(QTcpSocket* new_socket, bool reconnect_client);
    void rejectSocket(QTcpSocket* rejected_socket);
    void startAutoPeer();
    void beginAutoPairing(const QString& target_id, const PeerPresence& peer);
    void resetAutoPairing(const QString& reason = {});
    void markAutoConnected();
    void startClientDiscovery();
    void finishClientProbe(QTcpSocket* probe);
    void scheduleClientDiscoveryRetry();
    QString autoStateName() const;
    QString localAddressText() const;
    bool isExpectedAutoPeer(const QHostAddress& address) const;

    PocketWalker& emulator;
    Mode mode;
    QString host;
    QString configured_client_host;
    quint16 port;
    int packet_timeout_ms;
    QString peer_id;

    QTcpSocket* socket = nullptr;
    QTcpServer* server = nullptr;
    QUdpSocket* discovery_socket = nullptr;

    QTimer* accumulator_timer = nullptr;
    QTimer* reconnect_timer = nullptr;
    QTimer* discovery_timer = nullptr;
    QTimer* peer_evaluation_timer = nullptr;
    QTimer* pairing_timeout_timer = nullptr;
    QTimer* client_discovery_timer = nullptr;

    std::vector<uint8_t> tx_buffer;
    QHash<QString, PeerPresence> peers;
    AutoState auto_state = AutoState::Available;
    QString pairing_peer_id;
    QHostAddress pairing_peer_address;
    quint16 pairing_peer_port = 0;
    QString stable_peer_set;
    qint64 stable_peer_set_since_ms = 0;
    QElapsedTimer auto_clock;
    QVector<QHostAddress> client_discovery_candidates;
    QSet<QTcpSocket*> client_discovery_probes;
    qsizetype client_discovery_index = 0;
    int client_connection_failures = 0;
    bool client_discovery_retry_pending = false;
    bool probing_loopback = false;
};
