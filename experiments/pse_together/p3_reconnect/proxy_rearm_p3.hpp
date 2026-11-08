// P3 / S1.1: rearm of ONE visual proxy over a native retirement or ENGINE_RETIRED (successor of the DOC-58C idea).
// The accepted DOC-58B.2 automaton (visual_proxy.hpp, 32+18+21 contract checks) is reused unchanged: each native
// Actor lifetime is one VisualProxyStateMachine instance ("incarnation"). Latches (disconnect, teardown barrier,
// shutdown) are never cleared inside an instance. A NEW instance is created only when all of:
//  R1 the current instance is closed safely: ABSENT after its native retirement witness (Retired); latched and
//     never spawned (NeverSpawned); ENGINE_RETIRED by the accepted witness rule; or EngineRetiredQueueClear (R4);
//  R2 a session strictly newer than the closed one was configured after the closure (host generation greater);
//  R3 a native MapReady for exactly that session (epoch + client world serial) arrived after it was configured;
//     after a barrier closure the world serial must be greater than the world that was torn down;
//  R4 ORPHANED(RemoveInFlightAtBarrier) is closed only by ArenaDestroyed plus an adapter queue witness NotIssued or
//     IssuedDrained (the P3 adapter issues Remove immediately before the same live tick's Sweep/Drain).
// QUARANTINED, ORPHANED(QuarantinedAtBarrier), an undrained Remove, a refused/absent engine witness, a reused opaque
// handle or shutdown kill the slot for the process lifetime: fail-closed, never rearmed.
// No KEX access here; the caller owns every native witness. Offline contract: tests/proxy/p3_rearm_contract.cpp.
#pragma once
#include "visual_proxy.hpp"
#include <cstddef>
#include <cstdint>
#include <new>

namespace pse::p3 {
using pse::proxy::EngineTeardownWitness;
using pse::proxy::INativeVisualProxySeam;
using pse::proxy::OrphanCause;
using pse::proxy::ProxyHandle;
using pse::proxy::Rejection;
using pse::proxy::RouteResult;
using pse::proxy::TickAction;
using pse::proxy::TickContext;
using pse::proxy::VisualProxyState;
using pse::proxy::VisualProxyStateMachine;

// Precise native state of the Remove at the barrier, as attested by the P3 adapter.
enum class QueueWitness : std::int32_t { Unknown=0, NotIssued=1, IssuedDrained=2, IssuedQueued=3 };
inline const char* toString(QueueWitness q) noexcept {
    switch(q) {
        case QueueWitness::Unknown: return "Unknown";
        case QueueWitness::NotIssued: return "NotIssued";
        case QueueWitness::IssuedDrained: return "IssuedDrained";
        case QueueWitness::IssuedQueued: return "IssuedQueued";
    }
    return "Invalid";
}
enum class Closure : std::uint8_t { Open, Retired, NeverSpawned, EngineRetired, EngineRetiredQueueClear, DeadQuarantined, DeadQueueHazard, DeadNoWitness, DeadHandleReuse, DeadShutdown };
inline const char* toString(Closure c) noexcept {
    switch(c) {
        case Closure::Open: return "Open";
        case Closure::Retired: return "Retired";
        case Closure::NeverSpawned: return "NeverSpawned";
        case Closure::EngineRetired: return "EngineRetired";
        case Closure::EngineRetiredQueueClear: return "EngineRetiredQueueClear";
        case Closure::DeadQuarantined: return "DeadQuarantined";
        case Closure::DeadQueueHazard: return "DeadQueueHazard";
        case Closure::DeadNoWitness: return "DeadNoWitness";
        case Closure::DeadHandleReuse: return "DeadHandleReuse";
        case Closure::DeadShutdown: return "DeadShutdown";
    }
    return "Invalid";
}
inline bool safeClosure(Closure c) noexcept { return c==Closure::Retired || c==Closure::NeverSpawned || c==Closure::EngineRetired || c==Closure::EngineRetiredQueueClear; }
inline bool deadClosure(Closure c) noexcept { return c>=Closure::DeadQuarantined; }
enum class RearmRefusal : std::uint8_t { None, NotClosed, Dead, NoSession, SessionNotNewer, NoMapReady, WorldNotNewer, InSeamCall, Capacity, AllocationFailed };
inline const char* toString(RearmRefusal r) noexcept {
    switch(r) {
        case RearmRefusal::None: return "None";
        case RearmRefusal::NotClosed: return "NotClosed";
        case RearmRefusal::Dead: return "Dead";
        case RearmRefusal::NoSession: return "NoSession";
        case RearmRefusal::SessionNotNewer: return "SessionNotNewer";
        case RearmRefusal::NoMapReady: return "NoMapReady";
        case RearmRefusal::WorldNotNewer: return "WorldNotNewer";
        case RearmRefusal::InSeamCall: return "InSeamCall";
        case RearmRefusal::Capacity: return "Capacity";
        case RearmRefusal::AllocationFailed: return "AllocationFailed";
    }
    return "Invalid";
}

// Host-assigned connection generation, host map epoch and the client's own world serial (one per loaded level;
// the client DLL increments it after every native StateExit it observes).
struct SessionKey {
    std::uint32_t gen{},epoch{};
    std::uint64_t world{};
    bool valid() const noexcept { return gen && epoch && world; }
};
// Adapter verdict for the incarnation orphaned at a barrier, delivered after the original StateExit returned.
struct Verdict {
    ProxyHandle handle{};
    EngineTeardownWitness arena{EngineTeardownWitness::Unknown};
    QueueWitness queue{QueueWitness::Unknown};
};
struct IncarnationRecord {
    std::uint32_t incarnation{};
    SessionKey key{};
    ProxyHandle handle{};
    Closure closure{Closure::Open};
    std::uint32_t spawnCalls{},removeCalls{},retired{};
    std::uint64_t closedWorld{};
};

class RearmableProxy {
public:
    using ThreadIdFn=VisualProxyStateMachine::ThreadIdFn;
    static constexpr std::uint32_t MaxIncarnations=64;
    static constexpr std::size_t History=16;

    RearmableProxy(std::uint8_t slot,ThreadIdFn tid=&::GetCurrentThreadId) noexcept : m_slot(slot),m_tidFn(tid),m_cur(new (std::nothrow) VisualProxyStateMachine(slot,tid)) {}
    ~RearmableProxy() { delete m_cur; }
    RearmableProxy(const RearmableProxy&)=delete;
    RearmableProxy& operator=(const RearmableProxy&)=delete;

    bool bind(DWORD simTid) noexcept {
        if(m_simTid || !simTid || !m_cur) return false;
        m_simTid=simTid;return m_cur->bindSimulationThread(simTid);
    }
    // Gate 1. Forwarded only to an incarnation that has never called the factory and carries no latch; a spawned
    // incarnation of an older session is retired through the accepted adapter Remove intent, never adopted.
    void configureSession(SessionKey k) noexcept {
        if(!entry()) return;
        if(!k.valid()) return;
        m_pending=k;m_pendingAt=++m_serial;m_ready={};
        updateClosure();
        if(m_closure!=Closure::Open) { tryRearm();return; }
        if(forwardable()) { m_cur->configureSession(k.gen,k.epoch);m_curKey=k;m_bound=true;m_refusal=RearmRefusal::None;return; }
        if(m_cur->state()==VisualProxyState::ACTIVE && (k.gen!=m_curKey.gen || k.epoch!=m_curKey.epoch || k.world!=m_curKey.world)) { m_cur->requestRemove();++m_sessionRemoves; }
        evaluate();
    }
    // Gate 2: native map identity for the pending session on the given client world.
    void onNativeMapReady(std::uint32_t epoch,std::uint64_t token,std::uint64_t world) noexcept {
        if(!entry()) return;
        if(!epoch || !token || !world) return;
        m_ready={epoch,token,world,++m_serial,true};
        updateClosure();
        if(m_closure!=Closure::Open) { tryRearm();return; }
        if(forwardable() && m_bound && epoch==m_curKey.epoch && world==m_curKey.world && m_pending.gen==m_curKey.gen) m_cur->onNativeMapReady(epoch,token);
        evaluate();
    }
    Rejection handleSnapshot(const pse::BridgePlayerSnapshot& s,bool gameplay) noexcept {
        if(!entry()) return Rejection::AffinityViolation;
        updateClosure();
        if(m_closure!=Closure::Open) {
            tryRearm();
            if(m_closure!=Closure::Open) return deadClosure(m_closure) ? Rejection::Terminal : Rejection::SessionInactive;
        }
        return m_cur->handleSnapshot(s,gameplay);
    }
    TickAction tick(INativeVisualProxySeam* seam,TickContext ctx) noexcept {
        if(!entry()) return TickAction::AffinityViolation;
        updateClosure();
        if(m_closure!=Closure::Open) { tryRearm();if(m_closure!=Closure::Open) return TickAction::None; }
        const auto a=m_cur->tick(seam,ctx);
        if(a==TickAction::SpawnCreated) {
            const auto h=m_cur->handle();
            // Anti-ABA at the seam: an opaque handle is never reused by a later incarnation.
            if(!h || h<=m_lastHandle) { ++m_handleViolations;m_handleDead=true;m_cur->requestRemove(); }
            else { m_lastHandle=h;m_curHandle=h; }
        }
        evaluate();
        return a;
    }
    // Lifecycle intents are latched even while the automaton is inside a seam call (DOC-58B.2 D3; DOC-78 F1): the
    // automaton applies them when the seam call returns. Only the simulation-thread affinity is required here.
    // A lost session also drops the pending session and its MapReady (DOC-78 F5): a later rearm needs a session that
    // was configured, and received its MapReady, after the last loss.
    void onDisconnect() noexcept {
        if(!intentEntry()) return;
        m_disconnect=true;m_pending={};m_ready={};m_cur->onDisconnect();intentDone();
    }
    void requestShutdown() noexcept { if(!intentEntry()) return;m_shutdown=true;m_cur->requestShutdown();intentDone(); }
    void requestRemove() noexcept { if(!intentEntry()) return;m_cur->requestRemove();intentDone(); }
    // PRE_UNLOAD barrier for the world serial being torn down. No native call after this for this incarnation.
    void onPreUnload(std::uint64_t world) noexcept {
        if(!intentEntry()) return;
        if(!m_barrier) m_barrierWorld=world;
        m_barrier=true;m_cur->onPreUnload();intentDone();
    }
    // F4: the adapter owned nothing it could attest (no verdict armed) while the automaton is ORPHANED: the slot cannot be
    // proven free of the engine-owned Actor, so it is closed dead and visibly, never left Open.
    bool onTeardownMissing() noexcept {
        if(!entry()) return false;
        if(m_cur->state()!=VisualProxyState::ORPHANED || m_closure!=Closure::Open) return false;
        close(Closure::DeadNoWitness);evaluate();return true;
    }
    bool onTeardownVerdict(const Verdict& v) noexcept {
        if(!entry()) return false;
        if(m_cur->state()!=VisualProxyState::ORPHANED || !v.handle || v.handle!=m_cur->orphanedHandle() || m_closure!=Closure::Open) { ++m_verdictRefused;return false; }
        const auto cause=m_cur->orphanCause();
        bool accepted=false;
        if(cause==OrphanCause::LiveAtBarrier || cause==OrphanCause::RemovePendingNoCall) {
            if(v.queue==QueueWitness::IssuedQueued) close(Closure::DeadQueueHazard);
            else if(m_cur->onEngineTeardownWitness(v.handle,v.arena)) { close(Closure::EngineRetired);accepted=true; }
            else close(Closure::DeadNoWitness);
        } else if(cause==OrphanCause::RemoveInFlightAtBarrier) {
            if(v.arena==EngineTeardownWitness::ArenaDestroyed && (v.queue==QueueWitness::NotIssued || v.queue==QueueWitness::IssuedDrained)) { close(Closure::EngineRetiredQueueClear);accepted=true; }
            else close(v.queue==QueueWitness::IssuedQueued || v.queue==QueueWitness::Unknown ? Closure::DeadQueueHazard : Closure::DeadNoWitness);
        } else close(Closure::DeadQuarantined);
        evaluate();
        return accepted;
    }

    VisualProxyStateMachine* current() noexcept { return m_cur; }
    const VisualProxyStateMachine* current() const noexcept { return m_cur; }
    std::uint8_t targetSlot() const noexcept { return m_slot; }
    std::uint32_t incarnation() const noexcept { return m_incarnation; }
    Closure closure() const noexcept { return m_closure; }
    RearmRefusal lastRefusal() const noexcept { return m_refusal; }
    bool dead() const noexcept { return deadClosure(m_closure); }
    bool quiescent() const noexcept { return !m_cur || m_cur->quiescent(); }
    const SessionKey& currentKey() const noexcept { return m_curKey; }
    const SessionKey& pendingKey() const noexcept { return m_pending; }
    std::uint64_t barrierWorld() const noexcept { return m_barrierWorld; }
    std::uint64_t closedWorld() const noexcept { return m_closedWorld; }
    ProxyHandle lastHandle() const noexcept { return m_lastHandle; }
    std::uint32_t rearms() const noexcept { return m_rearms; }
    std::uint32_t handleViolations() const noexcept { return m_handleViolations; }
    std::uint32_t verdictRefused() const noexcept { return m_verdictRefused; }
    std::uint32_t reentrancyBlocked() const noexcept { return m_reentrancy; }
    std::uint32_t intentsDuringSeam() const noexcept { return m_intentsInSeam; }
    std::uint32_t affinityViolations() const noexcept { return m_affinity; }
    std::uint32_t sessionRemoves() const noexcept { return m_sessionRemoves; }
    std::uint32_t totalSpawnCalls() const noexcept { return m_pastSpawns+(m_cur ? m_cur->spawnCalls() : 0); }
    std::uint32_t totalRemoveCalls() const noexcept { return m_pastRemoves+(m_cur ? m_cur->removeCalls() : 0); }
    std::size_t historyCount() const noexcept { return m_historyCount; }
    const IncarnationRecord& history(std::size_t i) const noexcept { return m_history[i%History]; }

private:
    bool entry() noexcept {
        if(!m_cur) return false;
        if(!m_simTid || m_tidFn()!=m_simTid) { ++m_affinity;return false; }
        if(m_cur->inSeamCall()) { ++m_reentrancy;return false; }
        return true;
    }
    // Affinity only: used by the latch intents that must never be dropped.
    bool intentEntry() noexcept {
        if(!m_cur) return false;
        if(!m_simTid || m_tidFn()!=m_simTid) { ++m_affinity;return false; }
        return true;
    }
    // Inside a seam call the closure bookkeeping (and any rearm) waits for the next ordinary entry.
    void intentDone() noexcept { if(m_cur->inSeamCall()) { ++m_intentsInSeam;return; } evaluate(); }
    bool latched() const noexcept { return m_disconnect || m_barrier || m_shutdown; }
    bool forwardable() const noexcept { return !latched() && m_cur->spawnCalls()==0 && !m_cur->terminal(); }
    void close(Closure c) noexcept {
        if(m_closure!=Closure::Open) return;
        m_closure=c;m_closedKey=m_curKey;m_closedByBarrier=m_barrier;m_closedWorld=m_barrier ? m_barrierWorld : m_curKey.world;
        auto& r=m_history[m_historyCount++%History];
        r={m_incarnation,m_curKey,m_curHandle,c,m_cur->spawnCalls(),m_cur->removeCalls(),m_cur->retiredCount(),m_closedWorld};
    }
    void updateClosure() noexcept {
        if(m_closure!=Closure::Open) return;
        if(m_handleDead && m_cur->quiescent()) { close(Closure::DeadHandleReuse);return; }
        if(m_shutdown && m_cur->quiescent()) { close(Closure::DeadShutdown);return; }
        switch(m_cur->state()) {
            case VisualProxyState::ABSENT:
                if(m_cur->retiredCount()) close(Closure::Retired);
                else if(latched() && !m_cur->spawnCalls()) close(Closure::NeverSpawned);
                break;
            case VisualProxyState::QUARANTINED: close(Closure::DeadQuarantined);break;
            case VisualProxyState::ORPHANED: if(m_cur->orphanCause()==OrphanCause::QuarantinedAtBarrier) close(Closure::DeadQuarantined);break; // others await the adapter verdict
            case VisualProxyState::ENGINE_RETIRED: close(Closure::EngineRetired);break;
            default: break;
        }
    }
    // Closure bookkeeping after every entry; a closed incarnation is replaced as soon as R1-R3 hold.
    void evaluate() noexcept {
        updateClosure();
        if(m_closure!=Closure::Open) { tryRearm();return; }
        m_refusal=m_pending.valid() && m_pending.gen>m_curKey.gen ? RearmRefusal::NotClosed : RearmRefusal::None;
    }
    void tryRearm() noexcept {
        m_refusal=refusal();
        if(m_refusal!=RearmRefusal::None) return;
        auto* next=new (std::nothrow) VisualProxyStateMachine(m_slot,m_tidFn);
        if(!next) { m_refusal=RearmRefusal::AllocationFailed;return; }
        next->bindSimulationThread(m_simTid);
        next->configureSession(m_pending.gen,m_pending.epoch);
        next->onNativeMapReady(m_ready.epoch,m_ready.token);
        m_pastSpawns+=m_cur->spawnCalls();m_pastRemoves+=m_cur->removeCalls();
        delete m_cur;m_cur=next;++m_incarnation;++m_rearms;
        m_curKey=m_pending;m_bound=true;m_closure=Closure::Open;m_curHandle=0;
        m_disconnect=m_barrier=false;m_barrierWorld=0;
    }
    RearmRefusal refusal() const noexcept {
        if(deadClosure(m_closure) || m_shutdown) return RearmRefusal::Dead;
        if(!safeClosure(m_closure)) return RearmRefusal::NotClosed;
        if(m_cur->inSeamCall()) return RearmRefusal::InSeamCall;
        if(!m_pending.valid()) return RearmRefusal::NoSession;
        if(m_pending.gen<=m_closedKey.gen) return RearmRefusal::SessionNotNewer;
        // Any barrier seen by this incarnation (before or after its closure) pins rearm to a later world.
        if((m_closedByBarrier && m_pending.world<=m_closedWorld) || (m_barrier && m_pending.world<=m_barrierWorld)) return RearmRefusal::WorldNotNewer;
        if(!m_ready.valid || m_ready.at<=m_pendingAt || m_ready.epoch!=m_pending.epoch || m_ready.world!=m_pending.world) return RearmRefusal::NoMapReady;
        if(m_incarnation>=MaxIncarnations) return RearmRefusal::Capacity;
        return RearmRefusal::None;
    }
    struct Ready { std::uint32_t epoch{};std::uint64_t token{},world{},at{};bool valid{}; };
    std::uint8_t m_slot{};
    ThreadIdFn m_tidFn{};
    DWORD m_simTid{};
    VisualProxyStateMachine* m_cur{};
    std::uint32_t m_incarnation{1},m_rearms{},m_handleViolations{},m_verdictRefused{},m_reentrancy{},m_affinity{},m_sessionRemoves{},m_pastSpawns{},m_pastRemoves{};
    Closure m_closure{Closure::Open};
    RearmRefusal m_refusal{RearmRefusal::None};
    SessionKey m_curKey{},m_pending{},m_closedKey{};
    Ready m_ready{};
    std::uint64_t m_serial{},m_pendingAt{},m_barrierWorld{},m_closedWorld{};
    std::uint32_t m_intentsInSeam{};
    ProxyHandle m_lastHandle{},m_curHandle{};
    bool m_bound{},m_disconnect{},m_barrier{},m_shutdown{},m_closedByBarrier{},m_handleDead{};
    IncarnationRecord m_history[History]{};
    std::size_t m_historyCount{};
};

// F2 (DOC-78): an adapter request that the accepted P1 contract makes once per proxy lifetime (e.g. the 36000-step life
// cap) is once per INCARNATION in P3. take() is true at most once for each incarnation number.
class PerIncarnationOnce {
public:
    bool take(std::uint32_t incarnation) noexcept { if(!incarnation || incarnation==m_last) return false;m_last=incarnation;return true; }
private:
    std::uint32_t m_last{};
};

// Slot demultiplexer with the same contract as pse::proxy::SnapshotRouter (G9), holding rearmable slots.
// Capacity stays the DOC-58B bound (one native proxy); slots 0..7 remain addressable.
template<std::size_t Capacity=1>
class RearmRouter {
public:
    static_assert(Capacity>=1 && Capacity<=pse::MAX_PLAYERS-1);
    explicit RearmRouter(RearmableProxy::ThreadIdFn tid=&::GetCurrentThreadId) noexcept : m_tidFn(tid) {}
    ~RearmRouter() { for(auto*& p:m_slots) { delete p;p=nullptr; } }
    RearmRouter(const RearmRouter&)=delete;
    RearmRouter& operator=(const RearmRouter&)=delete;
    void bind(DWORD simTid) noexcept { if(!m_simTid) m_simTid=simTid; }
    void configureSession(std::uint8_t localSlot,SessionKey k) noexcept {
        m_localSlot=localSlot;m_hasLocal=localSlot<pse::MAX_PLAYERS;m_key=k;m_ready={};
        for(auto* p:m_slots) if(p) p->configureSession(k);
    }
    void onNativeMapReady(std::uint32_t epoch,std::uint64_t token,std::uint64_t world) noexcept {
        m_ready={epoch,token,world,true};
        for(auto* p:m_slots) if(p) p->onNativeMapReady(epoch,token,world);
    }
    RouteResult route(const pse::BridgePlayerSnapshot& s,bool gameplay,Rejection& rejection) noexcept {
        rejection=Rejection::None;
        if(!m_hasLocal) return RouteResult::NoLocalSlot;
        if(s.slotId>=pse::MAX_PLAYERS) return RouteResult::InvalidSlot;
        if(s.slotId==m_localSlot) return RouteResult::LocalExcluded;
        auto*& p=m_slots[s.slotId];
        if(!p) {
            std::size_t used=0;for(auto* q:m_slots) used+=q!=nullptr;
            if(used>=Capacity) return RouteResult::CapacityDeferred;
            p=new (std::nothrow) RearmableProxy(s.slotId,m_tidFn);
            if(!p || !p->current()) { delete p;p=nullptr;return RouteResult::CapacityDeferred; }
            p->bind(m_simTid);
            if(m_key.valid()) p->configureSession(m_key);
            if(m_ready.valid) p->onNativeMapReady(m_ready.epoch,m_ready.token,m_ready.world);
        }
        rejection=p->handleSnapshot(s,gameplay);
        return rejection==Rejection::None ? RouteResult::Routed : RouteResult::ProxyRejected;
    }
    // Current incarnations (same visitor shape as SnapshotRouter::forEach) and the rearmable slots.
    template<class F> void forEach(F&& f) noexcept { for(auto* p:m_slots) if(p && p->current()) f(*p->current()); }
    template<class F> void forEachSlot(F&& f) noexcept { for(auto* p:m_slots) if(p) f(*p); }
    bool quiescent() const noexcept { for(auto* p:m_slots) if(p && !p->quiescent()) return false;return true; }
    std::uint8_t localSlot() const noexcept { return m_localSlot; }
    VisualProxyStateMachine* proxy(std::uint8_t slot) noexcept { return slot<pse::MAX_PLAYERS && m_slots[slot] ? m_slots[slot]->current() : nullptr; }
    RearmableProxy* slot(std::uint8_t s) noexcept { return s<pse::MAX_PLAYERS ? m_slots[s] : nullptr; }
    void onPreUnload(std::uint64_t world) noexcept { for(auto* p:m_slots) if(p) p->onPreUnload(world); }
    // A lost session also forgets the router's own key and MapReady, so a slot created later is never configured with them.
    void onDisconnect() noexcept { m_key={};m_ready={};for(auto* p:m_slots) if(p) p->onDisconnect(); }
    // F4: no adapter verdict exists for a slot orphaned at the barrier.
    bool onTeardownMissing() noexcept { bool any=false;for(auto* p:m_slots) if(p) any|=p->onTeardownMissing();return any; }
    void requestShutdown() noexcept { for(auto* p:m_slots) if(p) p->requestShutdown(); }
    void requestRemove() noexcept { for(auto* p:m_slots) if(p) p->requestRemove(); }
    bool onTeardownVerdict(const Verdict& v) noexcept {
        for(auto* p:m_slots) if(p && p->current() && p->current()->state()==VisualProxyState::ORPHANED && p->current()->orphanedHandle()==v.handle) return p->onTeardownVerdict(v);
        return false;
    }
private:
    struct Ready { std::uint32_t epoch{};std::uint64_t token{},world{};bool valid{}; };
    RearmableProxy::ThreadIdFn m_tidFn;
    DWORD m_simTid{};
    std::uint8_t m_localSlot{0xff};bool m_hasLocal{};
    SessionKey m_key{};Ready m_ready{};
    RearmableProxy* m_slots[pse::MAX_PLAYERS]{};
};

} // namespace pse::p3
