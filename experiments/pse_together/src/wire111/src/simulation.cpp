#include "simulation.h"
#include <cmath>
#include <algorithm>
#include <numbers>
#include <random>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace pse {

static uint64_t generateRandomReconnectKey() {
    uint64_t val = 0;
#ifdef _WIN32
    while (val == 0) {
        NTSTATUS status = BCryptGenRandom(
            NULL,
            reinterpret_cast<PUCHAR>(&val),
            sizeof(val),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        );
        if (!BCRYPT_SUCCESS(status)) {
            // Fallback to hardware entropy if BCrypt provider is unavailable
            std::random_device rd;
            val = (static_cast<uint64_t>(rd()) << 32) | rd();
        }
    }
#else
    std::random_device rd;
    while (val == 0) {
        val = (static_cast<uint64_t>(rd()) << 32) | rd();
    }
#endif
    return val;
}

constexpr float PI = 3.14159265358979323846f;
constexpr float MOVE_SPEED = 280.0f; // Units per second
constexpr float MAP_BOUNDS = 4096.0f;

WorldSimulation::WorldSimulation(uint16_t tickRateHz)
    : m_tickRate(tickRateHz), m_dt(1.0f / tickRateHz)
{
    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        m_slots[i].slotId = i;
        m_slots[i].connected = false;
        m_slots[i].entityId = 1000 + i;
        m_slots[i].entityGeneration = 1;
    }
}

void WorldSimulation::setTickRate(uint16_t tickRateHz) {
    std::lock_guard<std::mutex> lock(m_simMutex);
    m_tickRate = tickRateHz > 0 ? tickRateHz : 60;
    m_dt = 1.0f / m_tickRate;
}

void WorldSimulation::changeMap(const std::string& newMapName) {
    std::lock_guard<std::mutex> lock(m_simMutex);
    m_mapName = newMapName;
    m_mapEpoch++;
    // Reposition all alive players at start
    for (auto& slot : m_slots) {
        if (slot.connected) {
            SpawnPoint sp = calculateSpawnPoint(slot.slotId);
            slot.posX = sp.x;
            slot.posY = sp.y;
            slot.posZ = sp.z;
            slot.velX = 0.0f;
            slot.velY = 0.0f;
            slot.velZ = 0.0f;
            slot.yaw = sp.yaw;
            slot.respawnRequested = false;
        }
    }
}

bool WorldSimulation::isSlotActive(uint8_t slotId) const {
    if (slotId >= MAX_PLAYERS) return false;
    std::lock_guard<std::mutex> lock(m_simMutex);
    return m_slots[slotId].connected;
}

bool WorldSimulation::initHostSlot(const std::string& participantId, const std::string& name) {
    std::lock_guard<std::mutex> lock(m_simMutex);
    auto& hostSlot = m_slots[HOST_SLOT_ID];
    hostSlot.participantId = participantId;
    hostSlot.displayName = name;
    hostSlot.connected = true;
    hostSlot.connectionGeneration = 1;
    hostSlot.peer = nullptr;
    SpawnPoint sp = calculateSpawnPoint(HOST_SLOT_ID);
    hostSlot.resetToStarterLoadout(sp, m_starterLoadout);
    return true;
}

int8_t WorldSimulation::allocateClientSlot(const std::string& participantId, const std::string& name, uint64_t reconnectKey, ENetPeer* peer, uint64_t& outAssignedKey, RejectReason& outRejectReason, ENetPeer** outOldPeerToDisconnect, uint8_t maxClientSlots) {
    std::lock_guard<std::mutex> lock(m_simMutex);

    if (outOldPeerToDisconnect) {
        *outOldPeerToDisconnect = nullptr;
    }

    if (participantId.empty()) {
        outRejectReason = REJECT_MALFORMED;
        return -1;
    }

    // Prohibit spoofing the Host's slot
    if (participantId == m_slots[HOST_SLOT_ID].participantId) {
        outRejectReason = REJECT_HIJACK_ATTEMPT;
        return -1;
    }

    // 1. Check for reconnecting participant by ID
    for (uint8_t i = 1; i < MAX_PLAYERS && i <= maxClientSlots; ++i) {
        if (m_slots[i].participantId == participantId) {
            // Verify secret reconnectKey
            if (reconnectKey == 0 || m_slots[i].reconnectKey != reconnectKey) {
                // If keys mismatch, reject immediately without touching slot
                outRejectReason = m_slots[i].connected ? REJECT_ALREADY_CONNECTED : REJECT_HIJACK_ATTEMPT;
                return -1;
            }

            // Authorized reconnect with valid cryptographic key!
            // If the old connection was still active (ghost / half-open peer), report it for disconnection
            if (m_slots[i].connected && m_slots[i].peer != nullptr && m_slots[i].peer != peer) {
                if (outOldPeerToDisconnect) {
                    *outOldPeerToDisconnect = m_slots[i].peer;
                }
            }

            // Invalidate old connectionGeneration and bind new peer
            m_slots[i].resetForNewConnection(peer, m_slots[i].connectionGeneration + 1);
            m_slots[i].displayName = name;
            outAssignedKey = m_slots[i].reconnectKey;
            return static_cast<int8_t>(i);
        }
    }

    // 2. Find a vacant admissible slot. Disconnected identities stay reserved.
    for (uint8_t i = 1; i < MAX_PLAYERS && i <= maxClientSlots; ++i) {
        if (!m_slots[i].connected && m_slots[i].participantId.empty()) {
            m_slots[i].participantId = participantId;
            m_slots[i].displayName = name;
            m_slots[i].reconnectKey = generateRandomReconnectKey();
            m_slots[i].resetForNewConnection(peer, 1);
            SpawnPoint sp = calculateSpawnPoint(i);
            m_slots[i].resetToStarterLoadout(sp, m_starterLoadout);
            outAssignedKey = m_slots[i].reconnectKey;
            return static_cast<int8_t>(i);
        }
    }

    // Server full
    outRejectReason = REJECT_SERVER_FULL;
    return -1;
}

void WorldSimulation::disconnectSlot(uint8_t slotId, uint8_t /*reason*/) {
    if (slotId >= MAX_PLAYERS) return;
    std::lock_guard<std::mutex> lock(m_simMutex);
    m_slots[slotId].connected = false;
    m_slots[slotId].peer = nullptr;
    m_slots[slotId].inboundCommandQueue.clear();
}

bool WorldSimulation::enqueueCommand(uint8_t slotId, const MsgInputCommand& cmd) {
    if (slotId >= MAX_PLAYERS) return false;
    std::lock_guard<std::mutex> lock(m_simMutex);
    auto& slot = m_slots[slotId];
    if (!slot.connected) return false;

    // Discard stale command sequences or generation mismatch
    if (cmd.connectionGeneration != slot.connectionGeneration) return false;
    if (cmd.commandSequence <= slot.lastReceivedCommandSeq && slot.lastReceivedCommandSeq != 0) return false;

    // Bounded queue overflow protection
    if (slot.inboundCommandQueue.size() >= MAX_INBOUND_QUEUE_SIZE) {
        slot.inboundCommandQueue.pop_front(); // Drop oldest
    }
    slot.lastReceivedCommandSeq = cmd.commandSequence;
    slot.inboundCommandQueue.push_back(cmd);
    return true;
}

bool WorldSimulation::applyDamage(uint8_t slotId, int32_t damageAmount) {
    if (slotId >= MAX_PLAYERS) return false;
    std::lock_guard<std::mutex> lock(m_simMutex);
    auto& slot = m_slots[slotId];
    if (!slot.connected || slot.lifeState != LIFE_ALIVE) return false;

    slot.health -= damageAmount;
    if (slot.health <= 0) {
        slot.health = 0;
        slot.lifeState = LIFE_DEAD;
        slot.deathCounter++;
        slot.respawnRequested = false;
    }
    return true;
}

WorldSimulation::RespawnResult WorldSimulation::processRespawnRequest(const MsgRespawnRequest& req, MsgRespawnConfirm& outConfirm) {
    if (req.slotId >= MAX_PLAYERS) return RespawnResult::REJECT_NOT_CONNECTED;
    std::lock_guard<std::mutex> lock(m_simMutex);
    auto& slot = m_slots[req.slotId];

    if (!slot.connected) {
        return RespawnResult::REJECT_NOT_CONNECTED;
    }
    if (slot.lifeState != LIFE_DEAD) {
        return RespawnResult::REJECT_NOT_DEAD;
    }
    if (req.mapEpoch != m_mapEpoch) {
        return RespawnResult::REJECT_EPOCH_MISMATCH;
    }
    if (req.deathSequence != slot.deathCounter) {
        return RespawnResult::REJECT_STALE_DEATH_SEQ;
    }
    if (slot.respawnRequested) {
        return RespawnResult::REJECT_ALREADY_REQUESTED;
    }

    // Gate passed: single-shot respawn confirmed!
    slot.respawnRequested = true;
    slot.lifeState = LIFE_RESPAWNING;

    SpawnPoint sp = calculateSpawnPoint(slot.slotId);
    slot.resetToStarterLoadout(sp, m_starterLoadout);
    slot.lastConfirmedDeathSeq = slot.deathCounter;

    outConfirm.slotId = slot.slotId;
    outConfirm.mapEpoch = m_mapEpoch;
    outConfirm.deathSequence = slot.deathCounter;
    outConfirm.spawnX = sp.x;
    outConfirm.spawnY = sp.y;
    outConfirm.spawnZ = sp.z;
    outConfirm.spawnYaw = sp.yaw;
    outConfirm.newEntityId = slot.entityId;
    outConfirm.entityGeneration = slot.entityGeneration;
    outConfirm.defaultWeapons = slot.weaponMask;
    outConfirm.defaultAmmo = slot.ammo;

    return RespawnResult::SUCCESS;
}

void WorldSimulation::tick() {
    std::lock_guard<std::mutex> lock(m_simMutex);
    m_currentTick++;

    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        auto& slot = m_slots[i];
        if (!slot.connected) continue;

        if (slot.lifeState == LIFE_ALIVE) {
            // Input scheduling hardening:
            // 1. Backlog management: clamp excess stale queue depth to bound worst-case latency
            constexpr size_t MAX_INPUT_BACKLOG = 16;
            while (slot.inboundCommandQueue.size() > MAX_INPUT_BACKLOG) {
                slot.inboundCommandQueue.pop_front();
            }

            // 2. Consume at most ONE valid monotonic command per simulation tick
            bool consumedCommand = false;
            MsgInputCommand cmd;

            while (!slot.inboundCommandQueue.empty()) {
                MsgInputCommand candidate = slot.inboundCommandQueue.front();
                slot.inboundCommandQueue.pop_front();

                if (candidate.commandSequence > slot.lastProcessedCommandSeq) {
                    cmd = candidate;
                    consumedCommand = true;
                    break;
                }
                // Obsolete or duplicate sequence: discarded without execution
            }

            if (consumedCommand) {
                slot.lastProcessedCommandSeq = cmd.commandSequence;
                slot.yaw = cmd.yaw;
                slot.pitch = cmd.pitch;

                // Clamped movement along viewing vector for exactly ONE simulation step of duration m_dt
                float rad = cmd.yaw * (PI / 180.0f);
                float cosA = std::cos(rad);
                float sinA = std::sin(rad);

                // Forward and Strafe components clamped to [-1, 1]
                float fwd = std::clamp(cmd.moveForward, -1.0f, 1.0f);
                float strafe = std::clamp(cmd.moveRight, -1.0f, 1.0f);

                float dx = (cosA * fwd - sinA * strafe) * MOVE_SPEED * m_dt;
                float dy = (sinA * fwd + cosA * strafe) * MOVE_SPEED * m_dt;

                slot.posX += dx;
                slot.posY += dy;
                slot.velX = dx / m_dt;
                slot.velY = dy / m_dt;

                // Boundary collision clamping
                slot.posX = std::clamp(slot.posX, -MAP_BOUNDS, MAP_BOUNDS);
                slot.posY = std::clamp(slot.posY, -MAP_BOUNDS, MAP_BOUNDS);

                // Weapon selection
                if (cmd.selectedWeapon < WEAPON_COUNT && (slot.weaponMask & (1u << cmd.selectedWeapon))) {
                    slot.activeWeapon = cmd.selectedWeapon;
                }

                // Predictable button handling: edge detection against previous tick state
                uint32_t newlyPressed = cmd.buttons & ~slot.buttons;
                slot.lastButtons = slot.buttons;
                slot.buttons = cmd.buttons;

                // Semi-automatic firing action (e.g. pistol fires on button-down edge)
                if (newlyPressed & BUTTON_ATTACK) {
                    if (slot.activeWeapon == WEAPON_PISTOL && slot.ammo > 0) {
                        slot.ammo--;
                    }
                }

                slot.hadInputThisTick = true;
            } else {
                // Starvation (delay/loss): zero movement, decay velocity, and release buttons
                slot.velX = 0.0f;
                slot.velY = 0.0f;
                slot.lastButtons = slot.buttons;
                slot.buttons = 0;
                slot.hadInputThisTick = false;
            }
        } else {
            // Clear inputs and velocities when dead or respawning
            slot.inboundCommandQueue.clear();
            slot.velX = 0.0f;
            slot.velY = 0.0f;
            slot.velZ = 0.0f;
            slot.buttons = 0;
            slot.lastButtons = 0;
            slot.hadInputThisTick = false;
        }
    }
}

MsgPlayerSnapshot WorldSimulation::generateSnapshot(uint32_t snapshotSeq) const {
    std::lock_guard<std::mutex> lock(m_simMutex);
    MsgPlayerSnapshot snap;
    snap.snapshotSequence = snapshotSeq;
    snap.serverTick = m_currentTick;
    snap.mapEpoch = m_mapEpoch;

    for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
        if (m_slots[i].connected) {
            snap.players.push_back(m_slots[i].toSnapshot());
        }
    }
    return snap;
}

SpawnPoint WorldSimulation::calculateSpawnPoint(uint8_t slotId) const {
    SpawnPoint sp = m_baseSpawn;
    if (slotId == 0) {
        return sp;
    }
    // Radial distribution to prevent puppet clipping
    float angle = (2.0f * PI * slotId) / 8.0f;
    sp.x += std::cos(angle) * 48.0f;
    sp.y += std::sin(angle) * 48.0f;
    sp.yaw = angle * (180.0f / PI);
    return sp;
}

} // namespace pse
