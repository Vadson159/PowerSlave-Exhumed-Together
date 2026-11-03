#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <windows.h>
#include "net_protocol.h"
#include "thread_bridge_dto.h"
#include "../../native_snapshot/src/snapshot_contract.hpp"
#include "../../native_map_ready/src/map_identity.hpp"

namespace pse::proxy::remediated {

// ============================================================================
// Tri-State Removal Outcome Contract (Task A)
// Distinguishes confirmed cleanup, definite rejection, and ambiguous status.
// ============================================================================
enum class RemoveOutcome : uint8_t {
    ConfirmedSuccess = 0, // Native actor was unequivocally unlinked and freed by KEX engine
    DefiniteFailure = 1,  // Native removal was definitely rejected / not performed (actor alive)
    Uncertain = 2         // Outcome ambiguous (exception caught, hook fault, partial teardown)
};

inline const char* toString(RemoveOutcome outcome) noexcept {
    switch (outcome) {
        case RemoveOutcome::ConfirmedSuccess: return "ConfirmedSuccess";
        case RemoveOutcome::DefiniteFailure: return "DefiniteFailure";
        case RemoveOutcome::Uncertain: return "Uncertain";
        default: return "Unknown";
    }
}

// ============================================================================
// Sector Validation Interface (Task D)
// Formal contract for verifying spatial sector membership without hardcoded constants.
// ============================================================================
class ISectorValidator {
public:
    virtual ~ISectorValidator() = default;

    // Returns true iff destinationSector exists in the currently loaded level
    // and the position (x, y, z) is verified to reside within that sector.
    virtual bool validateDestinationSector(int sectorId, float x, float y, float z) const noexcept = 0;
};

// ============================================================================
// Remediated Native Visual Proxy Seam Interface
// Abstract seam allowing native/mock implementations; callers wrap invocations
// to guarantee complete noexcept isolation across C-ABI / MinHook boundaries.
// ============================================================================
class INativeVisualProxySeamRemediated {
public:
    virtual ~INativeVisualProxySeamRemediated() = default;

    // Spawns native actor (maps to kexSlaveObjectFactoryLocal::SpawnActor in KEX).
    virtual void* spawnVisualActor(uint32_t actorType, float x, float y, float z, float yaw, int sector) = 0;

    // Updates native actor transform (maps to DOC-58A actor movement seam).
    virtual bool updateVisualTransform(void* actor, float x, float y, float z, float yaw, float pitch) = 0;

    // Changes actor sector membership (maps to LinkSector in KEX).
    virtual bool transitionSector(void* actor, int newSector) = 0;

    // Removes native actor with tri-state status reporting.
    virtual RemoveOutcome removeVisualActorEx(void* actor) = 0;

    // Legacy compatibility bridge
    virtual bool removeVisualActor(void* actor) {
        return removeVisualActorEx(actor) == RemoveOutcome::ConfirmedSuccess;
    }
};

// ============================================================================
// Visual Proxy Lifecycle States (Task A: Explicit QUARANTINED State)
// ABSENT -> SPAWN_PENDING -> ACTIVE -> REMOVE_PENDING -> ABSENT
//                                    \-> QUARANTINED (on failed/uncertain remove)
// ============================================================================
enum class VisualProxyState : uint8_t {
    ABSENT = 0,
    SPAWN_PENDING = 1,
    ACTIVE = 2,
    REMOVE_PENDING = 3,
    QUARANTINED = 4
};

inline const char* toString(VisualProxyState state) noexcept {
    switch (state) {
        case VisualProxyState::ABSENT: return "ABSENT";
        case VisualProxyState::SPAWN_PENDING: return "SPAWN_PENDING";
        case VisualProxyState::ACTIVE: return "ACTIVE";
        case VisualProxyState::REMOVE_PENDING: return "REMOVE_PENDING";
        case VisualProxyState::QUARANTINED: return "QUARANTINED";
        default: return "UNKNOWN";
    }
}

// Reason why proxy was placed into quarantine
enum class QuarantineReason : uint8_t {
    None = 0,
    RemoveDefiniteFailure = 1,
    RemoveUncertain = 2,
    NativeExceptionCaught = 3
};

inline const char* toString(QuarantineReason r) noexcept {
    switch (r) {
        case QuarantineReason::None: return "None";
        case QuarantineReason::RemoveDefiniteFailure: return "RemoveDefiniteFailure";
        case QuarantineReason::RemoveUncertain: return "RemoveUncertain";
        case QuarantineReason::NativeExceptionCaught: return "NativeExceptionCaught";
        default: return "Unknown";
    }
}

// Rejection / decision codes for snapshot handling (Fail-Closed, No Exceptions)
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
    SessionInactive,
    Quarantined,
    ShutdownRequested,
    InvalidOrUnverifiedSector,
    SectorValidationUnavailable,
    RepeatedRemoveBlocked
};

inline const char* toString(ProxyRejectionReason r) noexcept {
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
        case ProxyRejectionReason::Quarantined: return "Quarantined";
        case ProxyRejectionReason::ShutdownRequested: return "ShutdownRequested";
        case ProxyRejectionReason::InvalidOrUnverifiedSector: return "InvalidOrUnverifiedSector";
        case ProxyRejectionReason::SectorValidationUnavailable: return "SectorValidationUnavailable";
        case ProxyRejectionReason::RepeatedRemoveBlocked: return "RepeatedRemoveBlocked";
        default: return "Unknown";
    }
}

// Fine-grained Spawn Execution Result
enum class SpawnExecutionResult : uint8_t {
    Success = 0,
    NotPending,
    AffinityViolation,
    NullSeam,
    SectorValidatorMissing,
    SectorValidationFailed,
    NativeSpawnReturnedNull,
    Quarantined,
    ShutdownBlocked,
    NativeExceptionCaught
};

inline const char* toString(SpawnExecutionResult res) noexcept {
    switch (res) {
        case SpawnExecutionResult::Success: return "Success";
        case SpawnExecutionResult::NotPending: return "NotPending";
        case SpawnExecutionResult::AffinityViolation: return "AffinityViolation";
        case SpawnExecutionResult::NullSeam: return "NullSeam";
        case SpawnExecutionResult::SectorValidatorMissing: return "SectorValidatorMissing";
        case SpawnExecutionResult::SectorValidationFailed: return "SectorValidationFailed";
        case SpawnExecutionResult::NativeSpawnReturnedNull: return "NativeSpawnReturnedNull";
        case SpawnExecutionResult::Quarantined: return "Quarantined";
        case SpawnExecutionResult::ShutdownBlocked: return "ShutdownBlocked";
        case SpawnExecutionResult::NativeExceptionCaught: return "NativeExceptionCaught";
        default: return "Unknown";
    }
}

// Fine-grained Remove Execution Result
enum class RemoveExecutionResult : uint8_t {
    Success = 0,
    NotPending,
    AffinityViolation,
    NullSeam,
    NoActiveActor,
    NativeDefiniteFailureQuarantined,
    NativeUncertainQuarantined,
    QuarantinedBlocked,
    LevelInvalidSkipped,
    NativeExceptionCaught
};

inline const char* toString(RemoveExecutionResult res) noexcept {
    switch (res) {
        case RemoveExecutionResult::Success: return "Success";
        case RemoveExecutionResult::NotPending: return "NotPending";
        case RemoveExecutionResult::AffinityViolation: return "AffinityViolation";
        case RemoveExecutionResult::NullSeam: return "NullSeam";
        case RemoveExecutionResult::NoActiveActor: return "NoActiveActor";
        case RemoveExecutionResult::NativeDefiniteFailureQuarantined: return "NativeDefiniteFailureQuarantined";
        case RemoveExecutionResult::NativeUncertainQuarantined: return "NativeUncertainQuarantined";
        case RemoveExecutionResult::QuarantinedBlocked: return "QuarantinedBlocked";
        case RemoveExecutionResult::LevelInvalidSkipped: return "LevelInvalidSkipped";
        case RemoveExecutionResult::NativeExceptionCaught: return "NativeExceptionCaught";
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
// Remediated Visual Proxy State Machine
// C++20, noexcept-safe at API boundary, quarantine-aware, sector-verified.
// ============================================================================
class VisualProxyStateMachine {
public:
    explicit VisualProxyStateMachine(uint8_t targetSlot = pse::HOST_SLOT_ID) noexcept
        : m_targetSlot(targetSlot) {}

    // Register current thread as authoritative simulation thread
    void registerSimulationThread() noexcept {
        m_simulationTid = GetCurrentThreadId();
    }

    bool isSimulationThread() const noexcept {
        return m_simulationTid != 0 && GetCurrentThreadId() == m_simulationTid;
    }

    bool configureSession(uint32_t epoch, uint32_t connectionGen) noexcept {
        if (!isSimulationThread()) {
            return false;
        }
        m_activeEpoch = epoch;
        m_activeConnectionGen = connectionGen;
        m_sessionActive = (epoch > 1 && connectionGen > 0);
        m_shutdownRequested = false;
        m_guard.reset(connectionGen, epoch);
        return true;
    }

    bool resetSession() noexcept {
        if (!isSimulationThread()) {
            return false;
        }
        m_sessionActive = false;
        m_activeEpoch = 0;
        m_activeConnectionGen = 0;
        m_shutdownRequested = false;
        m_guard.reset(0, 0);
        return true;
    }

    // Process an incoming snapshot on Client Simulation Thread (noexcept, fail-closed)
    ProxyRejectionReason handleSnapshot(const pse::BridgePlayerSnapshot& s,
                                         bool mapReady,
                                         bool isGameplay,
                                         INativeVisualProxySeamRemediated* seam = nullptr) noexcept {
        if (!isSimulationThread()) {
            return ProxyRejectionReason::AffinityViolation;
        }

        if (m_shutdownRequested) {
            return ProxyRejectionReason::ShutdownRequested;
        }

        if (m_state == VisualProxyState::QUARANTINED) {
            // Task A: Under quarantine, reject updates to prevent operating on compromised actor
            return ProxyRejectionReason::Quarantined;
        }

        if (!m_sessionActive) {
            return ProxyRejectionReason::SessionInactive;
        }

        // Slot check: Proxy must strictly represent Host P1 (slot 0)
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
                    // Entity identity changed on host (respawn). Request teardown of existing proxy.
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
                // Update pending initial transform; do not duplicate spawn request
                m_pendingTransform = {s.posX, s.posY, s.posZ, s.yaw, s.pitch, 0, s.serverTick, s.sequence};
                return ProxyRejectionReason::None;
            }
            case VisualProxyState::ACTIVE: {
                // Apply update to active proxy
                m_currentTransform = {s.posX, s.posY, s.posZ, s.yaw, s.pitch, 0, s.serverTick, s.sequence};
                if (seam && m_nativeActorPtr) {
                    try {
                        seam->updateVisualTransform(m_nativeActorPtr, s.posX, s.posY, s.posZ, s.yaw, s.pitch);
                    } catch (...) {
                        // Native seam exception caught; isolate without crashing host process
                        return ProxyRejectionReason::None;
                    }
                }
                return ProxyRejectionReason::None;
            }
            case VisualProxyState::REMOVE_PENDING: {
                return ProxyRejectionReason::AlreadyPendingRemove;
            }
            case VisualProxyState::QUARANTINED: {
                return ProxyRejectionReason::Quarantined;
            }
        }
        return ProxyRejectionReason::None;
    }

    // Executes pending spawn at an attested safe simulation tick seam (Task D: sector verified)
    SpawnExecutionResult executePendingSpawn(INativeVisualProxySeamRemediated* seam,
                                             const ISectorValidator* validator,
                                             uint32_t actorType,
                                             int destinationSector) noexcept {
        if (!isSimulationThread()) {
            return SpawnExecutionResult::AffinityViolation;
        }

        if (m_shutdownRequested) {
            return SpawnExecutionResult::ShutdownBlocked;
        }

        if (m_state == VisualProxyState::QUARANTINED) {
            // Task A: Never spawn while an unconfirmed actor is in quarantine
            return SpawnExecutionResult::Quarantined;
        }

        if (m_state != VisualProxyState::SPAWN_PENDING) {
            return SpawnExecutionResult::NotPending;
        }

        if (!seam) {
            return SpawnExecutionResult::NullSeam;
        }

        // Task D: Sector validation is strictly required. No hardcoded sector259!
        if (!validator) {
            return SpawnExecutionResult::SectorValidatorMissing;
        }

        if (!validator->validateDestinationSector(destinationSector,
                                                   m_pendingTransform.x,
                                                   m_pendingTransform.y,
                                                   m_pendingTransform.z)) {
            // Coordinate/sector mismatch or unverified sector -> refuse spawn
            return SpawnExecutionResult::SectorValidationFailed;
        }

        void* actor = nullptr;
        try {
            actor = seam->spawnVisualActor(actorType,
                                           m_pendingTransform.x,
                                           m_pendingTransform.y,
                                           m_pendingTransform.z,
                                           m_pendingTransform.yaw,
                                           destinationSector);
        } catch (...) {
            // Isolate exception from mock or native seam
            return SpawnExecutionResult::NativeExceptionCaught;
        }

        if (!actor) {
            m_state = VisualProxyState::ABSENT;
            return SpawnExecutionResult::NativeSpawnReturnedNull;
        }

        m_nativeActorPtr = actor;
        m_pendingTransform.sector = destinationSector;
        m_currentTransform = m_pendingTransform;
        m_localProxyGen++;
        m_state = VisualProxyState::ACTIVE;
        m_spawnCount++;
        return SpawnExecutionResult::Success;
    }

    // Executes pending remove at an attested safe simulation tick seam (Task A: Tri-State / Quarantine)
    RemoveExecutionResult executePendingRemove(INativeVisualProxySeamRemediated* seam) noexcept {
        if (!isSimulationThread()) {
            return RemoveExecutionResult::AffinityViolation;
        }

        if (m_state == VisualProxyState::QUARANTINED) {
            // Task A: Do NOT repeatedly invoke native remove blindly on quarantined actor
            return RemoveExecutionResult::QuarantinedBlocked;
        }

        if (m_state != VisualProxyState::REMOVE_PENDING) {
            return RemoveExecutionResult::NotPending;
        }

        if (!seam) {
            return RemoveExecutionResult::NullSeam;
        }

        if (!m_nativeActorPtr) {
            m_state = VisualProxyState::ABSENT;
            return RemoveExecutionResult::NoActiveActor;
        }

        RemoveOutcome outcome = RemoveOutcome::Uncertain;
        try {
            outcome = seam->removeVisualActorEx(m_nativeActorPtr);
        } catch (...) {
            // Native exception caught during remove: place into quarantine
            m_state = VisualProxyState::QUARANTINED;
            m_quarantineReason = QuarantineReason::NativeExceptionCaught;
            return RemoveExecutionResult::NativeExceptionCaught;
        }

        if (outcome == RemoveOutcome::ConfirmedSuccess) {
            // Confirmed clean removal: safe to release pointer and advance generation
            m_nativeActorPtr = nullptr;
            m_localProxyGen++;
            m_state = VisualProxyState::ABSENT;
            m_removeCount++;
            return RemoveExecutionResult::Success;
        } else if (outcome == RemoveOutcome::DefiniteFailure) {
            // Task A: Native engine explicitly rejected removal. Actor is DEFINITELY still alive!
            // Retain pointer, quarantine state, block subsequent spawns.
            m_state = VisualProxyState::QUARANTINED;
            m_quarantineReason = QuarantineReason::RemoveDefiniteFailure;
            return RemoveExecutionResult::NativeDefiniteFailureQuarantined;
        } else {
            // Task A: Outcome is ambiguous. Retain pointer in quarantine until native sweep.
            m_state = VisualProxyState::QUARANTINED;
            m_quarantineReason = QuarantineReason::RemoveUncertain;
            return RemoveExecutionResult::NativeUncertainQuarantined;
        }
    }

    // Task C: Explicit Shutdown Request (Thread-safe initiation)
    bool requestShutdown() noexcept {
        if (!isSimulationThread()) {
            return false;
        }
        m_shutdownRequested = true;
        m_sessionActive = false;

        if (m_state == VisualProxyState::SPAWN_PENDING) {
            // Safely cancel pending spawn before actor is created
            m_state = VisualProxyState::ABSENT;
        } else if (m_state == VisualProxyState::ACTIVE) {
            // Mark for clean removal at the next safe simulation point
            m_state = VisualProxyState::REMOVE_PENDING;
        }
        return true;
    }

    // Task C: Safe Shutdown Cleanup Contract
    // Distinguishes valid level vs already-unloaded level to avoid Use-After-Free in Drain
    RemoveExecutionResult executeShutdownCleanup(INativeVisualProxySeamRemediated* seam, bool isLevelValid) noexcept {
        if (!isSimulationThread()) {
            return RemoveExecutionResult::AffinityViolation;
        }

        if (m_state == VisualProxyState::ABSENT) {
            return RemoveExecutionResult::Success;
        }

        if (!isLevelValid) {
            // KEX level is already unloaded / invalid (e.g. kexSlaveLevelLocal::Unload freed sectors).
            // Calling native remove now would access freed sector arrays -> STATUS_ACCESS_VIOLATION!
            // Cleanly retire the proxy without calling the native seam.
            m_nativeActorPtr = nullptr;
            m_localProxyGen++;
            m_state = VisualProxyState::ABSENT;
            m_quarantineReason = QuarantineReason::None;
            return RemoveExecutionResult::LevelInvalidSkipped;
        }

        // Level is still valid: attempt structured removal if pending or active
        if (m_state == VisualProxyState::ACTIVE || m_state == VisualProxyState::REMOVE_PENDING) {
            m_state = VisualProxyState::REMOVE_PENDING;
            return executePendingRemove(seam);
        }

        if (m_state == VisualProxyState::QUARANTINED) {
            // Actor remains quarantined; do not call native seam blindly
            return RemoveExecutionResult::QuarantinedBlocked;
        }

        return RemoveExecutionResult::Success;
    }

    // Triggered on network disconnect (e.g. LOCAL_SESSION_LOST)
    void onDisconnect() noexcept {
        if (!isSimulationThread()) return;
        m_sessionActive = false;
        if (m_state == VisualProxyState::ACTIVE) {
            m_state = VisualProxyState::REMOVE_PENDING;
        } else if (m_state == VisualProxyState::SPAWN_PENDING) {
            m_state = VisualProxyState::ABSENT;
        }
    }

    // Triggered on native map invalidation (level transition, unload, fault)
    void onMapInvalidated(bool levelAlreadyFreed = false) noexcept {
        if (!isSimulationThread()) return;
        m_sessionActive = false;
        if (levelAlreadyFreed) {
            // Engine already freed level structures; retire actor pointer safely
            m_nativeActorPtr = nullptr;
            m_localProxyGen++;
            m_state = VisualProxyState::ABSENT;
            m_quarantineReason = QuarantineReason::None;
        } else {
            if (m_state == VisualProxyState::ACTIVE) {
                m_state = VisualProxyState::REMOVE_PENDING;
            } else if (m_state == VisualProxyState::SPAWN_PENDING) {
                m_state = VisualProxyState::ABSENT;
            }
        }
    }

    // Explicit recovery / attested retirement from quarantine (e.g. engine sweep confirmed)
    bool retireQuarantinedActor(bool attestedEngineSwept) noexcept {
        if (!isSimulationThread()) return false;
        if (m_state != VisualProxyState::QUARANTINED) return false;

        if (attestedEngineSwept) {
            m_nativeActorPtr = nullptr;
            m_localProxyGen++;
            m_state = VisualProxyState::ABSENT;
            m_quarantineReason = QuarantineReason::None;
            return true;
        }
        return false;
    }

    // Getters (all noexcept)
    VisualProxyState state() const noexcept { return m_state; }
    QuarantineReason quarantineReason() const noexcept { return m_quarantineReason; }
    void* activeActor() const noexcept { return m_nativeActorPtr; }
    uint32_t localProxyGeneration() const noexcept { return m_localProxyGen; }
    uint32_t activeEntityId() const noexcept { return m_activeEntityId; }
    uint32_t activeEntityGeneration() const noexcept { return m_activeEntityGen; }
    const ProxyTransform& currentTransform() const noexcept { return m_currentTransform; }
    uint32_t spawnCount() const noexcept { return m_spawnCount; }
    uint32_t removeCount() const noexcept { return m_removeCount; }
    bool isSessionActive() const noexcept { return m_sessionActive; }
    bool isShutdownRequested() const noexcept { return m_shutdownRequested; }

private:
    VisualProxyState m_state{VisualProxyState::ABSENT};
    QuarantineReason m_quarantineReason{QuarantineReason::None};
    DWORD m_simulationTid{0};
    uint8_t m_targetSlot{0};
    bool m_sessionActive{false};
    bool m_shutdownRequested{false};
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

} // namespace pse::proxy::remediated
