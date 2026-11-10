#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "enet_worker.h"
#include "input_sequence.h"
#include "world_scale_config.h"
#include "ui/lobby.hpp"
#include "ui/shared_p23.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cmath>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace pse {

static uint64_t clientSteadyNs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// DOC-49-style wait on the fixed grid. The fallback still blocks; no spin.
class ClientCadenceSleeper {
#if defined(_WIN32)
    HANDLE m_timer = nullptr;
#endif
public:
    explicit ClientCadenceSleeper(bool enabled) {
#if defined(_WIN32)
        if (enabled) {
            m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!m_timer) m_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
#else
        (void)enabled;
#endif
    }
    ~ClientCadenceSleeper() {
#if defined(_WIN32)
        if (m_timer) CloseHandle(m_timer);
#endif
    }
    void until(uint64_t deadlineNs) {
        const uint64_t now = clientSteadyNs();
        if (deadlineNs <= now) return;
#if defined(_WIN32)
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>((deadlineNs - now + 99) / 100);
        if (m_timer && SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE) &&
            WaitForSingleObject(m_timer, INFINITE) == WAIT_OBJECT_0) return;
#endif
        std::this_thread::sleep_until(std::chrono::steady_clock::time_point(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::nanoseconds(deadlineNs))));
    }
};

static void bumpPeak(std::atomic<uint64_t>& peak, uint64_t value) {
    uint64_t cur = peak.load(std::memory_order_relaxed);
    while (value > cur && !peak.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {}
}

ENetWorker::ENetWorker(ThreadBridge& bridge, ENetWorkerMode mode)
    : m_bridge(bridge), m_mode(mode) {
    m_outboundBatch.reserve(THREAD_BRIDGE_OUTBOUND_CAPACITY);
}

ENetWorker::~ENetWorker() {
    stop();
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------
bool ENetWorker::startHost(const HostConfig& config) {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (m_running.load(std::memory_order_acquire)) return true;
    if (m_thread.joinable()) m_thread.join();

    m_hostConfig = config;
    m_hostConfig.bridgeMode = true;
    m_hostServer = std::make_unique<ENetHostServer>();
    m_hostServer->setCustomPacketHandler([this](ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t seq) {
        if(m_uiHost && m_uiHost->handle(peer,msgType,reader))return true;
        return handleHostCustomPacket(peer, msgType, reader, seq);
    });
    setupHostCallbacks();

    if (!m_hostServer->start(m_hostConfig)) {
        m_hostServer.reset();
        return false;
    }
    if (m_sessionMapEpoch != 0) {
        m_hostServer->setSessionMap(m_sessionMapName,m_sessionMapEpoch,m_sessionMapStructure);
    }
    m_uiHost=std::make_unique<ui::LobbyHost>(*m_hostServer);

    m_inboundPending.clear();
    m_inputs = {};
    m_slotActive = {};
    m_slotGen = {};
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&ENetWorker::threadLoop, this);
    return true;
}

bool ENetWorker::startClient(const ClientConfig& config) {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (m_running.load(std::memory_order_acquire)) return true;
    if (m_thread.joinable()) m_thread.join();

    m_clientConfig = config;
    m_clientConfig.bridgeMode = true;
    m_clientHadSession = false;
    m_reconnectRequested = false;
    resetClientInput();
    if (!createClientNode() || !m_clientNode->connect(m_clientConfig)) {
        m_clientNode.reset();
        return false;
    }

    m_inboundPending.clear();
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread(&ENetWorker::threadLoop, this);
    return true;
}

void ENetWorker::stop() {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    m_running.store(false, std::memory_order_release);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    // ENet teardown already happened on the worker thread (finalizeOnStop).
    m_hostServer.reset();
    m_clientNode.reset();
    m_uiHost.reset();m_uiClient.reset();ui::shared().clear();
}

void ENetWorker::setupHostCallbacks() {
    m_hostServer->onClientAuthenticatedCallback = [this](ENetPeer* /*peer*/, uint8_t slotId, uint32_t gen, bool isReconnect) {
        if (slotId >= MAX_PLAYERS) return;
        m_slotActive[slotId] = true;
        m_slotGen[slotId] = gen;
        resetMailbox(slotId, gen);

        BridgeSessionEvent evt;
        evt.type = isReconnect ? BridgeSessionEvent::Type::RECONNECTED : BridgeSessionEvent::Type::CLIENT_JOINED;
        evt.slotId = slotId;
        evt.connectionGeneration = gen;
        deliverInbound(std::move(evt));
    };

    m_hostServer->onClientSessionEndedCallback = [this](uint8_t slotId, uint32_t gen) {
        if (slotId >= MAX_PLAYERS) return;
        if (!m_slotActive[slotId] || m_slotGen[slotId] != gen) return; // Displaced/stale session
        m_slotActive[slotId] = false;
        resetMailbox(slotId, 0);

        BridgeSessionEvent evt;
        evt.type = BridgeSessionEvent::Type::CLIENT_LEFT;
        evt.slotId = slotId;
        evt.connectionGeneration = gen;
        evt.reasonCode = LEAVE_USER_QUIT;
        deliverInbound(std::move(evt));
    };
}

bool ENetWorker::createClientNode() {
    m_uiClient.reset();
    m_clientNode = std::make_unique<ENetClientNode>();
    m_clientNode->setCustomPacketHandler([this](uint8_t msgType, ByteReader& reader, uint32_t seq) {
        if(m_uiClient && m_uiClient->handle(msgType,reader))return true;
        return handleClientCustomPacket(msgType, reader, seq);
    });

    m_clientNode->onWelcomeCallback = [this](const MsgWelcome& welcome) {
        // The authority assigned this generation. Old input state/sequence must
        // never carry across it, including queued snapshots created before ready.
        resetClientInput();
        BridgeSessionEvent evt;
        evt.type = m_clientHadSession ? BridgeSessionEvent::Type::RECONNECTED : BridgeSessionEvent::Type::CLIENT_JOINED;
        evt.slotId = welcome.assignedSlot;
        evt.connectionGeneration = welcome.connectionGeneration;
        evt.reconnectKey = welcome.reconnectKey;
        evt.mapEpoch = welcome.mapEpoch;
        evt.mapName = welcome.mapName;
        evt.mapStructure=welcome.mapStructure;
        m_clientHadSession = true;
        deliverInbound(std::move(evt));
    };

    m_clientNode->onRejectCallback = [this](const MsgReject& reject) {
        resetClientInput();
        BridgeSessionEvent evt;
        evt.type = BridgeSessionEvent::Type::REJECTED;
        evt.reasonCode = reject.reasonCode;
        deliverInbound(std::move(evt));
    };

    m_clientNode->onDisconnectedCallback = [this]() {
        resetClientInput();
        if (m_clientInputCadence) m_reconnectRequested = true;
        BridgeSessionEvent evt;
        evt.type = BridgeSessionEvent::Type::LOCAL_SESSION_LOST;
        evt.reasonCode = LEAVE_TIMEOUT;
        deliverInbound(std::move(evt));
    };
    m_uiClient=std::make_unique<ui::LobbyClient>(*m_clientNode);
    return true;
}

void ENetWorker::reconnectClient() {
    m_reconnectRequested = false;
    resetClientInput();
    uint64_t key = m_clientNode ? m_clientNode->getReconnectKey() : 0;
    if (m_clientNode) {
        m_clientNode->onDisconnectedCallback = nullptr;
        m_clientNode->disconnect(LEAVE_USER_QUIT);
        m_clientNode.reset();
    }

    BridgeSessionEvent lost;
    lost.type = BridgeSessionEvent::Type::LOCAL_SESSION_LOST;
    lost.reasonCode = LEAVE_USER_QUIT;
    deliverInbound(std::move(lost));

    if (key != 0) m_clientConfig.reconnectKey = key;
    createClientNode();
    if (!m_clientNode->connect(m_clientConfig)) {
        std::cerr << "[ENetWorker] Reconnect attempt failed to start." << std::endl;
    }
    m_reconnects.fetch_add(1, std::memory_order_relaxed);
}

void ENetWorker::resetClientInput() {
    m_clientInputReady = false;
    m_clientHasSample = false;
    m_clientSample = {};
    m_clientReadyNs = 0;
    m_clientCadenceEpochNs = clientSteadyNs();
    m_clientNextGridSlot = 0;
    m_clientLastDispatchNs = 0;
    m_clientInputSequence = 0;
}

void ENetWorker::dispatchClientInput() {
    if (!m_clientInputCadence || !m_clientInputReady || !m_clientNode ||
        !m_clientNode->isAuthenticated() || m_reconnectRequested) return;
    const uint64_t now = clientSteadyNs();
    if (clientInputMustReconnect(m_clientInputSequence, m_clientLastDispatchNs, now)) {
        // No wrap and no catch-up after an unbounded scheduler/transport stall.
        // Re-authenticate with a new low per-generation sequence instead.
        m_reconnectRequested = true;
        m_clientInputReady = false;
        m_clientHasSample = false;
        return;
    }
    const uint64_t elapsed = now - m_clientCadenceEpochNs;
    // floor(elapsed * 60 / 1e9), without multiplying a long uptime by 60.
    const uint64_t dueSlot = (elapsed / 1000000000ull) * 60ull +
        (elapsed % 1000000000ull) * 60ull / 1000000000ull;
    if (dueSlot < m_clientNextGridSlot) return;
    const bool fresh = m_clientHasSample && m_clientSample.valid &&
        m_clientSample.sampledSteadyNs >= m_clientReadyNs &&
        now >= m_clientSample.sampledSteadyNs &&
        now - m_clientSample.sampledSteadyNs <= CLIENT_INPUT_SAMPLE_MAX_AGE_NS;
    MsgInputCommand net;
    net.slotId = m_clientNode->getAssignedSlot();
    net.connectionGeneration = m_clientNode->getConnectionGeneration();
    net.commandSequence = ++m_clientInputSequence;
    net.clientTick = static_cast<uint32_t>(m_clientHasSample ? m_clientSample.simulationTick : 0);
    net.moveForward = fresh ? m_clientSample.moveForward : 0.0f;
    net.selectedWeapon = 0xff;
    if (fresh) {
        net.selectedWeapon = m_clientSample.selectedWeapon;
        // v1.14: the whole client input and its own pose. A stale sample stays neutral with sector = -1 (no pose).
        net.moveRight = m_clientSample.moveRight;
        net.buttons = m_clientSample.buttons;
        net.yaw = m_clientSample.yaw;
        net.pitch = m_clientSample.pitch;
        net.posX = m_clientSample.posX; net.posY = m_clientSample.posY; net.posZ = m_clientSample.posZ;
        net.sector = m_clientSample.sector;
    }
    m_clientNode->sendPacket(CHANNEL_UNRELIABLE_SEQUENCED,
        encodePacket(MSG_INPUT_COMMAND, net.commandSequence, net), false);
    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
    m_clientLastDispatchNs = now;
    // One latest-state command, then skip every expired grid slot; no backlog.
    m_clientNextGridSlot = dueSlot + 1;
    if (m_clientInputObserver) {
        ClientInputDispatch observed;
        observed.slotId = net.slotId;
        observed.generation = net.connectionGeneration;
        observed.sequence = net.commandSequence;
        observed.sampleOrdinal = m_clientHasSample ? m_clientSample.sampleOrdinal : 0;
        observed.simulationTick = m_clientHasSample ? m_clientSample.simulationTick : 0;
        observed.sampledSteadyNs = m_clientHasSample ? m_clientSample.sampledSteadyNs : 0;
        observed.generatedSteadyNs = now;
        observed.forward = net.moveForward;
        observed.sampleFresh = fresh;
        m_clientInputObserver(observed);
    }
}

void ENetWorker::threadLoop() {
    m_bridge.registerNetworkThread();
    const bool nativeCadence = m_mode == ENetWorkerMode::CLIENT && m_clientInputCadence;
    ClientCadenceSleeper sleeper(nativeCadence);

    while (m_running.load(std::memory_order_acquire)) {
        m_pollLoops.fetch_add(1, std::memory_order_relaxed);

        // 1. Hand parked critical events and coalesced inputs to the simulation
        flushInboundPending();
        flushInputMailboxes();

        // 2. Drain Outbound Queue from Simulation and transmit over ENet
        if (!m_outboundPaused.load(std::memory_order_acquire)) {
            drainOutboundQueue();
        }
        if (m_reconnectRequested && m_mode == ENetWorkerMode::CLIENT) {
            m_reconnectRequested = false;
            reconnectClient();
        }
        if (m_mode == ENetWorkerMode::CLIENT) dispatchClientInput();

        // UI commands are value-only; ENet remains owned by NETWORK.
        for(auto& text:ui::shared().takeChat()) {
            if(m_uiHost)m_uiHost->say(std::move(text));
            else if(m_uiClient)m_uiClient->say(std::move(text));
        }
        // 3. Poll ENet Sockets
        if (m_mode == ENetWorkerMode::HOST && m_hostServer) {
            m_hostServer->poll(1);
        } else if (m_mode == ENetWorkerMode::CLIENT && m_clientNode) {
            m_clientNode->poll(nativeCadence ? 0 : 1);
        }

        if(m_uiHost) {m_uiHost->pollUi();ui::shared().publish(m_uiHost->view());}
        if(m_uiClient) ui::shared().publish(m_uiClient->view());
        flushInboundPending();
        flushInputMailboxes();
        checkPendingBound();

        if (nativeCadence) {
            uint64_t deadline = clientSteadyNs() + 4000000ull;
            if (m_clientInputReady) {
                const uint64_t gridDeadline = m_clientCadenceEpochNs +
                    (m_clientNextGridSlot / 60) * 1000000000ull +
                    ((m_clientNextGridSlot % 60) * 1000000000ull + 59) / 60;
                deadline = (std::min)(deadline, gridDeadline);
            }
            sleeper.until(deadline);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    finalizeOnStop();
    m_bridge.unregisterNetworkThread();
}

void ENetWorker::finalizeOnStop() {
    if (m_mode == ENetWorkerMode::CLIENT) resetClientInput();
    // ENet teardown stays on the thread that owns ENet.
    if (m_mode == ENetWorkerMode::HOST && m_hostServer) {
        m_hostServer->stop();
        for (uint8_t slot = 0; slot < MAX_PLAYERS; ++slot) {
            if (!m_slotActive[slot]) continue;
            m_slotActive[slot] = false;
            BridgeSessionEvent evt;
            evt.type = BridgeSessionEvent::Type::CLIENT_LEFT;
            evt.slotId = slot;
            evt.connectionGeneration = m_slotGen[slot];
            evt.reasonCode = LEAVE_SERVER_SHUTDOWN;
            deliverInbound(std::move(evt));
        }
    } else if (m_mode == ENetWorkerMode::CLIENT && m_clientNode) {
        m_clientNode->onDisconnectedCallback = nullptr;
        m_clientNode->disconnect(LEAVE_USER_QUIT);
        if (m_clientHadSession) {
            BridgeSessionEvent evt;
            evt.type = BridgeSessionEvent::Type::LOCAL_SESSION_LOST;
            evt.reasonCode = LEAVE_USER_QUIT;
            deliverInbound(std::move(evt));
        }
    }

    // Give the simulation a bounded chance to accept what is still parked.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_stopDrainTimeoutMs);
    while (!m_inboundPending.empty() && std::chrono::steady_clock::now() < deadline) {
        flushInboundPending();
        if (!m_inboundPending.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!m_inboundPending.empty()) {
        const size_t lost = m_inboundPending.size();
        m_lostOnStop.fetch_add(lost, std::memory_order_relaxed);
        m_inboundPending.clear();
        m_bridge.requestResync(RESYNC_INBOUND_LOSS,
            "ENetWorker stopped with " + std::to_string(lost) + " undelivered critical inbound events");
    }
    m_pendingCurrent.store(0, std::memory_order_relaxed);
    m_inputs = {};
}

ENetWorkerStats ENetWorker::getStats() const {
    ENetWorkerStats s;
    s.pollLoops = m_pollLoops.load(std::memory_order_relaxed);
    s.outboundDrained = m_outboundDrained.load(std::memory_order_relaxed);
    s.inboundPushed = m_inboundPushed.load(std::memory_order_relaxed);
    s.packetsSent = m_packetsSent.load(std::memory_order_relaxed);
    s.packetsReceived = m_packetsReceived.load(std::memory_order_relaxed);
    s.criticalDeferred = m_criticalDeferred.load(std::memory_order_relaxed);
    s.pendingCurrent = m_pendingCurrent.load(std::memory_order_relaxed);
    s.pendingPeak = m_pendingPeak.load(std::memory_order_relaxed);
    s.bestEffortDropped = m_bestEffortDropped.load(std::memory_order_relaxed);
    s.inputsReceived = m_inputsReceived.load(std::memory_order_relaxed);
    s.inputsCoalesced = m_inputsCoalesced.load(std::memory_order_relaxed);
    s.inputsRejectedStale = m_inputsRejectedStale.load(std::memory_order_relaxed);
    s.inputsRejectedForeign = m_inputsRejectedForeign.load(std::memory_order_relaxed);
    s.inputsRejectedGeneration = m_inputsRejectedGeneration.load(std::memory_order_relaxed);
    s.inputsRejectedJump = m_inputsRejectedJump.load(std::memory_order_relaxed);
    s.pendingEscalations = m_pendingEscalations.load(std::memory_order_relaxed);
    s.reconnects = m_reconnects.load(std::memory_order_relaxed);
    s.lostOnStop = m_lostOnStop.load(std::memory_order_relaxed);
    s.outboundNoPeer = m_outboundNoPeer.load(std::memory_order_relaxed);
    return s;
}

void ENetWorker::resetStats() {
    m_pollLoops.store(0);
    m_outboundDrained.store(0);
    m_inboundPushed.store(0);
    m_packetsSent.store(0);
    m_packetsReceived.store(0);
    m_criticalDeferred.store(0);
    m_pendingPeak.store(0);
    m_bestEffortDropped.store(0);
    m_inputsReceived.store(0);
    m_inputsCoalesced.store(0);
    m_inputsRejectedStale.store(0);
    m_inputsRejectedForeign.store(0);
    m_inputsRejectedGeneration.store(0);
    m_inputsRejectedJump.store(0);
    m_pendingEscalations.store(0);
    m_reconnects.store(0);
    m_lostOnStop.store(0);
    m_outboundNoPeer.store(0);
}

// ----------------------------------------------------------------------------
// Inbound ownership: pending FIFO for critical events, coalesced inputs
// ----------------------------------------------------------------------------
void ENetWorker::deliverInbound(BridgeMessage&& msg) {
    if (isCriticalMessage(msg)) {
        // Preserve FIFO order: nothing may overtake an already parked event.
        if (m_inboundPending.empty() && isSuccess(m_bridge.pushInbound(std::move(msg)))) {
            m_inboundPushed.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        m_inboundPending.push_back(std::move(msg));
        m_criticalDeferred.fetch_add(1, std::memory_order_relaxed);
        m_pendingCurrent.store(m_inboundPending.size(), std::memory_order_relaxed);
        bumpPeak(m_pendingPeak, m_inboundPending.size());
        return;
    }

    // Best-effort snapshots: superseded by the next one; never overtake parked criticals.
    if (!m_inboundPending.empty()) {
        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (isSuccess(m_bridge.pushInbound(std::move(msg)))) {
        m_inboundPushed.fetch_add(1, std::memory_order_relaxed);
    } else {
        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void ENetWorker::flushInboundPending() {
    while (!m_inboundPending.empty()) {
        if (!isSuccess(m_bridge.pushInbound(std::move(m_inboundPending.front())))) break;
        m_inboundPending.pop_front();
        m_inboundPushed.fetch_add(1, std::memory_order_relaxed);
    }
    m_pendingCurrent.store(m_inboundPending.size(), std::memory_order_relaxed);
}

void ENetWorker::resetMailbox(uint8_t slotId, uint32_t generation) {
    if (slotId >= MAX_PLAYERS) return;
    m_inputs[slotId] = InputMailbox{};
    m_inputs[slotId].generation = generation;
}

void ENetWorker::flushInputMailboxes() {
    // Session events parked ahead of inputs must reach the simulation first.
    if (!m_inboundPending.empty()) return;

    for (uint8_t slot = 0; slot < MAX_PLAYERS; ++slot) {
        InputMailbox& mb = m_inputs[slot];
        if (!mb.dirty) continue;

        BridgeInputCommand cmd = mb.latest;
        cmd.pressedEdges = mb.pressedEdges;
        cmd.attackPresses = mb.attackPresses;
        cmd.usePresses = mb.usePresses;
        BridgeMessage msg(std::move(cmd));
        if (isSuccess(m_bridge.pushInbound(std::move(msg)))) {
            m_inboundPushed.fetch_add(1, std::memory_order_relaxed);
            mb.dirty = false;
            mb.pressedEdges = 0;
            mb.attackPresses = 0;
            mb.usePresses = 0;
        }
        // Rejected at watermark: keep the mailbox; newer commands merge into it and
        // the latest state plus every accumulated press is retried next loop.
    }
}

void ENetWorker::checkPendingBound() {
    if (m_inboundPending.size() <= m_pendingCap) return;

    m_pendingEscalations.fetch_add(1, std::memory_order_relaxed);
    const size_t parked = m_inboundPending.size();

    if (m_mode == ENetWorkerMode::CLIENT) {
        // The simulation cannot keep up: drop the session and rebuild it from an
        // authoritative baseline instead of losing individual events.
        m_inboundPending.clear();
        m_bridge.requestResync(RESYNC_INBOUND_LOSS,
            "Client inbound pending exceeded bound (" + std::to_string(parked) + "); reconnecting");
        reconnectClient();
        return;
    }

    // Host: lifecycle events are kept (bounded by connections). Requests from
    // clients that cannot be honoured terminate those sessions; they reconnect
    // and resynchronize through WELCOME + MAP_READY + baseline.
    std::deque<BridgeMessage> kept;
    std::array<uint32_t, MAX_PLAYERS> kick{};
    for (auto& msg : m_inboundPending) {
        if (std::holds_alternative<BridgeSessionEvent>(msg)) {
            kept.push_back(std::move(msg));
            continue;
        }
        uint8_t slot = 0xFF;
        uint32_t generation = 0;
        if (auto* rr = std::get_if<BridgeRespawnRequest>(&msg)) { slot = rr->slotId; generation = rr->connectionGeneration; }
        else if (auto* mr = std::get_if<BridgeMapReady>(&msg)) { slot = mr->slotId; generation = mr->connectionGeneration; }
        else if (auto* ir = std::get_if<BridgeInteractionRequest>(&msg)) { slot = ir->slotId; generation = ir->connectionGeneration; }
        // The request may belong to a peer that already left while the FIFO
        // was parked. Escalate only the session that produced this request.
        if (slot < MAX_PLAYERS && m_slotActive[slot] && m_slotGen[slot] == generation) kick[slot] = generation;
    }
    m_inboundPending.swap(kept);
    m_pendingCurrent.store(m_inboundPending.size(), std::memory_order_relaxed);
    for (uint8_t slot = 0; slot < MAX_PLAYERS; ++slot) {
        if (kick[slot] && resolveOutboundPeer(slot, kick[slot])) m_hostServer->kickSlot(slot, LEAVE_KICKED);
    }
    m_bridge.requestResync(RESYNC_INBOUND_LOSS,
        "Host inbound pending exceeded bound (" + std::to_string(parked) + "); affected sessions kicked");
}

// ----------------------------------------------------------------------------
// Outbound
// ----------------------------------------------------------------------------
void ENetWorker::drainOutboundQueue() {
    m_outboundBatch.clear();
    size_t count = m_bridge.drainOutbound(m_outboundBatch, THREAD_BRIDGE_OUTBOUND_CAPACITY);
    m_outboundDrained.fetch_add(count, std::memory_order_relaxed);

    for (const auto& msg : m_outboundBatch) {
        sendOutboundMessage(msg);
    }
    m_outboundBatch.clear();
}

void ENetWorker::sendOutboundMessage(const BridgeMessage& msg) {
    std::visit([this](const auto& val) {
        using T = std::decay_t<decltype(val)>;
        const bool host = (m_mode == ENetWorkerMode::HOST && m_hostServer);
        const bool client = (m_mode == ENetWorkerMode::CLIENT && m_clientNode);

        auto broadcastReliable = [&](NetMessageType msgType, uint32_t seq, const auto& net) {
            auto encoded = encodePacket(msgType, seq, net);
            m_hostServer->broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
            m_packetsSent.fetch_add(1, std::memory_order_relaxed);
        };
        auto sendClientReliable = [&](NetMessageType msgType, const auto& net) {
            auto encoded = encodePacket(msgType, 0, net);
            m_clientNode->sendPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
            m_packetsSent.fetch_add(1, std::memory_order_relaxed);
        };

        if constexpr (std::is_same_v<T, BridgeInputCommand>) {
            if (client && m_clientNode->isAuthenticated()) {
                // Preserve the identity captured by the producer. In particular,
                // an old queued hold must not be relabeled as a rejoined peer.
                if (val.connectionGeneration == 0 ||
                    val.slotId != m_clientNode->getAssignedSlot() ||
                    val.connectionGeneration != m_clientNode->getConnectionGeneration() ||
                    m_clientInputCadence) {
                    m_outboundNoPeer.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                MsgInputCommand net = val.toNet();
                auto encoded = encodePacket(MSG_INPUT_COMMAND, val.sequence, net);
                m_clientNode->sendPacket(CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);
                m_packetsSent.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else if constexpr (std::is_same_v<T, BridgeClientInputSample>) {
            if (!client || !m_clientInputCadence || !m_clientInputReady ||
                !m_clientNode->isAuthenticated() || val.connectionGeneration == 0 ||
                val.slotId != m_clientNode->getAssignedSlot() ||
                val.connectionGeneration != m_clientNode->getConnectionGeneration() ||
                val.sampledSteadyNs < m_clientReadyNs || val.sampledSteadyNs > clientSteadyNs() ||
                val.sampleOrdinal == 0 || (m_clientHasSample && val.sampleOrdinal <= m_clientSample.sampleOrdinal) ||
                !std::isfinite(val.moveForward) ||
                (val.moveForward != -1.0f && val.moveForward != 0.0f && val.moveForward != 1.0f)) {
                m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            m_clientSample = val;
            m_clientHasSample = true;
        }
        else if constexpr (std::is_same_v<T, BridgePlayerSnapshot>) {
            if (host) {
                if (m_nativePlayerSnapshots && (val.targetSlot == BridgePlayerSnapshot::BROADCAST ||
                    !val.connectionGeneration || !val.mapEpoch || val.mapEpoch != m_sessionMapEpoch ||
                    !val.sequence || !val.entityId || !val.entityGeneration || val.slotId >= MAX_PLAYERS ||
                    !std::isfinite(val.posX) || !std::isfinite(val.posY) || !std::isfinite(val.posZ) ||
                    !std::isfinite(val.yaw) || !std::isfinite(val.pitch) || !val.toNet().valid112())) {
                    m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed); return;
                }
                MsgPlayerSnapshot ps;
                ps.snapshotSequence = val.sequence;
                ps.serverTick = val.serverTick;
                ps.mapEpoch = val.mapEpoch;
                ps.players.push_back(val.toNet());
                auto encoded = encodePacket(MSG_PLAYER_SNAPSHOT, val.sequence, ps);
                if (val.targetSlot == BridgePlayerSnapshot::BROADCAST) {
                    m_hostServer->broadcastPacket(CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);
                } else if (auto* peer = resolveOutboundPeer(val.targetSlot, val.connectionGeneration)) {
                    m_hostServer->sendPacket(peer, CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);
                } else return;
                // The accepted native adapter has two slots. Source generation
                // comes from the same validated recipient binding for P2, never
                // from a client pose request or its local simulation.
                if(m_uiHost && val.slotId<2) {
                    const auto sourceGen=val.slotId==0?1u:val.connectionGeneration;
                    m_uiHost->nativePose({val.slotId,sourceGen,val.mapEpoch,val.sequence,val.posX,val.posY,val.posZ,val.yaw,0,val.stateTimeNs},
                        val.lifeState==LIFE_DEAD?ui::State::Dead:ui::State::Alive);
                }
                if (m_playerSnapshotObserver) m_playerSnapshotObserver(true, val, encoded.size());
                m_packetsSent.fetch_add(1, std::memory_order_relaxed);
            }
        }
        else if constexpr (std::is_same_v<T, BridgeWorldSnapshot>) {
            if (host) {
                const auto net = val.toNet();
                const size_t total = net.entities.size();
                constexpr size_t BATCH_SIZE = 20; // 20 * 55B + 16B header = 1116B <= 1200B MTU
                uint8_t totalBatches = static_cast<uint8_t>((total + BATCH_SIZE - 1) / BATCH_SIZE);
                if (totalBatches == 0) totalBatches = 1;

                for (uint8_t b = 0; b < totalBatches; ++b) {
                    MsgWorldSnapshot bSnap;
                    bSnap.snapshotSequence = net.snapshotSequence;
                    bSnap.serverTick = net.serverTick;
                    bSnap.mapEpoch = net.mapEpoch;
                    bSnap.batchIndex = b;
                    bSnap.totalBatches = totalBatches;

                    size_t start = b * BATCH_SIZE;
                    size_t count = (std::min)(BATCH_SIZE, total - start);
                    for (size_t i = 0; i < count; ++i) {
                        bSnap.entities.push_back(net.entities[start + i]);
                    }

                    auto encoded = encodePacket(MSG_WORLD_SNAPSHOT, net.snapshotSequence, bSnap);
                    m_hostServer->broadcastPacket(CHANNEL_WORLD_SNAPSHOT, encoded, false);
                    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        else if constexpr (std::is_same_v<T, BridgeEntitySpawn>) {
            if (host) broadcastReliable(MSG_ENTITY_SPAWN, 0, val.toNet());
        }
        else if constexpr (std::is_same_v<T, BridgeEntityDespawn>) {
            if (host) broadcastReliable(MSG_ENTITY_DESPAWN, 0, val.toNet());
        }
        else if constexpr (std::is_same_v<T, BridgeRespawnRequest>) {
            if (client) {
                MsgRespawnRequest net = val.toNet();
                net.slotId = m_clientNode->getAssignedSlot();
                sendClientReliable(MSG_RESPAWN_REQUEST, net);
            }
        }
        else if constexpr (std::is_same_v<T, BridgeRespawnConfirm>) {
            if (host) broadcastReliable(MSG_RESPAWN_CONFIRM, 0, val.toNet());
        }
        else if constexpr (std::is_same_v<T, BridgeMapChange>) {
            if (host) {
                if (val.targetSlot == BridgeMapChange::BROADCAST) {
                    // WELCOME must announce the map of the authoritative simulation.
                    m_sessionMapName = val.mapName;
                    m_sessionMapEpoch = val.newMapEpoch;
                    m_sessionMapStructure=val.mapStructure;
                    m_hostServer->setSessionMap(val.mapName,val.newMapEpoch,val.mapStructure);
                    broadcastReliable(MSG_MAP_CHANGE, 0, val.toNet());
                } else if (ENetPeer* peer = resolveOutboundPeer(val.targetSlot, val.connectionGeneration)) {
                    auto encoded = encodePacket(MSG_MAP_CHANGE, 0, val.toNet());
                    m_hostServer->sendPacket(peer, CHANNEL_RELIABLE_CONTROL, encoded, true);
                    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        else if constexpr (std::is_same_v<T, BridgeMapReady>) {
            if (client) {
                // A ready queued by an old client session must not be re-stamped
                // onto a new connection when its outbound queue finally drains.
                if (!m_clientNode->isAuthenticated() || val.connectionGeneration == 0 ||
                    val.slotId != m_clientNode->getAssignedSlot() ||
                    val.connectionGeneration != m_clientNode->getConnectionGeneration()) {
                    m_outboundNoPeer.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                MsgMapReady net = val.toNet();
                sendClientReliable(MSG_MAP_READY, net);
                if (m_clientInputCadence && val.mapEpoch == m_clientNode->getMapEpoch() && !m_clientInputReady) {
                    m_clientInputReady = true;
                    m_clientReadyNs = clientSteadyNs();
                    m_clientHasSample = false;
                }
            }
        }
        else if constexpr (std::is_same_v<T, BridgeWorldEvent>) {
            if (host) broadcastReliable(MSG_WORLD_EVENT_CONFIRM, val.eventSequence, val.toNet());
        }
        else if constexpr (std::is_same_v<T, BridgeInteractionRequest>) {
            if (client) {
                MsgInteractionRequest net = val.toNet();
                net.slotId = m_clientNode->getAssignedSlot();
                sendClientReliable(MSG_INTERACTION_REQUEST, net);
            }
        }
        else if constexpr (std::is_same_v<T, BridgeWorldBaseline>) {
            if (host) {
                auto encoded = encodePacket(MSG_WORLD_BASELINE, val.serverTick, val.toNet());
                if (val.targetSlot == BridgeWorldBaseline::BROADCAST) {
                    m_hostServer->broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
                    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
                } else if (ENetPeer* peer = resolveOutboundPeer(val.targetSlot, val.connectionGeneration)) {
                    m_hostServer->sendPacket(peer, CHANNEL_RELIABLE_CONTROL, encoded, true);
                    m_packetsSent.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        else if constexpr (std::is_same_v<T, BridgeControl>) {
            if (host && val.type == BridgeControl::Type::KICK_SLOT) {
                if (resolveOutboundPeer(val.slotId, val.connectionGeneration) &&
                    !m_hostServer->kickSlot(val.slotId, val.reasonCode)) {
                    m_outboundNoPeer.fetch_add(1, std::memory_order_relaxed);
                }
            } else if (m_mode == ENetWorkerMode::CLIENT && val.type == BridgeControl::Type::RECONNECT) {
                if (m_clientInputCadence && (!m_clientNode || val.connectionGeneration == 0 ||
                    val.connectionGeneration != m_clientNode->getConnectionGeneration())) {
                    m_outboundNoPeer.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                m_reconnectRequested = true;
            }
        }
    }, msg);
}

// ----------------------------------------------------------------------------
// Inbound packet handlers (network thread)
// ----------------------------------------------------------------------------
ENetPeer* ENetWorker::resolveOutboundPeer(uint8_t slotId, uint32_t generation) {
    uint8_t currentSlot = 0;
    uint32_t currentGeneration = 0;
    ENetPeer* peer = m_hostServer && slotId < MAX_PLAYERS ? m_hostServer->getPeerForSlot(slotId) : nullptr;
    // Resolve and apply on this NETWORK thread, with no poll/callback between
    // the check and the action. Zero is not a wildcard for unicast.
    if (!peer || generation == 0 || !m_slotActive[slotId] || m_slotGen[slotId] != generation ||
        !m_hostServer->getPeerSession(peer, currentSlot, currentGeneration) ||
        currentSlot != slotId || currentGeneration != generation) {
        m_outboundNoPeer.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    return peer;
}

bool ENetWorker::resolvePeerSlot(ENetPeer* peer, uint8_t claimedSlot, uint8_t& outSlot, uint32_t& outGen) {
    if (!m_hostServer || !m_hostServer->getPeerSession(peer, outSlot, outGen) || claimedSlot != outSlot) {
        m_inputsRejectedForeign.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void ENetWorker::handleHostInput(ENetPeer* peer, ByteReader& reader) {
    MsgInputCommand cmd;
    if (!cmd.deserialize(reader)) return;

    uint8_t slot = 0;
    uint32_t gen = 0;
    if (!resolvePeerSlot(peer, cmd.slotId, slot, gen)) return;

    // Do not launder an old/forged wire generation into the current session.
    // Reject before touching lastSeq/edges: a foreign high sequence must not
    // poison the valid generation's mailbox (M1.2a real-ENet reproducer).
    if (cmd.connectionGeneration != gen) {
        m_inputsRejectedGeneration.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    InputMailbox& mb = m_inputs[slot];
    const auto sequenceResult = validateInputSequence(mb.generation == gen && mb.hasSeq,
        mb.lastSeq, cmd.commandSequence);
    if (sequenceResult == InputSequenceResult::STALE) {
        m_inputsRejectedStale.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (sequenceResult == InputSequenceResult::EXCESSIVE_JUMP) {
        m_inputsRejectedJump.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // No rejected sequence may reset or mutate the mailbox, including the
    // initial-packet path. Normal lifecycle callbacks already reset on auth.
    if (mb.generation != gen) resetMailbox(slot, gen);
    mb.hasSeq = true;
    mb.lastSeq = cmd.commandSequence;

    const uint32_t rising = cmd.buttons & ~mb.lastButtons;
    mb.lastButtons = cmd.buttons;
    mb.pressedEdges |= rising;
    if ((rising & BUTTON_ATTACK) && mb.attackPresses < 255) ++mb.attackPresses;
    if ((rising & BUTTON_USE) && mb.usePresses < 255) ++mb.usePresses;

    if (mb.dirty) m_inputsCoalesced.fetch_add(1, std::memory_order_relaxed);
    mb.latest = BridgeInputCommand::fromNet(cmd);
    mb.latest.slotId = slot;
    mb.latest.connectionGeneration = gen; // Authoritative generation from the session ledger
    mb.dirty = true;
    m_inputsReceived.fetch_add(1, std::memory_order_relaxed);
}

bool ENetWorker::handleHostCustomPacket(ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t /*seq*/) {
    m_packetsReceived.fetch_add(1, std::memory_order_relaxed);
    uint8_t slot = 0;
    uint32_t gen = 0;

    if (msgType == MSG_INPUT_COMMAND) {
        handleHostInput(peer, reader);
        return true;
    }
    else if (msgType == MSG_RESPAWN_REQUEST) {
        MsgRespawnRequest req;
        if (req.deserialize(reader) && resolvePeerSlot(peer, req.slotId, slot, gen)) {
            auto dto = BridgeRespawnRequest::fromNet(req);
            dto.connectionGeneration = gen;
            deliverInbound(std::move(dto));
            return true;
        }
    }
    else if (msgType == MSG_MAP_READY) {
        MsgMapReady mr;
        if (mr.deserialize(reader) && reader.remaining()==0 && resolvePeerSlot(peer, mr.slotId, slot, gen)) {
            auto dto = BridgeMapReady::fromNet(mr);
            dto.connectionGeneration = gen; // Authenticated sending peer, preserved through pending/FIFO.
            deliverInbound(std::move(dto));
            return true;
        }
    }
    else if (msgType == MSG_INTERACTION_REQUEST) {
        MsgInteractionRequest ir;
        if (ir.deserialize(reader) && resolvePeerSlot(peer, ir.slotId, slot, gen)) {
            auto dto = BridgeInteractionRequest::fromNet(ir);
            dto.connectionGeneration = gen;
            deliverInbound(std::move(dto));
            return true;
        }
    }
    return false;
}

bool ENetWorker::handleClientCustomPacket(uint8_t msgType, ByteReader& reader, uint32_t seq) {
    m_packetsReceived.fetch_add(1, std::memory_order_relaxed);

    if (msgType == MSG_PLAYER_SNAPSHOT) {
        MsgPlayerSnapshot ps;
        if (ps.deserialize(reader) && reader.remaining() == 0) {
            if (m_nativePlayerSnapshots) {
                if (!m_clientNode || !m_clientNode->isAuthenticated() || !ps.mapEpoch ||
                    ps.mapEpoch != m_clientNode->getMapEpoch() || !ps.snapshotSequence ||
                    ps.snapshotSequence != seq || ps.players.empty()) {
                    m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed); return true;
                }
                std::array<bool, MAX_PLAYERS> seen{};
                for (const auto& p : ps.players) {
                    if (p.slotId >= MAX_PLAYERS || seen[p.slotId] || !p.entityId || !p.entityGeneration) {
                        m_bestEffortDropped.fetch_add(1, std::memory_order_relaxed); return true;
                    }
                    seen[p.slotId] = true;
                }
            }
            for (const auto& p : ps.players) {
                auto dto = BridgePlayerSnapshot::fromNet(p, ps.snapshotSequence, ps.serverTick,
                    ps.mapEpoch, m_clientNode ? m_clientNode->getConnectionGeneration() : 0);
                if (m_playerSnapshotObserver) m_playerSnapshotObserver(false, dto, reader.position());
                deliverInbound(std::move(dto));
            }
            return true;
        }
    }
    else if (msgType == MSG_WORLD_SNAPSHOT) {
        MsgWorldSnapshot ws;
        if (ws.deserialize(reader)) {
            deliverInbound(BridgeWorldSnapshot::fromNet(ws));
            return true;
        }
    }
    else if (msgType == MSG_ENTITY_SPAWN) {
        MsgEntitySpawn sp;
        if (sp.deserialize(reader)) {
            deliverInbound(BridgeEntitySpawn::fromNet(sp));
            return true;
        }
    }
    else if (msgType == MSG_ENTITY_DESPAWN) {
        MsgEntityDespawn dp;
        if (dp.deserialize(reader)) {
            deliverInbound(BridgeEntityDespawn::fromNet(dp));
            return true;
        }
    }
    else if (msgType == MSG_RESPAWN_CONFIRM) {
        MsgRespawnConfirm rc;
        if (rc.deserialize(reader)) {
            deliverInbound(BridgeRespawnConfirm::fromNet(rc));
            return true;
        }
    }
    else if (msgType == MSG_MAP_CHANGE) {
        MsgMapChange mc;
        if (mc.deserialize(reader) && reader.remaining()==0) {
            auto dto = BridgeMapChange::fromNet(mc);
            if (m_clientInputCadence && m_clientNode) dto.connectionGeneration = m_clientNode->getConnectionGeneration();
            deliverInbound(std::move(dto));
            return true;
        }
    }
    else if (msgType == MSG_WORLD_EVENT_CONFIRM) {
        MsgWorldEventConfirm ec;
        if (ec.deserialize(reader)) {
            deliverInbound(BridgeWorldEvent::fromNet(ec));
            return true;
        }
    }
    else if (msgType == MSG_WORLD_BASELINE) {
        MsgWorldBaseline wb;
        if (wb.deserialize(reader)) {
            auto dto = BridgeWorldBaseline::fromNet(wb);
            if (m_clientInputCadence && m_clientNode) dto.connectionGeneration = m_clientNode->getConnectionGeneration();
            deliverInbound(std::move(dto));
            return true;
        }
    }
    (void)seq;
    return false;
}

} // namespace pse
