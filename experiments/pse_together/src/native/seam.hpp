// DOC58B DLL side of the native visual-proxy seam. The Frida adapter (adapter58b.js, a bounded copy of
// the DOC58A controller/adapter) registers callbacks; this DLL only passes DTO copies and opaque handles.
// Every call is made from the client's attested simulation-thread StateTick detour.
#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include "visual_proxy.hpp"

namespace pse::seam {
using pse::proxy::ProxyCommand;
using pse::proxy::ProxyHandle;
constexpr std::uint32_t Abi=0x58b00001u;
#pragma pack(push,8)
struct SeamTable {
    std::uint32_t abi,size;
    void (*tickEnter)(std::uint64_t tick);
    std::int32_t (*worldReady)(std::uint64_t tick);
    std::int32_t (*spawn)(const ProxyCommand*,std::uint64_t* handle);
    std::int32_t (*update)(std::uint64_t handle,const ProxyCommand*);
    std::int32_t (*remove)(std::uint64_t handle);
    std::int32_t (*retirement)(std::uint64_t handle);
    void (*tickLeave)(std::uint64_t tick,std::uint32_t flags);
    void (*worldExit)(std::uint32_t reason);
};
#pragma pack(pop)
static_assert(sizeof(SeamTable)==72 && offsetof(SeamTable,tickEnter)==8 && offsetof(SeamTable,worldExit)==64);
enum Status : std::uint32_t { None=0,Attached=1,Registering=2,Detached=3,Faulted=4,ExitDetached=5 };
// worldReady flags reported by the adapter; each is its own witness.
enum Ready : std::int32_t { StableWorld=1,LocalTicked=2,AdapterHealthy=4,AdapterRequestsRemove=8 };

// SEH boundary. A native fault surfacing through a callback becomes UNKNOWN (0) and the seam is never called again.
inline std::int32_t sehI(std::int32_t(*f)(std::uint64_t),std::uint64_t a,bool& fault) { __try { return f(a); } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true;return 0; } }
inline std::int32_t sehSpawn(std::int32_t(*f)(const ProxyCommand*,std::uint64_t*),const ProxyCommand* c,std::uint64_t* h,bool& fault) { __try { return f(c,h); } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true;return 0; } }
inline std::int32_t sehUpdate(std::int32_t(*f)(std::uint64_t,const ProxyCommand*),std::uint64_t h,const ProxyCommand* c,bool& fault) { __try { return f(h,c); } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true;return 0; } }
inline void sehTick(void(*f)(std::uint64_t),std::uint64_t t,bool& fault) { __try { f(t); } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true; } }
inline void sehLeave(void(*f)(std::uint64_t,std::uint32_t),std::uint64_t t,std::uint32_t v,bool& fault) { __try { f(t,v); } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true; } }
inline void sehExit(void(*f)(std::uint32_t),std::uint32_t v,bool& fault) { __try { f(v); } __except(EXCEPTION_EXECUTE_HANDLER) { fault=true; } }

class Seam final : public pse::proxy::INativeVisualProxySeam {
public:
    // Frida thread: one registration before Permit; published with release ordering.
    std::uint32_t registerTable(const SeamTable* t) {
        if(!t || t->abi!=Abi || t->size!=sizeof(SeamTable) || !t->tickEnter || !t->worldReady || !t->spawn || !t->update || !t->remove || !t->retirement || !t->tickLeave || !t->worldExit) return ERROR_INVALID_PARAMETER;
        std::uint32_t expected=None;
        if(!m_status.compare_exchange_strong(expected,Registering,std::memory_order_acq_rel)) return ERROR_ALREADY_INITIALIZED;
        m_table=*t;m_status.store(Attached,std::memory_order_release);
        return ERROR_SUCCESS;
    }
    bool attached() const { return m_status.load(std::memory_order_acquire)==Attached; }
    std::uint32_t status() const { return m_status.load(std::memory_order_acquire); }
    std::uint64_t faults() const { return m_faults; }
    // Simulation thread only.
    void detach(std::uint32_t s) { m_status.store(s,std::memory_order_release); }
    void tickEnter(std::uint64_t t) { if(attached()) { bool f{};sehTick(m_table.tickEnter,t,f);fault(f); } }
    std::int32_t worldReady(std::uint64_t t) { if(!attached()) return 0;bool f{};const auto r=sehI(reinterpret_cast<std::int32_t(*)(std::uint64_t)>(m_table.worldReady),t,f);fault(f);return f ? 0 : r; }
    void tickLeave(std::uint64_t t,std::uint32_t v) { if(attached()) { bool f{};sehLeave(m_table.tickLeave,t,v,f);fault(f); } }
    void worldExit(std::uint32_t reason) { if(attached()) { bool f{};sehExit(m_table.worldExit,reason,f);fault(f); } }
    // One Seam and one table serve every slot's state machine, which take turns on the simulation thread. The per-machine
    // m_inSeamCall flag cannot see a call made by ANOTHER machine, so the Seam keeps its own in-call latch: a state-machine call
    // made while another is in flight (or off the bound thread) makes no native call and returns the fail-closed answer.
    // Bind once (simulation thread id); 0 = unbound, no affinity check.
    void bindSimulationThread(DWORD tid) { m_simTid=tid; }
    std::uint64_t blocked() const { return m_blocked; }
    pse::proxy::SpawnOutcome spawn(const ProxyCommand& c,ProxyHandle& out) noexcept override {
        out=0;if(!attached()) return pse::proxy::SpawnOutcome::Deferred; // no adapter: no native call possible
        Call call(*this);if(!call.ok) return pse::proxy::SpawnOutcome::Deferred; // proven: no native call was made
        bool f{};std::uint64_t h{};const auto r=sehSpawn(m_table.spawn,&c,&h,f);fault(f);
        out=h;return f ? pse::proxy::SpawnOutcome::Unknown : static_cast<pse::proxy::SpawnOutcome>(r);
    }
    pse::proxy::UpdateOutcome update(ProxyHandle h,const ProxyCommand& c) noexcept override {
        if(!attached()) return pse::proxy::UpdateOutcome::Unknown;
        Call call(*this);if(!call.ok) return pse::proxy::UpdateOutcome::Refused; // no write
        bool f{};const auto r=sehUpdate(m_table.update,h,&c,f);fault(f);
        return f ? pse::proxy::UpdateOutcome::Unknown : static_cast<pse::proxy::UpdateOutcome>(r);
    }
    pse::proxy::RemoveOutcome remove(ProxyHandle h) noexcept override {
        if(!attached()) return pse::proxy::RemoveOutcome::Unknown;
        Call call(*this);if(!call.ok) return pse::proxy::RemoveOutcome::DefiniteNoCall;
        bool f{};const auto r=sehI(m_table.remove,h,f);fault(f);
        return f ? pse::proxy::RemoveOutcome::Unknown : static_cast<pse::proxy::RemoveOutcome>(r);
    }
    pse::proxy::RetirementStatus retirement(ProxyHandle h) noexcept override {
        if(!attached()) return pse::proxy::RetirementStatus::Unknown;
        Call call(*this);if(!call.ok) return pse::proxy::RetirementStatus::Pending; // read-only witness: ask again next step
        bool f{};const auto r=sehI(m_table.retirement,h,f);fault(f);
        return f ? pse::proxy::RetirementStatus::Unknown : static_cast<pse::proxy::RetirementStatus>(r);
    }
private:
    struct Call {
        Seam& s;bool ok;
        explicit Call(Seam& seam) noexcept : s(seam),ok(!seam.m_inCall && (!seam.m_simTid || GetCurrentThreadId()==seam.m_simTid)) { if(ok) s.m_inCall=true;else ++s.m_blocked; }
        ~Call() { if(ok) s.m_inCall=false; }
        Call(const Call&)=delete;Call& operator=(const Call&)=delete;
    };
    void fault(bool f) { if(f) { ++m_faults;m_status.store(Faulted,std::memory_order_release); } }
    SeamTable m_table{};
    std::atomic<std::uint32_t> m_status{None};
    std::uint64_t m_faults{},m_blocked{};
    DWORD m_simTid{};
    bool m_inCall{};
};
} // namespace pse::seam
