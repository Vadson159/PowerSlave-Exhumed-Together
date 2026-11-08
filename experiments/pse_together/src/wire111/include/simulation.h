#pragma once

#include "net_protocol.h"
#include "player_state.h"
#include <array>
#include <string>
#include <functional>
#include <mutex>

namespace pse {

class WorldSimulation {
public:
    WorldSimulation(uint16_t tickRateHz = 60);

    void setTickRate(uint16_t tickRateHz);
    uint16_t getTickRate() const { return m_tickRate; }
    uint32_t getCurrentTick() const { return m_currentTick; }
    uint32_t getMapEpoch() const { return m_mapEpoch; }
    const std::string& getMapName() const { return m_mapName; }

    // Map control
    void changeMap(const std::string& newMapName);

    // Player slots management
    PlayerParticipant& getSlot(uint8_t slotId) { return m_slots[slotId]; }
    const PlayerParticipant& getSlot(uint8_t slotId) const { return m_slots[slotId]; }
    bool isSlotActive(uint8_t slotId) const;

    // Connect / Disconnect in simulation
    bool initHostSlot(const std::string& participantId, const std::string& name);
    int8_t allocateClientSlot(const std::string& participantId, const std::string& name, uint64_t reconnectKey, ENetPeer* peer, uint64_t& outAssignedKey, RejectReason& outRejectReason, ENetPeer** outOldPeerToDisconnect = nullptr, uint8_t maxClientSlots = MAX_PLAYERS - 1);
    void disconnectSlot(uint8_t slotId, uint8_t reason);

    // Enqueue command for a slot (thread-safe, bounded)
    bool enqueueCommand(uint8_t slotId, const MsgInputCommand& cmd);

    // Simulate damage on a player (for death & respawn tests)
    bool applyDamage(uint8_t slotId, int32_t damageAmount);

    // Handle manual respawn request (strictly host-authoritative)
    enum class RespawnResult {
        SUCCESS,
        REJECT_NOT_CONNECTED,
        REJECT_NOT_DEAD,
        REJECT_EPOCH_MISMATCH,
        REJECT_STALE_DEATH_SEQ,
        REJECT_ALREADY_REQUESTED
    };
    RespawnResult processRespawnRequest(const MsgRespawnRequest& req, MsgRespawnConfirm& outConfirm);

    // Single simulation tick step
    void tick();

    // Snapshot generation
    MsgPlayerSnapshot generateSnapshot(uint32_t snapshotSeq) const;

    // Spawn point calculation with congestion avoidance
    SpawnPoint calculateSpawnPoint(uint8_t slotId) const;

    // Starter loadout configuration (allows KEX adapter / tests to configure dynamic start health/ammo)
    void setStarterLoadout(const StarterLoadout& loadout) {
        std::lock_guard<std::mutex> lock(m_simMutex);
        m_starterLoadout = loadout;
    }
    StarterLoadout getStarterLoadout() const {
        std::lock_guard<std::mutex> lock(m_simMutex);
        return m_starterLoadout;
    }

private:
    uint16_t m_tickRate;
    float m_dt;
    uint32_t m_currentTick = 0;
    uint32_t m_mapEpoch = 1;
    std::string m_mapName = "KARNAK_SANCTUARY";
    StarterLoadout m_starterLoadout{};
    
    SpawnPoint m_baseSpawn{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<PlayerParticipant, MAX_PLAYERS> m_slots;
    mutable std::mutex m_simMutex;
};

} // namespace pse
