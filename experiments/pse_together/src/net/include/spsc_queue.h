#pragma once

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <array>
#include <utility>
#include <thread>
#include <chrono>
#include <type_traits>
#include <vector>

namespace pse {

enum class QueuePushResult : uint8_t {
    SUCCESS = 0,
    DROPPED_WATERMARK = 1,
    DROPPED_FULL = 2,
    BACKPRESSURE_SUCCESS = 3,
    // Critical item rejected because the queue stayed full for the bounded
    // backpressure window. The item is NOT consumed: the caller still owns it and
    // must retry (outbox / inbound pending). It is not a loss and does not by
    // itself request a resync.
    CRITICAL_REJECTED_FULL = 4
};

inline bool isSuccess(QueuePushResult r) {
    return r == QueuePushResult::SUCCESS || r == QueuePushResult::BACKPRESSURE_SUCCESS;
}

struct QueueStats {
    uint64_t pushedCount = 0;
    uint64_t poppedCount = 0;
    uint64_t bestEffortDroppedCount = 0;
    uint64_t backpressureRetryCount = 0;
    uint64_t criticalOverflowCount = 0;
};

// ============================================================================
// Bounded Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
// C++20 compliant with strict acquire-release memory ordering and cacheline
// isolation to eliminate false sharing.
//
// Invariants:
// - Producer thread modifies ONLY m_head.
// - Consumer thread modifies ONLY m_tail.
// - Neither thread mutates the other's index, preventing data races.
// - Storage is fixed-size and bounded; zero dynamic heap allocations during push/pop.
// - Memory for popped items is reset immediately on the consumer thread.
// - Exactly ONE producer thread and ONE consumer thread may use the queue at any
//   time. Ownership may move to another thread only across a happens-before edge
//   (thread start / join). Two concurrent producers corrupt m_head irreversibly;
//   ThreadBridge enforces the ownership at runtime in every build configuration.
// - A rejected push never consumes the item: it is moved only on success, so the
//   caller (the owner of the event) keeps it for a later retry.
// ============================================================================
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static_assert(Capacity >= 4, "Capacity must be at least 4");

public:
    static constexpr size_t MASK = Capacity - 1;

    SpscQueue() = default;
    ~SpscQueue() {
        // Clean up remaining elements on destruction
        clear();
    }

    // Non-copyable, non-movable to guarantee address stability for atomic pointers
    SpscQueue(const SpscQueue&) = delete;
    SpscQueue& operator=(const SpscQueue&) = delete;
    SpscQueue(SpscQueue&&) = delete;
    SpscQueue& operator=(SpscQueue&&) = delete;

    // Direct push (standard SPSC push). Returns true on success, false if full.
    // Producer thread ONLY.
    bool push(T&& item) {
        const uint64_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tailCached >= Capacity) {
            m_tailCached = m_tail.load(std::memory_order_acquire);
            if (head - m_tailCached >= Capacity) {
                return false; // Queue full
            }
        }

        m_buffer[head & MASK] = std::move(item);
        m_head.store(head + 1, std::memory_order_release);
        m_pushedCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Priority-aware push with watermark dropping for best-effort and
    // bounded backpressure / resync signaling for critical events.
    // Producer thread ONLY.
    QueuePushResult pushWithPolicy(T&& item,
                                   bool isCritical,
                                   size_t watermark,
                                   uint32_t maxBackpressureRetries,
                                   std::atomic<bool>* outResyncSignal = nullptr) {
        const uint64_t head = m_head.load(std::memory_order_relaxed);
        uint64_t tail = m_tail.load(std::memory_order_acquire);
        m_tailCached = tail;

        const size_t currentOccupancy = static_cast<size_t>(head - tail);

        if (!isCritical) {
            // Best-Effort Policy:
            // If occupancy reaches or exceeds watermark, drop incoming item to preserve
            // reserved headroom for critical messages.
            if (currentOccupancy >= watermark) {
                m_bestEffortDroppedCount.fetch_add(1, std::memory_order_relaxed);
                return QueuePushResult::DROPPED_WATERMARK;
            }

            if (currentOccupancy >= Capacity) {
                m_bestEffortDroppedCount.fetch_add(1, std::memory_order_relaxed);
                return QueuePushResult::DROPPED_FULL;
            }

            m_buffer[head & MASK] = std::move(item);
            m_head.store(head + 1, std::memory_order_release);
            m_pushedCount.fetch_add(1, std::memory_order_relaxed);
            return QueuePushResult::SUCCESS;
        }

        // Critical Policy:
        // Critical messages can use 100% of buffer capacity.
        if (currentOccupancy < Capacity) {
            m_buffer[head & MASK] = std::move(item);
            m_head.store(head + 1, std::memory_order_release);
            m_pushedCount.fetch_add(1, std::memory_order_relaxed);
            return QueuePushResult::SUCCESS;
        }

        // Queue is completely saturated (100% full).
        // Apply bounded backpressure retry loop: do NOT block simulation thread infinitely!
        for (uint32_t retry = 1; retry <= maxBackpressureRetries; ++retry) {
            std::this_thread::yield();
            tail = m_tail.load(std::memory_order_acquire);
            m_tailCached = tail;

            if (head - tail < Capacity) {
                // Consumer freed space during backpressure window!
                m_buffer[head & MASK] = std::move(item);
                m_head.store(head + 1, std::memory_order_release);
                m_pushedCount.fetch_add(1, std::memory_order_relaxed);
                m_backpressureRetryCount.fetch_add(retry, std::memory_order_relaxed);
                return QueuePushResult::BACKPRESSURE_SUCCESS;
            }
        }

        // Exhausted bounded backpressure budget. The queue remains full; the
        // simulation thread must not hang, so the item is handed back untouched.
        m_criticalOverflowCount.fetch_add(1, std::memory_order_relaxed);
        if (outResyncSignal) {
            outResyncSignal->store(true, std::memory_order_release);
        }
        return QueuePushResult::CRITICAL_REJECTED_FULL;
    }

    // Direct pop. Consumer thread ONLY.
    // Extracts item and resets the buffer slot to free held memory immediately.
    bool pop(T& outItem) {
        const uint64_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCached) {
            m_headCached = m_head.load(std::memory_order_acquire);
            if (tail == m_headCached) {
                return false; // Queue empty
            }
        }

        outItem = std::move(m_buffer[tail & MASK]);
        m_buffer[tail & MASK] = T{}; // Immediate memory release on consumer thread

        m_tail.store(tail + 1, std::memory_order_release);
        m_poppedCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Drain up to maxItems into a vector. Consumer thread ONLY.
    size_t drain(std::vector<T>& outBatch, size_t maxItems = Capacity) {
        size_t count = 0;
        T item;
        while (count < maxItems && pop(item)) {
            outBatch.push_back(std::move(item));
            ++count;
        }
        return count;
    }

    // Query approximate current occupancy (safe from either thread)
    size_t size() const {
        const uint64_t head = m_head.load(std::memory_order_relaxed);
        const uint64_t tail = m_tail.load(std::memory_order_relaxed);
        return (head >= tail) ? static_cast<size_t>(head - tail) : 0;
    }

    bool empty() const {
        return m_head.load(std::memory_order_relaxed) == m_tail.load(std::memory_order_relaxed);
    }

    bool full() const {
        return size() >= Capacity;
    }

    static constexpr size_t capacity() {
        return Capacity;
    }

    // Consumer thread ONLY (or when no producer/consumer thread is running).
    void clear() {
        T item;
        while (pop(item)) {
            // Drain all items
        }
    }

    // Telemetry & metrics snapshot
    QueueStats getStats() const {
        QueueStats s;
        s.pushedCount = m_pushedCount.load(std::memory_order_relaxed);
        s.poppedCount = m_poppedCount.load(std::memory_order_relaxed);
        s.bestEffortDroppedCount = m_bestEffortDroppedCount.load(std::memory_order_relaxed);
        s.backpressureRetryCount = m_backpressureRetryCount.load(std::memory_order_relaxed);
        s.criticalOverflowCount = m_criticalOverflowCount.load(std::memory_order_relaxed);
        return s;
    }

    void resetStats() {
        m_pushedCount.store(0, std::memory_order_relaxed);
        m_poppedCount.store(0, std::memory_order_relaxed);
        m_bestEffortDroppedCount.store(0, std::memory_order_relaxed);
        m_backpressureRetryCount.store(0, std::memory_order_relaxed);
        m_criticalOverflowCount.store(0, std::memory_order_relaxed);
    }

private:
    // Align cachelines to 64 bytes to eliminate false sharing between producer & consumer
    alignas(64) std::atomic<uint64_t> m_head{ 0 };
    alignas(64) uint64_t m_tailCached{ 0 }; // Producer-private cached tail

    alignas(64) std::atomic<uint64_t> m_tail{ 0 };
    alignas(64) uint64_t m_headCached{ 0 }; // Consumer-private cached head

    // Preallocated ring buffer storage
    alignas(64) std::array<T, Capacity> m_buffer{};

    // Telemetry counters
    alignas(64) std::atomic<uint64_t> m_pushedCount{ 0 };
    std::atomic<uint64_t> m_poppedCount{ 0 };
    std::atomic<uint64_t> m_bestEffortDroppedCount{ 0 };
    std::atomic<uint64_t> m_backpressureRetryCount{ 0 };
    std::atomic<uint64_t> m_criticalOverflowCount{ 0 };
};

} // namespace pse

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
