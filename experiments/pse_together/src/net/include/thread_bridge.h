#pragma once

#include "spsc_queue.h"
#include "thread_bridge_dto.h"
#include <functional>
#include <atomic>
#include <thread>
#include <string>
#include <vector>

namespace pse {

// ============================================================================
// Thread Bridge Concurrency & Queue Configuration
// ============================================================================
constexpr size_t THREAD_BRIDGE_INBOUND_CAPACITY = 128;
constexpr size_t THREAD_BRIDGE_OUTBOUND_CAPACITY = 256;

constexpr size_t DEFAULT_INBOUND_WATERMARK = 96;   // 75% of 128
constexpr size_t DEFAULT_OUTBOUND_WATERMARK = 192; // 75% of 256

constexpr uint32_t DEFAULT_BACKPRESSURE_RETRIES = 50;

// Reasons an event owner gave up on in-session delivery and asked for a full
// authoritative resynchronization (baseline replacement or reconnect).
enum ResyncReason : uint32_t {
    RESYNC_NONE          = 0,
    RESYNC_OUTBOUND_LOSS = 1u << 0, // Simulation outbox exceeded its bound
    RESYNC_INBOUND_LOSS  = 1u << 1, // Network inbound pending exceeded its bound / dropped at stop
    RESYNC_EXTERNAL      = 1u << 2  // Requested by an adapter or a test
};

struct BridgeMetrics {
    QueueStats inbound;
    QueueStats outbound;
    bool resyncRequired = false;
    uint32_t resyncReasons = 0;
    uint64_t resyncTriggerCount = 0;
};

// ============================================================================
// ThreadBridge
// Strictly decouples KEX Simulation Thread from ENet Background Worker Thread
// via exactly TWO bounded lock-free SPSC ring buffers.
//
// Inbound Queue:  ENet Worker  -> KEX Simulation (Capacity 128)
// Outbound Queue: KEX Simulation -> ENet Worker  (Capacity 256)
//
// Delivery contract: the bridge is a bounded transport, not a durable store.
// A push that is rejected returns the event untouched to its owner (the
// simulation outbox or the worker inbound pending list), which retries it.
// Loss is declared only by an owner, via requestResync().
//
// Thread contract (enforced in every build configuration):
//   SIMULATION role: pushOutbound, pollInbound, drainInbound
//   NETWORK role:    pushInbound, pollOutbound, drainOutbound
// A role is owned by exactly one thread at a time. A thread takes a role with
// register*Thread() (or implicitly on first use while the role is free) and
// gives it back with unregister*Thread(). Any other thread touching the role
// is a fatal affinity violation.
// ============================================================================
class ThreadBridge {
public:
    using AffinityViolationHandler = std::function<void(const char* message)>;

    ThreadBridge();
    ~ThreadBridge();

    // Non-copyable, non-movable
    ThreadBridge(const ThreadBridge&) = delete;
    ThreadBridge& operator=(const ThreadBridge&) = delete;
    ThreadBridge(ThreadBridge&&) = delete;
    ThreadBridge& operator=(ThreadBridge&&) = delete;

    // ------------------------------------------------------------------------
    // Thread Identity & Isolation Enforcement
    // ------------------------------------------------------------------------
    void registerSimulationThread();
    void registerNetworkThread();
    void unregisterSimulationThread();
    void unregisterNetworkThread();

    // Checks are ON by default. Disabling them is only meant for benchmarks.
    void enableThreadChecks(bool enable) { m_threadChecksEnabled.store(enable, std::memory_order_relaxed); }
    bool areThreadChecksEnabled() const { return m_threadChecksEnabled.load(std::memory_order_relaxed); }

    // Default handler prints the violation and calls std::abort(). A handler
    // must not return normally; returning makes the guarded call proceed.
    // Tests may install one that throws to observe the violation in-process.
    void setAffinityViolationHandler(AffinityViolationHandler handler);

    void assertSimulationThread();
    void assertNetworkThread();
    uint64_t getAffinityViolationCount() const { return m_affinityViolations.load(std::memory_order_relaxed); }

    // ------------------------------------------------------------------------
    // KEX Simulation Thread Interface
    // ------------------------------------------------------------------------
    // Push local state mutation / event to network thread (Simulation -> ENet).
    // On any non-success result `msg` is left intact for the caller to retry.
    QueuePushResult pushOutbound(BridgeMessage&& msg);
    QueuePushResult pushOutbound(const BridgeMessage& msg);

    // Poll a single incoming message from network thread
    bool pollInbound(BridgeMessage& outMsg);

    // Drain up to maxBatch incoming messages from network thread
    size_t drainInbound(std::vector<BridgeMessage>& outBatch, size_t maxBatch = THREAD_BRIDGE_INBOUND_CAPACITY);

    // ------------------------------------------------------------------------
    // ENet Network Worker Thread Interface
    // ------------------------------------------------------------------------
    // Push parsed network packet into simulation queue (ENet -> Simulation).
    // On any non-success result `msg` is left intact for the caller to retry.
    QueuePushResult pushInbound(BridgeMessage&& msg);
    QueuePushResult pushInbound(const BridgeMessage& msg);

    // Poll a single outgoing message produced by simulation
    bool pollOutbound(BridgeMessage& outMsg);

    // Drain up to maxBatch outgoing messages produced by simulation
    size_t drainOutbound(std::vector<BridgeMessage>& outBatch, size_t maxBatch = THREAD_BRIDGE_OUTBOUND_CAPACITY);

    // ------------------------------------------------------------------------
    // Resynchronization signalling (any thread)
    // ------------------------------------------------------------------------
    // Called by an event owner that could not deliver in-session. Invokes the
    // resync callback synchronously on the calling thread.
    void requestResync(uint32_t reasons, const std::string& detail);

    // Atomically fetch and clear the pending reasons. A request racing with the
    // consumer is never lost: it is either returned now or by the next call.
    uint32_t consumeResyncReasons() { return m_resyncReasons.exchange(0, std::memory_order_acq_rel); }
    bool isResyncRequired() const { return m_resyncReasons.load(std::memory_order_acquire) != 0; }

    // ------------------------------------------------------------------------
    // Callbacks & Telemetry
    // ------------------------------------------------------------------------
    // Must be set while no other thread owns a bridge role. The callback runs on
    // whichever thread calls requestResync() and must be thread-safe.
    void setResyncCallback(std::function<void(const std::string&)> cb);

    BridgeMetrics getMetrics() const;
    void resetMetrics(); // Statistics only; never clears pending resync reasons

    // Clears both queues and all pending resync reasons. Only legal while no
    // thread other than the caller owns a role (both producer and consumer are
    // stopped); otherwise it is an affinity violation.
    void resetForNewSession();

    // Capacity & configuration inspection
    size_t getInboundCapacity() const { return THREAD_BRIDGE_INBOUND_CAPACITY; }
    size_t getOutboundCapacity() const { return THREAD_BRIDGE_OUTBOUND_CAPACITY; }
    size_t getInboundWatermark() const { return m_inboundWatermark.load(std::memory_order_relaxed); }
    size_t getOutboundWatermark() const { return m_outboundWatermark.load(std::memory_order_relaxed); }
    void setInboundWatermark(size_t w) { m_inboundWatermark.store(w, std::memory_order_relaxed); }
    void setOutboundWatermark(size_t w) { m_outboundWatermark.store(w, std::memory_order_relaxed); }
    void setMaxBackpressureRetries(uint32_t r) { m_maxBackpressureRetries.store(r, std::memory_order_relaxed); }

    size_t getInboundOccupancy() const { return m_inboundQueue.size(); }
    size_t getOutboundOccupancy() const { return m_outboundQueue.size(); }

private:
    void checkRole(std::atomic<std::thread::id>& role, const char* roleName);
    void registerRole(std::atomic<std::thread::id>& role, const char* roleName);
    void unregisterRole(std::atomic<std::thread::id>& role);
    bool rolesFreeOrOwnedByCaller() const;
    void reportViolation(const std::string& message);

    SpscQueue<BridgeMessage, THREAD_BRIDGE_INBOUND_CAPACITY> m_inboundQueue;
    SpscQueue<BridgeMessage, THREAD_BRIDGE_OUTBOUND_CAPACITY> m_outboundQueue;

    std::atomic<size_t> m_inboundWatermark{ DEFAULT_INBOUND_WATERMARK };
    std::atomic<size_t> m_outboundWatermark{ DEFAULT_OUTBOUND_WATERMARK };
    std::atomic<uint32_t> m_maxBackpressureRetries{ DEFAULT_BACKPRESSURE_RETRIES };

    std::atomic<uint32_t> m_resyncReasons{ 0 };
    std::atomic<uint64_t> m_resyncTriggerCount{ 0 };
    std::function<void(const std::string&)> m_resyncCallback;

    // Concurrency enforcement
    std::atomic<bool> m_threadChecksEnabled{ true };
    std::atomic<uint64_t> m_affinityViolations{ 0 };
    AffinityViolationHandler m_violationHandler;
    std::atomic<std::thread::id> m_simulationThreadId{};
    std::atomic<std::thread::id> m_networkThreadId{};
};

} // namespace pse
