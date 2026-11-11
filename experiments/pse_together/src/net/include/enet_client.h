#pragma once

#include "net_protocol.h"
#include "net_simulator.h"
#include "metrics.h"
#include <enet/enet.h>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <functional>

namespace pse {

struct ClientConfig {
    std::string hostAddress = "127.0.0.1";
    uint16_t hostPort = 27015;
    std::string participantId = "client_default";
    std::string displayName = "Player_P2";
    std::string joinToken = "";
    uint64_t reconnectKey = 0; // Secret key for reconnecting to prior slot
    float simDropPercent = 0.0f;
    uint32_t simLatencyMs = 0;

    // Thread Bridge mode: PLAYER_SNAPSHOT, RESPAWN_CONFIRM and MAP_CHANGE are routed
    // to the custom packet handler, and MAP_READY is NOT sent automatically: the
    // client simulation sends it after it has actually loaded the map.
    bool bridgeMode = false;
};

class ENetClientNode {
public:
    ENetClientNode();
    ~ENetClientNode();

    bool connect(const ClientConfig& config);
    void disconnect(uint8_t reason = LEAVE_USER_QUIT);
    void poll(uint32_t timeoutMs = 1);

    // Gameplay input & actions
    void sendInputCommand(float forward, float right, float pitch, float yaw, uint32_t buttons, uint8_t weapon);
    bool triggerManualRespawn(); // Emulates pressing 'E'

    // Status inspection
    bool isConnected() const { return m_connected.load(); }
    bool isAuthenticated() const { return m_authenticated.load(); }
    uint8_t getAssignedSlot() const { return m_assignedSlot; }
    uint32_t getConnectionGeneration() const { return m_connectionGeneration; }
    uint64_t getReconnectKey() const { return m_reconnectKey; }
    void clearReconnectKey() { m_reconnectKey = 0; m_config.reconnectKey = 0; }
    uint32_t getMapEpoch() const { return m_mapEpoch; }
    // Bridge mode: the host bound its session to a new world (MapChange with a new epoch inside the same connection).
    void setSessionMap(uint32_t epoch, const std::string& name) { m_mapEpoch = epoch; m_mapName = name; }
    const std::string& getMapName() const { return m_mapName; }

    const PlayerStateSnapshot* getLocalPlayerState() const;
    const std::vector<PlayerStateSnapshot>& getLastSnapshotPlayers() const { return m_lastSnapshot.players; }

    MetricsCollector& getMetrics() { return m_metrics; }
    const MetricsCollector& getMetrics() const { return m_metrics; }

    // Callbacks for test hooks
    std::function<void(const MsgWelcome&)> onWelcomeCallback;
    std::function<void(const MsgReject&)> onRejectCallback;
    std::function<void(const MsgPlayerSnapshot&)> onSnapshotCallback;
    std::function<void(const MsgRespawnConfirm&)> onRespawnConfirmCallback;
    std::function<void(const MsgMapChange&)> onMapChangeCallback;
    std::function<void()> onDisconnectedCallback;

    // Send raw packet over ENet transport
    void sendPacket(uint8_t channel, const std::vector<uint8_t>& data, bool reliable);

    // Custom packet handler for modular replication extensions
    using ClientPacketHandler = std::function<bool(uint8_t msgType, ByteReader& reader, uint32_t seq)>;
    void setCustomPacketHandler(ClientPacketHandler handler) { m_customHandler = handler; }

    // Simulation of network impairments
    NetworkSimulator& getNetSimulator() { return m_netSim; }

private:
    void handleReceive(ENetPacket* packet, uint8_t channel);
    void onMsgWelcome(ByteReader& reader, uint32_t seq);
    void onMsgReject(ByteReader& reader, uint32_t seq);
    void onMsgPlayerSnapshot(ByteReader& reader, uint32_t seq);
    void onMsgRespawnConfirm(ByteReader& reader, uint32_t seq);
    void onMsgMapChange(ByteReader& reader, uint32_t seq);
    void onMsgPong(ByteReader& reader, uint32_t seq);

    void sendPing();

private:
    ClientConfig m_config;
    bool m_enetInitialized = false;
    ENetHost* m_enetClient = nullptr;
    ENetPeer* m_serverPeer = nullptr;

    std::atomic<bool> m_connected{ false };
    std::atomic<bool> m_authenticated{ false };

    uint32_t m_sessionId = 0;
    uint8_t  m_assignedSlot = 0xFF;
    uint32_t m_connectionGeneration = 0;
    uint64_t m_reconnectKey = 0;
    uint32_t m_mapEpoch = 0;
    std::string m_mapName;
    uint16_t m_serverTickRate = 60;
    uint16_t m_serverSnapshotRate = 30;

    uint32_t m_commandSequence = 0;
    uint32_t m_clientTick = 0;
    uint32_t m_highestReceivedSnapshotSeq = 0;
    bool     m_respawnRequested = false;

    MsgPlayerSnapshot m_lastSnapshot;
    uint64_t m_lastPingTimeMs = 0;
    uint32_t m_pingSequence = 0;

    ClientPacketHandler m_customHandler;

    NetworkSimulator m_netSim;
    MetricsCollector m_metrics;
};

} // namespace pse
