#pragma once

#include "net_protocol.h"
#include <enet/enet.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace pse::qa {

enum class FaultMode {
    NONE,                       // Normal conformant host (verifies actual receipt on host)
    MALFORMED_WELCOME_SLOT,     // Sends MSG_WELCOME with assignedSlot = 0xFF (invalid >= 8)
    MALFORMED_WELCOME_GEN,      // Sends MSG_WELCOME with connectionGeneration = 0
    PREMATURE_DISCONNECT,       // Disconnects client abruptly mid-stream during W hold
    STALE_RECONNECT_GEN,        // Reconnect receives stale generation (gen1 instead of gen1 + 1)
    FORCE_RECONNECT_REJECT,     // Reconnect is rejected by host
    HANDSHAKE_TIMEOUT,          // Deliberately drops MSG_HELLO / never responds with WELCOME
    STALE_DUPLICATE_PACKETS,    // Injects duplicate and stale snapshots
    ACCEPT_FORGED_KEY,          // Host BUGGILY accepts forged key (to test false PASS detection!)
    DROP_UNRELIABLE_INPUT       // Drops specific unreliable input packets (contrasting send vs receive)
};

const char* faultModeToString(FaultMode mode);

struct FaultConfig {
    FaultMode mode = FaultMode::NONE;
    uint32_t disconnectAfterCommands = 5;
    uint32_t dropSequence = 15;
    uint8_t malformedSlot = 0xFF;
    uint32_t malformedGen = 0;
    uint64_t validReconnectKey = 0x8899AABBCCDDEEFFULL;
    std::string expectedParticipantId = "m12a_test_p2";
};

struct HostTelemetry {
    uint32_t hellosReceived = 0;
    uint32_t welcomesSent = 0;
    uint32_t rejectsSent = 0;
    uint32_t commandsReceived = 0;
    uint32_t forwardCommandsReceived = 0;
    uint32_t releaseCommandsReceived = 0;
    uint32_t neutralCommandsReceived = 0;
    uint32_t commandsDropped = 0;
    uint32_t connectsReceived = 0;
    uint32_t disconnectsReceived = 0;

    std::vector<uint32_t> receivedSequences;
    std::vector<uint32_t> receivedGenerations;
    std::vector<float> receivedForwards;
    std::vector<uint8_t> receivedSlots;
    std::vector<std::string> eventLog;
};

class AdversarialHost {
public:
    AdversarialHost();
    ~AdversarialHost();

    bool start(uint16_t port, const FaultConfig& config);
    void stop();
    void poll(uint32_t timeoutMs = 1);

    bool isRunning() const { return m_running.load(); }
    HostTelemetry getTelemetry() const;

private:
    void handleReceive(ENetPeer* peer, ENetPacket* packet, uint8_t channel);
    void handleHello(ENetPeer* peer, pse::ByteReader& reader, uint32_t seq);
    void handleInputCommand(ENetPeer* peer, pse::ByteReader& reader, uint32_t seq);
    void sendPacket(ENetPeer* peer, uint8_t channel, const std::vector<uint8_t>& data, bool reliable);
    void logEvent(const std::string& evt);

private:
    FaultConfig m_config;
    uint16_t m_port = 0;
    ENetHost* m_enetHost = nullptr;
    std::atomic<bool> m_running{ false };

    uint32_t m_sessionId = 771100;
    uint32_t m_snapshotSeq = 0;
    float m_lastForward = 0.0f;
    bool m_hadForward = false;

    // Active session tracking
    uint8_t m_assignedSlot = 1; // P2
    uint32_t m_generation = 1;
    uint64_t m_activeReconnectKey = 0;
    bool m_hasActiveParticipant = false;

    using MutexType = std::recursive_mutex;
    using LockGuard = std::lock_guard<MutexType>;
    mutable MutexType m_telemetryMutex;
    HostTelemetry m_telemetry;
};

} // namespace pse::qa
