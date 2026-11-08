// S0.3 unified header (namespace pse::proxy): the DOC58B.2 superset of the DOC58B automaton. The DOC58B (32) and
// DOC58B.1 (18) contract tests run unchanged against it as merge regression; DOC58B.2 (21) covers the additions.
// DOC58B integration copy of Gemini DOC58D.1 VisualProxyStateMachine (original left unchanged in
// tools/network-visual-proxy-readiness/doc58d1-contract-audit). Patched contracts, see
// docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/CODE_REVIEW.json:
//  D1 Remove is not synchronous in KEX (DOC58A: Remove -> later Drain/OnRemove/PoolReturn), so a returned
//     Remove only enters RETIRING; ABSENT requires a native retirement witness.
//  D2 Spawn exception / null / unknown result -> QUARANTINED (ownership undetermined), never a retry.
//  D3 Unload/shutdown/disconnect intent is latched, never dropped because a seam call is in flight.
//  D4 Teardown with a live/unknown Actor -> ORPHANED terminal; never ABSENT and never a native Remove.
//  D5 Session identity, native map readiness and sector readiness are separate gates.
//  D6 Snapshots never call the seam; the latest accepted target is applied once per safe simulation step.
//  D7 Update results are honoured: refusals do not become the current transform; faults stop updates.
//  D8 The seam receives an opaque handle, never a KEX address.
// DOC58B.2 isolated lifecycle patch (copy; the accepted DOC58B header is unchanged):
//  D9  Engine-owned unload. A live Actor at the teardown barrier is owned by KEX StateExit: the factory
//      retags it into tagged heap af4490, which StateExit purges after Level::Unload (per-block destructor
//      callback, then free); its ObjectNode block lives in heap af4600, purged next. The proxy issues no
//      native call; ORPHANED may become ENGINE_RETIRED only from an attested native witness for the same
//      opaque handle. ENGINE_RETIRED is terminal (no rearm here; reconnect is a later milestone).
//  D10 A Remove already issued but not yet witnessed as retired at the barrier (RETIRING) may still sit in
//      the native removal queue (globals af3380/af3388, drained only by a live-world StateTick, never reset
//      by StateExit). That cell keeps a reference to a node freed by the af4600 purge. It is never witnessed
//      away: the proxy stays ORPHANED with nativeQueueHazard()==true (fail-closed).
#pragma once
#include <cstdint>
#include <cmath>
#include <cstddef>
#include <new>
#include <windows.h>
#include "net_protocol.h"
#include "thread_bridge_dto.h"
#include "multi_slot_snapshot_guard.hpp"

namespace pse::proxy {

using ProxyHandle=std::uint64_t; // Opaque adapter token. Not an address; 0 = none.

// Callback ABI shared with the Frida adapter: every result 0 means UNKNOWN (a JS exception
// inside a NativeCallback returns 0), so 0 is always the fail-closed outcome.
enum class SpawnOutcome : std::int32_t { Unknown=0, Created=1, Deferred=2, Refused=3 };
enum class UpdateOutcome : std::int32_t { Unknown=0, Applied=1, Unchanged=2, Refused=3, Stopped=4 };
enum class RemoveOutcome : std::int32_t { Unknown=0, Requested=1, DefiniteNoCall=2 };
enum class RetirementStatus : std::int32_t { Unknown=0, Retired=1, Pending=2, Failed=3 };

#pragma pack(push,4)
struct ProxyCommand {
    std::uint32_t abi{0x58b00001u};
    std::uint32_t slot{},sequence{},serverTick{},mapEpoch{},connectionGeneration{},entityId{},entityGeneration{};
    float x{},y{},z{},yaw{},pitch{};
    std::uint32_t coalesced{};
};
#pragma pack(pop)
static_assert(sizeof(ProxyCommand)==56 && offsetof(ProxyCommand,x)==32 && offsetof(ProxyCommand,coalesced)==52);

class INativeVisualProxySeam {
public:
    virtual ~INativeVisualProxySeam()=default;
    virtual SpawnOutcome spawn(const ProxyCommand& c,ProxyHandle& out) noexcept=0;
    virtual UpdateOutcome update(ProxyHandle h,const ProxyCommand& c) noexcept=0;
    virtual RemoveOutcome remove(ProxyHandle h) noexcept=0;
    // Read-only witness query; never mutates KEX.
    virtual RetirementStatus retirement(ProxyHandle h) noexcept=0;
};

enum class VisualProxyState : std::uint8_t { ABSENT, SPAWN_PENDING, ACTIVE, REMOVE_PENDING, RETIRING, QUARANTINED, ORPHANED, ENGINE_RETIRED };
inline const char* toString(VisualProxyState s) noexcept {
    switch(s) {
        case VisualProxyState::ABSENT: return "ABSENT";
        case VisualProxyState::SPAWN_PENDING: return "SPAWN_PENDING";
        case VisualProxyState::ACTIVE: return "ACTIVE";
        case VisualProxyState::REMOVE_PENDING: return "REMOVE_PENDING";
        case VisualProxyState::RETIRING: return "RETIRING";
        case VisualProxyState::QUARANTINED: return "QUARANTINED";
        case VisualProxyState::ORPHANED: return "ORPHANED";
        case VisualProxyState::ENGINE_RETIRED: return "ENGINE_RETIRED";
    }
    return "UNKNOWN";
}
// Why an Actor was orphaned at the barrier: decides whether an engine-teardown witness may end it.
enum class OrphanCause : std::uint8_t { None, LiveAtBarrier, RemovePendingNoCall, RemoveInFlightAtBarrier, QuarantinedAtBarrier };
inline const char* toString(OrphanCause c) noexcept {
    switch(c) {
        case OrphanCause::None: return "None";
        case OrphanCause::LiveAtBarrier: return "LiveAtBarrier";
        case OrphanCause::RemovePendingNoCall: return "RemovePendingNoCall";
        case OrphanCause::RemoveInFlightAtBarrier: return "RemoveInFlightAtBarrier";
        case OrphanCause::QuarantinedAtBarrier: return "QuarantinedAtBarrier";
    }
    return "Unknown";
}
// Adapter/observer verdict for one opaque handle after native StateExit. 0 = UNKNOWN (fail-closed).
// ArenaDestroyed requires, for the identity recorded at Spawn: the af4490 purge destructor dispatch of that
// Actor block, the free of that block, and the af4600 purge free of the block holding its ObjectNode,
// all after the barrier and with no proxy native call after the barrier.
enum class EngineTeardownWitness : std::int32_t { Unknown=0, ArenaDestroyed=1, NotObserved=2 };
enum class QuarantineReason : std::uint8_t { None, SpawnUnknown, UpdateUnknown, RemoveUnknown, RemoveRefused, RetirementFailed, AdapterStoppedRemoveRefused };
inline const char* toString(QuarantineReason r) noexcept {
    switch(r) {
        case QuarantineReason::None: return "None";
        case QuarantineReason::SpawnUnknown: return "SpawnUnknown";
        case QuarantineReason::UpdateUnknown: return "UpdateUnknown";
        case QuarantineReason::RemoveUnknown: return "RemoveUnknown";
        case QuarantineReason::RemoveRefused: return "RemoveRefused";
        case QuarantineReason::RetirementFailed: return "RetirementFailed";
        case QuarantineReason::AdapterStoppedRemoveRefused: return "AdapterStoppedRemoveRefused";
    }
    return "Unknown";
}
enum class Rejection : std::uint8_t { None, AffinityViolation, ReentrancyBlocked, ShutdownLatched, TeardownBarrier, SessionInactive, WrongSlot, NotGameplay,
    NotReady, IdentityMismatch, StaleSequence, NonfiniteTransform, EntityMismatch, Removing, Terminal, SpawnBudgetExhausted };
inline const char* toString(Rejection r) noexcept {
    switch(r) {
        case Rejection::None: return "None";
        case Rejection::AffinityViolation: return "AffinityViolation";
        case Rejection::ReentrancyBlocked: return "ReentrancyBlocked";
        case Rejection::ShutdownLatched: return "ShutdownLatched";
        case Rejection::TeardownBarrier: return "TeardownBarrier";
        case Rejection::SessionInactive: return "SessionInactive";
        case Rejection::WrongSlot: return "WrongSlot";
        case Rejection::NotGameplay: return "NotGameplay";
        case Rejection::NotReady: return "NotReady";
        case Rejection::IdentityMismatch: return "IdentityMismatch";
        case Rejection::StaleSequence: return "StaleSequence";
        case Rejection::NonfiniteTransform: return "NonfiniteTransform";
        case Rejection::EntityMismatch: return "EntityMismatch";
        case Rejection::Removing: return "Removing";
        case Rejection::Terminal: return "Terminal";
        case Rejection::SpawnBudgetExhausted: return "SpawnBudgetExhausted";
    }
    return "Unknown";
}
// What one safe-phase tick did. At most one mutating seam call per tick.
enum class TickAction : std::uint8_t { None, NotSafe, AffinityViolation, ReentrancyBlocked, SpawnCreated, SpawnDeferred, SpawnRefused, SpawnUnknown,
    UpdateApplied, UpdateUnchanged, UpdateRefused, UpdateStopped, UpdateUnknown, RemoveRequested, RemoveRefused, RemoveUnknown,
    RetirementPending, Retired, RetirementFailed };
inline const char* toString(TickAction a) noexcept {
    switch(a) {
        case TickAction::None: return "None";
        case TickAction::NotSafe: return "NotSafe";
        case TickAction::AffinityViolation: return "AffinityViolation";
        case TickAction::ReentrancyBlocked: return "ReentrancyBlocked";
        case TickAction::SpawnCreated: return "SpawnCreated";
        case TickAction::SpawnDeferred: return "SpawnDeferred";
        case TickAction::SpawnRefused: return "SpawnRefused";
        case TickAction::SpawnUnknown: return "SpawnUnknown";
        case TickAction::UpdateApplied: return "UpdateApplied";
        case TickAction::UpdateUnchanged: return "UpdateUnchanged";
        case TickAction::UpdateRefused: return "UpdateRefused";
        case TickAction::UpdateStopped: return "UpdateStopped";
        case TickAction::UpdateUnknown: return "UpdateUnknown";
        case TickAction::RemoveRequested: return "RemoveRequested";
        case TickAction::RemoveRefused: return "RemoveRefused";
        case TickAction::RemoveUnknown: return "RemoveUnknown";
        case TickAction::RetirementPending: return "RetirementPending";
        case TickAction::Retired: return "Retired";
        case TickAction::RetirementFailed: return "RetirementFailed";
    }
    return "Unknown";
}
// Caller-attested safe phase: StateTick.onLeave equivalent after a real local P1 tick, stable
// world, native sector arrays validated by the adapter. Each is evaluated by its own owner.
struct TickContext { bool simulationStep{}; bool sectorsReady{}; };

class VisualProxyStateMachine {
public:
    using ThreadIdFn=DWORD(*)();
    explicit VisualProxyStateMachine(std::uint8_t targetSlot,ThreadIdFn tid=&::GetCurrentThreadId) noexcept : m_targetSlot(targetSlot),m_tid(tid) {}
    VisualProxyStateMachine(const VisualProxyStateMachine&)=delete;
    VisualProxyStateMachine& operator=(const VisualProxyStateMachine&)=delete;

    // Binds once to the attested KEX simulation thread; rebinding is refused (Gemini allowed any re-register).
    bool bindSimulationThread(DWORD tid) noexcept { if(m_simTid || !tid) return false;m_simTid=tid;return true; }
    bool onSimulationThread() const noexcept { return m_simTid && m_tid()==m_simTid; }

    // Gate 1: network session identity only. Never enables Spawn, never lowers the teardown barrier.
    bool configureSession(std::uint32_t connectionGeneration,std::uint32_t mapEpoch) noexcept {
        if(!affine()) return false;
        if(m_inSeamCall) { ++m_reentrancyBlocked;return false; }
        if(!connectionGeneration || !mapEpoch) return false;
        const bool changed=m_sessionBound && (connectionGeneration!=m_gen || mapEpoch!=m_epoch);
        m_gen=connectionGeneration;m_epoch=mapEpoch;m_sessionBound=true;m_mapReady=false;m_mapToken=0;
        m_guard.reset(connectionGeneration,mapEpoch);m_spawnBudgetUsed=false;
        if(m_state==VisualProxyState::SPAWN_PENDING) m_state=VisualProxyState::ABSENT;
        // An Actor owned by an older session is removed, never adopted.
        if(changed && m_state==VisualProxyState::ACTIVE) m_state=VisualProxyState::REMOVE_PENDING;
        return true;
    }
    // Gate 2: native map identity confirmed for the CURRENT epoch (client map gate), independent of Welcome.
    bool onNativeMapReady(std::uint32_t mapEpoch,std::uint64_t identityToken) noexcept {
        if(!affine()) return false;
        if(m_inSeamCall) { ++m_reentrancyBlocked;return false; }
        if(!m_sessionBound || mapEpoch!=m_epoch || !identityToken || m_teardownBarrier) return false;
        m_mapReady=true;m_mapToken=identityToken;return true;
    }

    // Network intake on the simulation thread. Stores the latest accepted target; never calls the seam.
    Rejection handleSnapshot(const pse::BridgePlayerSnapshot& s,bool gameplay) noexcept {
        if(!affine()) return Rejection::AffinityViolation;
        if(m_inSeamCall) { ++m_reentrancyBlocked;return Rejection::ReentrancyBlocked; }
        if(m_shutdownLatched || m_removeRequested) return Rejection::ShutdownLatched;
        if(m_teardownBarrier) return Rejection::TeardownBarrier;
        if(terminal()) return Rejection::Terminal;
        if(!m_sessionBound || !m_sessionActive()) return Rejection::SessionInactive;
        if(s.slotId!=m_targetSlot) return Rejection::WrongSlot;
        if(!gameplay) return Rejection::NotGameplay;
        switch(m_guard.accept(s,m_mapReady && gameplay)) {
            case snapshot55::Verdict::Accept: break;
            case snapshot55::Verdict::NotReady: return Rejection::NotReady;
            case snapshot55::Verdict::Identity: return Rejection::IdentityMismatch;
            case snapshot55::Verdict::Stale: return Rejection::StaleSequence;
            case snapshot55::Verdict::Nonfinite: return Rejection::NonfiniteTransform;
            case snapshot55::Verdict::EntityMismatch:
                if(m_state==VisualProxyState::ACTIVE) m_state=VisualProxyState::REMOVE_PENDING;
                return Rejection::EntityMismatch;
        }
        ProxyCommand c;c.slot=s.slotId;c.sequence=s.sequence;c.serverTick=s.serverTick;c.mapEpoch=s.mapEpoch;c.connectionGeneration=s.connectionGeneration;
        c.entityId=s.entityId;c.entityGeneration=s.entityGeneration;c.x=s.posX;c.y=s.posY;c.z=s.posZ;c.yaw=s.yaw;c.pitch=s.pitch;
        c.coalesced=m_dirty ? m_target.coalesced+1 : 0;
        if(m_state==VisualProxyState::ABSENT) {
            if(m_spawnBudgetUsed) return Rejection::SpawnBudgetExhausted;
            m_state=VisualProxyState::SPAWN_PENDING;m_entityId=s.entityId;m_entityGen=s.entityGeneration;
        }
        if(m_state!=VisualProxyState::SPAWN_PENDING && m_state!=VisualProxyState::ACTIVE) return Rejection::Removing;
        m_target=c;m_dirty=true;++m_accepted;return Rejection::None;
    }

    // Safe-phase executor: at most one mutating native call per simulation step.
    TickAction tick(INativeVisualProxySeam* seam,TickContext ctx) noexcept {
        if(!affine()) return TickAction::AffinityViolation;
        if(m_inSeamCall) { ++m_reentrancyBlocked;return TickAction::ReentrancyBlocked; }
        if(m_teardownBarrier || terminal() || m_state==VisualProxyState::ABSENT) return TickAction::None;
        if(!seam || !ctx.simulationStep || !ctx.sectorsReady) return TickAction::NotSafe;
        switch(m_state) {
            case VisualProxyState::SPAWN_PENDING: return doSpawn(*seam);
            case VisualProxyState::ACTIVE: return m_dirty ? doUpdate(*seam) : TickAction::None;
            case VisualProxyState::REMOVE_PENDING: return doRemove(*seam);
            case VisualProxyState::RETIRING: return doRetirement(*seam);
            default: return TickAction::None;
        }
    }

    // Lifecycle intents are latched even during a seam call (Gemini returned early and lost them).
    void onDisconnect() noexcept { if(!affine()) return;m_disconnected=true;applyLatches(); }
    void requestShutdown() noexcept { if(!affine()) return;m_shutdownLatched=true;applyLatches(); }
    // Adapter-initiated safe cleanup (DOC58A fault policy): stop updates, then exactly one native Remove.
    void requestRemove() noexcept { if(!affine()) return;m_removeRequested=true;applyLatches(); }
    // PRE_UNLOAD barrier: no native call after this point, ever, for this instance (G1 still OPEN natively).
    void onPreUnload() noexcept { if(!affine()) return;if(m_inSeamCall) ++m_unloadDuringSeam;m_teardownBarrier=true;m_mapReady=false;applyLatches(); }
    void onPostUnload() noexcept { onPreUnload(); }
    // DOC58B.2 D9: accepts exactly one attested engine-teardown witness for the orphaned handle. Never calls the seam.
    bool onEngineTeardownWitness(ProxyHandle h,EngineTeardownWitness w) noexcept {
        if(!affine()) return false;
        if(m_inSeamCall) { ++m_reentrancyBlocked;return false; }
        if(!m_teardownBarrier || m_state!=VisualProxyState::ORPHANED || !h || h!=m_orphanedHandle) { ++m_witnessRefused;return false; }
        if(m_orphanCause!=OrphanCause::LiveAtBarrier && m_orphanCause!=OrphanCause::RemovePendingNoCall) { ++m_witnessRefused;return false; }
        if(w!=EngineTeardownWitness::ArenaDestroyed) { ++m_witnessRefused;return false; }
        m_state=VisualProxyState::ENGINE_RETIRED;++m_engineRetired;++m_localGen;return true;
    }

    VisualProxyState state() const noexcept { return m_state; }
    QuarantineReason quarantineReason() const noexcept { return m_quarantine; }
    ProxyHandle handle() const noexcept { return m_handle; }
    ProxyHandle orphanedHandle() const noexcept { return m_orphanedHandle; } // diagnostic only, never passed to the seam
    const ProxyCommand& target() const noexcept { return m_target; }
    const ProxyCommand& applied() const noexcept { return m_applied; }
    bool dirty() const noexcept { return m_dirty; }
    bool teardownBarrier() const noexcept { return m_teardownBarrier; }
    bool shutdownLatched() const noexcept { return m_shutdownLatched; }
    bool mapReady() const noexcept { return m_mapReady; }
    bool sessionBound() const noexcept { return m_sessionBound; }
    bool inSeamCall() const noexcept { return m_inSeamCall; }
    bool terminal() const noexcept { return m_state==VisualProxyState::QUARANTINED || m_state==VisualProxyState::ORPHANED || m_state==VisualProxyState::ENGINE_RETIRED; }
    OrphanCause orphanCause() const noexcept { return m_orphanCause; }
    // True when a native Remove may still be queued while its node was freed by the StateExit purge.
    bool nativeQueueHazard() const noexcept { return m_orphanCause==OrphanCause::RemoveInFlightAtBarrier; }
    std::uint32_t engineRetiredCount() const noexcept { return m_engineRetired; }
    std::uint32_t witnessRefused() const noexcept { return m_witnessRefused; }
    // Shutdown can continue past this proxy only when no native call can still be owed.
    bool quiescent() const noexcept { return m_state==VisualProxyState::ABSENT || terminal(); }
    std::uint8_t targetSlot() const noexcept { return m_targetSlot; }
    std::uint32_t spawnCalls() const noexcept { return m_spawnCalls; }
    std::uint32_t removeCalls() const noexcept { return m_removeCalls; }
    std::uint32_t updateCalls() const noexcept { return m_updateCalls; }
    std::uint32_t retiredCount() const noexcept { return m_retired; }
    std::uint64_t accepted() const noexcept { return m_accepted; }
    std::uint32_t affinityViolations() const noexcept { return m_affinityViolations; }
    std::uint32_t reentrancyBlocked() const noexcept { return m_reentrancyBlocked; }
    std::uint32_t unloadDuringSeam() const noexcept { return m_unloadDuringSeam; }
    std::uint32_t localGeneration() const noexcept { return m_localGen; }
    std::uint32_t entityId() const noexcept { return m_entityId; }
    std::uint32_t entityGeneration() const noexcept { return m_entityGen; }

private:
    bool affine() noexcept { if(onSimulationThread()) return true;++m_affinityViolations;return false; }
    bool m_sessionActive() const noexcept { return !m_disconnected; }
    struct SeamScope { bool& f; explicit SeamScope(bool& v) noexcept : f(v) { f=true; } ~SeamScope() { f=false; } };
    void orphan() noexcept {
        m_orphanCause=m_state==VisualProxyState::ACTIVE ? OrphanCause::LiveAtBarrier
            : m_state==VisualProxyState::REMOVE_PENDING && !m_removeIssued ? OrphanCause::RemovePendingNoCall
            : m_state==VisualProxyState::RETIRING ? OrphanCause::RemoveInFlightAtBarrier : OrphanCause::QuarantinedAtBarrier;
        m_orphanedHandle=m_handle;m_handle=0;m_dirty=false;++m_localGen;m_state=VisualProxyState::ORPHANED; }
    void quarantine(QuarantineReason r) noexcept { m_quarantine=r;m_dirty=false;m_state=VisualProxyState::QUARANTINED; }
    // Applied after each seam return and on every latch call: intent can never be lost.
    void applyLatches() noexcept {
        if(m_inSeamCall) return;
        if(m_teardownBarrier) {
            if(m_state==VisualProxyState::SPAWN_PENDING) { m_state=VisualProxyState::ABSENT;m_dirty=false; }
            else if(m_handle && m_state!=VisualProxyState::ABSENT) orphan(); // live, removing, retiring or quarantined Actor
            return;
        }
        if(m_shutdownLatched || m_disconnected || m_removeRequested) {
            if(m_state==VisualProxyState::SPAWN_PENDING) { m_state=VisualProxyState::ABSENT;m_dirty=false; }
            else if(m_state==VisualProxyState::ACTIVE) { m_state=VisualProxyState::REMOVE_PENDING;m_dirty=false; }
        }
    }
    TickAction doSpawn(INativeVisualProxySeam& seam) noexcept {
        if(!m_sessionBound || !m_mapReady || m_disconnected || m_shutdownLatched || m_spawnBudgetUsed) return TickAction::NotSafe;
        ProxyHandle h{};SpawnOutcome r;
        { SeamScope g(m_inSeamCall);r=seam.spawn(m_target,h); }
        if(r==SpawnOutcome::Deferred || r==SpawnOutcome::Refused) { applyLatches();return r==SpawnOutcome::Deferred ? TickAction::SpawnDeferred : TickAction::SpawnRefused; }
        ++m_spawnCalls;m_spawnBudgetUsed=true;
        if(r!=SpawnOutcome::Created || !h) { m_handle=h;quarantine(QuarantineReason::SpawnUnknown);applyLatches();return TickAction::SpawnUnknown; }
        m_handle=h;m_removeIssued=false;++m_localGen;m_applied=m_target;m_dirty=false;m_state=VisualProxyState::ACTIVE;applyLatches();return TickAction::SpawnCreated;
    }
    TickAction doUpdate(INativeVisualProxySeam& seam) noexcept {
        const ProxyCommand c=m_target;UpdateOutcome r;++m_updateCalls;
        { SeamScope g(m_inSeamCall);r=seam.update(m_handle,c); }
        m_dirty=false;TickAction a;
        switch(r) {
            case UpdateOutcome::Applied: m_applied=c;a=TickAction::UpdateApplied;break;
            case UpdateOutcome::Unchanged: m_applied=c;a=TickAction::UpdateUnchanged;break;
            case UpdateOutcome::Refused: a=TickAction::UpdateRefused;break; // no write; applied transform stays the last real one
            case UpdateOutcome::Stopped: m_state=VisualProxyState::REMOVE_PENDING;a=TickAction::UpdateStopped;break; // adapter fault -> only Remove
            default: m_state=VisualProxyState::REMOVE_PENDING;a=TickAction::UpdateUnknown;break; // Remove re-validates identity itself
        }
        applyLatches();return a;
    }
    TickAction doRemove(INativeVisualProxySeam& seam) noexcept {
        if(m_removeIssued) { quarantine(QuarantineReason::RemoveUnknown);return TickAction::RemoveUnknown; } // never a second native Remove for one handle
        RemoveOutcome r;++m_removeCalls;m_removeIssued=true;
        { SeamScope g(m_inSeamCall);r=seam.remove(m_handle); }
        if(r==RemoveOutcome::Requested) { m_state=VisualProxyState::RETIRING;applyLatches();return TickAction::RemoveRequested; }
        quarantine(r==RemoveOutcome::DefiniteNoCall ? QuarantineReason::RemoveRefused : QuarantineReason::RemoveUnknown);applyLatches();
        return r==RemoveOutcome::DefiniteNoCall ? TickAction::RemoveRefused : TickAction::RemoveUnknown;
    }
    TickAction doRetirement(INativeVisualProxySeam& seam) noexcept {
        RetirementStatus r;
        { SeamScope g(m_inSeamCall);r=seam.retirement(m_handle); }
        if(r==RetirementStatus::Pending) { applyLatches();return TickAction::RetirementPending; }
        if(r==RetirementStatus::Retired) { m_handle=0;++m_localGen;++m_retired;m_state=VisualProxyState::ABSENT;applyLatches();return TickAction::Retired; }
        quarantine(QuarantineReason::RetirementFailed);applyLatches();return TickAction::RetirementFailed;
    }

    VisualProxyState m_state{VisualProxyState::ABSENT};
    QuarantineReason m_quarantine{QuarantineReason::None};
    OrphanCause m_orphanCause{OrphanCause::None};
    std::uint32_t m_engineRetired{},m_witnessRefused{};
    std::uint8_t m_targetSlot{};
    ThreadIdFn m_tid{};
    DWORD m_simTid{};
    bool m_sessionBound{},m_mapReady{},m_disconnected{},m_shutdownLatched{},m_teardownBarrier{},m_inSeamCall{},m_dirty{},m_spawnBudgetUsed{},m_removeIssued{},m_removeRequested{};
    std::uint32_t m_gen{},m_epoch{},m_entityId{},m_entityGen{},m_localGen{};
    std::uint64_t m_mapToken{},m_accepted{};
    ProxyHandle m_handle{},m_orphanedHandle{};
    ProxyCommand m_target{},m_applied{};
    pse::snapshot::MultiSlotGuard8 m_guard{};
    std::uint32_t m_spawnCalls{},m_removeCalls{},m_updateCalls{},m_retired{},m_affinityViolations{},m_reentrancyBlocked{},m_unloadDuringSeam{};
};

// Slot demultiplexer (G9). Local slot is excluded before any proxy sees the record; one native
// proxy is the DOC58B capacity bound even though slots 0..7 are addressable.
enum class RouteResult : std::uint8_t { Routed, LocalExcluded, InvalidSlot, CapacityDeferred, ProxyRejected, NoLocalSlot };
inline const char* toString(RouteResult r) noexcept {
    switch(r) {
        case RouteResult::Routed: return "Routed";
        case RouteResult::LocalExcluded: return "LocalExcluded";
        case RouteResult::InvalidSlot: return "InvalidSlot";
        case RouteResult::CapacityDeferred: return "CapacityDeferred";
        case RouteResult::ProxyRejected: return "ProxyRejected";
        case RouteResult::NoLocalSlot: return "NoLocalSlot";
    }
    return "Unknown";
}
template<std::size_t Capacity=1>
class SnapshotRouter {
public:
    static_assert(Capacity>=1 && Capacity<=pse::MAX_PLAYERS-1);
    explicit SnapshotRouter(VisualProxyStateMachine::ThreadIdFn tid=&::GetCurrentThreadId) noexcept : m_tidFn(tid) {}
    ~SnapshotRouter() { for(auto*& p:m_proxies) { delete p;p=nullptr; } }
    SnapshotRouter(const SnapshotRouter&)=delete;
    SnapshotRouter& operator=(const SnapshotRouter&)=delete;
    void bind(DWORD simTid) noexcept { m_simTid=simTid; }
    // New session: local slot from Welcome. A slot id reused by a new session is a new identity:
    // every existing proxy is told the new session (old Actors are removed, not adopted).
    void configureSession(std::uint8_t localSlot,std::uint32_t gen,std::uint32_t epoch) noexcept {
        m_localSlot=localSlot;m_hasLocal=localSlot<pse::MAX_PLAYERS;
        for(auto* p:m_proxies) if(p) p->configureSession(gen,epoch);
        m_gen=gen;m_epoch=epoch;
    }
    void onNativeMapReady(std::uint32_t epoch,std::uint64_t token) noexcept { m_mapEpoch=epoch;m_mapToken=token;for(auto* p:m_proxies) if(p) p->onNativeMapReady(epoch,token); }
    RouteResult route(const pse::BridgePlayerSnapshot& s,bool gameplay,Rejection& rejection) noexcept {
        rejection=Rejection::None;
        if(!m_hasLocal) return RouteResult::NoLocalSlot;
        if(s.slotId>=pse::MAX_PLAYERS) return RouteResult::InvalidSlot;
        if(s.slotId==m_localSlot) return RouteResult::LocalExcluded;
        auto*& p=m_proxies[s.slotId];
        if(!p) {
            std::size_t used=0;for(auto* q:m_proxies) used+=q!=nullptr;
            if(used>=Capacity) return RouteResult::CapacityDeferred;
            p=new (std::nothrow) VisualProxyStateMachine(s.slotId,m_tidFn);
            if(!p) return RouteResult::CapacityDeferred;
            p->bindSimulationThread(m_simTid);
            if(m_gen) p->configureSession(m_gen,m_epoch);
            if(m_mapToken) p->onNativeMapReady(m_mapEpoch,m_mapToken);
        }
        rejection=p->handleSnapshot(s,gameplay);
        return rejection==Rejection::None ? RouteResult::Routed : RouteResult::ProxyRejected;
    }
    template<class F> void forEach(F&& f) noexcept { for(auto* p:m_proxies) if(p) f(*p); }
    bool quiescent() const noexcept { for(auto* p:m_proxies) if(p && !p->quiescent()) return false;return true; }
    std::uint8_t localSlot() const noexcept { return m_localSlot; }
    VisualProxyStateMachine* proxy(std::uint8_t slot) noexcept { return slot<pse::MAX_PLAYERS ? m_proxies[slot] : nullptr; }
private:
    VisualProxyStateMachine::ThreadIdFn m_tidFn;
    DWORD m_simTid{};
    std::uint8_t m_localSlot{0xff};bool m_hasLocal{};
    std::uint32_t m_gen{},m_epoch{},m_mapEpoch{};std::uint64_t m_mapToken{};
    VisualProxyStateMachine* m_proxies[pse::MAX_PLAYERS]{};
};

} // namespace pse::proxy
