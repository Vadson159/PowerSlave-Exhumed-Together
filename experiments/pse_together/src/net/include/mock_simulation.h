#pragma once

#include "thread_bridge.h"
#include "world_entity.h"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <map>
#include <vector>
#include <deque>
#include <array>
#include <string>
#include <memory>

namespace pse {

struct MockEntityState {
    uint32_t entityId = 0;
    uint32_t generation = 1;
    EntityType type = EntityType::ENEMY;
    uint16_t subType = 0;
    Transform3D transform{};
    Velocity3D velocity{};
    ActorStateInfo state{};
};

enum class MockSimRole : uint8_t {
    HOST,   // Authoritative world (host simulation thread)
    CLIENT  // Replicated world of one remote client
};

enum class ClientSyncState : uint8_t {
    DISCONNECTED = 0,
    LOADING = 1,            // Map load in progress (MAP_READY not sent yet)
    AWAITING_BASELINE = 2,  // MAP_READY sent, waiting for the authoritative baseline
    SYNCED = 3,             // Baseline applied; incremental events are applied
    JOINING = 4             // Session up, waiting for the host simulation's map assignment
};

struct MockPlayerState {
    // Session (host view)
    bool active = false;
    uint32_t connectionGeneration = 0;
    bool ready = false;          // MAP_READY received for the current epoch
    bool kicked = false;
    uint32_t readyDeadlineTick = 0;

    // Gameplay state
    uint32_t entityId = 0;
    uint32_t entityGeneration = 1;
    uint8_t lifeState = LIFE_ALIVE;
    uint32_t deathSequence = 0;
    int32_t health = DEFAULT_HEALTH;
    Transform3D transform{};
    uint32_t lastSnapshotSeq = 0;  // Client view

    uint64_t shotsFired = 0;
    uint64_t usesPerformed = 0;
    uint64_t interactionsReceived = 0;
};

struct MockSimStats {
    uint64_t totalTicks = 0;
    uint64_t inboundDrained = 0;
    uint64_t outboundProduced = 0;       // Messages accepted by pushOutbound
    uint64_t commandsProcessed = 0;      // Input commands accepted
    uint64_t spawnsProcessed = 0;
    uint64_t despawnsProcessed = 0;
    uint64_t respawnsProcessed = 0;
    uint64_t mapChangesProcessed = 0;
    uint64_t worldEventsProcessed = 0;
    uint64_t stallCount = 0;

    // Delivery ownership (outbox)
    uint64_t outboxDepth = 0;
    uint64_t outboxPeak = 0;
    uint64_t outboxEscalations = 0;
    uint64_t bestEffortDropped = 0;

    // Input model
    uint64_t inputsRejectedGeneration = 0;
    uint64_t inputsRejectedStale = 0;
    uint64_t movementSteps = 0;
    uint64_t maxMovementStepsPerSlotTick = 0;
    uint64_t inputNeutralTimeouts = 0;

    // Session / map / respawn
    uint64_t baselinesSent = 0;
    uint64_t baselinesApplied = 0;
    uint64_t mapReadyReceived = 0;
    uint64_t mapReadyStale = 0;
    uint64_t mapTransitionsCompleted = 0;
    uint64_t mapLoads = 0;
    uint64_t kicksIssued = 0;
    uint64_t respawnsConfirmed = 0;
    uint64_t respawnDuplicates = 0;
    uint64_t respawnRejected = 0;
    uint64_t respawnRequestsSent = 0;
    uint64_t reconnectRequests = 0;
    uint64_t epochAnomalies = 0;
    uint64_t snapshotUnknownEntity = 0;
    uint64_t sessionEvents = 0;
};

// ============================================================================
// Mock KEX Simulation
// Simulates the KEX main loop (GameInstance::StateTick) in a dedicated OS thread
// at ~60 Hz, as either the authoritative host or a replicating client.
//
// Invariants:
// - Strictly ZERO calls to native KEX or ENet socket APIs.
// - The simulation thread is the ONLY producer of the outbound queue and the
//   ONLY consumer of the inbound queue (SIMULATION role of the bridge).
// - trigger*/request*/submit* hooks may be called from any thread: they are
//   posted to a mailbox and executed by the simulation thread at the start of
//   the next tick. sync() waits until previously posted hooks have run.
// - Every critical event is recorded in the outbox together with the local
//   state change and retried every tick until the bridge accepts it.
// ============================================================================
class MockKexSimulation {
public:
    static constexpr uint32_t INPUT_TIMEOUT_TICKS = 6;          // ~100 ms -> neutral input
    static constexpr uint32_t DEFAULT_MAP_READY_TIMEOUT_TICKS = 600; // 10 s
    static constexpr size_t DEFAULT_OUTBOX_CAP = 8192;
    static constexpr uint32_t RESPAWN_RETRY_TICKS = 60;
    static constexpr uint32_t BASELINE_WAIT_TIMEOUT_TICKS = 600;

    explicit MockKexSimulation(ThreadBridge& bridge, MockSimRole role = MockSimRole::HOST);
    ~MockKexSimulation();

    // Non-copyable, non-movable
    MockKexSimulation(const MockKexSimulation&) = delete;
    MockKexSimulation& operator=(const MockKexSimulation&) = delete;

    bool start(double targetHz = 60.0);
    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }
    MockSimRole getRole() const { return m_role; }

    // Wait until every hook posted before this call has been executed.
    bool sync(std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    // Test hook: Inject artificial game tick lag/freeze
    void injectTickStall(uint32_t stallMs);

    // Configuration (posted to the simulation thread)
    void setOutboxCap(size_t cap);
    void setMapReadyTimeoutTicks(uint32_t ticks);
    void setMapLoadTicks(uint32_t ticks);
    void setAutoReconnect(bool enabled, uint32_t delayTicks = 30);

    // Host hooks: simulation-side gameplay mutations (Simulation -> Network)
    void triggerSpawn(uint32_t entityId, uint32_t gen, EntityType type, uint16_t subType,
                      const Transform3D& transform, const Velocity3D& velocity, const ActorStateInfo& state);
    void triggerDespawn(uint32_t entityId, uint32_t gen, DespawnReason reason);
    void triggerMapChange(const std::string& newMap, uint32_t newEpoch);
    void triggerWorldEvent(uint16_t eventType, uint32_t sourceId, uint32_t targetId, uint32_t targetGen,
                           int32_t val, float fval);
    void triggerRespawn(uint8_t slotId, uint32_t deathSeq, uint32_t newEntityId, uint32_t newGen,
                        float x, float y, float z);
    void triggerPlayerDeath(uint8_t slotId);

    // Client hooks
    void submitLocalInput(float forward, float right, float yaw, float pitch, uint32_t heldButtons, uint8_t weapon = WEAPON_PISTOL);
    void submitButtonTap(uint32_t buttons);
    void stopLocalInput(); // Stop sending commands (emulates input stream loss while connected)
    void requestRespawn();
    void requestInteraction(uint32_t targetEntityId, uint32_t targetGeneration);
    void requestReconnect();

    // Inspection (any thread)
    MockSimStats getStats() const;
    void resetStats();

    uint32_t getCurrentTick() const { return m_tick.load(std::memory_order_relaxed); }
    uint32_t getMapEpoch() const { return m_mapEpoch.load(std::memory_order_relaxed); }
    std::string getMapName() const;
    size_t getMockEntityCount() const;
    bool hasEntity(uint32_t entityId) const;
    bool getEntityState(uint32_t entityId, MockEntityState& outState) const;
    std::map<uint32_t, uint32_t> getEntityGenerations() const; // id -> generation (sorted)
    Transform3D getPlayerTransform(uint8_t slotId) const;
    MockPlayerState getPlayerState(uint8_t slotId) const;
    ClientSyncState getClientSyncState() const { return static_cast<ClientSyncState>(m_clientState.load(std::memory_order_acquire)); }
    uint8_t getLocalSlot() const { return m_localSlot.load(std::memory_order_acquire); }
    uint32_t getLocalConnectionGeneration() const { return m_localGen.load(std::memory_order_acquire); }
    uint64_t getReconnectKey() const { return m_reconnectKey.load(std::memory_order_acquire); }

private:
    struct InputState {
        bool hasInput = false;
        uint32_t generation = 0;
        uint32_t lastSeq = 0;
        uint32_t ticksSinceInput = 0;
        BridgeInputCommand held;
        uint32_t pendingShots = 0;
        uint32_t pendingUses = 0;
    };

    struct BaselineAssembly {
        bool active = false;
        uint32_t epoch = 0;
        uint32_t serverTick = 0;
        uint8_t totalBatches = 0;
        uint64_t mask = 0;
        std::string mapName;
        std::vector<BridgeEntitySpawn> entities;
    };

    void post(std::function<void()> fn);
    void runMailbox();
    void threadLoop(double targetHz);
    void processInboundMessage(BridgeMessage& msg);
    void stepSimulation(float dt);

    // Outbox (simulation thread only)
    void publishCritical(BridgeMessage&& msg);
    void publishBestEffort(BridgeMessage&& msg);
    void flushOutbox();
    void checkOutboxBound();

    // Host helpers
    void hostOnSession(const BridgeSessionEvent& evt);
    void hostOnInput(const BridgeInputCommand& cmd);
    void hostOnRespawnRequest(const BridgeRespawnRequest& req);
    void hostOnMapReady(const BridgeMapReady& mr);
    void hostSendBaseline(uint8_t targetSlot);
    void hostStep();
    void hostPublishSnapshots();
    void hostCheckTransition();

    // Client helpers
    void clientOnSession(const BridgeSessionEvent& evt);
    void clientBeginLoad(uint32_t epoch, const std::string& mapName);
    void clientOnBaseline(const BridgeWorldBaseline& b);
    void clientPurgeWorld();
    void clientStep();
    void clientPublishInput();
    void clientRequestReconnect();
    void setClientState(ClientSyncState s) { m_clientState.store(static_cast<uint8_t>(s), std::memory_order_release); }

private:
    ThreadBridge& m_bridge;
    const MockSimRole m_role;
    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<uint32_t> m_tick{ 0 };
    std::atomic<uint32_t> m_mapEpoch{ 1 };
    std::atomic<uint32_t> m_stallMs{ 0 };

    // Mailbox (any thread -> simulation thread)
    std::mutex m_mailboxMutex;
    std::condition_variable m_mailboxCv;
    std::vector<std::function<void()>> m_mailbox;
    uint64_t m_mailboxPosted = 0;
    uint64_t m_mailboxExecuted = 0;

    // State readable by other threads (guarded)
    mutable std::mutex m_stateMutex;
    std::string m_mapName = "KARNAK_SANCTUARY";
    std::unordered_map<uint32_t, MockEntityState> m_entities;
    std::array<MockPlayerState, MAX_PLAYERS> m_players{};

    // Simulation-thread private
    std::deque<BridgeMessage> m_outbox;
    size_t m_outboxCap = DEFAULT_OUTBOX_CAP;
    std::array<InputState, MAX_PLAYERS> m_inputs{};
    uint32_t m_snapshotIntervalTicks = 2; // 30 Hz snapshot rate at 60 Hz tick
    uint32_t m_snapshotSeq = 0;
    uint32_t m_eventSeq = 0;
    uint32_t m_mapReadyTimeoutTicks = DEFAULT_MAP_READY_TIMEOUT_TICKS;
    bool m_transitionActive = false;

    // Client private
    std::atomic<uint8_t> m_clientState{ static_cast<uint8_t>(ClientSyncState::DISCONNECTED) };
    std::atomic<uint8_t> m_localSlot{ 0xFF };
    std::atomic<uint32_t> m_localGen{ 0 };
    std::atomic<uint64_t> m_reconnectKey{ 0 };
    bool m_mapLoaded = false;
    uint32_t m_loadingEpoch = 0;
    std::string m_loadingMap;
    uint32_t m_loadTicksRemaining = 0;
    uint32_t m_mapLoadTicks = 20;
    uint32_t m_awaitingSinceTick = 0;
    BaselineAssembly m_baseline;
    bool m_autoReconnect = false;
    uint32_t m_reconnectDelayTicks = 30;
    uint32_t m_reconnectAtTick = 0;
    bool m_reconnectScheduled = false;
    bool m_inputActive = false;
    BridgeInputCommand m_localInput;
    uint32_t m_inputSeq = 0;
    uint32_t m_pendingPressMask = 0;
    bool m_respawnPending = false;
    uint32_t m_respawnPendingDeathSeq = 0;
    uint32_t m_respawnLastSentTick = 0;

    // Telemetry
    std::atomic<uint64_t> m_ticksProcessed{ 0 };
    std::atomic<uint64_t> m_inboundDrained{ 0 };
    std::atomic<uint64_t> m_outboundProduced{ 0 };
    std::atomic<uint64_t> m_commandsProcessed{ 0 };
    std::atomic<uint64_t> m_spawnsProcessed{ 0 };
    std::atomic<uint64_t> m_despawnsProcessed{ 0 };
    std::atomic<uint64_t> m_respawnsProcessed{ 0 };
    std::atomic<uint64_t> m_mapChangesProcessed{ 0 };
    std::atomic<uint64_t> m_worldEventsProcessed{ 0 };
    std::atomic<uint64_t> m_stallCount{ 0 };
    std::atomic<uint64_t> m_outboxDepth{ 0 };
    std::atomic<uint64_t> m_outboxPeak{ 0 };
    std::atomic<uint64_t> m_outboxEscalations{ 0 };
    std::atomic<uint64_t> m_bestEffortDropped{ 0 };
    std::atomic<uint64_t> m_inputsRejectedGeneration{ 0 };
    std::atomic<uint64_t> m_inputsRejectedStale{ 0 };
    std::atomic<uint64_t> m_movementSteps{ 0 };
    std::atomic<uint64_t> m_maxMovementStepsPerSlotTick{ 0 };
    std::atomic<uint64_t> m_inputNeutralTimeouts{ 0 };
    std::atomic<uint64_t> m_baselinesSent{ 0 };
    std::atomic<uint64_t> m_baselinesApplied{ 0 };
    std::atomic<uint64_t> m_mapReadyReceived{ 0 };
    std::atomic<uint64_t> m_mapReadyStale{ 0 };
    std::atomic<uint64_t> m_mapTransitionsCompleted{ 0 };
    std::atomic<uint64_t> m_mapLoads{ 0 };
    std::atomic<uint64_t> m_kicksIssued{ 0 };
    std::atomic<uint64_t> m_respawnsConfirmed{ 0 };
    std::atomic<uint64_t> m_respawnDuplicates{ 0 };
    std::atomic<uint64_t> m_respawnRejected{ 0 };
    std::atomic<uint64_t> m_respawnRequestsSent{ 0 };
    std::atomic<uint64_t> m_reconnectRequests{ 0 };
    std::atomic<uint64_t> m_epochAnomalies{ 0 };
    std::atomic<uint64_t> m_snapshotUnknownEntity{ 0 };
    std::atomic<uint64_t> m_sessionEvents{ 0 };
};

} // namespace pse
