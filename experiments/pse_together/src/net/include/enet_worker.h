#pragma once

#include "thread_bridge.h"
#include "enet_host.h"
#include "enet_client.h"
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <array>
#include <mutex>

namespace pse {
namespace ui { class LobbyHost; class LobbyClient; }

enum class ENetWorkerMode {
    HOST,
    CLIENT
};

// Local ENet enqueue/dispatch observation, NOT proof of UDP send or host receipt.
// Callback runs only on NETWORK; install before start and keep its target alive
// until stop() has joined. No KEX pointers or reconnect secrets cross this API.
struct ClientInputDispatch {
    uint8_t slotId = 0;
    uint32_t generation = 0;
    uint32_t sequence = 0;
    uint64_t sampleOrdinal = 0;
    uint64_t simulationTick = 0;
    uint64_t sampledSteadyNs = 0;
    uint64_t generatedSteadyNs = 0;
    float forward = 0.0f;
    bool sampleFresh = false;
};

inline constexpr uint64_t CLIENT_INPUT_SAMPLE_MAX_AGE_NS = 100000000;
inline constexpr uint64_t CLIENT_INPUT_MAX_DISPATCH_GAP_NS = 20000000000ull;
// Explicitly rotate before exhaustion: wrap is forbidden within a generation.
inline constexpr uint32_t CLIENT_INPUT_ROTATE_SEQUENCE = UINT32_MAX - 1800u;
constexpr bool clientInputMustReconnect(uint32_t lastSequence, uint64_t lastDispatchNs, uint64_t nowNs) {
    return lastSequence >= CLIENT_INPUT_ROTATE_SEQUENCE ||
        (lastDispatchNs != 0 && nowNs >= lastDispatchNs &&
         nowNs - lastDispatchNs > CLIENT_INPUT_MAX_DISPATCH_GAP_NS);
}

struct ENetWorkerStats {
    uint64_t pollLoops = 0;
    uint64_t outboundDrained = 0;
    uint64_t inboundPushed = 0;          // Successful pushInbound only
    uint64_t packetsSent = 0;
    uint64_t packetsReceived = 0;
    uint64_t criticalDeferred = 0;       // Critical messages parked in pending (not lost)
    uint64_t pendingCurrent = 0;
    uint64_t pendingPeak = 0;
    uint64_t bestEffortDropped = 0;      // Snapshots dropped (superseded by newer ones)
    uint64_t inputsReceived = 0;
    uint64_t inputsCoalesced = 0;        // Wire commands merged into a newer latest state
    uint64_t inputsRejectedStale = 0;    // commandSequence <= last accepted
    uint64_t inputsRejectedForeign = 0;  // slotId does not belong to the sending peer
    uint64_t pendingEscalations = 0;     // Pending bound exceeded -> kick / reconnect
    uint64_t reconnects = 0;
    uint64_t lostOnStop = 0;             // Critical messages undeliverable at stop (resync requested)
    uint64_t outboundNoPeer = 0;         // Targeted message has no matching slot + generation (includes unstamped DTOs)
    uint64_t inputsRejectedGeneration = 0; // Wire generation differs from authenticated peer (formerly Foreign)
    uint64_t inputsRejectedJump = 0;     // Initial / forward sequence exceeds local acceptance window
};

// ============================================================================
// ENetWorker
// Dedicated background worker thread executing network I/O and packet pump.
//
// Invariants:
// - All ENet socket and peer APIs run on this worker thread (start/teardown are
//   handed over through std::thread start / join).
// - Interacts with the simulation ONLY through ThreadBridge, as its NETWORK role.
// - The host/client node always runs in bridge mode: no gameplay simulation on
//   this thread.
// - Owner of every critical message received from ENet: once ENet has
//   acknowledged a reliable packet, the worker keeps it in its inbound pending
//   FIFO until the bridge accepts it. The FIFO is bounded; on overflow the
//   affected session is terminated (host: kick, client: reconnect) instead of
//   silently dropping events.
// - Input commands are coalesced per slot into the latest state plus the
//   accumulated rising button edges / press counts.
// ============================================================================
class ENetWorker {
public:
    static constexpr size_t DEFAULT_INBOUND_PENDING_CAP = 4096;
    static constexpr uint32_t DEFAULT_STOP_DRAIN_TIMEOUT_MS = 250;

    ENetWorker(ThreadBridge& bridge, ENetWorkerMode mode);
    ~ENetWorker();

    // Non-copyable, non-movable
    ENetWorker(const ENetWorker&) = delete;
    ENetWorker& operator=(const ENetWorker&) = delete;

    // Start as Host (bridge mode is forced on)
    bool startHost(const HostConfig& config);

    // Start as Client (bridge mode is forced on)
    bool startClient(const ClientConfig& config);

    void stop();
    bool isRunning() const { return m_running.load(std::memory_order_acquire); }

    ENetWorkerStats getStats() const;
    void resetStats();

    // Configuration (call before start)
    void setInboundPendingCap(size_t cap) { m_pendingCap = cap; }
    void setStopDrainTimeoutMs(uint32_t ms) { m_stopDrainTimeoutMs = ms; }
    // Opt-in native input generator; default false preserves existing callers.
    // KEX SIMULATION remains the sole bridge producer. NETWORK caches DTOs,
    // produces one current command per rational 1/60s slot, and never reads KEX.
    void setClientInputCadence(bool enabled) { m_clientInputCadence = enabled; }
    void setClientInputObserver(std::function<void(const ClientInputDispatch&)> observer) { m_clientInputObserver = std::move(observer); }
    // Default-off bounded native snapshot contract. Observer runs on NETWORK and
    // sees scalar DTOs only; true means ENet enqueue, not successful UDP send.
    void setNativePlayerSnapshots(bool enabled) { m_nativePlayerSnapshots = enabled; }
    void setInitialSessionMap(const std::string& name,uint32_t epoch,NativeMapStructure structure={}) { m_sessionMapName=name;m_sessionMapEpoch=epoch;m_sessionMapStructure=structure; }
    void setPlayerSnapshotObserver(std::function<void(bool, const BridgePlayerSnapshot&, size_t)> observer) { m_playerSnapshotObserver = std::move(observer); }

    // Fault injection (any thread): stop draining the outbound queue while still
    // servicing ENet, emulating a stalled transport without destroying sessions.
    void setOutboundDrainPaused(bool paused) { m_outboundPaused.store(paused, std::memory_order_release); }

    // Test inspection only. Not thread-safe while the worker is running.
    ENetHostServer* getHost() { return m_hostServer.get(); }
    ENetClientNode* getClient() { return m_clientNode.get(); }

private:
    std::unique_ptr<ui::LobbyHost> m_uiHost;
    std::unique_ptr<ui::LobbyClient> m_uiClient;
    struct InputMailbox {
        bool dirty = false;
        bool hasSeq = false;
        uint32_t generation = 0;
        uint32_t lastSeq = 0;
        uint32_t lastButtons = 0;
        uint32_t pressedEdges = 0;
        uint8_t attackPresses = 0;
        uint8_t usePresses = 0;
        BridgeInputCommand latest;
    };

    void threadLoop();
    void finalizeOnStop();

    void setupHostCallbacks();
    bool createClientNode();
    void reconnectClient();
    void resetClientInput();
    void dispatchClientInput();

    void deliverInbound(BridgeMessage&& msg);
    void flushInboundPending();
    void flushInputMailboxes();
    void checkPendingBound();

    void drainOutboundQueue();
    void sendOutboundMessage(const BridgeMessage& msg);

    bool handleHostCustomPacket(ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t seq);
    bool handleClientCustomPacket(uint8_t msgType, ByteReader& reader, uint32_t seq);
    void handleHostInput(ENetPeer* peer, ByteReader& reader);
    bool resolvePeerSlot(ENetPeer* peer, uint8_t claimedSlot, uint8_t& outSlot, uint32_t& outGen);
    ENetPeer* resolveOutboundPeer(uint8_t slotId, uint32_t generation);
    void resetMailbox(uint8_t slotId, uint32_t generation);

private:
    ThreadBridge& m_bridge;
    ENetWorkerMode m_mode;
    std::thread m_thread;
    std::mutex m_lifecycleMutex; // Serializes start/stop callers
    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_outboundPaused{ false };

    HostConfig m_hostConfig;
    ClientConfig m_clientConfig;
    std::unique_ptr<ENetHostServer> m_hostServer;
    std::unique_ptr<ENetClientNode> m_clientNode;

    // Network-thread private state
    std::deque<BridgeMessage> m_inboundPending;
    size_t m_pendingCap = DEFAULT_INBOUND_PENDING_CAP;
    uint32_t m_stopDrainTimeoutMs = DEFAULT_STOP_DRAIN_TIMEOUT_MS;
    std::array<InputMailbox, MAX_PLAYERS> m_inputs{};
    std::array<bool, MAX_PLAYERS> m_slotActive{};
    std::array<uint32_t, MAX_PLAYERS> m_slotGen{};
    std::string m_sessionMapName;   // Last map published by the simulation (survives host restart)
    NativeMapStructure m_sessionMapStructure;
    uint32_t m_sessionMapEpoch = 0;
    bool m_clientHadSession = false;
    bool m_reconnectRequested = false;
    std::vector<BridgeMessage> m_outboundBatch;
    bool m_clientInputCadence = false; // configured only before start
    bool m_clientInputReady = false;
    bool m_clientHasSample = false;
    BridgeClientInputSample m_clientSample{};
    uint64_t m_clientReadyNs = 0;
    uint64_t m_clientCadenceEpochNs = 0;
    uint64_t m_clientNextGridSlot = 0;
    uint64_t m_clientLastDispatchNs = 0;
    uint32_t m_clientInputSequence = 0;
    std::function<void(const ClientInputDispatch&)> m_clientInputObserver;
    bool m_nativePlayerSnapshots = false;
    std::function<void(bool, const BridgePlayerSnapshot&, size_t)> m_playerSnapshotObserver;

    // Telemetry
    std::atomic<uint64_t> m_pollLoops{ 0 };
    std::atomic<uint64_t> m_outboundDrained{ 0 };
    std::atomic<uint64_t> m_inboundPushed{ 0 };
    std::atomic<uint64_t> m_packetsSent{ 0 };
    std::atomic<uint64_t> m_packetsReceived{ 0 };
    std::atomic<uint64_t> m_criticalDeferred{ 0 };
    std::atomic<uint64_t> m_pendingCurrent{ 0 };
    std::atomic<uint64_t> m_pendingPeak{ 0 };
    std::atomic<uint64_t> m_bestEffortDropped{ 0 };
    std::atomic<uint64_t> m_inputsReceived{ 0 };
    std::atomic<uint64_t> m_inputsCoalesced{ 0 };
    std::atomic<uint64_t> m_inputsRejectedStale{ 0 };
    std::atomic<uint64_t> m_inputsRejectedForeign{ 0 };
    std::atomic<uint64_t> m_inputsRejectedGeneration{ 0 };
    std::atomic<uint64_t> m_inputsRejectedJump{ 0 };
    std::atomic<uint64_t> m_pendingEscalations{ 0 };
    std::atomic<uint64_t> m_reconnects{ 0 };
    std::atomic<uint64_t> m_lostOnStop{ 0 };
    std::atomic<uint64_t> m_outboundNoPeer{ 0 };
};

} // namespace pse
