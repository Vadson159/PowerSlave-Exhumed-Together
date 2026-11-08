// P3 / DOC-64 N1: a fault detected off the attested simulation thread is only posted here (lock-free); the owner
// thread applies it at its next entry. Simulation-owned state is never mutated from a foreign thread.
#pragma once
#include <atomic>

namespace pse::p3 {
class ForeignFault {
public:
    // Any thread. The first reason wins until taken; later posts are counted, not lost silently.
    bool post(const char* reason) noexcept {
        m_posts.fetch_add(1,std::memory_order_relaxed);
        const char* expected=nullptr;
        return reason && m_reason.compare_exchange_strong(expected,reason,std::memory_order_acq_rel);
    }
    // Owner (simulation) thread only.
    const char* take() noexcept { return m_reason.exchange(nullptr,std::memory_order_acq_rel); }
    bool pending() const noexcept { return m_reason.load(std::memory_order_acquire)!=nullptr; }
    unsigned posts() const noexcept { return m_posts.load(std::memory_order_relaxed); }
private:
    std::atomic<const char*> m_reason{nullptr};
    std::atomic<unsigned> m_posts{0};
};
} // namespace pse::p3
