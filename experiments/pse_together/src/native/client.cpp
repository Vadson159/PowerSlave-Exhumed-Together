// Unified client: P4 background, P7 self state, P16 respawn, P23 UI, proxies of every OTHER player (slots 0..7 minus its own).
// S0.3 unified source: the former lab copies src/client.cpp (DOC-51..56, no proxy) and src/client58b.cpp (DOC-58B)
// were one file plus a diff; they are now this file selected by flags (PSE_FEATURE_SNAPSHOT / _MAPREADY / _VISUAL_PROXY).
// PSE_FEATURE_VISUAL_PROXY routes accepted remote snapshots to VisualProxyStateMachine and drives the registered
// native C++ adapter from this simulation-thread StateTick detour. Frida supplies read-only observation only.
#include "portable_policy.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include "ui_native.hpp"
#include "ui/shared_p23.hpp"
#include <atomic>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <share.h>
#include <pse_together/config.hpp>
#include "build_profile.hpp"
#include "world_sync.hpp"
#include "log_throttle.hpp"
#include "async_journal.hpp"
#include "self_native_p16.hpp"
#include "lifecycle_p16.hpp"
#include "respawn_p16.hpp"
#include "client_slots.hpp"
#include "self_state_p7.hpp"
#ifdef PSE_FEATURE_P4
#include "background_policy.hpp"
#include "background_profile.hpp"
#include <intrin.h>
#endif
#ifndef PSE_ROLE_CLIENT
#error "client.cpp is the client DLL: build it with PSE_ROLE_CLIENT"
#endif
#include "enet_worker.h"
#ifdef PSE_FEATURE_SNAPSHOT
#include "snapshot_contract.hpp"
#endif
#ifdef PSE_FEATURE_MAPREADY
#include "map_reader.hpp"
#ifdef PSE_FEATURE_P4
#include "native_map111.hpp"
#endif
#endif
#ifdef PSE_FEATURE_VISUAL_PROXY
#include "seam.hpp"
#include "native_proxy.hpp"
#endif

namespace {
using Byte=unsigned char;
using Unary=void(__fastcall*)(void*);
using Final=void(__fastcall*)();
using Clock=std::chrono::steady_clock;
constexpr wchar_t ClientExe[]=LR"(S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\experiments\m1\native_second_instance\game\anubis_Shipping_Steam_x64.exe)";
#ifdef PSE_FEATURE_SNAPSHOT
constexpr auto& Domain=snapshot55::Domain;
#else
constexpr char Domain[]="M12A_INPUT_ONLY";
#endif
std::uint64_t steadyNs() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()); }
struct Session;
void reportExit();
struct Context {
    HMODULE module{}; Byte* base{}; DWORD tid{};
    FILE* journal{}; SRWLOCK logLock=SRWLOCK_INIT; char buffer[65536]{};
    std::atomic<unsigned> prepared{},permit{};
    void* original[profile::Count]{}; bool enabled[profile::Count]{};
    bool inTick{},stopped{},shutdown{},failed{},attested{};
    unsigned localThisTick{}; std::uint64_t ticks{},localUpdates{},ordinal{},logSeq{},lastFlush{},foreign{},reconnectKey{},worldSerial{1}; unsigned identity{},identityResets{};bool freshIdentity{}; // see the REJECTED handler
    std::uint32_t buttons{},cmdButtons{},forwardHeld{},backHeld{}; bool sampled{};
    float yaw{},pitch{},px{},py{},pz{}; int sector{-1}; // this client's own view/pose, sampled with the buttons (v1.14 input)
    Session* session{};
#ifdef PSE_FEATURE_P4
    void* focusOriginal{};background::FocusLease focusLease;
    std::atomic<unsigned> backgroundPermit{};
    std::uint64_t focusSerial{},backgroundTicks{};
    bool focusKnown{},focusLost{},neutral{},backgroundPeriod{},resetPending{},uiCaptureWas{};
#endif
} g;
template<class T> T read(const void* p,std::size_t off=0) { return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+off); }
Byte* primary() { return g.base+0xaf33e0; }
Byte* instance() { return g.base+0xaf33a0; }
void* at(profile::Index i) { return g.base+profile::Sites[i].rva; }
bool thread() { return GetCurrentThreadId()==g.tid; }
bool gameplay() { return read<int>(instance(),0x24)==3 && read<int>(instance(),0x28)==0; }
pse::journal::Throttle logThrottle; // guarded by g.logLock
pse::journal::AsyncWriter journalWriter; // the file is written by its own thread (async_journal.hpp): the game's thread never waits for the disk
void journalSink(const char* data,std::size_t size,bool) { if(size) std::fwrite(data,1,size,g.journal);std::fflush(g.journal); }
void log(const char* event,const char* fmt="",...) {
    if(!g.journal) return;
    {   AcquireSRWLockExclusive(&g.logLock);const bool write=logThrottle.allow(event,GetTickCount64());ReleaseSRWLockExclusive(&g.logLock);if(!write) return; }
    char detail[2200]{};va_list args;va_start(args,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args);va_end(args);
    const auto ns=steadyNs();SYSTEMTIME utc{};GetSystemTime(&utc);
    AcquireSRWLockExclusive(&g.logLock);
    char line[2600]{};
    const int n=_snprintf_s(line,sizeof(line),_TRUNCATE,"{\"event\":\"%s\",\"record\":%llu,\"pid\":%lu,\"tid\":%lu,\"steady_ns\":%llu,\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\"%s%s}\n",event,++g.logSeq,GetCurrentProcessId(),GetCurrentThreadId(),ns,utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds,detail[0]?",":"",detail);
    if(n>0) journalWriter.append(line,static_cast<std::size_t>(n));
    (void)g.lastFlush;
    ReleaseSRWLockExclusive(&g.logLock);
}
char lastBlock[64]{};unsigned blockRecoveries{};
void block(const char* reason) { if(!g.failed) strncpy_s(lastBlock,reason,_TRUNCATE);g.failed=true;log("blocked","\"reason\":\"%s\",\"retry\":false",reason); }
// See host.cpp processFault(): hooks, threads, exact profiles never recover; a world-level fault ends co-op only until this world is left.
bool processFault(const char* r) {
    for(const char* k:{"install","hook","minhook","affinity","prefix","profile","restore","bootstrap","init","seam","detach"}) if(std::strstr(r,k)) return true;
    return false;
}
bool prefix(profile::Index i) { return std::equal(std::begin(profile::Sites[i].prefix),std::end(profile::Sites[i].prefix),static_cast<Byte*>(at(i))); }
void flushJournal() { journalWriter.flush(true); }
#include "client_self_p16.inc"
#include "client_respawn_p16.inc"
// The host's session moved to another world (MapChange with a new epoch, or a welcome): the host builds every body afresh (generation 1)
// in that world, so every per-binding latch of the respawn / self-state handshakes starts over. No native call.
void bindingReset16() {
    input16.invalidate();requestPending16=newLocal16=protect16=false;stagedConfirm16.reset();
    p7Authority=ss7::Authority{};p7Admitted=false;pse::self_native_p7::invalidate();
    inputBound16=false;hostDead16=false;hostDeath16=0;localGeneration16=1;
    slots16.reset();
}
#ifdef PSE_FEATURE_VISUAL_PROXY
pse::seam::Seam seam58;
void nativeProxyJournal(const char* event,const char* detail) { log(event,"%s",detail); }
std::atomic<unsigned> proxyShutdown58{},detach58{},status58{};
bool detachDeferredLogged58{};std::uint64_t faultsLogged58{};
std::uint64_t mapToken58(const map56::Identity& id,std::uint32_t epoch) {
    std::uint64_t h=1469598103934665603ULL;auto mix=[&](std::uint64_t v) { for(int i=0;i<8;++i) { h^=(v>>(i*8))&0xff;h*=1099511628211ULL; } };
    for(char ch:id.name) mix(static_cast<unsigned char>(ch));mix(id.countA);mix(id.countB);mix(id.version);mix(epoch);return h ? h : 1;
}
#endif

// Native overworld map object (global 0xa45e20, vtable 0x14052c330): the location the map screen is "at" is a 16-bit field (+0x10); the exit trigger
// sets it (setter = vtable slot 0x30 = 0x1d5920, `mov [rcx+0x10],dx`) to the next location BEFORE the game asks state 2, and the map screen opens its cursor
// (and its "Will you travel to X?" box) on it. A player who is only LED to the map screen must get the same value, or his map opens on a stale location.
short overworldCurrent() { return read<short>(g.base+0xa45e20,0x10); }
bool setOverworldCurrent(int id) {
    static constexpr Byte setter[5]{0x66,0x89,0x51,0x10,0xc3};
    if(id<0||id>99||!std::equal(std::begin(setter),std::end(setter),g.base+0x1d5920))return false;
    reinterpret_cast<void(__fastcall*)(void*,short)>(g.base+0x1d5920)(g.base+0xa45e20,static_cast<short>(id));return true;
}
// The game's own "Travel" answer of the exit box "Will you travel to X?" (0x1b0e00 -> GameInstance vtable+0xe0 = 0x1b1a50): takes the map name of the
// overworld location, asks state 2 with the 1 s fade, marks the current level AND that location unlocked (GameInstance+0x10: bool per location, what
// the map screen's arrows and cursor use) and autosaves. The leader's game did all of it; a follower that only set the location and asked state 2
// kept its map progress behind: no arrow back to the previous level, the next level locked.
bool travelToOverworld(int location) {
    static constexpr Byte travel[28]{0x48,0x8b,0x05,0x99,0x25,0x94,0x00,0x48,0x8d,0x0d,0x92,0x25,0x94,0x00,0xc6,0x05,0x7f,0xb5,0x93,0x00,0x00,0x48,0xff,0xa0,0xe0,0x00,0x00,0x00};
    static constexpr Byte enter[16]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x05,0xbf,0x43,0x89};
    if(!std::equal(std::begin(travel),std::end(travel),g.base+0x1b0e00) || !std::equal(std::begin(enter),std::end(enter),g.base+0x1b1a50) ||
       read<Byte*>(instance())!=g.base+0x52ad08 || read<Byte*>(g.base+0x52ad08,0xe0)!=g.base+0x1b1a50 || !setOverworldCurrent(location)) return false;
    reinterpret_cast<void(__fastcall*)()>(g.base+0x1b0e00)();
    return read<int>(instance(),0x28)==2;
}
// Only the actual KEX simulation thread owns both SIMULATION bridge operations.
// Worker uses DTOs only and independently schedules the latest sample at 60 Hz.
struct Session {
    pse::ThreadBridge bridge;
    pse::ENetWorker worker{bridge,pse::ENetWorkerMode::CLIENT};
    bool started{},stopped{},active{},readySent{},baseline{},reconnectPending{};
    std::uint16_t worldKeysPending{};std::uint32_t worldOnTicks{},lastEnemySequence{},achPending{};unsigned useHold{};std::vector<pse::world::ScriptCall> scriptPending;std::vector<int> breakPending;std::vector<std::pair<unsigned,std::uint32_t>> soundPending; // (slot, sound) of other players to play at their proxy
    std::vector<int> gonePending,pickupPending; // map enemies gone at the host, map spheres taken there
    int difficultyPending{-1}; // the host's difficulty (EvDifficulty), adopted when the world is ready
    std::vector<std::pair<int,int>> switchPending; // (face, event) of switches pressed at the host: their texture here
    std::array<std::uint16_t,8> hostAmmo{};std::uint8_t hostOwned{};bool hostLoadoutValid{}; // P2's real ammo / owned weapons (host) -> this client's Player
    int hostHealth{};bool hostHealthValid{},vitalsSynced{}; // Milestone 3: the host decides this player's health (P2 takes real AI damage there)
    std::uint8_t slot{};std::uint32_t gen{},epoch{};
    std::uint64_t lastReconnectNs{};
    std::array<std::uint32_t,pse::MAX_PLAYERS> genWaitLogged16{}; // log throttle for p16_snapshot_generation_wait, per slot
    std::string followMap; // Milestone 4: the host announced the next map; hookTick calls the native ChangeMap with it
    std::uint64_t exitTick{};bool followedByUs{},exitSent{}; // followedByUs: the map change in progress is the one WE started on the host's order; exitSent: our own exit was reported
    // One session for the whole game: when the host's binding is new (welcome, MapChange), the next stable gameplay world of this client is
    // compared with the host's level once; another level -> follow it. Not continuously: while the host itself is between worlds (this client
    // walked into the exit first and is already in the next level), the old binding must not pull it back.
    bool followCheck{};
    std::uint32_t bindings{};
#ifdef PSE_FEATURE_SNAPSHOT
    snapshot55::Guard snapshots;
#endif
#ifdef PSE_FEATURE_MAPREADY
    map56::ClientGate mapGate;
    map56::Identity localMap;
    map56::Fault lastFault{map56::Fault::None};
    map56::ClientGate::State lastState{map56::ClientGate::State::Disconnected};
    // Local native observation each simulation tick; terminal gate states fail closed.
    void observeMap() {
        map56::Identity id;const auto fault=map56::derive(map56::observe(g.base),id);
        mapGate.observe(fault,id);
        if(fault==map56::Fault::None) localMap=id; // the current world (MapReady and the proxy map token describe it)
        if(followCheck && fault==map56::Fault::None && active && !g.failed) {
            followCheck=false;
            if(id.name!=mapGate.hostName() && followMap.empty() && pse::validNativeMapName(mapGate.hostName())) {
                followMap=mapGate.hostName();
                log("client_follow_host_map","\"host\":\"%s\",\"local\":\"%s\",\"epoch\":%u",mapGate.hostName().c_str(),id.name.c_str(),mapGate.mapEpoch());
            }
        }
        const auto state=mapGate.state();
        if(fault!=lastFault || state!=lastState || g.ticks%600==0)
            log("client_map_status","\"tick\":%llu,\"fault\":\"%s\",\"local\":\"%s\",\"host\":\"%s\",\"state\":\"%s\",\"stable_ticks\":%u,\"epoch\":%u,\"generation\":%u,\"count_a\":%llu,\"count_b\":%llu",g.ticks,map56::text(fault),id.name.c_str(),mapGate.hostName().c_str(),map56::ClientGate::text(state),mapGate.stableTicks(),mapGate.mapEpoch(),mapGate.connection(),id.countA,id.countB);
        if(state!=lastState && state==map56::ClientGate::State::Mismatch) block("native_map_mismatch_no_ready");
        if(state!=lastState && state==map56::ClientGate::State::Invalidated) {
            reportExit(); // our own exit (ChangeMap/ending) is what invalidated the map: tell the host before the session flags are reset
            readySent=baseline=false;p7Authority.barrier();p7Admitted=false;pse::self_native_p7::invalidate();p7LifetimeClosed=true;
            log("p16_world_barrier","\"reason\":\"native map invalidated\",\"further_gameplay_writes\":0");
#ifdef PSE_FEATURE_VISUAL_PROXY
            barrier58("native_map_invalidated");
#endif
        }
        lastFault=fault;lastState=state;
    }
#endif
#ifdef PSE_FEATURE_VISUAL_PROXY
    pse::proxy::SnapshotRouter<pse::MAX_PLAYERS-1> proxies; // every other slot (up to 7) is a passive proxy actor
    std::uint64_t routeCounts[6]{};bool lastSafe{},shutdownApplied{},adapterRemoveApplied{};std::uint32_t retireMask58{},removeAppliedMask58{};std::uint64_t retireRefused58{};std::uint64_t deferredLogged{},notSafeLogged{};
    void logProxies(const char* why) {
        bool any=false;
        proxies.forEach([&](pse::proxy::VisualProxyStateMachine& p) { any=true;
            log("proxy_state","\"why\":\"%s\",\"slot\":%u,\"state\":\"%s\",\"quarantine\":\"%s\",\"handle\":%llu,\"orphaned_handle\":%llu,\"accepted\":%llu,\"spawn_calls\":%u,\"update_calls\":%u,\"remove_calls\":%u,\"retired\":%u,\"affinity_violations\":%u,\"reentrancy_blocked\":%u,\"unload_during_seam\":%u,\"barrier\":%s,\"shutdown\":%s,\"map_ready\":%s,\"local_generation\":%u,\"entity\":%u,\"entity_generation\":%u",
                why,p.targetSlot(),toString(p.state()),toString(p.quarantineReason()),p.handle(),p.orphanedHandle(),p.accepted(),p.spawnCalls(),p.updateCalls(),p.removeCalls(),p.retiredCount(),p.affinityViolations(),p.reentrancyBlocked(),p.unloadDuringSeam(),p.teardownBarrier()?"true":"false",p.shutdownLatched()?"true":"false",p.mapReady()?"true":"false",p.localGeneration(),p.entityId(),p.entityGeneration()); });
        if(!any) log("proxy_state","\"why\":\"%s\",\"proxies\":0",why);
        log("proxy_route_counts","\"why\":\"%s\",\"routed\":%llu,\"local_excluded\":%llu,\"invalid_slot\":%llu,\"capacity_deferred\":%llu,\"proxy_rejected\":%llu,\"no_local_slot\":%llu",why,routeCounts[0],routeCounts[1],routeCounts[2],routeCounts[3],routeCounts[4],routeCounts[5]);
    }
    void barrier58(const char* why) { proxies.forEach([](pse::proxy::VisualProxyStateMachine& p) { p.onPreUnload(); });log("proxy_teardown_barrier","\"reason\":\"%s\",\"native_calls_after\":\"none_permitted\"",why);logProxies(why); }
    // One remote player's proxy must go (it stopped sending, started a new life, or the session was rejoined): tell it the player is
    // gone, then phase58() waits until it owes no native call, rearms that adapter slot and deletes the machine, so that the next
    // snapshot of the slot spawns a fresh body. Every other slot's proxy keeps running.
    void retireProxy58(std::uint8_t remoteSlot,const char* why) {
        auto* p=proxies.proxy(remoteSlot);if(!p) return;
        p->onDisconnect();retireMask58|=1u<<remoteSlot;removeAppliedMask58&=~(1u<<remoteSlot);
        log("proxy_slot_retire","\"slot\":%u,\"why\":\"%s\",\"state\":\"%s\"",remoteSlot,why,toString(p->state()));
    }
    void sweepRetired58() {
        for(unsigned i=0;i<pse::MAX_PLAYERS;++i) {
            const auto bit=1u<<i;if(!(retireMask58&bit)) continue;
            const auto id=static_cast<std::uint8_t>(i);
            if(!proxies.proxy(id)) { retireMask58&=~bit;continue; }
            if(!proxies.slotQuiescent(id)) continue;
            if(pse::native_proxy::rearmSlot(i) && proxies.resetSlot(id)) { retireMask58&=~bit;log("proxy_slot_reset","\"slot\":%u",i); }
            else if(++retireRefused58%120==1) log("proxy_slot_reset_refused","\"slot\":%u,\"attempts\":%llu",i,static_cast<unsigned long long>(retireRefused58));
        }
    }
    void disconnect58(const char* why) { proxies.forEach([](pse::proxy::VisualProxyStateMachine& p) { p.onDisconnect(); });log("proxy_disconnect","\"reason\":\"%s\"",why);logProxies(why); }
    void route58(const pse::BridgePlayerSnapshot& s) {
        pse::proxy::Rejection rj{};const auto rr=proxies.route(s,gameplay(),rj);++routeCounts[static_cast<unsigned>(rr)];
#ifdef PSE_FEATURE_P8
        if(rr==pse::proxy::RouteResult::Routed) pse::native_proxy::stageAnimation(s);
#endif
        auto* p=proxies.proxy(s.slotId);
        log("proxy_snapshot_routed","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"route\":\"%s\",\"rejection\":\"%s\",\"proxy_state\":\"%s\",\"coalesced\":%u,\"pos_bits\":[%u,%u,%u],\"yaw_bits\":%u,\"local_this_tick\":%u,\"tick\":%llu,\"kex_actor_access\":false",
            s.slotId,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,toString(rr),toString(rj),p ? toString(p->state()) : "none",
            p && rr==pse::proxy::RouteResult::Routed ? p->target().coalesced : 0u,snapshot55::bits(s.posX),snapshot55::bits(s.posY),snapshot55::bits(s.posZ),snapshot55::bits(s.yaw),g.localThisTick,g.ticks);
    }
    // StateTick.onLeave-equivalent phase on the attested simulation thread, after consume().
    void phase58(std::int32_t ready) {
        using namespace pse::proxy;
        if(proxyShutdown58.load(std::memory_order_acquire) && !shutdownApplied) {
            shutdownApplied=true;proxies.forEach([](VisualProxyStateMachine& p) { p.requestShutdown(); });log("proxy_shutdown_requested","\"source\":\"PSE58B_RequestProxyShutdown\"");logProxies("shutdown_request");
        }
        if(ready&pse::seam::AdapterRequestsRemove) {
            // The adapter asks per slot (removeRequestMask); a flag without any bit still means "every proxy" once.
            const auto asked=pse::native_proxy::removeRequestMask();bool any=false;
            proxies.forEach([&](VisualProxyStateMachine& p) {
                const auto bit=1u<<p.targetSlot();
                if(((asked&bit)||(!asked&&!adapterRemoveApplied)) && !(removeAppliedMask58&bit)) { removeAppliedMask58|=bit;p.requestRemove();any=true; }
            });
            if(!asked) adapterRemoveApplied=true;
            if(any) { log("proxy_adapter_remove_requested","\"ready_flags\":%d,\"slots\":%u",ready,asked);logProxies("adapter_remove_request"); }
        }
        const bool step=gameplay() && g.localThisTick==1 && !g.failed && (ready&pse::seam::LocalTicked) && (ready&pse::seam::AdapterHealthy);
        const TickContext ctx{step,(ready&pse::seam::StableWorld)!=0};
        if(step!=lastSafe) { lastSafe=step;log("proxy_safe_phase","\"safe\":%s,\"ready_flags\":%d,\"local_this_tick\":%u,\"gameplay\":%s,\"tick\":%llu",step?"true":"false",ready,g.localThisTick,gameplay()?"true":"false",g.ticks); }
        proxies.forEach([&](VisualProxyStateMachine& p) {
            const auto before=p.state();const ProxyCommand t=p.target();
            const auto a=p.tick(&seam58,ctx);
            if(a==TickAction::None || (a==TickAction::NotSafe && ++notSafeLogged%600!=1) || (a==TickAction::SpawnDeferred && ++deferredLogged%120!=1)) return;
            log("proxy_tick_action","\"slot\":%u,\"action\":\"%s\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"quarantine\":\"%s\",\"handle\":%llu,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"coalesced\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pos_bits\":[%u,%u,%u],\"yaw_bits\":%u,\"tick\":%llu,\"safe\":%s,\"sectors_ready\":%s,\"spawn_calls\":%u,\"update_calls\":%u,\"remove_calls\":%u,\"retired\":%u,\"local_generation\":%u",
                p.targetSlot(),toString(a),toString(before),toString(p.state()),toString(p.quarantineReason()),p.handle() ? p.handle() : p.orphanedHandle(),t.sequence,t.serverTick,t.mapEpoch,t.connectionGeneration,t.entityId,t.entityGeneration,t.coalesced,t.x,t.y,t.z,t.yaw,
                snapshot55::bits(t.x),snapshot55::bits(t.y),snapshot55::bits(t.z),snapshot55::bits(t.yaw),g.ticks,ctx.simulationStep?"true":"false",ctx.sectorsReady?"true":"false",p.spawnCalls(),p.updateCalls(),p.removeCalls(),p.retiredCount(),p.localGeneration());
        });
        sweepRetired58();
        unsigned st=0;proxies.forEach([&](VisualProxyStateMachine& p) { if(!st) st=static_cast<unsigned>(p.state())+1; }); // first (lowest slot) proxy
        status58.store((proxies.quiescent() ? 0x100u : 0u)|(st<<16),std::memory_order_release);
    }
#endif
    // ---- Milestone 2: doors / lifts / shared keys (world_sync.hpp). Host actions arrive as world events and are replayed
    // natively; this client's own mover spawns and key pickups are reported to the host as interaction requests.
    Byte* worldPuppet() const {
        Byte* n=read<Byte*>(primary(),0xd8);Byte* a=n?read<Byte*>(n,0x28):nullptr;
        return a && read<Byte*>(a,0x10)==n && read<void*>(a,0x1b8)==primary() && read<Byte*>(a)==g.base+0x52d198 ? a : nullptr;
    }
    bool worldReady() const {
        return active && readySent && baseline && !stopped && !g.failed && gameplay() && mapGate.snapshotEligible(gen,epoch);
    }
    bool sendWorld(std::uint8_t type,std::uint32_t target,std::uint32_t generation) {
        pse::BridgeInteractionRequest r;r.mapEpoch=epoch;r.slotId=slot;r.interactionType=type;r.targetEntityId=target;r.targetGeneration=generation;
        return pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{r}));
    }
    // This client's own world ended (worldTransition) while the session stays: proxies of the old world are gone with it (barrier58 +
    // native_proxy::rearm), their machines are replaced; MapReady is due again in the next world.
    void worldLeft() {
        // An order received before or while leaving that world is done by leaving it (or was our own exit's echo): the next world
        // follows the host's level through followCheck when the host's binding changes.
        if(!followMap.empty()) log("client_order_superseded","\"order\":\"%s\"",followMap.c_str());
        followMap.clear();
        readySent=baseline=false;exitSent=false;followedByUs=false;exitTick=0;
        hostHealthValid=vitalsSynced=hostLoadoutValid=false;
        mapGate.newWorld();
#ifdef PSE_FEATURE_VISUAL_PROXY
        for(unsigned i=0;i<pse::MAX_PLAYERS;++i) proxies.resetSlot(static_cast<std::uint8_t>(i));
        retireMask58=removeAppliedMask58=0;adapterRemoveApplied=false;
        proxies.configureSession(slot,gen,epoch);
#endif
        log("client_world_left","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"host\":\"%s\",\"follow\":\"%s\"",slot,gen,epoch,mapGate.hostName().c_str(),followMap.c_str());
    }
    // A new binding to the host's world (first welcome, or the host moved on inside this session): world events of the old one are void,
    // the host builds this player's body afresh (generation 1), every other player's proxy is replaced by its next snapshot.
    void newBindingState() {
        ++bindings;
        worldKeysPending=0;scriptPending.clear();breakPending.clear();soundPending.clear();gonePending.clear();pickupPending.clear();switchPending.clear();
        hostHealthValid=vitalsSynced=hostLoadoutValid=false;
        bindingReset16();
#ifdef PSE_FEATURE_VISUAL_PROXY
        proxies.configureSession(slot,gen,epoch);
        for(unsigned i=0;i<pse::MAX_PLAYERS;++i) retireProxy58(static_cast<std::uint8_t>(i),"host_world_changed");
#endif
    }
    void worldEvent(const pse::BridgeWorldEvent& e) {
        namespace w=pse::world;
        if(e.mapEpoch!=epoch) return;
        if(e.eventType==w::EvKeys) { worldKeysPending|=static_cast<std::uint16_t>(e.eventValue&0xf);return; } // applied when the world is ready
        if(e.eventType==w::EvSound && e.sourceEntityId<8 && e.sourceEntityId!=slot && e.eventValue>=0 && soundPending.size()<64) { soundPending.push_back({e.sourceEntityId,static_cast<std::uint32_t>(e.eventValue)});return; }
        if(e.eventType==w::EvEnemyGone && e.eventValue>=0 && gonePending.size()<512) { gonePending.push_back(e.eventValue);return; }
        if(e.eventType==w::EvPickup && e.eventValue>=0 && pickupPending.size()<512) { pickupPending.push_back(e.eventValue);return; }
        if(e.eventType==w::EvDifficulty && e.eventValue>=0 && e.eventValue<8) { difficultyPending=e.eventValue;return; }
        if(e.eventType==w::EvSwitch && e.eventValue>=0 && switchPending.size()<256) { switchPending.push_back({e.eventValue,static_cast<int>(e.targetEntityId)});return; }
        if(e.eventType==w::EvBreak && e.eventValue>=0 && breakPending.size()<64) { breakPending.push_back(e.eventValue);return; } // another player broke a vase/urn
        if(e.eventType==w::EvAchievement && e.eventValue>=0 && e.eventValue<20) { achPending|=1u<<e.eventValue;return; } // another player earned it
        if(e.eventType==w::EvScript && e.eventValue>0 && e.eventValue<256 && scriptPending.size()<16) { scriptPending.push_back(w::unpackScript(e.eventValue,e.sourceEntityId,static_cast<std::uint16_t>(e.targetEntityId)));return; } // another player crossed a story trigger
        if(e.eventType==w::EvMover && worldReady())
            w::applyMover(w::moverFromWire(static_cast<std::uint32_t>(e.eventValue),e.targetEntityId));
    }
    void worldSync() {
        namespace w=pse::world;
        const bool on=worldReady();
        w::setActive(on);
        if(!on) { worldOnTicks=0;return; }
        if(worldOnTicks==199) w::debugTeleport(worldPuppet());
        if(++worldOnTicks==300) w::debugSpawn();
        if(worldOnTicks==450) w::debugKeys(primary());
        if(worldOnTicks%10==0) { w::debugCensusOnFile();w::debugHit(worldPuppet());w::debugSelfDamage(worldPuppet()); }
        w::cullLocalProjectiles(); // the client's AI is a replica: enemy projectiles come from the host only
        w::cullLocalDynamic();     // ... and so are enemies and spheres spawned during play (vases, deaths): this machine's own random ones go
        // Own player: local damage is muted, the host's health is applied (red flash through the native Damage call).
        {
            Byte* me=worldPuppet();w::setOwnPlayer(me);
            // A native respawn comes back with 200 even where the cap is 100: down to the cap (never below the host's value) without the damage flash.
            if(me && hostHealthValid && !requestPending16 && !newLocal16 && !stagedConfirm16) w::clampHealth(me,primary(),hostHealth);
            if(me && hostHealthValid && !requestPending16 && !newLocal16 && !stagedConfirm16) { w::applyHostHealth(me,hostHealth,!vitalsSynced);vitalsSynced=true; }
            if(me && hostLoadoutValid && !requestPending16 && !newLocal16 && !stagedConfirm16) {
                // Ammo and owned weapons are the host's (P2 picks things up and fires there); the weapon in hand stays the client's choice.
                Byte* pl=primary();unsigned wrote=0;
                for(unsigned i=0;i<8;++i) {
                    if(read<std::uint16_t>(pl,0xbc+2*i)!=hostAmmo[i]) {*reinterpret_cast<std::uint16_t*>(pl+0xbc+2*i)=hostAmmo[i];++wrote;}
                    const Byte own=(hostOwned>>i)&1u;if(read<Byte>(pl,0xcc+i)!=own) {*(pl+0xcc+i)=own;++wrote;}
                }
                static unsigned logged=0;if(wrote && logged++<20) log("client_loadout_applied","\"fields\":%u,\"owned\":%u,\"ammo\":[%u,%u,%u,%u,%u,%u,%u,%u],\"in_hand\":%d",wrote,hostOwned,hostAmmo[0],hostAmmo[1],hostAmmo[2],hostAmmo[3],hostAmmo[4],hostAmmo[5],hostAmmo[6],hostAmmo[7],read<int>(pl,0x88));
                const int inHand=read<int>(pl,0x88),pending=read<int>(pl,0x8c);
                if(inHand>=0&&inHand<8&&!((hostOwned>>inHand)&1u)&&pending==inHand) *reinterpret_cast<int*>(pl+0x8c)=0; // not owned on the host: back to the machete
            }
        }
        if(worldKeysPending) { w::applyKeys(primary(),worldPuppet(),worldKeysPending);worldKeysPending=0; }
        if(achPending) { for(unsigned i=0;i<20;++i) if(achPending>>i&1) w::applyAchievement(i);achPending=0; }
        if(const auto ach=w::popAchievements()) for(unsigned i=0;i<20;++i) if(ach>>i&1) sendWorld(w::ReqAchievement,i,0);
        w::debugAchievement();
        for(const auto& f:soundPending) w::playPlayerSound(pse::native_proxy::actorOf(f.first),f.second); // another player's jump, shot, pain ... at its proxy
        soundPending.clear();
        for(auto snd=w::popSound();snd!=0xffffffffu;snd=w::popSound()) sendWorld(w::ReqSound,snd,0); // this player's own sounds: the host and the others hear them
        for(const int id:gonePending) w::applyEnemyGone(id);
        gonePending.clear();
        for(const int id:pickupPending) w::applyPickup(id);
        pickupPending.clear();
        for(int id;(id=w::popPickupAsk())>=0;) sendWorld(w::ReqPickup,static_cast<std::uint32_t>(id),0); // this player touched a sphere here: the host gives it
        for(int id,amount,kind;w::popHit(id,amount,kind);) sendWorld(w::ReqHit,static_cast<std::uint32_t>(id),static_cast<std::uint32_t>(amount)|(static_cast<std::uint32_t>(kind)<<16)); // this player's hits on the host's enemies
        for(int amount,kind;w::popSelfDamage(amount,kind);) sendWorld(w::ReqSelfDamage,static_cast<std::uint32_t>(amount),static_cast<std::uint32_t>(kind)); // drowning, lava, falls of this player
        for(int id;(id=w::popCheckpoint())>=0;) sendWorld(w::ReqCheckpoint,static_cast<std::uint32_t>(id),0); // this player's scarab: it respawns there
        if(difficultyPending>=0) { w::applyDifficulty(difficultyPending);difficultyPending=-1; }
        for(const auto& [f,ev]:switchPending) w::applySwitch(f,ev);
        switchPending.clear();
        for(const int id:breakPending) w::applyBreak(id);
        breakPending.clear();
        for(int id;(id=w::popBreak())>=0;) sendWorld(w::ReqBreak,static_cast<std::uint32_t>(id),0);
        if(!scriptPending.empty()) { Byte* me=worldPuppet();if(me) { for(const auto& c:scriptPending) w::applyScript(c.num,me,&c);scriptPending.clear(); } }
        for(auto c=w::popScript();c.num>=0;c=w::popScript()) sendWorld(w::ReqScript,static_cast<std::uint32_t>(c.num)|(static_cast<std::uint32_t>(c.pos?w::packZ(c.z):0)<<16),c.pos?w::packXY(c.x,c.y):0);
        if(worldOnTicks==330) w::debugScript(worldPuppet());
        if(worldOnTicks>=240 && worldOnTicks%10==0) {   // smoke only: this client's player "walks into the exit": PSE_DEBUG_CHANGE_MAP_CLIENT=maps/X.MAP + PSE_DEBUG_MAP_FILE_CLIENT=<flag file>
            static bool fired{};char name[64]{},file[MAX_PATH]{};
            if(!fired && GetEnvironmentVariableA("PSE_DEBUG_CHANGE_MAP_CLIENT",name,sizeof(name)) && GetEnvironmentVariableA("PSE_DEBUG_MAP_FILE_CLIENT",file,sizeof(file)) && GetFileAttributesA(file)!=INVALID_FILE_ATTRIBUTES) {
                fired=true;log("debug_change_map_client","\"map\":\"%s\"",name);
                reinterpret_cast<void(__fastcall*)(void*,const char*)>(g.base+0x1b54d0)(instance(),name);
            }
        }
        if(const auto gained=w::popKeysGained()) sendWorld(w::ReqKeys,gained,0);
        w::Mover m;
        while(w::popMover(m)) sendWorld(w::ReqMover,static_cast<std::uint32_t>(m.sector),w::moverToWire(m));
    }
    bool start() {
        bridge.registerSimulationThread();
#ifdef PSE_FEATURE_VISUAL_PROXY
        proxies.bind(g.tid);
#endif
#if defined(PSE_FEATURE_MAPREADY)
        {
            const auto observed=map56::observe(g.base);
            const auto fault=map56::derive(observed,localMap);
            log("client_map_identity_observed","\"fault\":\"%s\",\"state\":%d,\"next\":%d,\"pending\":%d,\"name_length\":%llu,\"name_capacity\":%llu,\"map\":\"%s\",\"mapinfo\":\"%p\",\"mapinfo_key\":\"%s\",\"level_vtable\":%s,\"psm_version\":%u,\"count_a\":%llu,\"count_b\":%llu,\"source\":\"GameInstance+1e0 kexStr / +1f8 mapInfo / level a45940\"",
                map56::text(fault),observed.state,observed.next,observed.pending,observed.nameLength,observed.nameCapacity,localMap.name.c_str(),reinterpret_cast<void*>(observed.mapInfo),fault==map56::Fault::None ? observed.key.data() : "",observed.levelVtable ? "true" : "false",observed.version,observed.countA,observed.countB);
            if(fault!=map56::Fault::None) { bridge.unregisterSimulationThread();block("client_native_map_identity");return false; }
            mapGate.observe(fault,localMap);
        }
        worker.setNativePlayerSnapshots(true);
        worker.setPlayerSnapshotObserver([](bool sending,const pse::BridgePlayerSnapshot& s,std::size_t bytes) {
            log(sending ? "snapshot_worker_send" : "snapshot_worker_receive","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"bytes\":%zu,\"opcode\":32,\"channel\":1,\"kex_reads\":0",s.slotId,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,bytes);
        });
#elif defined(PSE_FEATURE_SNAPSHOT)
        Byte* node=read<Byte*>(primary(),0xd8);Byte* actor=node ? read<Byte*>(node,0x28) : nullptr;
        if(!actor || read<Byte*>(actor,0x10)!=node || read<Byte*>(actor,0x1b8)!=primary() ||
            read<float>(actor,0x18)!=-7420.0f || read<float>(actor,0x1c)!=-6775.0f || read<float>(actor,0x20)!=1152.0f || read<int>(actor,0x3c)!=259) { bridge.unregisterSimulationThread();block("snapshot_client_first_map_witness");return false; }
        log("snapshot_client_map_witness","\"sector\":259,\"pos\":[-7420,-6775,1152],\"map_selection\":\"fresh_profile_Tomb_of_Ramses\",\"general_native_mapready\":false");
        worker.setNativePlayerSnapshots(true);
        worker.setPlayerSnapshotObserver([](bool sending,const pse::BridgePlayerSnapshot& s,std::size_t bytes) {
            log(sending ? "snapshot_worker_send" : "snapshot_worker_receive","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"bytes\":%zu,\"opcode\":32,\"channel\":1,\"kex_reads\":0",s.slotId,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,bytes);
        });
#endif
        worker.setClientInputCadence(true);
        worker.setClientInputObserver([](const pse::ClientInputDispatch& d) {
            log("client_input_dispatched","\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"sample_ordinal\":%llu,\"sample_tick\":%llu,\"sampled_ns\":%llu,\"generated_ns\":%llu,\"forward\":%.1f,\"fresh\":%s,\"stage\":\"enet_dispatch_not_udp_proof\"",d.slotId,d.generation,d.sequence,d.sampleOrdinal,d.simulationTick,d.sampledSteadyNs,d.generatedSteadyNs,d.forward,d.sampleFresh?"true":"false");
        });
        const auto menu=pse::ui::shared().settings();
        pse::ClientConfig cfg;cfg.hostAddress=menu.address;cfg.hostPort=*pse::ui::port(menu.portText);cfg.joinToken=menu.token;cfg.participantId="p23_client_"+std::to_string(GetCurrentProcessId())+(g.identity?"_"+std::to_string(g.identity):std::string());cfg.displayName=menu.displayName;cfg.reconnectKey=g.reconnectKey;
        if(!worker.startClient(cfg)) { bridge.unregisterSimulationThread();block("client_worker_start");return false; }
#ifdef PSE_FEATURE_P4
        started=true;log("client_worker_started","\"target_hz\":60,\"freshness_ms\":100,\"protocol\":275");return true;
#else
        started=true;log("client_worker_started","\"target_hz\":60,\"freshness_ms\":100,\"protocol\":272");return true;
#endif
    }
    void consume() {
        bridge.assertSimulationThread();pse::BridgeMessage m;
#ifdef PSE_FEATURE_MAPREADY
        observeMap();
#endif
        std::size_t polled=0;
        for(;polled<pse::THREAD_BRIDGE_INBOUND_CAPACITY && bridge.pollInbound(m);++polled) {
            if(const auto* e=std::get_if<pse::BridgeSessionEvent>(&m)) {
                log("client_session","\"type\":%u,\"slot\":%u,\"generation\":%u,\"epoch\":%u",static_cast<unsigned>(e->type),e->slotId,e->connectionGeneration,e->mapEpoch);
                if(e->type==pse::BridgeSessionEvent::Type::CLIENT_JOINED || e->type==pse::BridgeSessionEvent::Type::RECONNECTED) {
                    if(e->connectionGeneration<=gen) continue;
                    // The key first: a welcome for another level sends this client to the host's level, whose fresh Session must rejoin this
                    // reserved slot with it. Without the key the host rejected that rejoin as a hijack, for good ("Session already started").
                    if(e->reconnectKey) g.reconnectKey=e->reconnectKey;
#ifdef PSE_FEATURE_MAPREADY
                    #ifdef PSE_FEATURE_P4
                    // Same level name but another structure: not the same map (another game version) - never playable together.
                    // Another level: welcome as usual, then this client follows the host's level (observeMap, followCheck).
                    if(e->mapName==localMap.name && !map111::matches(localMap,e->mapName,e->mapStructure)) { active=readySent=baseline=false;block("welcome_native_map_structure_v111");continue; }
                    #endif
                    if(!mapGate.welcome(e->slotId,e->connectionGeneration,e->mapEpoch,e->mapName)) { block("welcome_map_identity_or_epoch");continue; }
                    const bool newBinding=e->mapEpoch!=epoch;
                    slot=e->slotId;gen=e->connectionGeneration;epoch=e->mapEpoch;active=true;readySent=baseline=false;reconnectPending=false;followCheck=true;
                    if(e->reconnectKey) g.reconnectKey=e->reconnectKey; // a later session of this process rejoins the same slot
                    snapshots.reset(gen,epoch);
                    if(newBinding) newBindingState(); else slots16.quiet(); // a reconnect inside the same host world keeps the respawn generations
                    log("client_welcome","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"host_map\":\"%s\",\"local_map\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,mapGate.hostName().c_str(),localMap.name.c_str());
#ifdef PSE_FEATURE_VISUAL_PROXY
                    proxies.configureSession(slot,gen,epoch);log("proxy_session_configured","\"local_slot\":%u,\"generation\":%u,\"epoch\":%u,\"spawn_enabled_by_session\":false,\"native_capacity\":%u",slot,gen,epoch,static_cast<unsigned>(pse::MAX_PLAYERS-1));
                    for(unsigned i=0;i<pse::MAX_PLAYERS;++i) retireProxy58(static_cast<std::uint8_t>(i),"session_rejoined"); // a rejoin: bodies of the old session never carry over (no-op on a first join)
#endif
#else
                    if(!e->connectionGeneration || e->slotId==0 || e->slotId>=pse::MAX_PLAYERS || e->mapName!=Domain ||
#ifdef PSE_FEATURE_SNAPSHOT
                        !e->mapEpoch
#else
                        e->mapEpoch!=1
#endif
                        ) { block("welcome_identity_or_input_domain");continue; }
                    slot=e->slotId;gen=e->connectionGeneration;epoch=e->mapEpoch;active=true;readySent=baseline=false;reconnectPending=false;
#ifdef PSE_FEATURE_SNAPSHOT
                    snapshots.reset(gen,epoch);slots16.quiet();
#endif
                    log("client_welcome","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"domain\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,Domain);
#endif
                } else if(e->type==pse::BridgeSessionEvent::Type::LOCAL_SESSION_LOST) {
                    active=readySent=baseline=false;reconnectPending=true;
#ifdef PSE_FEATURE_MAPREADY
                    mapGate.lost();
#endif
#ifdef PSE_FEATURE_VISUAL_PROXY
                    disconnect58("LOCAL_SESSION_LOST");
#endif
                } else if(e->type==pse::BridgeSessionEvent::Type::REJECTED) {
                    active=readySent=baseline=false;
                    // The host keeps a departed identity's slot reserved and wants its key back. A Session that was never welcomed (so no proxy of
                    // it exists) and is refused for its identity rejoins as a new player instead of staying out of the game until a restart.
                    if(!gen && (e->reasonCode==pse::REJECT_HIJACK_ATTEMPT || e->reasonCode==pse::REJECT_ALREADY_CONNECTED) && g.identityResets<3) {
                        ++g.identityResets;g.freshIdentity=true;log("client_identity_reset","\"reason\":%u,\"attempt\":%u",static_cast<unsigned>(e->reasonCode),g.identityResets);
                        continue;
                    }
                    block("session_rejected");
#ifdef PSE_FEATURE_VISUAL_PROXY
                    disconnect58("REJECTED");
#endif
                }
            } else if(const auto* map=std::get_if<pse::BridgeMapChange>(&m)) {
                if(map->connectionGeneration!=gen) continue;
                if(map->newMapEpoch==0) { // an order, not a session map: the host leads, this client follows (see followHost())
                    if(active && pse::validNativeMapName(map->mapName)) { followMap=map->mapName;log("client_follow_map_received","\"map\":\"%s\",\"generation\":%u",followMap.c_str(),gen); }
                    continue;
                }
                if(!active || (map->newMapEpoch==epoch && map->mapName==mapGate.hostName())) continue; // the binding we already have (join echo)
#ifdef PSE_FEATURE_MAPREADY
                // One host session for the whole game: the host is in its next world (next level, Restart Level, loaded save, back from the
                // map screen). Same connection and slot; this client binds to the new epoch and becomes ready again once its own world is
                // that level (following it when it is another one).
                if(!mapGate.mapChange(map->connectionGeneration,map->newMapEpoch,map->mapName)) { log("client_map_change_refused","\"map\":\"%s\",\"epoch\":%u",map->mapName.c_str(),map->newMapEpoch);continue; }
                const auto oldEpoch=epoch;
                epoch=map->newMapEpoch;readySent=baseline=false;followCheck=true;
                snapshots.reset(gen,epoch);
                newBindingState();
                log("client_host_world_changed","\"map\":\"%s\",\"epoch\":%u,\"old_epoch\":%u,\"local\":\"%s\",\"generation\":%u,\"bindings\":%u",map->mapName.c_str(),epoch,oldEpoch,localMap.name.c_str(),gen,bindings);
#else
                if(map->mapName!=Domain || map->newMapEpoch!=epoch) block("unsupported_map_domain");
#endif
            } else if(const auto* b=std::get_if<pse::BridgeWorldBaseline>(&m)) {
                if(b->connectionGeneration!=gen) continue;
#ifdef PSE_FEATURE_MAPREADY
                if(b->mapEpoch!=epoch) { log("client_baseline_stale","\"epoch\":%u,\"current\":%u",b->mapEpoch,epoch);continue; } // of a host world already left
                if(!active || !readySent || b->totalBatches!=1 || b->batchIndex!=0 || !b->entities.empty() || !mapGate.baseline(b->connectionGeneration,b->mapEpoch,b->mapName)) { block("map_baseline_contract");continue; }
                baseline=true;log("client_input_domain_ready","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"map\":\"%s\",\"entities\":0,\"state\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,mapGate.hostName().c_str(),map56::ClientGate::text(mapGate.state()));
#ifdef PSE_FEATURE_VISUAL_PROXY
                {
                    const auto token=mapToken58(localMap,epoch);proxies.onNativeMapReady(epoch,token);
                    log("proxy_native_map_ready","\"epoch\":%u,\"map\":\"%s\",\"identity_token\":\"%016llx\",\"gate_state\":\"%s\",\"count_a\":%llu,\"count_b\":%llu,\"source\":\"ClientGate Ready after native 30-tick identity; separate from Welcome\"",epoch,localMap.name.c_str(),token,map56::ClientGate::text(mapGate.state()),localMap.countA,localMap.countB);
                }
#endif
#else
                if(!active || !readySent || b->mapEpoch!=epoch || b->mapName!=Domain || b->totalBatches!=1 || b->batchIndex!=0 || !b->entities.empty()) { block("input_baseline_contract");continue; }
                baseline=true;log("client_input_domain_ready","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"entities\":0,\"world_synchronized\":false",slot,gen,epoch);
#endif
            } else if(const auto* we=std::get_if<pse::BridgeWorldEvent>(&m)) {
                worldEvent(*we);
            } else if(const auto* ws=std::get_if<pse::BridgeWorldSnapshot>(&m)) {
                if(ws->mapEpoch==epoch && worldReady() && ws->snapshotSequence>=lastEnemySequence) {
                    lastEnemySequence=ws->snapshotSequence;
                    pse::world::Projectile plist[24];int pn=0;
                    for(const auto& ent:ws->entities) {
                        if(ent.type!=2||pn>=24) continue;
                        auto& q=plist[pn++];q={};
                        q.id=ent.entityId;q.type=ent.subType;q.x=ent.posX;q.y=ent.posY;q.z=ent.posZ;q.vx=ent.velX;q.vy=ent.velY;q.vz=ent.velZ;q.yaw=ent.yaw;
                        q.sector=static_cast<std::int16_t>(ent.stateFlags);q.anim=static_cast<std::uint16_t>(ent.meta);
                    }
                    pse::world::applyProjectiles(plist,pn);
                    pse::world::Enemy list[64];int cnt=0;
                    for(const auto& ent:ws->entities) {
                        if(cnt>=64||ent.type!=1) continue;
                        auto& o=list[cnt++];o={};
                        o.id=ent.entityId;o.x=ent.posX;o.y=ent.posY;o.z=ent.posZ;o.vx=ent.velX;o.vy=ent.velY;o.vz=ent.velZ;o.yaw=ent.yaw;o.hp=static_cast<std::int16_t>(ent.health);
                        o.sector=static_cast<std::int16_t>(ent.stateFlags>>16);o.aiState=static_cast<std::uint16_t>(ent.stateFlags&0xffff);o.anim=ent.subType;o.frame=static_cast<std::uint16_t>(ent.meta);
                    }
                    pse::world::applyEnemies(list,cnt);
                    pse::world::Dynamic dlist[24];int dn=0;
                    for(const auto& ent:ws->entities) {
                        if(ent.type!=3||dn>=24) continue;
                        auto& d=dlist[dn++];d={};
                        d.id=ent.entityId;d.type=ent.subType;d.x=ent.posX;d.y=ent.posY;d.z=ent.posZ;d.vx=ent.velX;d.vy=ent.velY;d.vz=ent.velZ;d.yaw=ent.yaw;d.scale=ent.pitch;
                        d.hp=static_cast<std::int16_t>(ent.health);d.aiState=static_cast<std::uint16_t>(ent.stateFlags&0xffff);d.sector=static_cast<std::int16_t>(ent.stateFlags>>16);
                        d.anim=static_cast<std::uint16_t>(ent.meta>>16);d.frame=static_cast<std::uint16_t>(ent.meta&0xffff);
                    }
                    pse::world::applyDynamic(dlist,dn);
                    pse::world::Platform plat[24];int pln=0; // floating platform ticks / drop pad states of the host (type 4)
                    for(const auto& ent:ws->entities) if(ent.type==4 && pln<24 && ent.entityId<32768) plat[pln++]={static_cast<std::int16_t>(ent.entityId),static_cast<std::uint8_t>(ent.subType),ent.meta};
                    pse::world::applyPlatforms(plat,pln);
                }
#ifdef PSE_FEATURE_SNAPSHOT
            } else if(const auto* c=std::get_if<pse::BridgeRespawnConfirm>(&m)) {
                // Broadcast: the confirm may concern ANY slot 0..7 (own slot: local revive; another slot: bookkeeping + a fresh proxy).
                if(!active||!readySent||!baseline||!slots16.admitConfirm(*c,slot,epoch))continue;
                if(c->slotId==slot&&!stageLocal16(*c,slot,gen,epoch))continue;
                slots16.applyConfirm(snapshots,*c);
                if(c->slotId==slot){hostHealthValid=false;vitalsSynced=false;hostLoadoutValid=false;} // the pre-death health must not hit the fresh body: wait for a new-generation snapshot
                log("p16_generation_admitted","\"slot\":%u,\"epoch\":%u,\"generation\":%u,\"death\":%u,\"own\":%s",c->slotId,epoch,c->newGeneration,c->deathSequence,c->slotId==slot?"true":"false");
#ifdef PSE_FEATURE_VISUAL_PROXY
                if(c->slotId!=slot) retireProxy58(c->slotId,"respawn_confirm"); // the old body is gone, the next snapshot spawns the new one
#endif
            } else if(const auto* s=std::get_if<pse::BridgePlayerSnapshot>(&m)) {
                // Per slot (Tracker::intake): the own slot only follows its own respawn handshake; another slot's newer generation is
                // adopted (late join, missed confirm); an older one is a stale in-flight record and waits.
#ifdef PSE_FEATURE_MAPREADY
                const bool mapEligible=mapGate.snapshotEligible(s->connectionGeneration,s->mapEpoch);
#else
                const bool mapEligible=true;
#endif
                const auto in=slots16.intake(snapshots,*s,active && readySent && baseline && gameplay() && !g.failed && !g.stopped && mapEligible,slot,steadyNs());
                if(in.waited){
                    if(s->slotId<genWaitLogged16.size() && genWaitLogged16[s->slotId]++%120==0)
                        log("p16_snapshot_generation_wait","\"slot\":%u,\"generation\":%u",s->slotId,s->entityGeneration);
                    continue;
                }
                if(in.adopted){
                    log("p16_generation_adopted","\"slot\":%u,\"generation\":%u,\"entity\":%u",s->slotId,s->entityGeneration,s->entityId);
#ifdef PSE_FEATURE_VISUAL_PROXY
                    if(in.replaced) retireProxy58(s->slotId,"generation_adopted");
#endif
                }
                const auto verdict=in.verdict;
                if(verdict==snapshot55::Verdict::EntityMismatch || verdict==snapshot55::Verdict::Nonfinite) { block("snapshot_unexplained_entity_or_transform");continue; }
                if(verdict!=snapshot55::Verdict::Accept) { log("native_snapshot_rejected","\"slot\":%u,\"snapshot_seq\":%u,\"reason\":%u",s->slotId,s->sequence,static_cast<unsigned>(verdict));continue; }
                genWaitLogged16[s->slotId]=0;
                if(s->slotId==slot){hostDeath16=s->deathSequence;hostDead16=s->lifeState==pse::LIFE_DEAD;hostHealth=s->health;hostHealthValid=true;hostAmmo=s->ammoByWeapon;hostOwned=static_cast<std::uint8_t>(s->weaponMask);hostLoadoutValid=true;}
                log("native_snapshot_received","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"bits\":[%u,%u,%u,%u,%u],\"player_writes\":0",
                    s->slotId,s->sequence,s->serverTick,s->mapEpoch,s->connectionGeneration,s->entityId,s->entityGeneration,s->posX,s->posY,s->posZ,s->yaw,s->pitch,snapshot55::bits(s->posX),snapshot55::bits(s->posY),snapshot55::bits(s->posZ),snapshot55::bits(s->yaw),snapshot55::bits(s->pitch));
#ifdef PSE_FEATURE_VISUAL_PROXY
                route58(*s);
#endif
#endif
            }
        }
#ifdef PSE_FEATURE_SNAPSHOT
        // A remote slot that stopped sending (player left, or the slot is reused) is not kept as a frozen proxy: forget it so that a
        // later snapshot starts from scratch. Only when this tick drained the inbox, so a backlog cannot fake silence.
        if(active&&readySent&&baseline&&polled<pse::THREAD_BRIDGE_INBOUND_CAPACITY)
            if(const auto stale=slots16.collectStale(snapshots,steadyNs(),slot))
                for(unsigned i=0;i<pse::client_slots::Slots;++i) if(stale>>i&1u) {
                    genWaitLogged16[i]=0;
                    log("p16_slot_stale","\"slot\":%u,\"silent_ms\":%llu",i,static_cast<unsigned long long>(pse::client_slots::StaleNs/1000000ULL));
#ifdef PSE_FEATURE_VISUAL_PROXY
                    retireProxy58(static_cast<std::uint8_t>(i),"stale_slot");
#endif
                }
#endif
        if(bridge.consumeResyncReasons()) { active=readySent=baseline=false;reconnectPending=true; }
        if(reconnectPending && steadyNs()-lastReconnectNs>=1000000000ULL) {
            pse::BridgeControl c;c.type=pse::BridgeControl::Type::RECONNECT;c.slotId=slot;c.connectionGeneration=gen;
            if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{c}))) { reconnectPending=false;lastReconnectNs=steadyNs();log("client_reconnect_requested","\"generation\":%u",gen); }
        }
        // This loads only the already accepted empty INPUT control domain.
        // Client's local level is independent and never claimed to match host world.
#ifdef PSE_FEATURE_MAPREADY
        // MapReady only after the authenticated host identity equals the locked local native
        // identity for StableTicks consecutive stable ticks: never on connection or a timer.
        // Not while an order of the host is still to be executed here (this world is about to be left: Restart Level, next level).
        if(active && !readySent && !g.failed && gameplay() && g.localThisTick==1 && followMap.empty() && mapGate.shouldSendReady()) {
            pse::BridgeMapReady r;r.slotId=slot;r.connectionGeneration=gen;r.mapEpoch=epoch;
#ifdef PSE_FEATURE_P4
            r.mapName=localMap.name;r.mapStructure=map111::structure(localMap);
#endif
            if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{r}))) { readySent=true;mapGate.readyQueued();log("client_map_ready_queued","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"map\":\"%s\",\"stable_ticks\":%u,\"tick\":%llu",slot,gen,epoch,localMap.name.c_str(),mapGate.stableTicks(),g.ticks); }
        }
#else
        if(active && !readySent && !g.failed && gameplay() && g.localThisTick==1) {
            pse::BridgeMapReady r;r.slotId=slot;r.connectionGeneration=gen;r.mapEpoch=epoch;
            if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{r}))) { readySent=true;log("client_input_ready_queued","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"domain\":\"%s\"",slot,gen,epoch,Domain); }
        }
#endif
    }
    void sample(bool valid) {
        if(!active || stopped) return;
        const bool ready16=readySent&&baseline&&mapGate.snapshotEligible(gen,epoch)&&gameplay()&&!g.failed&&g.localThisTick==1;
        if(ready16&&!restart16.install(g.base,g.tid)){block("p16_restart_install");return;}
        if(ready16&&!inputBound16){input16.bind({slot,gen,epoch},localGeneration16);inputBound16=true;}
        if(ready16&&newLocal16)completeLocal16();
        if(ready16&&!newLocal16){
            input16.observe(hostDead16,hostDeath16);
            nl16::Graph graph16{primary()};const bool nativeDead16=nl16::graph(g.base,graph16)&&read<std::int16_t>(graph16.actor,0x17a)<=0;
            DWORD foreground16{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground16);
            const bool canRequest=foreground16==GetCurrentProcessId()&&nativeDead16&&read<Byte>(instance(),background::MenuByte)==0;
            if(const auto request=input16.sample((GetAsyncKeyState('E')&0x8000)!=0,GetTickCount64(),canRequest)){
                pse::BridgeRespawnRequest out;out.slotId=slot;out.connectionGeneration=gen;out.mapEpoch=epoch;out.deathSequence=request->death;
                if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{out}))){
                    if(!requestPending16){p7Authority.disconnect();p7Admitted=false;}
                    requestPending16=true;log("p16_E_request_sent","\"slot\":%u,\"connection\":%u,\"epoch\":%u,\"death\":%u",slot,gen,epoch,out.deathSequence);
                }
            }
        }
        pse::BridgeClientInputSample s;s.slotId=slot;s.connectionGeneration=gen;s.sampleOrdinal=++g.ordinal;s.sampledSteadyNs=steadyNs();s.simulationTick=g.ticks;
        s.valid=valid && baseline;s.moveForward=s.valid ? (g.forwardHeld?1.0f:0.0f)-(g.backHeld?1.0f:0.0f) : 0.0f;
        if(s.valid) {
            // Full input (v1.14): native KEX bits of Player+0 -> wire ButtonFlags + axes; the client's own pose goes along.
            const auto b=g.buttons;
            s.moveRight=((b>>7)&1u?1.0f:0.0f)-((b>>6)&1u?1.0f:0.0f);
            {const int w=read<int>(primary(),0x88);s.selectedWeapon=w>=0&&w<8?static_cast<std::uint8_t>(w):0xff;}
            s.buttons=((b&(1u<<0))?pse::BUTTON_ATTACK:0u)|((b&(1u<<1))?pse::BUTTON_JUMP:0u)|((b&(1u<<10))?pse::BUTTON_USE:0u)|
                      ((b&(1u<<8))?pse::BUTTON_WEAPON_NEXT:0u)|((b&(1u<<9))?pse::BUTTON_WEAPON_PREV:0u);
            // A one-tick impulse (E) must survive the 60 Hz latest-sample cadence: hold it for a few samples.
            if(b&(1u<<10)) useHold=6;
            if(useHold) { s.buttons|=pse::BUTTON_USE;--useHold; }
            if(g.sector>=0 && g.sector<=32767 && std::isfinite(g.yaw) && std::isfinite(g.pitch) && std::isfinite(g.px) && std::isfinite(g.py) && std::isfinite(g.pz)) {
                s.yaw=g.yaw;s.pitch=g.pitch;s.posX=g.px;s.posY=g.py;s.posZ=g.pz;s.sector=static_cast<std::int16_t>(g.sector);
            }
        }
        const bool queued=pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{s}));
        log("client_input_sampled","\"slot\":%u,\"generation\":%u,\"sample_ordinal\":%llu,\"tick\":%llu,\"sampled_ns\":%llu,\"valid\":%s,\"kex_buttons\":%u,\"held_forward\":%u,\"held_back\":%u,\"forward\":%.1f,\"right\":%.1f,\"wire_buttons\":%u,\"yaw\":%.4f,\"pitch\":%.4f,\"pose\":[%.1f,%.1f,%.1f],\"sector\":%d,\"bridge_queued\":%s",slot,gen,s.sampleOrdinal,g.ticks,s.sampledSteadyNs,s.valid?"true":"false",g.buttons,g.forwardHeld,g.backHeld,s.moveForward,s.moveRight,s.buttons,s.yaw,s.pitch,s.posX,s.posY,s.posZ,static_cast<int>(s.sector),queued?"true":"false");
    }
    void stop(const char* reason) {
        if(!started || stopped) return;
        bridge.assertSimulationThread();sample(false);stopped=true;
#ifdef PSE_FEATURE_VISUAL_PROXY
        logProxies("session_stop");if(!proxies.quiescent()) log("proxy_not_quiescent_at_stop","\"reason\":\"%s\",\"native_calls_after\":\"none_permitted\"",reason);
#endif
        log("client_stop_begin","\"reason\":\"%s\"",reason);worker.stop();
        pse::BridgeMessage m;std::size_t retired{};while(bridge.pollInbound(m)) ++retired;
        const auto s=worker.getStats();log("client_worker_joined","\"running\":%s,\"retired_inbound\":%zu,\"retired_outbound\":%zu,\"affinity_violations\":%llu,\"pending_peak\":%llu,\"lost_on_stop\":%llu,\"poll_loops\":%llu",worker.isRunning()?"true":"false",retired,bridge.getOutboundOccupancy(),bridge.getAffinityViolationCount(),s.pendingPeak,s.lostOnStop,s.pollLoops);
        bridge.unregisterSimulationThread();bridge.resetForNewSession();active=baseline=readySent=false;
    }
};
#ifdef PSE_FEATURE_P4
using Respond=std::uint64_t(__fastcall*)(void*,void*);
void endClientLease() {
    if(g.focusLease.applied) g.focusLease.release(*reinterpret_cast<Byte*>(instance()+background::FocusByte),g.focusSerial);
}
std::uint64_t __fastcall hookClientFocus(void* self,void* event) {
    if(!thread() || self!=instance() || g.stopped || !event) return reinterpret_cast<Respond>(g.focusOriginal)(self,event);
    const auto type=read<std::uint32_t>(event),reason=type==13 ? read<std::uint32_t>(event,16) : 0;
    const auto result=reinterpret_cast<Respond>(g.focusOriginal)(self,event);
    if(type==13 && (reason==1||reason==2)) {
        ++g.focusSerial;g.focusKnown=read<Byte>(instance(),background::FocusByte)==(reason==2?1:0);g.focusLost=g.focusKnown&&reason==2;
    }
    return result;
}
void neutralInput() { for(std::size_t off=0;off<=0x7c;off+=4) *reinterpret_cast<std::uint32_t*>(primary()+off)=0; }
bool clientNeutral(void* p) { return thread() && g.inTick && p==primary() && g.neutral && !g.stopped; }
void __fastcall hookClientCommands(void* p) {
    if(clientNeutral(p)) neutralInput();
    reinterpret_cast<Unary>(g.original[profile::Commands])(p);
    // "+use" style actions are one-frame impulses that the rest of Update may already have consumed: take the freshly built bits here.
    if(p==primary() && thread() && g.inTick && !g.neutral) g.cmdButtons|=read<std::uint32_t>(p);
    if(clientNeutral(p)) neutralInput();
}
void __fastcall hookClientTurning(void* p) {
    reinterpret_cast<Unary>(g.original[profile::Turning])(p);
    if(clientNeutral(p)) neutralInput();
}
#endif
void stop(const char* reason) {
    if(!thread()) { ++g.foreign;block("stop_affinity");return; }
    if(g.stopped) {if(std::strcmp(reason,"DirectQuit")==0) {pse::native_ui::stop();flushJournal();}return;}
#ifdef PSE_FEATURE_P4
    endClientLease();g.neutral=false;
    for(const auto i:{profile::Commands,profile::Turning}) if(g.enabled[i]) {
        const auto rc=MH_DisableHook(at(i));g.enabled[i]=false;
        log("p4_control_restore","\"site\":\"%s\",\"exact16\":%s",profile::Sites[i].name,prefix(i)?"true":"false");
        if(rc!=MH_OK || !prefix(i)) block("p4_control_restore");
    }
#endif
    if(!restart16.restore(log))block("p16_restart_restore");
    input16.invalidate();requestPending16=newLocal16=protect16=false;
    stagedConfirm16.reset();
    p7Authority.barrier();p7Admitted=false;pse::self_native_p7::invalidate();
    if(std::strcmp(reason,"StateExit")==0)pse::native_ui::worldExit();else pse::native_ui::stop();
    if(g.session) g.session->stop(reason);
    g.stopped=true;
    if(g.enabled[profile::Update]) {
        const auto rc=MH_DisableHook(at(profile::Update));g.enabled[profile::Update]=false;
        log("client_control_restored","\"exact16\":%s,\"status\":%d",prefix(profile::Update)?"true":"false",rc);
        if(rc!=MH_OK || !prefix(profile::Update)) block("restore_update");
    }
#ifdef PSE_FEATURE_P4
    log("client_quiesced","\"reason\":\"%s\",\"pinned\":true,\"command_block_neutralization\":true,\"inventory_writes\":0",reason);flushJournal();
#else
    log("client_quiesced","\"reason\":\"%s\",\"pinned\":true,\"player_writes\":0",reason);flushJournal();
#endif
}
// Leaving a gameplay world (next level, Restart Level, checkpoint reload, map screen, title) keeps the process, every hook AND the
// network session: the host's session lives until the host quits, so this client stays connected; only the per-world latches go
// (self-state authority, respawn gate, proxy adapter: the engine purges the proxy Actors itself). The next stable world sends MapReady
// again (a new epoch from the host's MapChange, or the same one after a world change of this client alone, see Session::consume()).
// A session that is no longer connected (rejected, never welcomed) is dropped here and the next world starts a fresh one.
void worldTransition(const char* reason) {
    if(!thread() || g.stopped) return;
    if(g.failed && !processFault(lastBlock) && blockRecoveries<16) { // the next world rejoins the host by itself (no game restart)
        g.failed=false;++blockRecoveries;log("client_recovered_after_block","\"reason\":\"%s\",\"recoveries\":%u,\"at\":\"%s\"",lastBlock,blockRecoveries,reason);lastBlock[0]=0;
    }
    ++g.worldSerial;
    auto* ss=g.session;
    const bool keep=ss && ss->started && !ss->stopped && (ss->active || ss->reconnectPending);
    log("world_transition","\"reason\":\"%s\",\"world\":%llu,\"session_kept\":%s",reason,g.worldSerial,keep?"true":"false");
    if(ss) ss->barrier58(reason);
    pse::native_proxy::exitBarrier(1);
    if(!restart16.restore(log)) block("p16_restart_restore");
    input16.invalidate();requestPending16=newLocal16=protect16=false;stagedConfirm16.reset();
    p7Authority=ss7::Authority{};p7Admitted=false;pse::self_native_p7::invalidate();
    p7LifetimeClosed=false;p7PinnedActor=p7PinnedNode=0;p7PinnedAllocation=0;
    inputBound16=false;startValid16=false;
    if(!keep) {
        // A fresh session follows: the respawn generations start over with it.
        hostDead16=false;hostDeath16=0;localGeneration16=1;slots16.reset();
        if(ss) ss->stop(reason);
        g.session=nullptr; // the stopped Session is retained (never freed): its worker has joined, nothing references it
    }
    if(!pse::native_proxy::rearm()) block("native_proxy_rearm");
    pse::world::resetWorld();
    if(keep) ss->worldLeft();
}
// Host leads, client follows: kexSlaveGameInstanceLocal::ChangeMap (0x1b54d0, "maps/NAME.MAP") is what the game itself calls from an exit
// trigger: it starts the 1 s fade and sets the pending map; the usual StateExit -> worldTransition() -> new Session path does the rest.
// Only from the simulation thread, only in a stable gameplay world (never mid-fade or mid-load).
void followHost() {
    if(!g.session || g.session->followMap.empty() || !thread() || !g.inTick) return;
    if(read<int>(instance(),0x24)!=3 || read<int>(instance(),0x20)!=3 || read<int>(instance(),0x28)!=0) return;
    static constexpr Byte prefixBytes[16]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xc7,0x41,0x28,0x04,0x00,0x00};
    const std::string name=std::move(g.session->followMap);g.session->followMap.clear();
    if(name.rfind("@STATE:",0)==0) {
        // GameInstance::SetGameState (vtable+0x98 = 0x1ae650): 2 = the overworld map screen (where the level exit leads: everybody picks the next location
        // on the game's own map), 6 (bad) / 7 (good) = the story ending, exactly what EndGame(bool) ends with.
        static constexpr Byte stateBytes[9]{0x89,0x51,0x28,0x0f,0x57,0xd2,0x48,0x8b,0x0d};
        const int state=std::atoi(name.c_str()+7);
        if(state==2) {   // "@STATE:2:<location>": the same "Travel" the leader's game made (location, unlocks, autosave); the bare state only as a fallback
            const auto colon=name.find(':',7);
            const int at=colon!=std::string::npos ? std::atoi(name.c_str()+colon+1) : -1;
            if(at>=0 && travelToOverworld(at)) {
                log("client_follow_state","\"state\":2,\"location\":%d,\"native_travel\":true,\"tick\":%llu",at,g.ticks);
                g.session->followedByUs=true;return;
            }
            if(colon!=std::string::npos && !setOverworldCurrent(at)) log("client_overworld_location_refused","\"order\":\"%s\"",name.c_str());
        }
        if((state!=2 && state!=6 && state!=7) || !std::equal(std::begin(stateBytes),std::end(stateBytes),g.base+0x1ae650)) { block("end_game_profile");return; }
        log("client_follow_state","\"state\":%d,\"tick\":%llu",state,g.ticks);
        g.session->followedByUs=true;
        reinterpret_cast<void(__fastcall*)(void*,int)>(g.base+0x1ae650)(instance(),state);
        return;
    }
    if(!std::equal(std::begin(prefixBytes),std::end(prefixBytes),g.base+0x1b54d0)) { block("change_map_profile");return; }
    log("client_follow_map_change","\"map\":\"%s\",\"tick\":%llu",name.c_str(),g.ticks);
    g.session->followedByUs=true;
    reinterpret_cast<void(__fastcall*)(void*,const char*)>(g.base+0x1b54d0)(instance(),name.c_str());
}
// Any player's exit moves everybody: when THIS client's own game starts a map change / an ending (its player walked into the exit trigger),
// the host is told which one (world request ReqMap); the host runs the same native call and orders every client to follow.
void reportExit() {
    auto* ss=g.session;
    {   static unsigned why=0;if(ss && ss->exitSent==false && read<int>(instance(),0x28)!=0 && ++why<=3) log("client_exit_gate","\"followedByUs\":%s,\"followMap\":%zu,\"inTick\":%s,\"active\":%s,\"ready\":%s,\"baseline\":%s",ss->followedByUs?"true":"false",ss->followMap.size(),g.inTick?"true":"false",ss->active?"true":"false",ss->readySent?"true":"false",ss->baseline?"true":"false"); }
    if(!ss || ss->exitSent || ss->followedByUs || !ss->followMap.empty() || !thread() || !g.inTick || !ss->active || !ss->readySent || !ss->baseline) return;
    const auto o=map56::observe(g.base);
    if(o.state!=3) return;
    if(o.pending!=0) { static unsigned probes=0;if(++probes<=3) log("client_exit_probe","\"pending\":%d,\"readable\":%s,\"len\":%llu,\"name\":\"%.40s\"",o.pending,o.nameReadable?"true":"false",static_cast<unsigned long long>(o.nameLength),o.nameReadable?o.name.data():""); }
    std::string stem;
    if(o.pending==4 && o.nameReadable && o.nameLength>5 && o.nameLength<=map56::NameReadMax) {
        const std::string name(o.name.data(),static_cast<std::size_t>(o.nameLength));
        const auto dot=name.rfind('.');
        if(name.compare(0,5,"maps/")==0 && dot!=std::string::npos && dot>5) stem=name.substr(5,dot-5);
    } else if(o.pending==2) {   // the overworld map: the exit trigger sent THIS player there; "STATE2" + the 2-digit location it chose (fits the 8 characters of the request)
        const int at=overworldCurrent();char buf[16]{};
        if(at<0||at>99) { ss->exitSent=true;return; }
        sprintf_s(buf,"STATE2%02d",at);stem=buf;
    } else if(o.pending==6 || o.pending==7) stem="STATE"+std::to_string(o.pending);
    else return;
    if(stem.empty() || stem.size()>8 || !std::all_of(stem.begin(),stem.end(),[](char ch){return (ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_';})) { ss->exitSent=true;return; }
    std::uint32_t lo=0,hi=0;
    for(std::size_t i=0;i<stem.size();++i) (i<4?lo:hi)|=static_cast<std::uint32_t>(static_cast<unsigned char>(stem[i]))<<(8*(i%4));
    ss->exitSent=true;ss->exitTick=g.ticks;
    log("client_exit_reported","\"stem\":\"%s\",\"pending\":%d,\"tick\":%llu",stem.c_str(),o.pending,g.ticks);
    ss->sendWorld(pse::world::ReqMap,lo,hi);
}
void __fastcall hookUpdate(void* p) {
#ifdef PSE_FEATURE_P4
    if(p==primary() && !g.stopped) {
        if(!thread() || !g.inTick || g.localThisTick || reinterpret_cast<Byte*>(_ReturnAddress())!=g.base+background::PrimaryReturn) { block("p4_primary_update_scope");return; }
        if(g.neutral) {
            if(g.resetPending) { reinterpret_cast<Final>(g.base+background::ResetBindings)();g.resetPending=false; }
            neutralInput();
        }
    }
#endif
    reinterpret_cast<Unary>(g.original[profile::Update])(p);
    if(p!=primary() || g.stopped) return;
    if(!thread() || !g.inTick) { ++g.foreign;block("update_affinity");return; }
    ++g.localUpdates;if(++g.localThisTick!=1) { block("duplicate_local_update");return; }
    if(!gameplay() || g.failed) return;
    if(stagedConfirm16){
        const auto staged=*stagedConfirm16;stagedConfirm16.reset();
        if(!confirmLocal16(staged.wire,staged.slot,staged.connection,staged.epoch)){block("p16_staged_confirm_failed");return;}
    }
    Byte* node=read<Byte*>(p,0xd8);Byte* actor=node?read<Byte*>(node,0x28):nullptr;
    if(!actor || read<Byte*>(actor,0x10)!=node || read<void*>(actor,0x1b8)!=p || read<Byte*>(actor)!=g.base+0x52d198) { block("local_player_graph");return; }
    if(read<std::int16_t>(actor,0x17a)<=0 || (read<unsigned>(actor,0x1c0)&0x200u)) {
        static std::uint64_t lastDeadLog=0;
        if(g.ticks-lastDeadLog>=300) { lastDeadLog=g.ticks;log("client_player_dead","\"tick\":%llu,\"health\":%d,\"flags1c0\":%u,\"pos\":[%.1f,%.1f,%.1f],\"sector\":%d",g.ticks,read<std::int16_t>(actor,0x17a),read<unsigned>(actor,0x1c0),read<float>(actor,0x18),read<float>(actor,0x1c),read<float>(actor,0x20),read<int>(actor,0x3c)); }
        return;
    }
#ifdef PSE_FEATURE_P4
    if(g.neutral) neutralInput();
#endif
    g.buttons=read<std::uint32_t>(p)|g.cmdButtons;g.forwardHeld=read<std::uint32_t>(p,0xc);g.backHeld=read<std::uint32_t>(p,0x10);g.sampled=true;
    g.yaw=read<float>(actor,0x38);g.pitch=read<float>(actor,0x30);g.px=read<float>(actor,0x18);g.py=read<float>(actor,0x1c);g.pz=read<float>(actor,0x20);g.sector=read<int>(actor,0x3c);
}
void __fastcall hookTick(void* self) {
    if(g.stopped) { reinterpret_cast<Unary>(g.original[profile::Tick])(self);return; }
    if(self!=instance() || !thread() || g.inTick) { ++g.foreign;block("tick_affinity_or_nested");reinterpret_cast<Unary>(g.original[profile::Tick])(self);return; }
    if(!g.attested) { g.attested=true;log("simulation_attested","\"expected_tid\":%lu,\"observed_tid\":%lu",g.tid,GetCurrentThreadId()); }
    g.inTick=true;++g.ticks;g.localThisTick=0;g.sampled=false;g.buttons=g.cmdButtons=g.forwardHeld=g.backHeld=0;
#ifdef PSE_FEATURE_VISUAL_PROXY
    seam58.tickEnter(g.ticks);
#endif
#ifdef PSE_FEATURE_P4
    // Observe identity before granting even a single background tick; do not use a stale Ready state.
    if(g.session && !g.session->stopped && g.session->baseline && g.session->readySent) g.session->observeMap();
    DWORD foreground{};GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool ready=g.session && !g.session->stopped && g.session->active && g.session->baseline &&
        g.session->mapGate.snapshotEligible(g.session->gen,g.session->epoch);
    const background::Conditions conditions{thread(),g.session && g.session->worker.isRunning(),ready,gameplay(),
        read<int>(instance(),0x20)==3,read<Byte>(instance(),background::MenuByte)!=0,g.stopped||g.shutdown,g.failed,g.focusKnown,g.focusLost};
    const bool allowed=g.backgroundPermit.load(std::memory_order_acquire) && foreground!=GetCurrentProcessId() && background::eligible(conditions);
    const bool leased=g.focusLease.acquire(*reinterpret_cast<Byte*>(instance()+background::FocusByte),g.focusSerial,allowed);
    if(leased && !g.backgroundPeriod) g.resetPending=true;
    const bool returning=g.backgroundPeriod && g.focusKnown && !g.focusLost;
    if(returning) g.resetPending=true;
    const bool uiCapture=pse::ui::nativeCapture(g.permit.load(std::memory_order_acquire)!=0,gameplay(),pse::ui::shared().capture.load());
    if(uiCapture!=g.uiCaptureWas) {g.resetPending=true;log("p23_ui_input_gate","\"capture\":%s,\"scoped_native_neutral\":true",uiCapture?"true":"false");}
    g.neutral=leased||returning||uiCapture||g.uiCaptureWas;g.uiCaptureWas=uiCapture;
    if(leased) { g.backgroundPeriod=true;++g.backgroundTicks; }
    if(returning) g.backgroundPeriod=false;
#endif
    reinterpret_cast<Unary>(g.original[profile::Tick])(self);
#ifdef PSE_FEATURE_P4
    endClientLease();g.neutral=false;
    if(leased && (g.backgroundTicks<=3 || g.backgroundTicks%120==0))
        log("p4_client_background_tick","\"count\":%llu,\"local_this_tick\":%u,\"focus_after\":%u,\"buttons\":%u,\"held_forward\":%u,\"held_back\":%u",g.backgroundTicks,g.localThisTick,read<Byte>(instance(),background::FocusByte),g.buttons,g.forwardHeld,g.backHeld);
#endif
    if(!g.stopped) {
        if(!g.session && g.permit.load(std::memory_order_acquire) && pse::ui::shared().requested.load(std::memory_order_acquire) && !g.failed && gameplay() && g.localThisTick==1) { g.session=new Session;g.session->start(); }
        if(g.session && g.session->started && !g.session->stopped) {
            reportExit(); // before consume(): the map change we just started invalidates this session's gate in consume(), too late to report
            g.session->consume();
            followHost();
            // our own exit was just reported: give the worker a few ticks to put the request on the wire before this world's session is torn down
            if(g.session->mapGate.state()==map56::ClientGate::State::Invalidated && !(g.session->exitSent && g.ticks-g.session->exitTick<20)) worldTransition("native_map_invalidated");
            if(g.session) {
            DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            {
                static std::uint64_t lastWhy=~0ull;
                const std::uint64_t why=(g.sampled?1u:0u)|(g.localThisTick==1?2u:0u)|(gameplay()?4u:0u)|(g.failed?8u:0u)|(pse::ui::shared().capture.load()?16u:0u)|(foregroundPid==GetCurrentProcessId()?32u:0u);
                if(why!=lastWhy) { lastWhy=why;log("client_input_gate","\"tick\":%llu,\"sampled\":%u,\"local_once\":%u,\"gameplay\":%u,\"failed\":%u,\"ui_capture\":%u,\"foreground\":%u",g.ticks,(why&1)!=0,(why&2)!=0,(why&4)!=0,(why&8)!=0,(why&16)!=0,(why&32)!=0); }
            }
            g.session->sample(g.sampled && g.localThisTick==1 && gameplay() && !g.failed && !pse::ui::shared().capture.load() && foregroundPid==GetCurrentProcessId());
#ifdef PSE_FEATURE_VISUAL_PROXY
            if(seam58.attached()) g.session->phase58(seam58.worldReady(g.ticks));
#endif
            g.session->worldSync();
            }
        }
        if(g.freshIdentity && g.session) { g.freshIdentity=false;++g.identity;g.reconnectKey=0;g.session->stop("identity_reset");g.session=nullptr; } // next tick: a new Session
        if((g.localUpdates==1 && g.localThisTick==1) || g.ticks<=3 || g.ticks%120==0)
            log("tick_status","\"ticks\":%llu,\"state\":%d,\"pending\":%d,\"local_updates\":%llu,\"local_this_tick\":%u",g.ticks,read<int>(instance(),0x24),read<int>(instance(),0x28),g.localUpdates,g.localThisTick);
    }
#ifdef PSE_FEATURE_VISUAL_PROXY
    seam58.tickLeave(g.ticks,g.localThisTick);
    if(detach58.load(std::memory_order_acquire) && seam58.attached()) {
        // The adapter (and its callbacks) may be unloaded only after every proxy is quiescent.
        if(!g.session || g.session->proxies.quiescent()) {
            if(pse::native_proxy::detach()) { seam58.detach(pse::seam::Detached);log("seam_detached","\"tick\":%llu,\"proxies_quiescent\":true",g.ticks); }
            else block("native_proxy_detach_restore");
        }
        else if(!detachDeferredLogged58) { detachDeferredLogged58=true;log("seam_detach_deferred","\"tick\":%llu,\"reason\":\"proxy_not_quiescent\"",g.ticks); }
    }
    if(seam58.faults()!=faultsLogged58) { faultsLogged58=seam58.faults();pse::native_proxy::fault();block("seam_native_fault");log("seam_native_fault","\"faults\":%llu,\"seam_status\":%u,\"further_calls\":false",faultsLogged58,seam58.status()); }
#endif
    {   // Esc menu: gameplay state, but the local Player Update is skipped. Debounced over a few ticks.
        static unsigned idleTicks=0;idleTicks=(gameplay() && g.localThisTick==0) ? idleTicks+1 : 0;
        pse::ui::shared().paused.store(idleTicks>=3,std::memory_order_release);
    }
    pse::native_ui::tick(!g.stopped && !g.failed && gameplay() && g.session && g.session->active && g.session->readySent && g.session->baseline,g.session?g.session->epoch:0,g.session?g.session->slot:255,g.session?g.session->gen:0);
    g.inTick=false;
}
#ifdef PSE_FEATURE_VISUAL_PROXY
// Pre-teardown barrier at the existing exit seams: latch first, tell the adapter, then never call it again.
void exit58(const char* reason,std::uint32_t code) {
    if(!thread()) return;
    if(g.session) g.session->barrier58(reason);
    const bool wasAttached=seam58.attached();
    if(wasAttached) seam58.worldExit(code);
    pse::native_proxy::exitBarrier(code); // also invalidate after an earlier fault detached the seam
    const bool restored=pse::native_proxy::detach();
    if(!restored) block("native_proxy_exit_restore");
    seam58.detach(restored && !seam58.faults() ? pse::seam::ExitDetached : pse::seam::Faulted);
    log("seam_world_exit","\"reason\":\"%s\",\"was_attached\":%s,\"seam_status\":%u",reason,wasAttached?"true":"false",seam58.status());
}
#endif
void __fastcall hookStateExit(void* self) {
    if(thread() && self==instance() && read<int>(instance(),0x24)==3 && read<int>(instance(),0x20)!=3) {
        // Leaving a gameplay world (restart, checkpoint reload, title, next level) is a world transition, not the end of the
        // process: hooks stay, the next gameplay world starts a fresh Session. Only DirectQuit is terminal.
        worldTransition("StateExit");
    }
    reinterpret_cast<Unary>(g.original[profile::StateExit])(self);
}
void __fastcall hookShutdown(void* self) {
#ifdef PSE_FEATURE_VISUAL_PROXY
    exit58("DirectQuit",2);
#endif
    stop("DirectQuit");g.shutdown=true;reinterpret_cast<Unary>(g.original[profile::Shutdown])(self); }
void __fastcall hookEngineShutdown(void* self) { log("engine_shutdown_entry","\"quiesced\":%s",g.stopped?"true":"false");reinterpret_cast<Unary>(g.original[profile::EngineShutdown])(self);log("engine_shutdown_return"); }
void __fastcall hookFinal() { log("client_native_final_exit","\"quiesced\":%s,\"failed\":%s,\"foreign_calls\":%llu,\"module_pinned\":true",g.stopped?"true":"false",g.failed?"true":"false",g.foreign);flushJournal();reinterpret_cast<Final>(g.original[profile::FinalExit])(); }
LONG CALLBACK exceptions(EXCEPTION_POINTERS* e) { const auto c=e->ExceptionRecord->ExceptionCode;if(c==EXCEPTION_ACCESS_VIOLATION || c==EXCEPTION_ILLEGAL_INSTRUCTION || c==EXCEPTION_STACK_OVERFLOW) { log("native_fault","\"code\":%lu,\"address\":\"%p\"",c,e->ExceptionRecord->ExceptionAddress);flushJournal(); }return EXCEPTION_CONTINUE_SEARCH; }
void CALLBACK bootstrap(ULONG_PTR) {
    if(!thread()) { block("bootstrap_affinity");return; }
    const std::array<std::pair<profile::Index,void*>,7> hooks{{{profile::Tick,reinterpret_cast<void*>(&hookTick)},{profile::Update,reinterpret_cast<void*>(&hookUpdate)},{profile::StateExit,reinterpret_cast<void*>(&hookStateExit)},{profile::Shutdown,reinterpret_cast<void*>(&hookShutdown)},{profile::EngineShutdown,reinterpret_cast<void*>(&hookEngineShutdown)},{profile::FinalExit,reinterpret_cast<void*>(&hookFinal)},{profile::Getter,nullptr}}};
    for(const auto& [i,h]:hooks) if(h && !prefix(i)) { block("exact_prefix");return; }
#ifdef PSE_FEATURE_P4
    if(!prefix(profile::Commands)||!prefix(profile::Turning) ||
       !std::equal(std::begin(background::ResponderPrefix),std::end(background::ResponderPrefix),g.base+background::FocusResponder) ||
       !std::equal(std::begin(background::GatePrefix),std::end(background::GatePrefix),g.base+background::FocusGate) ||
       !std::equal(std::begin(background::ResetPrefix),std::end(background::ResetPrefix),g.base+background::ResetBindings)) { block("p4_background_profile");return; }
#endif
    if(MH_Initialize()!=MH_OK) { block("minhook_init");return; }
    for(const auto& [i,h]:hooks) if(h) {
        if(MH_CreateHook(at(i),h,&g.original[i])!=MH_OK || MH_EnableHook(at(i))!=MH_OK) { block("hook_install");return; }g.enabled[i]=true;
    }
#ifdef PSE_FEATURE_P4
    for(const auto& h:{std::pair{profile::Commands,reinterpret_cast<void*>(&hookClientCommands)},std::pair{profile::Turning,reinterpret_cast<void*>(&hookClientTurning)}}) {
        if(MH_CreateHook(at(h.first),h.second,&g.original[h.first])!=MH_OK || MH_EnableHook(at(h.first))!=MH_OK) { block("p4_input_hooks");return; }g.enabled[h.first]=true;
    }
    if(MH_CreateHook(g.base+background::FocusResponder,reinterpret_cast<void*>(&hookClientFocus),&g.focusOriginal)!=MH_OK || MH_EnableHook(g.base+background::FocusResponder)!=MH_OK) { block("p4_focus_hook");return; }
#endif
#ifdef PSE_FEATURE_VISUAL_PROXY
    if(!pse::native_proxy::install(g.base,g.tid,nativeProxyJournal) || seam58.registerTable(&pse::native_proxy::table())!=ERROR_SUCCESS) { block("native_proxy_install");return; }
    log("seam_registered","\"result\":0,\"abi\":\"58b00001\",\"size\":72,\"owner\":\"native_cpp\",\"external_callbacks\":false,\"before_permit\":true");
#endif
    if(!pse::world::install(g.base,g.tid,log,false)) {block("world_hook_install");return;}
    if(!pse::native_ui::install(g.base,g.tid,false,log)) {block("p23_ui_install");return;}
    AddVectoredExceptionHandler(0,exceptions);log("bootstrap_ready","\"game_calls\":0,\"hooks\":6,\"module_pinned\":true");
}
bool within(const wchar_t* p,const wchar_t* root) { const auto n=wcslen(root);return _wcsnicmp(p,root,n)==0 && (p[n]==L'\\' || p[n]==L'\0'); }
bool hashMatches(const wchar_t* exe) {
    HANDLE f=CreateFileW(exe,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};Byte result[32]{},buf[65536]{};DWORD n{};
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0 && BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    while(ok) { if(!ReadFile(f,buf,sizeof(buf),&n,nullptr)) { ok=false;break; }if(!n) break;ok=BCryptHashData(hash,buf,n,0)>=0; }
    if(ok) ok=BCryptFinishHash(hash,result,sizeof(result),0)>=0;
    if(hash) BCryptDestroyHash(hash);if(alg) BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);
    char hex[65]{};for(unsigned i=0;i<32;++i) sprintf_s(hex+2*i,3,"%02x",result[i]);return ok && strcmp(hex,profile::Sha256)==0;
}
}
extern "C" DWORD WINAPI client_M11Prepare(const wchar_t* logfile,DWORD tid) {
    if(g.prepared.exchange(1)) return ERROR_ALREADY_INITIALIZED;
    wchar_t exe[32768]{},dll[32768]{},path[32768]{},installRoot[32768]{};
    // P27: pe-play (PSE_GUARD_ROOT) selects the portable install-root policy; otherwise the unchanged lab policy.
    const bool portable=pse::portable::root(installRoot,32768);
    const wchar_t* allowed=portable?installRoot:profile::Workspace;
    if(!GetModuleFileNameW(nullptr,exe,32768) || !(portable?pse::portable::exeMatches(exe,installRoot):_wcsicmp(exe,ClientExe)==0) || (portable && !within(exe,allowed)) || !hashMatches(exe)) return ERROR_BAD_EXE_FORMAT;
    if(!GetModuleFileNameW(g.module,dll,32768) || !within(dll,allowed)) return ERROR_BAD_PATHNAME;
    const DWORD length=GetFullPathNameW(logfile,32768,path,nullptr);if(!length || length>=32768 || !within(path,allowed)) return ERROR_BAD_PATHNAME;
    HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&client_M11Prepare),&pinned)) return GetLastError();
    g.base=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr));g.tid=tid;
    // Permit the read-only isolation/acceptance observer while retaining sole
    // write ownership. Secure fopen's default sharing blocked the smoke gate.
    g.journal=_wfsopen(path,L"wx",_SH_DENYWR);
    journalWriter.bind(&journalSink);
    if(!g.journal) return ERROR_OPEN_FAILED;std::setvbuf(g.journal,g.buffer,_IOFBF,sizeof(g.buffer));
    log("client_dll_prepared","\"binary_sha256\":\"%s\",\"image_base\":\"%p\",\"expected_primary_tid\":%lu,\"player_writes\":0,\"kex_calls\":0",profile::Sha256,g.base,tid);
    HANDLE th=OpenThread(THREAD_SET_CONTEXT|THREAD_QUERY_LIMITED_INFORMATION,FALSE,tid);if(!th) return GetLastError();
    if(GetProcessIdOfThread(th)!=GetCurrentProcessId()) { CloseHandle(th);return ERROR_INVALID_PARAMETER; }
    const DWORD queued=QueueUserAPC(bootstrap,th,0);const DWORD result=queued?ERROR_SUCCESS:GetLastError();CloseHandle(th);return result;
}
extern "C" DWORD WINAPI client_M11PermitTest() { pse::native_ui::permit();g.permit.store(1,std::memory_order_release);return ERROR_SUCCESS; }
#ifdef PSE_FEATURE_VISUAL_PROXY
// Frida-thread exports: they only publish atomics; every proxy/KEX action happens on the simulation thread.
extern "C" DWORD WINAPI client_PSE58B_RegisterSeam(const pse::seam::SeamTable* t) {
    // The P1 adapter is internal. Retain the export for an explicit, machine-checkable refusal.
    (void)t;return ERROR_NOT_SUPPORTED;
}
extern "C" DWORD WINAPI client_PSE_P1_PermitProxySpawn() { pse::native_proxy::permitSpawn();return ERROR_SUCCESS; }
#ifdef PSE_FEATURE_P8
extern "C" DWORD WINAPI client_PSE_P8_Preview(unsigned slot,unsigned pose) { return pse::native_proxy::previewAnimation(slot,pose); }
#endif
extern "C" DWORD WINAPI client_PSE_P1_ReadObservation(pse::native_proxy::Observation* out,std::uint32_t size) { return pse::native_proxy::observe(out,size); }
extern "C" DWORD WINAPI client_PSE_P1_IsPositionInterpolant(const void* candidate) { return pse::native_proxy::isPositionInterpolant(candidate) ? 1u : 0u; }
extern "C" DWORD WINAPI client_PSE58B_RequestProxyShutdown() { proxyShutdown58.store(1,std::memory_order_release);return ERROR_SUCCESS; }
extern "C" DWORD WINAPI client_PSE58B_RequestDetach() { detach58.store(1,std::memory_order_release);return ERROR_SUCCESS; }
// seam status | proxies quiescent<<8 | (slot proxy state+1)<<16, as last published by the simulation thread.
extern "C" DWORD WINAPI client_PSE58B_Status() { return seam58.status()|status58.load(std::memory_order_acquire); }
#endif
extern "C" DWORD WINAPI client_PSE_P7_RequestKeyProbe() {p7ProbeRequest.store(1,std::memory_order_release);return ERROR_SUCCESS;}
extern "C" DWORD WINAPI client_PSE_P16_PermitRestore(std::uint64_t probe,std::uint64_t review){
    if(!probe||!review)return ERROR_INVALID_PARAMETER;
    approvedReview16.store(0,std::memory_order_release);approvedProbe16.store(probe,std::memory_order_release);approvedReview16.store(review,std::memory_order_release);return ERROR_SUCCESS;
}
extern "C" DWORD WINAPI client_PSE_P7_PermitKeys(std::uint64_t probe,std::uint64_t review) {pse::self_native_p7::permitKeys(probe,review);return ERROR_SUCCESS;}
extern "C" DWORD WINAPI client_PSE_P7_LocalDamage(int type,int amount) {
    if(type<0||type>3||amount<1||amount>32767)return ERROR_INVALID_PARAMETER;
    if(p7LocalDamageRequest.load(std::memory_order_acquire))return ERROR_BUSY;
    p7LocalDamageType.store(type);p7LocalDamageAmount.store(amount);p7LocalDamageRequest.store(1,std::memory_order_release);return ERROR_SUCCESS;
}
extern "C" void client_attach(HINSTANCE module) { g.module=module; }

#ifdef PSE_FEATURE_P4
// Explicit test-only atomic; background mode defaults off. No KEX calls from the caller's thread.
extern "C" DWORD WINAPI client_PSE_P4_EnableBackground(DWORD enabled) { g.backgroundPermit.store(enabled!=0,std::memory_order_release);return ERROR_SUCCESS; }
#endif
