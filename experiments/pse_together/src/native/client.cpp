// Unified client: P4 background, P7 self state, P16 respawn, P23 UI, proxy of the host player.
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
#include <cstdio>
#include <cstdarg>
#include <cwchar>
#include <share.h>
#include <pse_together/config.hpp>
#include "build_profile.hpp"
#include "self_native_p16.hpp"
#include "lifecycle_p16.hpp"
#include "respawn_p16.hpp"
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
struct Context {
    HMODULE module{}; Byte* base{}; DWORD tid{};
    FILE* journal{}; SRWLOCK logLock=SRWLOCK_INIT; char buffer[65536]{};
    std::atomic<unsigned> prepared{},permit{};
    void* original[profile::Count]{}; bool enabled[profile::Count]{};
    bool inTick{},stopped{},shutdown{},failed{},attested{};
    unsigned localThisTick{}; std::uint64_t ticks{},localUpdates{},ordinal{},logSeq{},lastFlush{},foreign{},reconnectKey{},worldSerial{1};
    std::uint32_t buttons{},forwardHeld{},backHeld{}; bool sampled{};
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
void log(const char* event,const char* fmt="",...) {
    if(!g.journal) return;
    char detail[2200]{};va_list args;va_start(args,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args);va_end(args);
    const auto ns=steadyNs();SYSTEMTIME utc{};GetSystemTime(&utc);
    AcquireSRWLockExclusive(&g.logLock);
    std::fprintf(g.journal,"{\"event\":\"%s\",\"record\":%llu,\"pid\":%lu,\"tid\":%lu,\"steady_ns\":%llu,\"utc\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\"%s%s}\n",event,++g.logSeq,GetCurrentProcessId(),GetCurrentThreadId(),ns,utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds,detail[0]?",":"",detail);
    if(ns-g.lastFlush>=500000000ULL) { std::fflush(g.journal);g.lastFlush=ns; }
    ReleaseSRWLockExclusive(&g.logLock);
}
void block(const char* reason) { g.failed=true;log("blocked","\"reason\":\"%s\",\"retry\":false",reason); }
bool prefix(profile::Index i) { return std::equal(std::begin(profile::Sites[i].prefix),std::end(profile::Sites[i].prefix),static_cast<Byte*>(at(i))); }
void flushJournal() { AcquireSRWLockExclusive(&g.logLock);std::fflush(g.journal);ReleaseSRWLockExclusive(&g.logLock); }
#include "client_self_p16.inc"
#include "client_respawn_p16.inc"
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

// Only the actual KEX simulation thread owns both SIMULATION bridge operations.
// Worker uses DTOs only and independently schedules the latest sample at 60 Hz.
struct Session {
    pse::ThreadBridge bridge;
    pse::ENetWorker worker{bridge,pse::ENetWorkerMode::CLIENT};
    bool started{},stopped{},active{},readySent{},baseline{},reconnectPending{};
    std::uint8_t slot{};std::uint32_t gen{},epoch{};
    std::uint64_t lastReconnectNs{};
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
        const auto state=mapGate.state();
        if(fault!=lastFault || state!=lastState || g.ticks%600==0)
            log("client_map_status","\"tick\":%llu,\"fault\":\"%s\",\"local\":\"%s\",\"host\":\"%s\",\"state\":\"%s\",\"stable_ticks\":%u,\"epoch\":%u,\"generation\":%u,\"count_a\":%llu,\"count_b\":%llu",g.ticks,map56::text(fault),id.name.c_str(),mapGate.hostName().c_str(),map56::ClientGate::text(state),mapGate.stableTicks(),mapGate.mapEpoch(),mapGate.connection(),id.countA,id.countB);
        if(state!=lastState && state==map56::ClientGate::State::Mismatch) block("native_map_mismatch_no_ready");
        if(state!=lastState && state==map56::ClientGate::State::Invalidated) {
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
    pse::proxy::SnapshotRouter<1> proxies;
    std::uint64_t routeCounts[6]{};bool lastSafe{},shutdownApplied{},adapterRemoveApplied{};std::uint64_t deferredLogged{},notSafeLogged{};
    void logProxies(const char* why) {
        bool any=false;
        proxies.forEach([&](pse::proxy::VisualProxyStateMachine& p) { any=true;
            log("proxy_state","\"why\":\"%s\",\"slot\":%u,\"state\":\"%s\",\"quarantine\":\"%s\",\"handle\":%llu,\"orphaned_handle\":%llu,\"accepted\":%llu,\"spawn_calls\":%u,\"update_calls\":%u,\"remove_calls\":%u,\"retired\":%u,\"affinity_violations\":%u,\"reentrancy_blocked\":%u,\"unload_during_seam\":%u,\"barrier\":%s,\"shutdown\":%s,\"map_ready\":%s,\"local_generation\":%u,\"entity\":%u,\"entity_generation\":%u",
                why,p.targetSlot(),toString(p.state()),toString(p.quarantineReason()),p.handle(),p.orphanedHandle(),p.accepted(),p.spawnCalls(),p.updateCalls(),p.removeCalls(),p.retiredCount(),p.affinityViolations(),p.reentrancyBlocked(),p.unloadDuringSeam(),p.teardownBarrier()?"true":"false",p.shutdownLatched()?"true":"false",p.mapReady()?"true":"false",p.localGeneration(),p.entityId(),p.entityGeneration()); });
        if(!any) log("proxy_state","\"why\":\"%s\",\"proxies\":0",why);
        log("proxy_route_counts","\"why\":\"%s\",\"routed\":%llu,\"local_excluded\":%llu,\"invalid_slot\":%llu,\"capacity_deferred\":%llu,\"proxy_rejected\":%llu,\"no_local_slot\":%llu",why,routeCounts[0],routeCounts[1],routeCounts[2],routeCounts[3],routeCounts[4],routeCounts[5]);
    }
    void barrier58(const char* why) { proxies.forEach([](pse::proxy::VisualProxyStateMachine& p) { p.onPreUnload(); });log("proxy_teardown_barrier","\"reason\":\"%s\",\"native_calls_after\":\"none_permitted\"",why);logProxies(why); }
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
        if((ready&pse::seam::AdapterRequestsRemove) && !adapterRemoveApplied) {
            adapterRemoveApplied=true;proxies.forEach([](VisualProxyStateMachine& p) { p.requestRemove(); });log("proxy_adapter_remove_requested","\"ready_flags\":%d",ready);logProxies("adapter_remove_request");
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
        unsigned st=0;proxies.forEach([&](VisualProxyStateMachine& p) { st=static_cast<unsigned>(p.state())+1; });
        status58.store((proxies.quiescent() ? 0x100u : 0u)|(st<<16),std::memory_order_release);
    }
#endif
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
        pse::ClientConfig cfg;cfg.hostAddress=menu.address;cfg.hostPort=*pse::ui::port(menu.portText);cfg.joinToken=menu.token;cfg.participantId="p23_client_"+std::to_string(GetCurrentProcessId());cfg.displayName=menu.displayName;cfg.reconnectKey=g.reconnectKey;
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
        for(std::size_t n=0;n<pse::THREAD_BRIDGE_INBOUND_CAPACITY && bridge.pollInbound(m);++n) {
            if(const auto* e=std::get_if<pse::BridgeSessionEvent>(&m)) {
                log("client_session","\"type\":%u,\"slot\":%u,\"generation\":%u,\"epoch\":%u",static_cast<unsigned>(e->type),e->slotId,e->connectionGeneration,e->mapEpoch);
                if(e->type==pse::BridgeSessionEvent::Type::CLIENT_JOINED || e->type==pse::BridgeSessionEvent::Type::RECONNECTED) {
                    if(e->connectionGeneration<=gen) continue;
#ifdef PSE_FEATURE_MAPREADY
                    #ifdef PSE_FEATURE_P4
                    if(!map111::matches(localMap,e->mapName,e->mapStructure)) { active=readySent=baseline=false;block("welcome_native_map_structure_v111");continue; }
                    #endif
                    if(!mapGate.welcome(e->slotId,e->connectionGeneration,e->mapEpoch,e->mapName)) { block("welcome_map_identity_or_epoch");continue; }
                    slot=e->slotId;gen=e->connectionGeneration;epoch=e->mapEpoch;active=true;readySent=baseline=false;reconnectPending=false;
                    if(e->reconnectKey) g.reconnectKey=e->reconnectKey; // a later session of this process rejoins the same slot
                    snapshots.reset(gen,epoch);
                    log("client_welcome","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"host_map\":\"%s\",\"local_map\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,mapGate.hostName().c_str(),localMap.name.c_str());
#ifdef PSE_FEATURE_VISUAL_PROXY
                    proxies.configureSession(slot,gen,epoch);log("proxy_session_configured","\"local_slot\":%u,\"generation\":%u,\"epoch\":%u,\"spawn_enabled_by_session\":false,\"native_capacity\":1",slot,gen,epoch);
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
                    snapshots.reset(gen,epoch);
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
                    active=readySent=baseline=false;block("session_rejected");
#ifdef PSE_FEATURE_VISUAL_PROXY
                    disconnect58("REJECTED");
#endif
                }
            } else if(const auto* map=std::get_if<pse::BridgeMapChange>(&m)) {
                if(map->connectionGeneration!=gen) continue;
#ifdef PSE_FEATURE_P4
                if(!map111::matches(localMap,map->mapName,map->mapStructure)) { readySent=baseline=false;block("map_change_native_structure_v111");continue; }
#endif
#ifdef PSE_FEATURE_MAPREADY
                if(!mapGate.mapChange(map->connectionGeneration,map->newMapEpoch,map->mapName)) {
                    readySent=baseline=false;block("unsupported_map_change");
#ifdef PSE_FEATURE_VISUAL_PROXY
                    barrier58("unsupported_map_change");
#endif
                }
#else
                if(map->mapName!=Domain || map->newMapEpoch!=epoch) block("unsupported_map_domain");
#endif
            } else if(const auto* b=std::get_if<pse::BridgeWorldBaseline>(&m)) {
                if(b->connectionGeneration!=gen) continue;
#ifdef PSE_FEATURE_MAPREADY
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
#ifdef PSE_FEATURE_SNAPSHOT
            } else if(const auto* c=std::get_if<pse::BridgeRespawnConfirm>(&m)) {
                if(!active||!readySent||!baseline||c->mapEpoch!=epoch||c->slotId>=2||!c->deathSequence||
                   c->newEntityId!=0x5500u+c->slotId||c->newGeneration!=expectedGeneration16[c->slotId]+1||c->defaultWeapons!=3||c->ammo!=60)continue;
                if(c->slotId==slot&&!stageLocal16(*c,slot,gen,epoch))continue;
                expectedGeneration16[c->slotId]=c->newGeneration;
                auto& prior=snapshots.slots[c->slotId];prior.entity=c->newEntityId;prior.generation=c->newGeneration;
                log("p16_generation_admitted","\"slot\":%u,\"epoch\":%u,\"generation\":%u,\"death\":%u",c->slotId,epoch,c->newGeneration,c->deathSequence);
            } else if(const auto* s=std::get_if<pse::BridgePlayerSnapshot>(&m)) {
                if(s->slotId>=2||s->entityGeneration!=expectedGeneration16[s->slotId]){
                    log("p16_snapshot_generation_wait","\"slot\":%u,\"generation\":%u",s->slotId,s->entityGeneration);continue;
                }
#ifdef PSE_FEATURE_MAPREADY
                const bool mapEligible=mapGate.snapshotEligible(s->connectionGeneration,s->mapEpoch);
#else
                const bool mapEligible=true;
#endif
                const auto verdict=snapshots.accept(*s,active && readySent && baseline && gameplay() && !g.failed && !g.stopped && mapEligible);
                if(verdict==snapshot55::Verdict::EntityMismatch || verdict==snapshot55::Verdict::Nonfinite) { block("snapshot_unexplained_entity_or_transform");continue; }
                if(verdict!=snapshot55::Verdict::Accept) { log("native_snapshot_rejected","\"slot\":%u,\"snapshot_seq\":%u,\"reason\":%u",s->slotId,s->sequence,static_cast<unsigned>(verdict));continue; }
                lastDeath16[s->slotId]=s->deathSequence;lastDead16[s->slotId]=s->lifeState==pse::LIFE_DEAD;
                if(s->slotId==slot){hostDeath16=s->deathSequence;hostDead16=s->lifeState==pse::LIFE_DEAD;p7Offer(*s,slot,active&&readySent&&baseline);}
                log("native_snapshot_received","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"bits\":[%u,%u,%u,%u,%u],\"player_writes\":0",
                    s->slotId,s->sequence,s->serverTick,s->mapEpoch,s->connectionGeneration,s->entityId,s->entityGeneration,s->posX,s->posY,s->posZ,s->yaw,s->pitch,snapshot55::bits(s->posX),snapshot55::bits(s->posY),snapshot55::bits(s->posZ),snapshot55::bits(s->yaw),snapshot55::bits(s->pitch));
#ifdef PSE_FEATURE_VISUAL_PROXY
                route58(*s);
#endif
#endif
            }
        }
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
        if(active && !readySent && !g.failed && gameplay() && g.localThisTick==1 && mapGate.shouldSendReady()) {
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
                    if(!requestPending16){if(!sn7::quiesceForRespawn()){block("p16_request_quiesce");return;}p7Authority.disconnect();p7Admitted=false;}
                    requestPending16=true;log("p16_E_request_sent","\"slot\":%u,\"connection\":%u,\"epoch\":%u,\"death\":%u",slot,gen,epoch,out.deathSequence);
                }
            }
        }
        pse::BridgeClientInputSample s;s.slotId=slot;s.connectionGeneration=gen;s.sampleOrdinal=++g.ordinal;s.sampledSteadyNs=steadyNs();s.simulationTick=g.ticks;
        s.valid=valid && baseline;s.moveForward=s.valid ? (g.forwardHeld?1.0f:0.0f)-(g.backHeld?1.0f:0.0f) : 0.0f;
        const bool queued=pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{s}));
        log("client_input_sampled","\"slot\":%u,\"generation\":%u,\"sample_ordinal\":%llu,\"tick\":%llu,\"sampled_ns\":%llu,\"valid\":%s,\"kex_buttons\":%u,\"held_forward\":%u,\"held_back\":%u,\"forward\":%.1f,\"bridge_queued\":%s",slot,gen,s.sampleOrdinal,g.ticks,s.sampledSteadyNs,s.valid?"true":"false",g.buttons,g.forwardHeld,g.backHeld,s.moveForward,queued?"true":"false");
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
// A level restart or checkpoint reload keeps the process and every hook alive, but it is a NEW world: the engine purges the
// old proxy Actor itself. Close the old network session, reset every per-world latch (self-state authority, respawn gate,
// proxy adapter) and let a fresh Session rejoin with the saved reconnect key when the next gameplay world is stable.
void worldTransition(const char* reason) {
    if(!thread() || g.stopped) return;
    ++g.worldSerial;
    log("world_transition","\"reason\":\"%s\",\"world\":%llu",reason,g.worldSerial);
    if(g.session) g.session->barrier58(reason);
    pse::native_proxy::exitBarrier(1);
    if(!restart16.restore(log)) block("p16_restart_restore");
    input16.invalidate();requestPending16=newLocal16=protect16=false;stagedConfirm16.reset();
    p7Authority=ss7::Authority{};p7Admitted=false;pse::self_native_p7::invalidate();
    p7LifetimeClosed=false;p7PinnedActor=p7PinnedNode=0;p7PinnedAllocation=0;
    inputBound16=false;hostDead16=false;hostDeath16=0;localGeneration16=1;startValid16=false;
    lastDeath16={};lastDead16={};expectedGeneration16={1,1};
    // The in-game Together UI stays up: the menu request is kept so the next gameplay world rejoins on its own.
    if(g.session) g.session->stop(reason);
    g.session=nullptr; // the stopped Session is retained (never freed): its worker has joined, nothing references it
    if(!pse::native_proxy::rearm()) block("native_proxy_rearm");
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
    if(read<std::int16_t>(actor,0x17a)<=0 || (read<unsigned>(actor,0x1c0)&0x200u)) return;
#ifdef PSE_FEATURE_P4
    if(g.neutral) neutralInput();
#endif
    g.buttons=read<std::uint32_t>(p);g.forwardHeld=read<std::uint32_t>(p,0xc);g.backHeld=read<std::uint32_t>(p,0x10);g.sampled=true;
}
void __fastcall hookTick(void* self) {
    if(g.stopped) { reinterpret_cast<Unary>(g.original[profile::Tick])(self);return; }
    if(self!=instance() || !thread() || g.inTick) { ++g.foreign;block("tick_affinity_or_nested");reinterpret_cast<Unary>(g.original[profile::Tick])(self);return; }
    if(!g.attested) { g.attested=true;log("simulation_attested","\"expected_tid\":%lu,\"observed_tid\":%lu",g.tid,GetCurrentThreadId()); }
    g.inTick=true;++g.ticks;g.localThisTick=0;g.sampled=false;g.buttons=g.forwardHeld=g.backHeld=0;
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
            g.session->consume();
            if(g.session->mapGate.state()==map56::ClientGate::State::Invalidated) worldTransition("native_map_invalidated");
            if(g.session) {
            DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            g.session->sample(g.sampled && g.localThisTick==1 && gameplay() && !g.failed && !pse::ui::shared().capture.load() && foregroundPid==GetCurrentProcessId());
#ifdef PSE_FEATURE_VISUAL_PROXY
            if(seam58.attached()) g.session->phase58(seam58.worldReady(g.ticks));
#endif
            }
        }
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
