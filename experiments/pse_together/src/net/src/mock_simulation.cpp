#include "mock_simulation.h"
#include "world_scale_config.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace pse {

static void bumpMax(std::atomic<uint64_t>& peak, uint64_t value) {
    uint64_t cur = peak.load(std::memory_order_relaxed);
    while (value > cur && !peak.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {}
}

static constexpr uint32_t playerEntityId(uint8_t slotId) { return 1001u + slotId; }

MockKexSimulation::MockKexSimulation(ThreadBridge& bridge, MockSimRole role)
    : m_bridge(bridge), m_role(role) {
    if (m_role == MockSimRole::HOST) {
        // Slot 0 is the local host player: always present and on the current map.
        MockPlayerState& host = m_players[HOST_SLOT_ID];
        host.active = true;
        host.ready = true;
        host.entityId = playerEntityId(HOST_SLOT_ID);
    }
}

MockKexSimulation::~MockKexSimulation() {
    stop();
}

bool MockKexSimulation::start(double targetHz) {
    if (m_running.load()) return true;
    if (m_thread.joinable()) m_thread.join();
    m_running.store(true);
    m_thread = std::thread(&MockKexSimulation::threadLoop, this, targetHz);
    return true;
}

void MockKexSimulation::stop() {
    m_running.store(false);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

// ----------------------------------------------------------------------------
// Mailbox: the only way other threads mutate the simulation
// ----------------------------------------------------------------------------
void MockKexSimulation::post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(m_mailboxMutex);
    m_mailbox.push_back(std::move(fn));
    ++m_mailboxPosted;
}

void MockKexSimulation::runMailbox() {
    std::vector<std::function<void()>> batch;
    {
        std::lock_guard<std::mutex> lock(m_mailboxMutex);
        batch.swap(m_mailbox);
    }
    for (auto& fn : batch) {
        fn();
    }
    if (!batch.empty()) {
        std::lock_guard<std::mutex> lock(m_mailboxMutex);
        m_mailboxExecuted += batch.size();
    }
    m_mailboxCv.notify_all();
}

bool MockKexSimulation::sync(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(m_mailboxMutex);
    m_mailbox.push_back([] {});
    const uint64_t target = ++m_mailboxPosted;
    return m_mailboxCv.wait_for(lock, timeout, [&] { return m_mailboxExecuted >= target; });
}

void MockKexSimulation::injectTickStall(uint32_t stallMs) {
    m_stallMs.store(stallMs, std::memory_order_release);
}

void MockKexSimulation::setOutboxCap(size_t cap) { post([this, cap] { m_outboxCap = cap; }); }
void MockKexSimulation::setMapReadyTimeoutTicks(uint32_t ticks) { post([this, ticks] { m_mapReadyTimeoutTicks = ticks; }); }
void MockKexSimulation::setMapLoadTicks(uint32_t ticks) { post([this, ticks] { m_mapLoadTicks = (std::max)(1u, ticks); }); }
void MockKexSimulation::setAutoReconnect(bool enabled, uint32_t delayTicks) {
    post([this, enabled, delayTicks] { m_autoReconnect = enabled; m_reconnectDelayTicks = delayTicks; });
}

// ----------------------------------------------------------------------------
// Host hooks
// ----------------------------------------------------------------------------
void MockKexSimulation::triggerSpawn(uint32_t entityId, uint32_t gen, EntityType type, uint16_t subType,
                                     const Transform3D& transform, const Velocity3D& velocity, const ActorStateInfo& state) {
    post([=, this] {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_entities[entityId] = { entityId, gen, type, subType, transform, velocity, state };
        }
        BridgeEntitySpawn spawn;
        spawn.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        spawn.entityId = entityId;
        spawn.entityGeneration = gen;
        spawn.type = static_cast<uint8_t>(type);
        spawn.subType = subType;
        spawn.transform = transform;
        spawn.velocity = velocity;
        spawn.state = state;
        publishCritical(std::move(spawn));
    });
}

void MockKexSimulation::triggerDespawn(uint32_t entityId, uint32_t gen, DespawnReason reason) {
    post([=, this] {
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_entities.erase(entityId);
        }
        BridgeEntityDespawn despawn;
        despawn.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        despawn.entityId = entityId;
        despawn.entityGeneration = gen;
        despawn.reason = static_cast<uint8_t>(reason);
        publishCritical(std::move(despawn));
    });
}

void MockKexSimulation::triggerMapChange(const std::string& newMap, uint32_t newEpoch) {
    post([=, this] {
        if (newEpoch <= m_mapEpoch.load(std::memory_order_relaxed)) {
            m_epochAnomalies.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        // Phase 1: the host switches level; every remote slot must load it and
        // confirm with MAP_READY before it receives the new world's baseline.
        m_mapEpoch.store(newEpoch, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_mapName = newMap;
            m_entities.clear(); // Old level's actors do not survive the transition
            for (uint8_t slot = 1; slot < MAX_PLAYERS; ++slot) {
                MockPlayerState& p = m_players[slot];
                if (!p.active) continue;
                p.ready = false;
                p.kicked = false;
                p.readyDeadlineTick = m_tick.load(std::memory_order_relaxed) + m_mapReadyTimeoutTicks;
            }
        }
        m_transitionActive = true;
        m_mapChangesProcessed.fetch_add(1, std::memory_order_relaxed);

        BridgeMapChange mc;
        mc.newMapEpoch = newEpoch;
        mc.mapName = newMap;
        publishCritical(std::move(mc));
        hostCheckTransition();
    });
}

void MockKexSimulation::triggerWorldEvent(uint16_t eventType, uint32_t sourceId, uint32_t targetId,
                                          uint32_t targetGen, int32_t val, float fval) {
    post([=, this] {
        BridgeWorldEvent evt;
        evt.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        evt.eventSequence = ++m_eventSeq;
        evt.eventType = static_cast<WorldEventType>(eventType);
        evt.sourceEntityId = sourceId;
        evt.targetEntityId = targetId;
        evt.targetGeneration = targetGen;
        evt.eventValue = val;
        evt.floatValue = fval;
        publishCritical(std::move(evt));
    });
}

void MockKexSimulation::triggerRespawn(uint8_t slotId, uint32_t deathSeq, uint32_t newEntityId, uint32_t newGen,
                                       float x, float y, float z) {
    post([=, this] {
        if (slotId >= MAX_PLAYERS) return;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            MockPlayerState& p = m_players[slotId];
            p.transform = Transform3D(x, y, z);
            p.entityId = newEntityId;
            p.entityGeneration = newGen;
            p.deathSequence = deathSeq;
            p.lifeState = LIFE_ALIVE;
            p.health = DEFAULT_HEALTH;
        }
        BridgeRespawnConfirm confirm;
        confirm.slotId = slotId;
        confirm.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        confirm.deathSequence = deathSeq;
        confirm.newEntityId = newEntityId;
        confirm.newGeneration = newGen;
        confirm.posX = x;
        confirm.posY = y;
        confirm.posZ = z;
        confirm.health = DEFAULT_HEALTH;
        confirm.ammo = DEFAULT_PISTOL_AMMO;
        m_respawnsProcessed.fetch_add(1, std::memory_order_relaxed);
        publishCritical(std::move(confirm));
    });
}

void MockKexSimulation::triggerPlayerDeath(uint8_t slotId) {
    post([=, this] {
        if (slotId >= MAX_PLAYERS) return;
        std::lock_guard<std::mutex> lock(m_stateMutex);
        MockPlayerState& p = m_players[slotId];
        if (p.lifeState == LIFE_DEAD) return;
        p.lifeState = LIFE_DEAD;
        p.health = 0;
        ++p.deathSequence;
    });
}

// ----------------------------------------------------------------------------
// Client hooks
// ----------------------------------------------------------------------------
void MockKexSimulation::submitLocalInput(float forward, float right, float yaw, float pitch, uint32_t heldButtons, uint8_t weapon) {
    post([=, this] {
        m_localInput.moveForward = forward;
        m_localInput.moveRight = right;
        m_localInput.yaw = yaw;
        m_localInput.pitch = pitch;
        m_localInput.buttons = heldButtons;
        m_localInput.selectedWeapon = weapon;
        m_inputActive = true;
    });
}

void MockKexSimulation::submitButtonTap(uint32_t buttons) {
    // A press that must be observed even if it is released before the next
    // command is generated: transmitted as pressed in exactly one command.
    post([=, this] {
        m_pendingPressMask |= buttons;
        m_inputActive = true;
    });
}

void MockKexSimulation::stopLocalInput() {
    post([this] { m_inputActive = false; });
}

void MockKexSimulation::requestRespawn() {
    post([this] {
        const uint8_t slot = m_localSlot.load(std::memory_order_relaxed);
        if (slot >= MAX_PLAYERS) return;
        uint32_t deathSeq = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_players[slot].lifeState != LIFE_DEAD) return;
            deathSeq = m_players[slot].deathSequence;
        }
        m_respawnPending = true;
        m_respawnPendingDeathSeq = deathSeq;
        m_respawnLastSentTick = m_tick.load(std::memory_order_relaxed);

        BridgeRespawnRequest req;
        req.slotId = slot;
        req.deathSequence = deathSeq;
        req.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        m_respawnRequestsSent.fetch_add(1, std::memory_order_relaxed);
        publishCritical(std::move(req));
    });
}

void MockKexSimulation::requestInteraction(uint32_t targetEntityId, uint32_t targetGeneration) {
    post([=, this] {
        BridgeInteractionRequest ir;
        ir.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        ir.slotId = m_localSlot.load(std::memory_order_relaxed);
        ir.targetEntityId = targetEntityId;
        ir.targetGeneration = targetGeneration;
        publishCritical(std::move(ir));
    });
}

void MockKexSimulation::requestReconnect() {
    post([this] { clientRequestReconnect(); });
}

// ----------------------------------------------------------------------------
// Inspection
// ----------------------------------------------------------------------------
MockSimStats MockKexSimulation::getStats() const {
    MockSimStats s;
    s.totalTicks = m_ticksProcessed.load(std::memory_order_relaxed);
    s.inboundDrained = m_inboundDrained.load(std::memory_order_relaxed);
    s.outboundProduced = m_outboundProduced.load(std::memory_order_relaxed);
    s.commandsProcessed = m_commandsProcessed.load(std::memory_order_relaxed);
    s.spawnsProcessed = m_spawnsProcessed.load(std::memory_order_relaxed);
    s.despawnsProcessed = m_despawnsProcessed.load(std::memory_order_relaxed);
    s.respawnsProcessed = m_respawnsProcessed.load(std::memory_order_relaxed);
    s.mapChangesProcessed = m_mapChangesProcessed.load(std::memory_order_relaxed);
    s.worldEventsProcessed = m_worldEventsProcessed.load(std::memory_order_relaxed);
    s.stallCount = m_stallCount.load(std::memory_order_relaxed);
    s.outboxDepth = m_outboxDepth.load(std::memory_order_relaxed);
    s.outboxPeak = m_outboxPeak.load(std::memory_order_relaxed);
    s.outboxEscalations = m_outboxEscalations.load(std::memory_order_relaxed);
    s.bestEffortDropped = m_bestEffortDropped.load(std::memory_order_relaxed);
    s.inputsRejectedGeneration = m_inputsRejectedGeneration.load(std::memory_order_relaxed);
    s.inputsRejectedStale = m_inputsRejectedStale.load(std::memory_order_relaxed);
    s.movementSteps = m_movementSteps.load(std::memory_order_relaxed);
    s.maxMovementStepsPerSlotTick = m_maxMovementStepsPerSlotTick.load(std::memory_order_relaxed);
    s.inputNeutralTimeouts = m_inputNeutralTimeouts.load(std::memory_order_relaxed);
    s.baselinesSent = m_baselinesSent.load(std::memory_order_relaxed);
    s.baselinesApplied = m_baselinesApplied.load(std::memory_order_relaxed);
    s.mapReadyReceived = m_mapReadyReceived.load(std::memory_order_relaxed);
    s.mapReadyStale = m_mapReadyStale.load(std::memory_order_relaxed);
    s.mapTransitionsCompleted = m_mapTransitionsCompleted.load(std::memory_order_relaxed);
    s.mapLoads = m_mapLoads.load(std::memory_order_relaxed);
    s.kicksIssued = m_kicksIssued.load(std::memory_order_relaxed);
    s.respawnsConfirmed = m_respawnsConfirmed.load(std::memory_order_relaxed);
    s.respawnDuplicates = m_respawnDuplicates.load(std::memory_order_relaxed);
    s.respawnRejected = m_respawnRejected.load(std::memory_order_relaxed);
    s.respawnRequestsSent = m_respawnRequestsSent.load(std::memory_order_relaxed);
    s.reconnectRequests = m_reconnectRequests.load(std::memory_order_relaxed);
    s.epochAnomalies = m_epochAnomalies.load(std::memory_order_relaxed);
    s.snapshotUnknownEntity = m_snapshotUnknownEntity.load(std::memory_order_relaxed);
    s.sessionEvents = m_sessionEvents.load(std::memory_order_relaxed);
    return s;
}

void MockKexSimulation::resetStats() {
    m_ticksProcessed.store(0);
    m_inboundDrained.store(0);
    m_outboundProduced.store(0);
    m_commandsProcessed.store(0);
    m_spawnsProcessed.store(0);
    m_despawnsProcessed.store(0);
    m_respawnsProcessed.store(0);
    m_mapChangesProcessed.store(0);
    m_worldEventsProcessed.store(0);
    m_stallCount.store(0);
    m_outboxPeak.store(0);
    m_bestEffortDropped.store(0);
    m_movementSteps.store(0);
    m_maxMovementStepsPerSlotTick.store(0);
}

std::string MockKexSimulation::getMapName() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_mapName;
}

size_t MockKexSimulation::getMockEntityCount() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_entities.size();
}

bool MockKexSimulation::hasEntity(uint32_t entityId) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_entities.find(entityId) != m_entities.end();
}

bool MockKexSimulation::getEntityState(uint32_t entityId, MockEntityState& outState) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    auto it = m_entities.find(entityId);
    if (it != m_entities.end()) {
        outState = it->second;
        return true;
    }
    return false;
}

std::map<uint32_t, uint32_t> MockKexSimulation::getEntityGenerations() const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    std::map<uint32_t, uint32_t> out;
    for (const auto& kv : m_entities) {
        out[kv.first] = kv.second.generation;
    }
    return out;
}

Transform3D MockKexSimulation::getPlayerTransform(uint8_t slotId) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (slotId < MAX_PLAYERS) return m_players[slotId].transform;
    return {};
}

MockPlayerState MockKexSimulation::getPlayerState(uint8_t slotId) const {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    if (slotId < MAX_PLAYERS) return m_players[slotId];
    return {};
}

// ----------------------------------------------------------------------------
// Simulation thread
// ----------------------------------------------------------------------------
void MockKexSimulation::threadLoop(double targetHz) {
    m_bridge.registerSimulationThread();

    const auto frameDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(1.0 / targetHz)
    );

    auto lastTime = std::chrono::steady_clock::now();

    std::vector<BridgeMessage> inboundBatch;
    inboundBatch.reserve(THREAD_BRIDGE_INBOUND_CAPACITY);

    while (m_running.load(std::memory_order_relaxed)) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;

        // Artificial stall test hook
        uint32_t stall = m_stallMs.exchange(0, std::memory_order_acquire);
        if (stall > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(stall));
            m_stallCount.fetch_add(1, std::memory_order_relaxed);
            lastTime = std::chrono::steady_clock::now();
        }

        // 0. Hooks posted by other threads run here, on the simulation thread
        runMailbox();

        // 1. Retry the outbox before anything new is produced (FIFO)
        flushOutbox();

        // 2. Drain Inbound Queue (ENet Worker -> Simulation)
        inboundBatch.clear();
        size_t drained = m_bridge.drainInbound(inboundBatch, THREAD_BRIDGE_INBOUND_CAPACITY);
        m_inboundDrained.fetch_add(drained, std::memory_order_relaxed);
        for (auto& msg : inboundBatch) {
            processInboundMessage(msg);
        }

        // 3. Step Simulation Logic (applies at most one input state per player)
        stepSimulation(dt);

        flushOutbox();
        checkOutboxBound();

        m_ticksProcessed.fetch_add(1, std::memory_order_relaxed);
        m_tick.fetch_add(1, std::memory_order_relaxed);

        // Sleep to maintain target frequency (60 Hz)
        auto frameEnd = std::chrono::steady_clock::now();
        auto elapsed = frameEnd - now;
        if (elapsed < frameDuration) {
            std::this_thread::sleep_for(frameDuration - elapsed);
        }
    }

    // Hooks posted after the last tick are executed so sync() callers are released.
    runMailbox();
    flushOutbox();
    if (!m_outbox.empty()) {
        const size_t lost = m_outbox.size();
        m_outbox.clear();
        m_outboxDepth.store(0, std::memory_order_relaxed);
        m_bridge.requestResync(RESYNC_OUTBOUND_LOSS,
            "Simulation stopped with " + std::to_string(lost) + " unpublished critical events");
    }
    m_bridge.unregisterSimulationThread();
}

// ----------------------------------------------------------------------------
// Outbox: the simulation owns every critical event until the bridge accepts it
// ----------------------------------------------------------------------------
void MockKexSimulation::publishCritical(BridgeMessage&& msg) {
    m_outbox.push_back(std::move(msg));
    bumpMax(m_outboxPeak, m_outbox.size());
    flushOutbox();
}

void MockKexSimulation::publishBestEffort(BridgeMessage&& msg) {
    // Superseded by the next snapshot; must never overtake queued critical events.
    if (!m_outbox.empty()) {
        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (isSuccess(m_bridge.pushOutbound(std::move(msg)))) {
        m_outboundProduced.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void MockKexSimulation::flushOutbox() {
    while (!m_outbox.empty()) {
        if (!isSuccess(m_bridge.pushOutbound(std::move(m_outbox.front())))) break;
        m_outbox.pop_front();
        m_outboundProduced.fetch_add(1, std::memory_order_relaxed);
    }
    m_outboxDepth.store(m_outbox.size(), std::memory_order_relaxed);
}

void MockKexSimulation::checkOutboxBound() {
    if (m_outbox.size() <= m_outboxCap) return;

    const size_t dropped = m_outbox.size();
    m_outbox.clear();
    m_outboxDepth.store(0, std::memory_order_relaxed);
    m_outboxEscalations.fetch_add(1, std::memory_order_relaxed);
    m_bridge.requestResync(RESYNC_OUTBOUND_LOSS,
        "Simulation outbox exceeded bound (" + std::to_string(dropped) + "); replacing state by baseline");

    if (m_role == MockSimRole::HOST) {
        // Incremental history is abandoned; every remote slot receives an
        // authoritative replacement (a client that missed a MAP_CHANGE detects
        // the newer epoch in it and loads the level first).
        for (uint8_t slot = 1; slot < MAX_PLAYERS; ++slot) {
            bool active = false;
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                active = m_players[slot].active && !m_players[slot].kicked;
            }
            if (active) hostSendBaseline(slot);
        }
    } else {
        clientRequestReconnect();
    }
}

// ----------------------------------------------------------------------------
// Inbound dispatch
// ----------------------------------------------------------------------------
void MockKexSimulation::processInboundMessage(BridgeMessage& msg) {
    std::visit([this](auto& val) {
        using T = std::decay_t<decltype(val)>;
        const bool host = (m_role == MockSimRole::HOST);
        const auto state = getClientSyncState();
        const uint32_t epoch = m_mapEpoch.load(std::memory_order_relaxed);

        if constexpr (std::is_same_v<T, BridgeSessionEvent>) {
            m_sessionEvents.fetch_add(1, std::memory_order_relaxed);
            if (host) { hostOnSession(val); flushOutbox(); } else { clientOnSession(val); }
        }
        else if constexpr (std::is_same_v<T, BridgeInputCommand>) {
            if (host) hostOnInput(val);
        }
        else if constexpr (std::is_same_v<T, BridgeRespawnRequest>) {
            if (host) hostOnRespawnRequest(val);
        }
        else if constexpr (std::is_same_v<T, BridgeMapReady>) {
            if (host) hostOnMapReady(val);
        }
        else if constexpr (std::is_same_v<T, BridgeInteractionRequest>) {
            if (host && val.slotId < MAX_PLAYERS) {
                m_worldEventsProcessed.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(m_stateMutex);
                ++m_players[val.slotId].interactionsReceived;
            }
        }
        else if constexpr (std::is_same_v<T, BridgeEntitySpawn>) {
            if (host) return;
            if (state != ClientSyncState::SYNCED) return; // Covered by the coming baseline
            if (val.mapEpoch == epoch) {
                m_spawnsProcessed.fetch_add(1, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(m_stateMutex);
                m_entities[val.entityId] = {
                    val.entityId, val.entityGeneration,
                    static_cast<EntityType>(val.type), val.subType,
                    val.transform, val.velocity, val.state
                };
            } else if (val.mapEpoch > epoch) {
                // A newer level without MAP_CHANGE: cannot be repaired in-session.
                m_epochAnomalies.fetch_add(1, std::memory_order_relaxed);
                clientRequestReconnect();
            }
        }
        else if constexpr (std::is_same_v<T, BridgeEntityDespawn>) {
            if (host || state != ClientSyncState::SYNCED || val.mapEpoch != epoch) return;
            m_despawnsProcessed.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto it = m_entities.find(val.entityId);
            if (it != m_entities.end() && it->second.generation == val.entityGeneration) {
                m_entities.erase(it);
            }
        }
        else if constexpr (std::is_same_v<T, BridgeRespawnConfirm>) {
            if (host || val.slotId >= MAX_PLAYERS) return;
            m_respawnsProcessed.fetch_add(1, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(m_stateMutex);
                MockPlayerState& p = m_players[val.slotId];
                p.transform = Transform3D(val.posX, val.posY, val.posZ, val.yaw);
                p.entityId = val.newEntityId;
                p.entityGeneration = val.newGeneration;
                p.deathSequence = val.deathSequence;
                p.lifeState = LIFE_ALIVE;
                p.health = val.health;
            }
            if (val.slotId == m_localSlot.load(std::memory_order_relaxed) &&
                m_respawnPending && val.deathSequence == m_respawnPendingDeathSeq) {
                m_respawnPending = false;
            }
        }
        else if constexpr (std::is_same_v<T, BridgeMapChange>) {
            if (host || state == ClientSyncState::DISCONNECTED) return;
            if (state == ClientSyncState::JOINING) {
                // First assignment of this session: authoritative, may even be older
                // than a level this client loaded in a previous session.
                m_mapChangesProcessed.fetch_add(1, std::memory_order_relaxed);
                if (m_mapLoaded && val.newMapEpoch == epoch && val.mapName == getMapName()) {
                    setClientState(ClientSyncState::AWAITING_BASELINE);
                    m_awaitingSinceTick = m_tick.load(std::memory_order_relaxed);
                    BridgeMapReady ready;
                    ready.slotId = m_localSlot.load(std::memory_order_relaxed);
                    ready.mapEpoch = val.newMapEpoch;
                    ready.connectionGeneration = m_localGen.load(std::memory_order_relaxed);
                    publishCritical(std::move(ready));
                } else {
                    clientBeginLoad(val.newMapEpoch, val.mapName);
                }
                return;
            }
            // In session: levels only move forward.
            const uint32_t target = (std::max)(epoch, state == ClientSyncState::LOADING ? m_loadingEpoch : 0u);
            if (val.newMapEpoch > target) {
                m_mapChangesProcessed.fetch_add(1, std::memory_order_relaxed);
                clientBeginLoad(val.newMapEpoch, val.mapName);
            }
        }
        else if constexpr (std::is_same_v<T, BridgeWorldBaseline>) {
            if (!host) clientOnBaseline(val);
        }
        else if constexpr (std::is_same_v<T, BridgeWorldEvent>) {
            m_worldEventsProcessed.fetch_add(1, std::memory_order_relaxed);
            if (host || state != ClientSyncState::SYNCED || val.mapEpoch != epoch) return;
            std::lock_guard<std::mutex> lock(m_stateMutex);
            auto it = m_entities.find(val.targetEntityId);
            if (it != m_entities.end() && it->second.generation == val.targetGeneration) {
                if (val.eventType == EVENT_DAMAGE_CONFIRMED) {
                    it->second.state.health -= val.eventValue;
                }
            }
        }
        else if constexpr (std::is_same_v<T, BridgeWorldSnapshot>) {
            if (host || state != ClientSyncState::SYNCED || val.mapEpoch != epoch) return;
            std::lock_guard<std::mutex> lock(m_stateMutex);
            for (const auto& es : val.entities) {
                auto it = m_entities.find(es.entityId);
                if (it == m_entities.end() || it->second.generation != es.generation) {
                    // Snapshots update; only Spawn/baseline create.
                    m_snapshotUnknownEntity.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                auto& e = it->second;
                e.transform = Transform3D(es.posX, es.posY, es.posZ, es.yaw, es.pitch, 0.0f);
                e.velocity = Velocity3D(es.velX, es.velY, es.velZ);
                e.state = ActorStateInfo(es.health, DEFAULT_HEALTH, es.stateFlags, es.meta);
            }
        }
        else if constexpr (std::is_same_v<T, BridgePlayerSnapshot>) {
            if (host || val.slotId >= MAX_PLAYERS) return;
            std::lock_guard<std::mutex> lock(m_stateMutex);
            MockPlayerState& p = m_players[val.slotId];
            if (p.lastSnapshotSeq != 0 && val.sequence <= p.lastSnapshotSeq) return;
            p.lastSnapshotSeq = val.sequence;
            p.entityId = val.entityId;
            p.entityGeneration = val.entityGeneration;
            p.transform = Transform3D(val.posX, val.posY, val.posZ, val.yaw, val.pitch);
            p.health = val.health;
            p.lifeState = val.lifeState;
            p.deathSequence = val.deathSequence;
            if (val.slotId == m_localSlot.load(std::memory_order_relaxed) && m_respawnPending &&
                val.lifeState == LIFE_ALIVE && val.deathSequence == m_respawnPendingDeathSeq) {
                m_respawnPending = false; // Reconciled from state
            }
        }
        else if constexpr (std::is_same_v<T, BridgeControl>) {
            // Simulation -> worker only
        }
    }, msg);
}

void MockKexSimulation::stepSimulation(float dt) {
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (auto& kv : m_entities) {
            auto& ent = kv.second;
            ent.transform.x += ent.velocity.vx * dt;
            ent.transform.y += ent.velocity.vy * dt;
            ent.transform.z += ent.velocity.vz * dt;
        }
    }

    if (m_role == MockSimRole::HOST) {
        hostStep();
    } else {
        clientStep();
    }
}

// ----------------------------------------------------------------------------
// Host
// ----------------------------------------------------------------------------
void MockKexSimulation::hostOnSession(const BridgeSessionEvent& evt) {
    if (evt.slotId >= MAX_PLAYERS || evt.slotId == HOST_SLOT_ID) return;
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(m_stateMutex);
    MockPlayerState& p = m_players[evt.slotId];

    switch (evt.type) {
        case BridgeSessionEvent::Type::CLIENT_JOINED:
        case BridgeSessionEvent::Type::RECONNECTED:
            p.active = true;
            p.connectionGeneration = evt.connectionGeneration;
            p.ready = false; // Baseline only after MAP_READY for the current epoch
            p.kicked = false;
            p.readyDeadlineTick = tick + m_mapReadyTimeoutTicks;
            if (p.entityId == 0) {
                p.entityId = playerEntityId(evt.slotId);
                p.entityGeneration = 1;
                p.lifeState = LIFE_ALIVE;
                p.health = DEFAULT_HEALTH;
            }
            m_inputs[evt.slotId] = InputState{};
            m_inputs[evt.slotId].generation = evt.connectionGeneration;
            {
                // The simulation, not WELCOME, is the authority on the level: tell
                // the joining slot which map/epoch to load (no-op if it matches).
                BridgeMapChange mc;
                mc.newMapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
                mc.mapName = m_mapName;
                mc.targetSlot = evt.slotId;
                mc.connectionGeneration = evt.connectionGeneration;
                m_outbox.push_back(std::move(mc));
            }
            break;
        case BridgeSessionEvent::Type::CLIENT_LEFT:
            if (p.active && p.connectionGeneration == evt.connectionGeneration) {
                p.active = false;
                p.ready = false;
                m_inputs[evt.slotId] = InputState{};
            }
            break;
        default:
            break;
    }
}

void MockKexSimulation::hostOnInput(const BridgeInputCommand& cmd) {
    if (cmd.slotId >= MAX_PLAYERS || cmd.slotId == HOST_SLOT_ID) {
        m_inputsRejectedGeneration.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const MockPlayerState& p = m_players[cmd.slotId]; // Written only by this thread
    if (!p.active || cmd.connectionGeneration != p.connectionGeneration) {
        m_inputsRejectedGeneration.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    InputState& in = m_inputs[cmd.slotId];
    if (in.generation != cmd.connectionGeneration) {
        in = InputState{};
        in.generation = cmd.connectionGeneration;
    }
    if (in.hasInput && cmd.sequence <= in.lastSeq) {
        m_inputsRejectedStale.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Latest state replaces older state; one-shot presses accumulate.
    in.hasInput = true;
    in.lastSeq = cmd.sequence;
    in.held = cmd;
    in.ticksSinceInput = 0;
    in.pendingShots += cmd.attackPresses;
    in.pendingUses += cmd.usePresses;
    m_commandsProcessed.fetch_add(1, std::memory_order_relaxed);
}

void MockKexSimulation::hostOnRespawnRequest(const BridgeRespawnRequest& req) {
    if (req.slotId >= MAX_PLAYERS) return;
    std::unique_lock<std::mutex> lock(m_stateMutex);
    MockPlayerState& p = m_players[req.slotId];
    if (!p.active || req.mapEpoch != m_mapEpoch.load(std::memory_order_relaxed)) {
        m_respawnRejected.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    if (p.lifeState == LIFE_DEAD && req.deathSequence == p.deathSequence) {
        p.lifeState = LIFE_ALIVE;
        p.health = DEFAULT_HEALTH;
        p.entityGeneration += 1;
        p.transform = Transform3D(100.0f + 10.0f * req.slotId, 200.0f, 0.0f);
        BridgeRespawnConfirm confirm;
        confirm.slotId = req.slotId;
        confirm.mapEpoch = req.mapEpoch;
        confirm.deathSequence = p.deathSequence;
        confirm.newEntityId = p.entityId;
        confirm.newGeneration = p.entityGeneration;
        confirm.posX = p.transform.x;
        confirm.posY = p.transform.y;
        confirm.posZ = p.transform.z;
        lock.unlock();
        m_respawnsConfirmed.fetch_add(1, std::memory_order_relaxed);
        m_respawnsProcessed.fetch_add(1, std::memory_order_relaxed);
        publishCritical(std::move(confirm));
    } else if (p.lifeState == LIFE_ALIVE && req.deathSequence == p.deathSequence) {
        // Retry of an already honoured request: idempotent, the state travels in
        // PlayerSnapshot; no second respawn.
        m_respawnDuplicates.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_respawnRejected.fetch_add(1, std::memory_order_relaxed);
    }
}

void MockKexSimulation::hostOnMapReady(const BridgeMapReady& mr) {
    if (mr.slotId >= MAX_PLAYERS || mr.slotId == HOST_SLOT_ID) return;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        MockPlayerState& p = m_players[mr.slotId];
        if (!p.active || mr.connectionGeneration != p.connectionGeneration || mr.mapEpoch != m_mapEpoch.load(std::memory_order_relaxed)) {
            m_mapReadyStale.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        p.ready = true;
    }
    m_mapReadyReceived.fetch_add(1, std::memory_order_relaxed);
    // Phase 2: the client has loaded this level; replace its world.
    hostSendBaseline(mr.slotId);
    hostCheckTransition();
}

void MockKexSimulation::hostSendBaseline(uint8_t targetSlot) {
    constexpr size_t PER_BATCH = WorldScaleConfig::calculateMaxBaselineEntitiesPerBatch(1200);
    constexpr size_t MAX_BATCHES = 64; // Client-side limit of MSG_WORLD_BASELINE reassembly

    std::vector<BridgeEntitySpawn> all;
    std::string mapName;
    uint32_t connectionGeneration = 0;
    const uint32_t epoch = m_mapEpoch.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        mapName = m_mapName;
        connectionGeneration = m_players[targetSlot].connectionGeneration;
        all.reserve(m_entities.size());
        for (const auto& kv : m_entities) {
            const auto& e = kv.second;
            BridgeEntitySpawn s;
            s.mapEpoch = epoch;
            s.entityId = e.entityId;
            s.entityGeneration = e.generation;
            s.type = static_cast<uint8_t>(e.type);
            s.subType = e.subType;
            s.transform = e.transform;
            s.velocity = e.velocity;
            s.state = e.state;
            all.push_back(s);
        }
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.entityId < b.entityId; });

    size_t totalBatches = (all.size() + PER_BATCH - 1) / PER_BATCH;
    if (totalBatches == 0) totalBatches = 1;
    if (totalBatches > MAX_BATCHES) {
        // Cannot be represented in one baseline: the session cannot be synced.
        BridgeControl kick;
        kick.type = BridgeControl::Type::KICK_SLOT;
        kick.slotId = targetSlot;
        kick.connectionGeneration = connectionGeneration;
        kick.reasonCode = LEAVE_PROTOCOL_ERROR;
        m_kicksIssued.fetch_add(1, std::memory_order_relaxed);
        publishCritical(std::move(kick));
        return;
    }

    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    for (size_t b = 0; b < totalBatches; ++b) {
        BridgeWorldBaseline batch;
        batch.targetSlot = targetSlot;
        batch.connectionGeneration = connectionGeneration;
        batch.mapEpoch = epoch;
        batch.serverTick = tick;
        batch.mapName = mapName;
        batch.batchIndex = static_cast<uint8_t>(b);
        batch.totalBatches = static_cast<uint8_t>(totalBatches);
        const size_t begin = b * PER_BATCH;
        const size_t end = (std::min)(begin + PER_BATCH, all.size());
        batch.entities.assign(all.begin() + begin, all.begin() + end);
        publishCritical(std::move(batch));
    }
    m_baselinesSent.fetch_add(1, std::memory_order_relaxed);
}

void MockKexSimulation::hostCheckTransition() {
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    std::vector<std::pair<uint8_t, uint32_t>> toKick;
    bool allReady = true;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        for (uint8_t slot = 1; slot < MAX_PLAYERS; ++slot) {
            MockPlayerState& p = m_players[slot];
            if (!p.active || p.ready || p.kicked) continue;
            if (tick >= p.readyDeadlineTick) {
                p.kicked = true; // Could not load the level in time: terminate the session
                toKick.emplace_back(slot, p.connectionGeneration);
            } else {
                allReady = false;
            }
        }
    }
    for (const auto& [slot, generation] : toKick) {
        BridgeControl kick;
        kick.type = BridgeControl::Type::KICK_SLOT;
        kick.slotId = slot;
        kick.connectionGeneration = generation;
        kick.reasonCode = LEAVE_TIMEOUT;
        m_kicksIssued.fetch_add(1, std::memory_order_relaxed);
        publishCritical(std::move(kick));
    }
    if (m_transitionActive && allReady) {
        m_transitionActive = false;
        m_mapTransitionsCompleted.fetch_add(1, std::memory_order_relaxed);
    }
}

void MockKexSimulation::hostStep() {
    // At most ONE movement application per player per tick, from the latest
    // input state; accumulated presses are consumed one per tick (no burst,
    // no loss).
    std::array<uint32_t, MAX_PLAYERS> stepsThisTick{};
    for (uint8_t slot = 1; slot < MAX_PLAYERS; ++slot) {
        InputState& in = m_inputs[slot];
        if (!in.hasInput || !m_players[slot].active) continue;

        if (in.ticksSinceInput >= INPUT_TIMEOUT_TICKS &&
            (in.held.moveForward != 0.0f || in.held.moveRight != 0.0f || in.held.buttons != 0)) {
            in.held.moveForward = 0.0f;
            in.held.moveRight = 0.0f;
            in.held.buttons = 0;
            m_inputNeutralTimeouts.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            MockPlayerState& p = m_players[slot];
            p.transform.x += in.held.moveRight * 0.1f;
            p.transform.y += in.held.moveForward * 0.1f;
            p.transform.yaw = in.held.yaw;
            p.transform.pitch = in.held.pitch;
            if (in.pendingShots > 0) { --in.pendingShots; ++p.shotsFired; }
            if (in.pendingUses > 0) { --in.pendingUses; ++p.usesPerformed; }
        }
        ++stepsThisTick[slot];
        m_movementSteps.fetch_add(1, std::memory_order_relaxed);
        ++in.ticksSinceInput;
    }
    for (uint32_t steps : stepsThisTick) {
        bumpMax(m_maxMovementStepsPerSlotTick, steps);
    }

    hostCheckTransition();

    if ((m_tick.load(std::memory_order_relaxed) % m_snapshotIntervalTicks) == 0) {
        hostPublishSnapshots();
    }
}

void MockKexSimulation::hostPublishSnapshots() {
    if (!m_outbox.empty()) {
        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    const uint32_t seq = ++m_snapshotSeq;

    BridgeWorldSnapshot snap;
    snap.snapshotSequence = seq;
    snap.serverTick = tick;
    snap.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
    std::vector<BridgePlayerSnapshot> players;
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        snap.entities.reserve(m_entities.size());
        for (const auto& kv : m_entities) {
            const auto& ent = kv.second;
            EntitySnapshot es;
            es.entityId = ent.entityId;
            es.generation = ent.generation;
            es.type = static_cast<uint8_t>(ent.type);
            es.subType = ent.subType;
            es.posX = ent.transform.x; es.posY = ent.transform.y; es.posZ = ent.transform.z;
            es.velX = ent.velocity.vx; es.velY = ent.velocity.vy; es.velZ = ent.velocity.vz;
            es.yaw = ent.transform.yaw; es.pitch = ent.transform.pitch;
            es.health = ent.state.health;
            es.stateFlags = ent.state.stateFlags;
            es.meta = ent.state.meta;
            snap.entities.push_back(es);
        }
        for (uint8_t slot = 0; slot < MAX_PLAYERS; ++slot) {
            const MockPlayerState& p = m_players[slot];
            if (!p.active) continue;
            BridgePlayerSnapshot ps;
            ps.slotId = slot;
            ps.entityId = p.entityId;
            ps.entityGeneration = p.entityGeneration;
            ps.sequence = seq;
            ps.serverTick = tick;
            ps.posX = p.transform.x; ps.posY = p.transform.y; ps.posZ = p.transform.z;
            ps.yaw = p.transform.yaw; ps.pitch = p.transform.pitch;
            ps.health = p.health;
            ps.lifeState = p.lifeState;
            ps.deathSequence = p.deathSequence;
            ps.buttons = m_inputs[slot].held.buttons;
            players.push_back(ps);
        }
    }
    publishBestEffort(std::move(snap));
    for (auto& ps : players) {
        publishBestEffort(std::move(ps));
    }
}

// ----------------------------------------------------------------------------
// Client
// ----------------------------------------------------------------------------
void MockKexSimulation::clientPurgeWorld() {
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_entities.clear();
    for (auto& p : m_players) {
        p.lastSnapshotSeq = 0;
    }
    m_baseline = BaselineAssembly{};
}

void MockKexSimulation::clientOnSession(const BridgeSessionEvent& evt) {
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    switch (evt.type) {
        case BridgeSessionEvent::Type::CLIENT_JOINED:
        case BridgeSessionEvent::Type::RECONNECTED: {
            m_reconnectScheduled = false;
            m_localSlot.store(evt.slotId, std::memory_order_release);
            m_localGen.store(evt.connectionGeneration, std::memory_order_release);
            if (evt.reconnectKey != 0) m_reconnectKey.store(evt.reconnectKey, std::memory_order_release);
            m_inputSeq = 0;
            clientPurgeWorld();
            // WELCOME's map is only a hint (it comes from the transport, which may
            // have been restarted). The host simulation assigns the level with a
            // targeted MAP_CHANGE right after it sees this session.
            setClientState(ClientSyncState::JOINING);
            m_awaitingSinceTick = tick;
            break;
        }
        case BridgeSessionEvent::Type::LOCAL_SESSION_LOST:
        case BridgeSessionEvent::Type::REJECTED:
            clientPurgeWorld();
            setClientState(ClientSyncState::DISCONNECTED);
            if (m_autoReconnect && evt.type == BridgeSessionEvent::Type::LOCAL_SESSION_LOST) {
                m_reconnectScheduled = true;
                m_reconnectAtTick = tick + m_reconnectDelayTicks;
            }
            break;
        default:
            break;
    }
}

void MockKexSimulation::clientBeginLoad(uint32_t epoch, const std::string& mapName) {
    clientPurgeWorld();
    m_loadingEpoch = epoch;
    m_loadingMap = mapName;
    m_loadTicksRemaining = m_mapLoadTicks;
    setClientState(ClientSyncState::LOADING);
}

void MockKexSimulation::clientOnBaseline(const BridgeWorldBaseline& b) {
    const auto state = getClientSyncState();
    const uint32_t epoch = m_mapEpoch.load(std::memory_order_relaxed);

    if (state == ClientSyncState::DISCONNECTED || state == ClientSyncState::JOINING) return;
    if (state == ClientSyncState::LOADING) return; // A baseline follows our MAP_READY
    if (b.mapEpoch < epoch) return;                 // Stale level
    if (b.mapEpoch > epoch) {
        // Authoritative state of a newer level we never loaded (missed transition):
        // load it first, MAP_READY will bring a matching baseline.
        m_epochAnomalies.fetch_add(1, std::memory_order_relaxed);
        clientBeginLoad(b.mapEpoch, b.mapName);
        return;
    }
    if (b.totalBatches == 0 || b.totalBatches > 64 || b.batchIndex >= b.totalBatches) return;

    if (!m_baseline.active || m_baseline.epoch != b.mapEpoch || m_baseline.serverTick != b.serverTick ||
        m_baseline.totalBatches != b.totalBatches) {
        m_baseline = BaselineAssembly{};
        m_baseline.active = true;
        m_baseline.epoch = b.mapEpoch;
        m_baseline.serverTick = b.serverTick;
        m_baseline.totalBatches = b.totalBatches;
        m_baseline.mapName = b.mapName;
    }
    const uint64_t bit = 1ull << b.batchIndex;
    if (m_baseline.mask & bit) return; // Duplicate batch
    m_baseline.mask |= bit;
    m_baseline.entities.insert(m_baseline.entities.end(), b.entities.begin(), b.entities.end());

    const uint64_t full = (b.totalBatches == 64) ? ~0ull : ((1ull << b.totalBatches) - 1);
    if ((m_baseline.mask & full) != full) return;

    // Complete: the baseline REPLACES the replicated world (entities missing from
    // it are gone, including those whose despawn was never observed).
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_entities.clear();
        for (const auto& s : m_baseline.entities) {
            m_entities[s.entityId] = {
                s.entityId, s.entityGeneration, static_cast<EntityType>(s.type), s.subType,
                s.transform, s.velocity, s.state
            };
        }
        m_mapName = m_baseline.mapName;
    }
    m_baseline = BaselineAssembly{};
    m_baselinesApplied.fetch_add(1, std::memory_order_relaxed);
    setClientState(ClientSyncState::SYNCED);
}

void MockKexSimulation::clientRequestReconnect() {
    m_reconnectScheduled = false;
    clientPurgeWorld();
    setClientState(ClientSyncState::DISCONNECTED);
    m_reconnectRequests.fetch_add(1, std::memory_order_relaxed);
    BridgeControl ctl;
    ctl.type = BridgeControl::Type::RECONNECT;
    publishCritical(std::move(ctl));
}

void MockKexSimulation::clientStep() {
    const uint32_t tick = m_tick.load(std::memory_order_relaxed);
    const auto state = getClientSyncState();

    if (state == ClientSyncState::DISCONNECTED && m_reconnectScheduled && tick >= m_reconnectAtTick) {
        clientRequestReconnect();
        return;
    }

    if (state == ClientSyncState::LOADING && m_loadTicksRemaining > 0 && --m_loadTicksRemaining == 0) {
        // The level is actually loaded now: only now may MAP_READY be sent.
        m_mapEpoch.store(m_loadingEpoch, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_mapName = m_loadingMap;
        }
        m_mapLoaded = true;
        m_mapLoads.fetch_add(1, std::memory_order_relaxed);
        setClientState(ClientSyncState::AWAITING_BASELINE);
        m_awaitingSinceTick = tick;

        BridgeMapReady ready;
        ready.slotId = m_localSlot.load(std::memory_order_relaxed);
        ready.mapEpoch = m_loadingEpoch;
        ready.connectionGeneration = m_localGen.load(std::memory_order_relaxed);
        publishCritical(std::move(ready));
        return;
    }

    if ((state == ClientSyncState::AWAITING_BASELINE || state == ClientSyncState::JOINING) &&
        tick - m_awaitingSinceTick > BASELINE_WAIT_TIMEOUT_TICKS) {
        clientRequestReconnect();
        return;
    }

    if (state != ClientSyncState::SYNCED) return;

    // Respawn request owned by the client until the host's state confirms it.
    if (m_respawnPending && tick - m_respawnLastSentTick >= RESPAWN_RETRY_TICKS) {
        m_respawnLastSentTick = tick;
        BridgeRespawnRequest req;
        req.slotId = m_localSlot.load(std::memory_order_relaxed);
        req.deathSequence = m_respawnPendingDeathSeq;
        req.mapEpoch = m_mapEpoch.load(std::memory_order_relaxed);
        m_respawnRequestsSent.fetch_add(1, std::memory_order_relaxed);
        publishCritical(std::move(req));
    }

    clientPublishInput();
}

void MockKexSimulation::clientPublishInput() {
    if (!m_inputActive) return;
    BridgeInputCommand cmd = m_localInput;
    cmd.slotId = m_localSlot.load(std::memory_order_relaxed);
    cmd.connectionGeneration = m_localGen.load(std::memory_order_relaxed);
    cmd.sequence = ++m_inputSeq;
    cmd.clientTick = m_tick.load(std::memory_order_relaxed);
    cmd.buttons = m_localInput.buttons | m_pendingPressMask;
    // Inputs carry state and do not depend on critical ordering: pushed directly.
    if (isSuccess(m_bridge.pushOutbound(BridgeMessage(std::move(cmd))))) {
        m_outboundProduced.fetch_add(1, std::memory_order_relaxed);
        m_pendingPressMask = 0; // The press is on the wire; the next command releases it
    } else {
        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

} // namespace pse
