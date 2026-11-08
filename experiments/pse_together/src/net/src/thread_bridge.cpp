#include "thread_bridge.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace pse {

ThreadBridge::ThreadBridge() = default;

ThreadBridge::~ThreadBridge() {
    // Both producer/consumer threads must already be joined (bridge outlives them).
    m_inboundQueue.clear();
    m_outboundQueue.clear();
}

// ----------------------------------------------------------------------------
// Thread affinity enforcement (active in Release, not an assert)
// ----------------------------------------------------------------------------
void ThreadBridge::reportViolation(const std::string& message) {
    m_affinityViolations.fetch_add(1, std::memory_order_relaxed);
    if (m_violationHandler) {
        m_violationHandler(message.c_str());
        return;
    }
    std::fprintf(stderr, "[ThreadBridge FATAL] %s\n", message.c_str());
    std::fflush(stderr);
    std::abort();
}

void ThreadBridge::setAffinityViolationHandler(AffinityViolationHandler handler) {
    if (!rolesFreeOrOwnedByCaller()) {
        reportViolation("setAffinityViolationHandler called while another thread owns a bridge role");
        return;
    }
    m_violationHandler = std::move(handler);
}

void ThreadBridge::checkRole(std::atomic<std::thread::id>& role, const char* roleName) {
    if (!m_threadChecksEnabled.load(std::memory_order_relaxed)) return;

    const std::thread::id me = std::this_thread::get_id();
    std::thread::id owner = role.load(std::memory_order_acquire);
    if (owner == me) return;

    if (owner == std::thread::id{}) {
        // Free role: the first user claims it.
        if (role.compare_exchange_strong(owner, me, std::memory_order_acq_rel)) return;
        if (owner == me) return;
    }

    std::ostringstream ss;
    ss << roleName << " role used by thread " << me << " while owned by thread " << owner;
    reportViolation(ss.str());
}

void ThreadBridge::registerRole(std::atomic<std::thread::id>& role, const char* roleName) {
    const std::thread::id me = std::this_thread::get_id();
    std::thread::id expected{};
    if (role.compare_exchange_strong(expected, me, std::memory_order_acq_rel) || expected == me) {
        return;
    }
    if (!m_threadChecksEnabled.load(std::memory_order_relaxed)) {
        role.store(me, std::memory_order_release);
        return;
    }
    std::ostringstream ss;
    ss << "register " << roleName << " thread " << me << " while still owned by thread " << expected
       << " (missing unregister / two concurrent owners)";
    reportViolation(ss.str());
}

void ThreadBridge::unregisterRole(std::atomic<std::thread::id>& role) {
    const std::thread::id me = std::this_thread::get_id();
    std::thread::id expected = me;
    role.compare_exchange_strong(expected, std::thread::id{}, std::memory_order_acq_rel);
}

bool ThreadBridge::rolesFreeOrOwnedByCaller() const {
    const std::thread::id me = std::this_thread::get_id();
    const auto sim = m_simulationThreadId.load(std::memory_order_acquire);
    const auto net = m_networkThreadId.load(std::memory_order_acquire);
    return (sim == std::thread::id{} || sim == me) && (net == std::thread::id{} || net == me);
}

void ThreadBridge::registerSimulationThread() { registerRole(m_simulationThreadId, "SIMULATION"); }
void ThreadBridge::registerNetworkThread() { registerRole(m_networkThreadId, "NETWORK"); }
void ThreadBridge::unregisterSimulationThread() { unregisterRole(m_simulationThreadId); }
void ThreadBridge::unregisterNetworkThread() { unregisterRole(m_networkThreadId); }

void ThreadBridge::assertSimulationThread() { checkRole(m_simulationThreadId, "SIMULATION"); }
void ThreadBridge::assertNetworkThread() { checkRole(m_networkThreadId, "NETWORK"); }

// ----------------------------------------------------------------------------
// Simulation role
// ----------------------------------------------------------------------------
QueuePushResult ThreadBridge::pushOutbound(BridgeMessage&& msg) {
    assertSimulationThread();
    const bool isCritical = (getMessagePriority(msg) == MessagePriority::CRITICAL);
    return m_outboundQueue.pushWithPolicy(
        std::move(msg),
        isCritical,
        m_outboundWatermark.load(std::memory_order_relaxed),
        m_maxBackpressureRetries.load(std::memory_order_relaxed),
        nullptr
    );
}

QueuePushResult ThreadBridge::pushOutbound(const BridgeMessage& msg) {
    BridgeMessage copy(msg);
    return pushOutbound(std::move(copy));
}

bool ThreadBridge::pollInbound(BridgeMessage& outMsg) {
    assertSimulationThread();
    return m_inboundQueue.pop(outMsg);
}

size_t ThreadBridge::drainInbound(std::vector<BridgeMessage>& outBatch, size_t maxBatch) {
    assertSimulationThread();
    return m_inboundQueue.drain(outBatch, maxBatch);
}

// ----------------------------------------------------------------------------
// Network role
// ----------------------------------------------------------------------------
QueuePushResult ThreadBridge::pushInbound(BridgeMessage&& msg) {
    assertNetworkThread();
    const bool isCritical = (getMessagePriority(msg) == MessagePriority::CRITICAL);
    return m_inboundQueue.pushWithPolicy(
        std::move(msg),
        isCritical,
        m_inboundWatermark.load(std::memory_order_relaxed),
        m_maxBackpressureRetries.load(std::memory_order_relaxed),
        nullptr
    );
}

QueuePushResult ThreadBridge::pushInbound(const BridgeMessage& msg) {
    BridgeMessage copy(msg);
    return pushInbound(std::move(copy));
}

bool ThreadBridge::pollOutbound(BridgeMessage& outMsg) {
    assertNetworkThread();
    return m_outboundQueue.pop(outMsg);
}

size_t ThreadBridge::drainOutbound(std::vector<BridgeMessage>& outBatch, size_t maxBatch) {
    assertNetworkThread();
    return m_outboundQueue.drain(outBatch, maxBatch);
}

// ----------------------------------------------------------------------------
// Resync & lifecycle
// ----------------------------------------------------------------------------
void ThreadBridge::requestResync(uint32_t reasons, const std::string& detail) {
    if (reasons == RESYNC_NONE) reasons = RESYNC_EXTERNAL;
    m_resyncReasons.fetch_or(reasons, std::memory_order_acq_rel);
    m_resyncTriggerCount.fetch_add(1, std::memory_order_relaxed);
    if (m_resyncCallback) {
        m_resyncCallback(detail);
    }
}

void ThreadBridge::setResyncCallback(std::function<void(const std::string&)> cb) {
    if (!rolesFreeOrOwnedByCaller()) {
        reportViolation("setResyncCallback called while another thread owns a bridge role");
        return;
    }
    m_resyncCallback = std::move(cb);
}

BridgeMetrics ThreadBridge::getMetrics() const {
    BridgeMetrics m;
    m.inbound = m_inboundQueue.getStats();
    m.outbound = m_outboundQueue.getStats();
    m.resyncReasons = m_resyncReasons.load(std::memory_order_acquire);
    m.resyncRequired = (m.resyncReasons != 0);
    m.resyncTriggerCount = m_resyncTriggerCount.load(std::memory_order_relaxed);
    return m;
}

void ThreadBridge::resetMetrics() {
    m_inboundQueue.resetStats();
    m_outboundQueue.resetStats();
    m_resyncTriggerCount.store(0, std::memory_order_relaxed);
}

void ThreadBridge::resetForNewSession() {
    if (!rolesFreeOrOwnedByCaller()) {
        reportViolation("resetForNewSession called while a producer/consumer thread is still running");
        return;
    }
    m_inboundQueue.clear();
    m_outboundQueue.clear();
    m_resyncReasons.store(0, std::memory_order_release);
    resetMetrics();
}

} // namespace pse
