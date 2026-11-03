#pragma once

#include "enet_client.h"
#include "net_protocol.h"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>

namespace pse::m12a {

enum class ClientState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AUTHENTICATED,
    DISCONNECTING,
    ERROR_STATE
};

const char* clientStateToString(ClientState st);

struct SentCommand {
    uint32_t sequence = 0;
    uint32_t tick = 0;
    uint8_t slotId = 0;
    uint32_t connectionGeneration = 0;
    float forward = 0.0f;
    float right = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    uint32_t buttons = 0;
    uint8_t weapon = 0;
    double timestampMs = 0.0;
};

struct ReceivedMessage {
    uint8_t msgType = 0;
    uint32_t sequence = 0;
    std::string summary;
    double timestampMs = 0.0;
};

class M12aClient {
public:
    M12aClient();
    ~M12aClient();

    // Connection lifecycle
    bool connect(const std::string& host, uint16_t port,
                 const std::string& participantId, const std::string& displayName,
                 const std::string& joinToken = "", uint64_t reconnectKey = 0);
    void disconnect(uint8_t reason = pse::LEAVE_USER_QUIT);
    void poll(uint32_t timeoutMs = 1);
    bool waitAuthenticated(uint32_t timeoutMs = 5000);

    // Gameplay commands
    bool sendInputCommand(float forward, float right, float pitch = 0.0f, float yaw = 0.0f,
                          uint32_t buttons = 0, uint8_t weapon = pse::WEAPON_PISTOL);

    // Session reset for clean reconnect without stale state reuse
    void resetSessionState();

    // Inspection
    ClientState getState() const { return m_state.load(); }
    bool isConnected() const { return m_state.load() == ClientState::CONNECTED || m_state.load() == ClientState::AUTHENTICATED; }
    bool isAuthenticated() const { return m_state.load() == ClientState::AUTHENTICATED; }

    uint8_t getAssignedSlot() const { return m_assignedSlot; }
    uint32_t getConnectionGeneration() const { return m_connectionGeneration; }
    uint64_t getReconnectKey() const { return m_reconnectKey; }
    uint32_t getSessionId() const { return m_sessionId; }
    uint32_t getMapEpoch() const { return m_mapEpoch; }
    const std::string& getMapName() const { return m_mapName; }
    uint16_t getServerTickRate() const { return m_serverTickRate; }
    uint16_t getServerSnapshotRate() const { return m_serverSnapshotRate; }

    const std::vector<SentCommand>& getSentCommands() const { return m_sentCommands; }
    const std::vector<ReceivedMessage>& getReceivedMessages() const { return m_receivedMessages; }
    const pse::MsgReject& getLastReject() const { return m_lastReject; }
    bool hasReject() const { return m_hasReject; }

    // Counts by generation for generational verification
    size_t getCommandsSentWithGen(uint32_t gen) const;
    size_t getForwardCommandsCount() const;
    size_t getNeutralCommandsCount() const;
    bool hadExplicitReleaseCommand() const;

private:
    void setState(ClientState newState, const std::string& ctx = "");
    void setupClientCallbacks();

private:
    std::unique_ptr<pse::ENetClientNode> m_clientNode;
    pse::ClientConfig m_currentConfig;
    std::atomic<ClientState> m_state{ ClientState::DISCONNECTED };

    uint32_t m_sessionId = 0;
    uint8_t  m_assignedSlot = 0xFF;
    uint32_t m_connectionGeneration = 0;
    uint64_t m_reconnectKey = 0;
    uint32_t m_mapEpoch = 0;
    std::string m_mapName;
    uint16_t m_serverTickRate = 60;
    uint16_t m_serverSnapshotRate = 30;

    // Local sequence counters
    uint32_t m_localCommandSeq = 0;
    uint32_t m_localClientTick = 0;

    // Journals
    std::vector<SentCommand> m_sentCommands;
    std::vector<ReceivedMessage> m_receivedMessages;

    pse::MsgReject m_lastReject;
    bool m_hasReject = false;

    std::chrono::steady_clock::time_point m_epochTime;
};

} // namespace pse::m12a
