#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <stdexcept>
#include <windows.h>
#include "net_protocol.h"
#include "thread_bridge_dto.h"
#include "../../native_snapshot/src/snapshot_contract.hpp"
#include "../../native_map_ready/src/map_identity.hpp"

namespace pse::proxy {

// ============================================================================
// Abstract Seam Interface for DOC-58A Native Actor Movement & Lifecycle
// Decouples network/lifecycle state machine from the concrete native KEX engine calls.
// ============================================================================
class INativeVisualProxySeam {
public:
    virtual ~INativeVisualProxySeam() = default;

    // Spawns native actor (maps to kexSlaveObjectFactoryLocal::SpawnActor in KEX).
    virtual void* spawnVisualActor(uint32_t actorType, float x, float y, float z, float yaw, int sector) = 0;

    // Updates native actor transform (maps to DOC-58A actor movement seam).
    virtual bool updateVisualTransform(void* actor, float x, float y, float z, float yaw, float pitch) = 0;

    // Changes actor sector membership (maps to LinkSector in KEX).
    virtual bool transitionSector(void* actor, int newSector) = 0;

    // Removes native actor and performs deferred retirement (maps to native Remove in DOC-57).
    virtual bool removeVisualActor(void* actor) = 0;
};

// ============================================================================
// Visual Proxy Lifecycle States
// Strict state machine: ABSENT -> SPAWN_PENDING -> ACTIVE -> REMOVE_PENDING -> ABSENT
// ============================================================================
enum class VisualProxyState : uint8_t {
    ABSENT = 0,
    SPAWN_PENDING = 1,
    ACTIVE = 2,
    REMOVE_PENDING = 3
};

inline const char* toString(VisualProxyState state) {
    switch (state) {
        case VisualProxyState::ABSENT: return "ABSENT";
        case VisualProxyState::SPAWN_PENDING: return "SPAWN_PENDING";
        case VisualProxyState::ACTIVE: return "ACTIVE";
        case VisualProxyState::REMOVE_PENDING: return "REMOVE_PENDING";
        default: return "UNKNOWN";
    }
}

// Rejection / decision codes for snapshot handling
enum class ProxyRejectionReason : uint8_t {
    None = 0,
    WrongSlot,
    NotReady,
    NotGameplay,
    IdentityMismatch,
    StaleSequence,
    NonfiniteTransform,
    EntityMismatch,
    AffinityViolation,
    NullSeam,
    SpawnFailed,
    AlreadyActive,
    AlreadyPendingRemove,
    SessionInactive
};

inline const char* toString(ProxyRejectionReason r) {
    switch (r) {
        case ProxyRejectionReason::None: return "None";
        case ProxyRejectionReason::WrongSlot: return "WrongSlot";
        case ProxyRejectionReason::NotReady: return "NotReady";
        case ProxyRejectionReason::NotGameplay: return "NotGameplay";
        case ProxyRejectionReason::IdentityMismatch: return "IdentityMismatch";
        case ProxyRejectionReason::StaleSequence: return "StaleSequence";
        case ProxyRejectionReason::NonfiniteTransform: return "NonfiniteTransform";
        case ProxyRejectionReason::EntityMismatch: return "EntityMismatch";
        case ProxyRejectionReason::AffinityViolation: return "AffinityViolation";
        case ProxyRejectionReason::NullSeam: return "NullSeam";
        case ProxyRejectionReason::SpawnFailed: return "SpawnFailed";
        case ProxyRejectionReason::AlreadyActive: return "AlreadyActive";
        case ProxyRejectionReason::AlreadyPendingRemove: return "AlreadyPendingRemove";
        case ProxyRejectionReason::SessionInactive: return "SessionInactive";
        default: return "Unknown";
    }
}

struct ProxyTransform {
    float x{0.0f}, y{0.0f}, z{0.0f};
    float yaw{0.0f}, pitch{0.0f};
    int sector{0};
    uint32_t serverTick{0};
    uint32_t sequence{0};
};

// ============================================================================
// Visual Proxy State Machine (Client B side representation of Host P1)
// ============================================================================
class VisualProxyStateMachine {
public:
    explicit VisualProxyStateMachine(uint8_t targetSlot = pse::HOST_SLOT_ID)
        : m_targetSlot(targetSlot) {}

    // Register current thread as the authoritative simulation thread
    void registerSimulationThread() {
        m_simulationTid = GetCurrentThreadId();
    }

    bool isSimulationThread() const {
        return m_simulationTid != 0 && GetCurrentThreadId() == m_simulationTid;
    }

    void configureSession(uint32_t epoch, uint32_t connectionGen) {
        assertSimulationThread();
        m_activeEpoch = epoch;
        m_activeConnectionGen = connectionGen;
        m_sessionActive = (epoch > 1 && connectionGen > 0);
        m_guard.reset(connectionGen, epoch);
    }

    void resetSession() {
        assertSimulationThread();
        m_sessionActive = false;
        m_activeEpoch = 0;
        m_activeConnectionGen = 0;
        m_guard.reset(0, 0);
    }

    // Process an incoming snapshot on Client Simulation Thread
    ProxyRejectionReason handleSnapshot(const pse::BridgePlayerSnapshot& s, bool mapReady, bool isGameplay, INativeVisualProxySeam* seam = nullptr) {
        if (!isSimulationThread()) {
            return ProxyRejectionReason::AffinityViolation;
        }

        if (!m_sessionActive) {
            return ProxyRejectionReason::SessionInactive;
        }

        // Slot check: Client visual proxy must strictly represent Host P1 (slot 0).
        // Any other slot (especially slot 1, the client's own puppet) must never spawn a proxy!
        if (s.slotId != m_targetSlot) {
            return ProxyRejectionReason::WrongSlot;
        }

        if (!isGameplay) {
            return ProxyRejectionReason::NotGameplay;
        }

        // Verify with snapshot55 Guard contract
        const auto verdict = m_guard.accept(s, mapReady && isGameplay);
        if (verdict != snapshot55::Verdict::Accept) {
            switch (verdict) {
                case snapshot55::Verdict::NotReady:
                    return ProxyRejectionReason::NotReady;
                case snapshot55::Verdict::Identity:
                    return ProxyRejectionReason::IdentityMismatch;
                case snapshot55::Verdict::Stale:
                    return ProxyRejectionReason::StaleSequence;
                case snapshot55::Verdict::EntityMismatch: {
                    // Entity identity changed on host (e.g. host respawned with new entity generation).
                    // Trigger teardown of existing proxy actor so a fresh one can be spawned.
                    if (m_state == VisualProxyState::ACTIVE) {
                        m_state = VisualProxyState::REMOVE_PENDING;
                    }
                    return ProxyRejectionReason::EntityMismatch;
                }
                case snapshot55::Verdict::Nonfinite:
                    return ProxyRejectionReason::NonfiniteTransform;
            }
        }

        // State machine handling
        switch (m_state) {
            case VisualProxyState::ABSENT: {
                m_state = VisualProxyState::SPAWN_PENDING;
                m_activeEntityId = s.entityId;
                m_activeEntityGen = s.entityGeneration;
                m_pendingTransform = {s.posX, s.posY, s.posZ, s.yaw, s.pitch, 0, s.serverTick, s.sequence};
                return ProxyRejectionReason::None;
            }
            case VisualProxyState::SPAWN_PENDING: {
                // Already pending spawn: update the pending initial transform, do NOT duplicate spawn request
                m_pendingTransform = {s.posX, s.posY, s.posZ, s.yaw, s.pitch, 0, s.serverTick, s.sequence};
                return ProxyRejectionReason::None;
            }
            case VisualProxyState::ACTIVE: {
                // Apply update to active proxy
                m_currentTransform = {s.posX, s.posY, s.posZ, s.yaw, s.pitch, 0, s.serverTick, s.sequence};
                if (seam && m_nativeActorPtr) {
                    seam->updateVisualTransform(m_nativeActorPtr, s.posX, s.posY, s.posZ, s.yaw, s.pitch);
                }
                return ProxyRejectionReason::None;
            }
            case VisualProxyState::REMOVE_PENDING: {
                // In removal phase: cannot accept updates for the dying actor
                return ProxyRejectionReason::AlreadyPendingRemove;
            }
        }
        return ProxyRejectionReason::None;
    }

    // Executes pending spawn at a attested safe simulation tick seam
    bool executePendingSpawn(INativeVisualProxySeam* seam, uint32_t actorType, int defaultSector) {
        assertSimulationThread();
        if (m_state != VisualProxyState::SPAWN_PENDING) {
            return false;
        }
        if (!seam) {
            return false;
        }

        void* actor = seam->spawnVisualActor(actorType,
                                             m_pendingTransform.x,
                                             m_pendingTransform.y,
                                             m_pendingTransform.z,
                                             m_pendingTransform.yaw,
                                             defaultSector);
        if (!actor) {
            // Spawn failed, stay ABSENT
            m_state = VisualProxyState::ABSENT;
            return false;
        }

        m_nativeActorPtr = actor;
        m_pendingTransform.sector = defaultSector;
        m_currentTransform = m_pendingTransform;
        m_localProxyGen++;
        m_state = VisualProxyState::ACTIVE;
        m_spawnCount++;
        return true;
    }

    // Executes pending remove at a attested safe simulation tick seam
    bool executePendingRemove(INativeVisualProxySeam* seam) {
        assertSimulationThread();
        if (m_state != VisualProxyState::REMOVE_PENDING) {
            return false;
        }
        if (!seam || !m_nativeActorPtr) {
            m_state = VisualProxyState::ABSENT;
            m_nativeActorPtr = nullptr;
            return false;
        }

        const bool ok = seam->removeVisualActor(m_nativeActorPtr);
        m_nativeActorPtr = nullptr;
        m_localProxyGen++; // Increment local generation to prevent ABA / stale pointer reuse
        m_state = VisualProxyState::ABSENT;
        m_removeCount++;
        return ok;
    }

    // Triggered on network disconnect (LOCAL_SESSION_LOST, etc.)
    void onDisconnect() {
        assertSimulationThread();
        m_sessionActive = false;
        if (m_state == VisualProxyState::ACTIVE) {
            m_state = VisualProxyState::REMOVE_PENDING;
        } else if (m_state == VisualProxyState::SPAWN_PENDING) {
            // Cancel pending spawn immediately
            m_state = VisualProxyState::ABSENT;
        }
    }

    // Triggered on native map invalidation (level transition, unload, fault)
    void onMapInvalidated() {
        assertSimulationThread();
        m_sessionActive = false;
        if (m_state == VisualProxyState::ACTIVE) {
            m_state = VisualProxyState::REMOVE_PENDING;
        } else if (m_state == VisualProxyState::SPAWN_PENDING) {
            m_state = VisualProxyState::ABSENT;
        }
    }

    // Forced synchronous shutdown (e.g. game exit / Session::stop)
    void forceShutdown(INativeVisualProxySeam* seam) {
        assertSimulationThread();
        m_sessionActive = false;
        if (m_state == VisualProxyState::ACTIVE || m_state == VisualProxyState::REMOVE_PENDING) {
            if (seam && m_nativeActorPtr) {
                seam->removeVisualActor(m_nativeActorPtr);
                m_nativeActorPtr = nullptr;
                m_removeCount++;
            }
            m_localProxyGen++;
            m_state = VisualProxyState::ABSENT;
        } else if (m_state == VisualProxyState::SPAWN_PENDING) {
            m_state = VisualProxyState::ABSENT;
        }
    }

    // Getters
    VisualProxyState state() const { return m_state; }
    void* activeActor() const { return m_nativeActorPtr; }
    uint32_t localProxyGeneration() const { return m_localProxyGen; }
    uint32_t activeEntityId() const { return m_activeEntityId; }
    uint32_t activeEntityGeneration() const { return m_activeEntityGen; }
    const ProxyTransform& currentTransform() const { return m_currentTransform; }
    uint32_t spawnCount() const { return m_spawnCount; }
    uint32_t removeCount() const { return m_removeCount; }
    bool isSessionActive() const { return m_sessionActive; }

private:
    void assertSimulationThread() const {
        if (!isSimulationThread()) {
            // In test environment or failure handling
            throw std::runtime_error("VisualProxyStateMachine affinity violation: not on Simulation Thread!");
        }
    }

    VisualProxyState m_state{VisualProxyState::ABSENT};
    DWORD m_simulationTid{0};
    uint8_t m_targetSlot{0};
    bool m_sessionActive{false};
    uint32_t m_activeEpoch{0};
    uint32_t m_activeConnectionGen{0};
    uint32_t m_activeEntityId{0};
    uint32_t m_activeEntityGen{0};
    uint32_t m_localProxyGen{0};
    void* m_nativeActorPtr{nullptr};

    ProxyTransform m_pendingTransform{};
    ProxyTransform m_currentTransform{};

    snapshot55::Guard m_guard{};

    uint32_t m_spawnCount{0};
    uint32_t m_removeCount{0};
};

} // namespace pse::proxy
