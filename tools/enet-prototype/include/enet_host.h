#pragma once

#include "net_protocol.h"
#include "simulation.h"
#include "net_simulator.h"
#include "metrics.h"
#include <enet/enet.h>
#include <string>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <thread>

namespace pse {

struct HostConfig {
    std::string bindAddress = "127.0.0.1";
    uint16_t port = 27015;
    uint8_t maxClients = 7;
    uint16_t tickRate = 60;
    uint16_t snapshotRate = 30;
    std::string joinToken = "";
    std::string hostName = "Host_Player";
    std::string participantId = "host_local_p1";
    float simDropPercent = 0.0f;
    uint32_t simLatencyMs = 0;

    // Thread Bridge mode: the authoritative simulation lives behind the bridge.
    // The host only keeps the session ledger (slots, generations, reconnect keys);
    // its internal WorldSimulation is never ticked, never broadcasts snapshots and
    // never consumes gameplay messages. INPUT_COMMAND, MAP_READY and
    // RESPAWN_REQUEST are routed to the custom packet handler instead.
    bool bridgeMode = false;
    // Admission range [1..maxClientSlots], independent of transport peer count.
    // Default preserves the general host. Reserved IDs are not rotated.
    uint8_t maxClientSlots = MAX_PLAYERS - 1;
    // Opt-in bound on rejected unauthenticated peers waiting for REJECT ACK.
    uint32_t rejectedPeerTimeoutMs = 0;
};

class ENetHostServer {
public:
    ENetHostServer();
    ~ENetHostServer();

    bool start(const HostConfig& config);
    void stop();

    // Pump network events and step simulation
    void poll(uint32_t timeoutMs = 1);

    // World control
    void changeMap(const std::string& newMapName);
    void triggerDamageOnSlot(uint8_t slotId, int32_t amount);

    // State inspection
    bool isRunning() const { return m_running.load(); }
    uint32_t getSessionId() const { return m_sessionId; }
    uint32_t getConnectedClientCount() const;
    WorldSimulation& getSimulation() { return m_sim; }
    const WorldSimulation& getSimulation() const { return m_sim; }
    MetricsCollector& getMetrics() { return m_metrics; }
    const MetricsCollector& getMetrics() const { return m_metrics; }

    // Direct peer to slot lookup
    int8_t getSlotForPeer(ENetPeer* peer) const;
    ENetPeer* getPeerForSlot(uint8_t slotId) const;

    // Authenticated session of a peer (slot + connection generation). False for
    // unknown or not yet authenticated peers. Network thread only.
    bool getPeerSession(ENetPeer* peer, uint8_t& outSlot, uint32_t& outGeneration) const;

    // Bridge mode: map announced in WELCOME, published by the simulation thread.
    void setSessionMap(const std::string& mapName, uint32_t mapEpoch);
    uint32_t getSessionMapEpoch() const { return m_sessionMapEpoch; }

    // Gracefully disconnect the peer of a slot (DISCONNECT event follows).
    bool kickSlot(uint8_t slotId, uint8_t reason);
    bool isBridgeMode() const { return m_config.bridgeMode; }

    // Simulation of network impairments
    NetworkSimulator& getNetSimulator() { return m_netSim; }

    // Broadcast & Send helpers
    void sendPacket(ENetPeer* peer, uint8_t channel, const std::vector<uint8_t>& data, bool reliable);
    void broadcastPacket(uint8_t channel, const std::vector<uint8_t>& data, bool reliable, ENetPeer* ignorePeer = nullptr);

    // Custom packet handler for modular replication extensions
    using HostPacketHandler = std::function<bool(ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t seq)>;
    void setCustomPacketHandler(HostPacketHandler handler) { m_customHandler = handler; }

    // Hooks for replication subsystem
    std::function<void(ENetPeer* peer, uint8_t slotId, uint32_t connectionGen, bool isReconnect)> onClientAuthenticatedCallback;
    std::function<void(uint8_t slotId)> onClientDisconnectedCallback;
    std::function<void(uint8_t slotId, uint32_t connectionGen)> onClientSessionEndedCallback;
    std::function<void(float dt)> onTickCallback;
    std::function<void()> onSnapshotBroadcastCallback;
    std::function<void(const std::string& newMap, uint32_t epoch)> onMapChangeCallback;

private:
    void handleConnect(ENetPeer* peer);
    void handleReceive(ENetPeer* peer, ENetPacket* packet, uint8_t channel);
    void handleDisconnect(ENetPeer* peer);

    // Dispatch messages
    void onMsgHello(ENetPeer* peer, ByteReader& reader, uint32_t seq);
    void onMsgInputCommand(ENetPeer* peer, ByteReader& reader, uint32_t seq);
    void onMsgMapReady(ENetPeer* peer, ByteReader& reader, uint32_t seq);
    void onMsgRespawnRequest(ENetPeer* peer, ByteReader& reader, uint32_t seq);
    void onMsgPing(ENetPeer* peer, ByteReader& reader, uint32_t seq);

    void sendWelcome(ENetPeer* peer, uint8_t slotId, uint32_t connectionGeneration, uint64_t reconnectKey);
    void sendReject(ENetPeer* peer, RejectReason reason, const std::string& msg);
    void rejectPeer(ENetPeer* peer, RejectReason reason, const std::string& msg);
    void broadcastPlayerJoin(uint8_t slotId, const std::string& partId, const std::string& name, uint32_t gen);
    void broadcastPlayerLeave(uint8_t slotId, const std::string& partId, LeaveReason reason);
    void broadcastSnapshot();

private:
    HostConfig m_config;
    ENetHost* m_enetHost = nullptr;
    std::atomic<bool> m_running{ false };
    uint32_t m_sessionId = 0;
    HostPacketHandler m_customHandler;
    uint32_t m_snapshotSequence = 0;
    uint64_t m_lastSnapshotTimeMs = 0;
    uint64_t m_lastTickTimeMs = 0;
    std::string m_sessionMapName;
    uint32_t m_sessionMapEpoch = 1;

    WorldSimulation m_sim;
    NetworkSimulator m_netSim;
    MetricsCollector m_metrics;

    // Participant session ledger
    struct PeerInfo {
        uint8_t slotId = 0xFF;
        std::string participantId;
        std::string displayName;
        bool authenticated = false;
        uint32_t connectionGeneration = 1;
        uint64_t connectTimeMs = 0;
        uint64_t rejectDeadlineMs = 0;
        uint8_t rejectReason = 0;
    };
    std::unordered_map<ENetPeer*, PeerInfo> m_peers;
    std::unordered_map<std::string, uint8_t> m_participantSlotMap;
};

} // namespace pse
