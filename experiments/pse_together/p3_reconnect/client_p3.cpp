// P3 / S1.1-S1.2 client DLL (target pse_dll_client_p3, PSE_FEATURE_P3). Fork of the accepted P1 client.cpp
// (DOC-61/64, all three client features) so that accepted P1 targets keep their exact machine code. P3 changes only:
//  * the proxy router is process-lifetime (pse::p3::RearmRouter): each Actor lifetime is one accepted DOC-58B.2
//    automaton instance; latches are never cleared inside an instance (proxy_rearm_p3.hpp, rules R1-R4);
//  * a client world serial (one per loaded level) and the host reconnect key survive the network Session;
//  * a ClientGate invalidation is a world transition (barrier), not a terminal block; a map mismatch only withholds
//    MapReady (journal p3_map_mismatch) and is not a barrier;
//  * StateExit of a gameplay world: barrier -> network Session stop -> original StateExit (KEX purges the proxy) ->
//    in-DLL teardown verdict -> world serial + 1. Hooks stay; the next gameplay world starts a new Session that
//    reconnects with the saved key (new host generation), and native MapReady of that session rearms the slot;
//  * PSE_P3_RequestReconnect: test-only atomic, the simulation thread pushes the accepted RECONNECT control.
// DirectQuit and process exit keep the P1 semantics (barrier, exact hook restore, seam ExitDetached, pinned module).
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
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
#if !defined(PSE_ROLE_CLIENT) || !defined(PSE_FEATURE_VISUAL_PROXY) || !defined(PSE_FEATURE_P3)
#error "client_p3.cpp is the P3 client DLL: PSE_ROLE_CLIENT + SNAPSHOT + MAPREADY + VISUAL_PROXY + P3"
#endif
#include "enet_worker.h"
#include "snapshot_contract.hpp"
#include "map_reader.hpp"
#include "seam.hpp"
#include "native_proxy_p3.hpp"
#include "proxy_rearm_p3.hpp"

namespace {
namespace np=pse::native_proxy_p3;
using Byte=unsigned char;
using Unary=void(__fastcall*)(void*);
using Final=void(__fastcall*)();
using Clock=std::chrono::steady_clock;
constexpr wchar_t ClientExe[]=LR"(S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\experiments\m1\native_second_instance\game\anubis_Shipping_Steam_x64.exe)";
std::uint64_t steadyNs() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count()); }
struct Session;
struct Context {
    HMODULE module{}; Byte* base{}; DWORD tid{};
    FILE* journal{}; SRWLOCK logLock=SRWLOCK_INIT; char buffer[65536]{};
    std::atomic<unsigned> prepared{},permit{};
    void* original[profile::Count]{}; bool enabled[profile::Count]{};
    bool inTick{},stopped{},shutdown{},failed{},attested{};
    unsigned localThisTick{}; std::uint64_t ticks{},localUpdates{},ordinal{},logSeq{},lastFlush{},foreign{},nextSessionTick{};
    std::uint32_t buttons{},forwardHeld{},backHeld{}; bool sampled{};
    Session* session{};
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
pse::seam::Seam seam58;
void nativeProxyJournal(const char* event,const char* detail) { log(event,"%s",detail); }
std::atomic<unsigned> proxyShutdown58{},detach58{},status58{},reconnectRequest58{},statusP3{};
bool detachDeferredLogged58{};std::uint64_t faultsLogged58{};
// P3 process-lifetime state (simulation thread only).
pse::p3::RearmRouter<1> router58;
std::uint64_t world58{1},reconnectKey58{},worldExits58{};
bool worldLostApplied58{},shutdownApplied58{};
pse::p3::PerIncarnationOnce adapterRemoveOnce58; // DOC-78 F2: the adapter's life-cap Remove request is once per incarnation
std::uint32_t lastIncarnation58{1};pse::p3::Closure lastClosure58{pse::p3::Closure::Open};
std::uint64_t mapToken58(const map56::Identity& id,std::uint32_t epoch,std::uint64_t world) {
    std::uint64_t h=1469598103934665603ULL;auto mix=[&](std::uint64_t v) { for(int i=0;i<8;++i) { h^=(v>>(i*8))&0xff;h*=1099511628211ULL; } };
    for(char ch:id.name) mix(static_cast<unsigned char>(ch));mix(id.countA);mix(id.countB);mix(id.version);mix(epoch);mix(world);return h ? h : 1;
}
void logRouter(const char* why) {
    bool any=false;
    router58.forEachSlot([&](pse::p3::RearmableProxy& r) { any=true;const auto* p=r.current();
        log("proxy_state","\"why\":\"%s\",\"slot\":%u,\"state\":\"%s\",\"quarantine\":\"%s\",\"handle\":%llu,\"orphaned_handle\":%llu,\"accepted\":%llu,\"spawn_calls\":%u,\"update_calls\":%u,\"remove_calls\":%u,\"retired\":%u,\"affinity_violations\":%u,\"reentrancy_blocked\":%u,\"unload_during_seam\":%u,\"barrier\":%s,\"shutdown\":%s,\"map_ready\":%s,\"local_generation\":%u,\"entity\":%u,\"entity_generation\":%u,"
            "\"incarnation\":%u,\"closure\":\"%s\",\"rearm_refusal\":\"%s\",\"rearms\":%u,\"total_spawn_calls\":%u,\"total_remove_calls\":%u,\"last_handle\":%llu,\"handle_violations\":%u,\"verdict_refused\":%u,\"session\":[%u,%u,%llu],\"pending\":[%u,%u,%llu],\"world\":%llu",
            why,r.targetSlot(),toString(p->state()),toString(p->quarantineReason()),p->handle(),p->orphanedHandle(),p->accepted(),p->spawnCalls(),p->updateCalls(),p->removeCalls(),p->retiredCount(),p->affinityViolations(),p->reentrancyBlocked(),p->unloadDuringSeam(),p->teardownBarrier()?"true":"false",p->shutdownLatched()?"true":"false",p->mapReady()?"true":"false",p->localGeneration(),p->entityId(),p->entityGeneration(),
            r.incarnation(),toString(r.closure()),toString(r.lastRefusal()),r.rearms(),r.totalSpawnCalls(),r.totalRemoveCalls(),r.lastHandle(),r.handleViolations(),r.verdictRefused(),r.currentKey().gen,r.currentKey().epoch,r.currentKey().world,r.pendingKey().gen,r.pendingKey().epoch,r.pendingKey().world,world58); });
    if(!any) log("proxy_state","\"why\":\"%s\",\"proxies\":0,\"world\":%llu",why,world58);
}
void publishStatus() {
    unsigned st=0,inc=0,cl=0;
    router58.forEachSlot([&](pse::p3::RearmableProxy& r) { st=static_cast<unsigned>(r.current()->state())+1;inc=r.incarnation();cl=static_cast<unsigned>(r.closure()); });
    status58.store((router58.quiescent() ? 0x100u : 0u)|(st<<16),std::memory_order_release);
    statusP3.store((inc&0xffu)|((cl&0xffu)<<8)|(static_cast<unsigned>(world58&0xffffu)<<16),std::memory_order_release);
}
// Barrier for the current world: automaton latch first, then the adapter (which arms the teardown witness).
void barrierWorld(const char* why,std::uint32_t code) {
    router58.onPreUnload(world58);np::exitBarrier(code);
    log("proxy_teardown_barrier","\"reason\":\"%s\",\"native_calls_after\":\"none_permitted\",\"world\":%llu",why,world58);logRouter(why);publishStatus();
}

// Only the actual KEX simulation thread owns both SIMULATION bridge operations.
// Worker uses DTOs only and independently schedules the latest sample at 60 Hz.
struct Session {
    pse::ThreadBridge bridge;
    pse::ENetWorker worker{bridge,pse::ENetWorkerMode::CLIENT};
    bool started{},stopped{},active{},readySent{},baseline{},reconnectPending{};
    std::uint8_t slot{};std::uint32_t gen{},epoch{};
    std::uint64_t lastReconnectNs{};
    snapshot55::Guard snapshots;
    map56::ClientGate mapGate;
    map56::Identity localMap;
    map56::Fault lastFault{map56::Fault::None};
    map56::ClientGate::State lastState{map56::ClientGate::State::Disconnected};
    std::uint64_t routeCounts[6]{};bool lastSafe{};std::uint64_t deferredLogged{},notSafeLogged{};
    // Local native observation each simulation tick. P3: invalidation/mismatch end this world's proxy, not the DLL.
    void observeMap() {
        map56::Identity id;const auto fault=map56::derive(map56::observe(g.base),id);
        mapGate.observe(fault,id);
        const auto state=mapGate.state();
        if(fault!=lastFault || state!=lastState || g.ticks%600==0)
            log("client_map_status","\"tick\":%llu,\"fault\":\"%s\",\"local\":\"%s\",\"host\":\"%s\",\"state\":\"%s\",\"stable_ticks\":%u,\"epoch\":%u,\"generation\":%u,\"count_a\":%llu,\"count_b\":%llu,\"world\":%llu",g.ticks,map56::text(fault),id.name.c_str(),mapGate.hostName().c_str(),map56::ClientGate::text(state),mapGate.stableTicks(),mapGate.mapEpoch(),mapGate.connection(),id.countA,id.countB,world58);
        if(state!=lastState && state==map56::ClientGate::State::Mismatch) { readySent=baseline=false;log("p3_map_mismatch","\"local\":\"%s\",\"host\":\"%s\",\"map_ready\":false,\"proxy_spawn\":false",id.name.c_str(),mapGate.hostName().c_str()); }
        if(state!=lastState && state==map56::ClientGate::State::Invalidated) { readySent=baseline=false;barrierWorld("native_map_invalidated",4); }
        lastFault=fault;lastState=state;
    }
    void disconnect58(const char* why) { router58.onDisconnect();log("proxy_disconnect","\"reason\":\"%s\",\"world\":%llu",why,world58);logRouter(why); }
    void route58(const pse::BridgePlayerSnapshot& s) {
        pse::proxy::Rejection rj{};const auto rr=router58.route(s,gameplay(),rj);++routeCounts[static_cast<unsigned>(rr)];
        auto* p=router58.proxy(s.slotId);
        log("proxy_snapshot_routed","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"route\":\"%s\",\"rejection\":\"%s\",\"proxy_state\":\"%s\",\"coalesced\":%u,\"pos_bits\":[%u,%u,%u],\"yaw_bits\":%u,\"local_this_tick\":%u,\"tick\":%llu,\"kex_actor_access\":false",
            s.slotId,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,toString(rr),toString(rj),p ? toString(p->state()) : "none",
            p && rr==pse::proxy::RouteResult::Routed ? p->target().coalesced : 0u,snapshot55::bits(s.posX),snapshot55::bits(s.posY),snapshot55::bits(s.posZ),snapshot55::bits(s.yaw),g.localThisTick,g.ticks);
    }
    // StateTick.onLeave-equivalent phase on the attested simulation thread, after consume().
    void phase58(std::int32_t ready) {
        using namespace pse::proxy;
        if(np::worldLost() && !worldLostApplied58) { worldLostApplied58=true;router58.onPreUnload(world58);log("p3_adapter_world_lost_barrier","\"world\":%llu",world58);logRouter("adapter_world_lost"); }
        if(proxyShutdown58.load(std::memory_order_acquire) && !shutdownApplied58) {
            shutdownApplied58=true;router58.requestShutdown();log("proxy_shutdown_requested","\"source\":\"PSE58B_RequestProxyShutdown\"");logRouter("shutdown_request");
        }
        if(ready&pse::seam::AdapterRequestsRemove) {
            std::uint32_t inc=0;router58.forEachSlot([&](pse::p3::RearmableProxy& r) { inc=r.incarnation(); });
            if(adapterRemoveOnce58.take(inc)) { router58.requestRemove();log("proxy_adapter_remove_requested","\"ready_flags\":%d,\"incarnation\":%u",ready,inc);logRouter("adapter_remove_request"); }
        }
        const bool step=gameplay() && g.localThisTick==1 && !g.failed && (ready&pse::seam::LocalTicked) && (ready&pse::seam::AdapterHealthy);
        const TickContext ctx{step,(ready&pse::seam::StableWorld)!=0};
        if(step!=lastSafe) { lastSafe=step;log("proxy_safe_phase","\"safe\":%s,\"ready_flags\":%d,\"local_this_tick\":%u,\"gameplay\":%s,\"tick\":%llu",step?"true":"false",ready,g.localThisTick,gameplay()?"true":"false",g.ticks); }
        router58.forEachSlot([&](pse::p3::RearmableProxy& r) {
            const auto before=r.current()->state();const ProxyCommand t=r.current()->target();const auto incBefore=r.incarnation();
            const auto a=r.tick(&seam58,ctx);
            auto& p=*r.current();
            if(r.incarnation()!=lastIncarnation58 || r.closure()!=lastClosure58) {
                log("p3_slot_lifecycle","\"slot\":%u,\"incarnation_before\":%u,\"incarnation\":%u,\"closure\":\"%s\",\"refusal\":\"%s\",\"rearms\":%u,\"world\":%llu,\"session\":[%u,%u,%llu],\"tick\":%llu",r.targetSlot(),lastIncarnation58,r.incarnation(),toString(r.closure()),toString(r.lastRefusal()),r.rearms(),world58,r.currentKey().gen,r.currentKey().epoch,r.currentKey().world,g.ticks);
                lastIncarnation58=r.incarnation();lastClosure58=r.closure();
            }
            if(a==TickAction::None || (a==TickAction::NotSafe && ++notSafeLogged%600!=1) || (a==TickAction::SpawnDeferred && ++deferredLogged%120!=1)) return;
            log("proxy_tick_action","\"slot\":%u,\"action\":\"%s\",\"state_before\":\"%s\",\"state_after\":\"%s\",\"quarantine\":\"%s\",\"handle\":%llu,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"coalesced\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pos_bits\":[%u,%u,%u],\"yaw_bits\":%u,\"tick\":%llu,\"safe\":%s,\"sectors_ready\":%s,\"spawn_calls\":%u,\"update_calls\":%u,\"remove_calls\":%u,\"retired\":%u,\"local_generation\":%u,\"incarnation\":%u,\"incarnation_at_entry\":%u,\"world\":%llu",
                p.targetSlot(),toString(a),toString(before),toString(p.state()),toString(p.quarantineReason()),p.handle() ? p.handle() : p.orphanedHandle(),t.sequence,t.serverTick,t.mapEpoch,t.connectionGeneration,t.entityId,t.entityGeneration,t.coalesced,t.x,t.y,t.z,t.yaw,
                snapshot55::bits(t.x),snapshot55::bits(t.y),snapshot55::bits(t.z),snapshot55::bits(t.yaw),g.ticks,ctx.simulationStep?"true":"false",ctx.sectorsReady?"true":"false",p.spawnCalls(),p.updateCalls(),p.removeCalls(),p.retiredCount(),p.localGeneration(),r.incarnation(),incBefore,world58);
        });
        publishStatus();
    }
    bool start() {
        bridge.registerSimulationThread();
        router58.bind(g.tid);
        {
            const auto observed=map56::observe(g.base);
            const auto fault=map56::derive(observed,localMap);
            log("client_map_identity_observed","\"fault\":\"%s\",\"state\":%d,\"next\":%d,\"pending\":%d,\"name_length\":%llu,\"name_capacity\":%llu,\"map\":\"%s\",\"mapinfo\":\"%p\",\"mapinfo_key\":\"%s\",\"level_vtable\":%s,\"psm_version\":%u,\"count_a\":%llu,\"count_b\":%llu,\"source\":\"GameInstance+1e0 kexStr / +1f8 mapInfo / level a45940\",\"world\":%llu",
                map56::text(fault),observed.state,observed.next,observed.pending,observed.nameLength,observed.nameCapacity,localMap.name.c_str(),reinterpret_cast<void*>(observed.mapInfo),fault==map56::Fault::None ? observed.key.data() : "",observed.levelVtable ? "true" : "false",observed.version,observed.countA,observed.countB,world58);
            if(fault!=map56::Fault::None) { bridge.unregisterSimulationThread();log("p3_session_deferred","\"reason\":\"native_map_identity_not_ready\",\"fault\":\"%s\"",map56::text(fault));return false; }
            mapGate.observe(fault,localMap);
        }
        worker.setNativePlayerSnapshots(true);
        worker.setPlayerSnapshotObserver([](bool sending,const pse::BridgePlayerSnapshot& s,std::size_t bytes) {
            log(sending ? "snapshot_worker_send" : "snapshot_worker_receive","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"bytes\":%zu,\"opcode\":32,\"channel\":1,\"kex_reads\":0",s.slotId,s.sequence,s.serverTick,s.mapEpoch,s.connectionGeneration,s.entityId,s.entityGeneration,bytes);
        });
        worker.setClientInputCadence(true);
        worker.setClientInputObserver([](const pse::ClientInputDispatch& d) {
            log("client_input_dispatched","\"slot\":%u,\"generation\":%u,\"input_seq\":%u,\"sample_ordinal\":%llu,\"sample_tick\":%llu,\"sampled_ns\":%llu,\"generated_ns\":%llu,\"forward\":%.1f,\"fresh\":%s,\"stage\":\"enet_dispatch_not_udp_proof\"",d.slotId,d.generation,d.sequence,d.sampleOrdinal,d.simulationTick,d.sampledSteadyNs,d.generatedSteadyNs,d.forward,d.sampleFresh?"true":"false");
        });
        pse::ClientConfig cfg;cfg.hostPort=28745;cfg.joinToken="M12A_Loopback";cfg.participantId="native_second_instance_p2";cfg.displayName="KEX_Input_Client";
        // P3: the host keeps slot 1 reserved for this participant; a later Session must present its reconnect key.
        cfg.reconnectKey=reconnectKey58;
        if(!worker.startClient(cfg)) { bridge.unregisterSimulationThread();block("client_worker_start");return false; }
        started=true;log("client_worker_started","\"target_hz\":60,\"freshness_ms\":100,\"protocol\":272,\"world\":%llu,\"reconnect_key_present\":%s",world58,reconnectKey58?"true":"false");return true;
    }
    void consume() {
        bridge.assertSimulationThread();pse::BridgeMessage m;
        observeMap();
        for(std::size_t n=0;n<pse::THREAD_BRIDGE_INBOUND_CAPACITY && bridge.pollInbound(m);++n) {
            if(const auto* e=std::get_if<pse::BridgeSessionEvent>(&m)) {
                log("client_session","\"type\":%u,\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"world\":%llu",static_cast<unsigned>(e->type),e->slotId,e->connectionGeneration,e->mapEpoch,world58);
                if(e->type==pse::BridgeSessionEvent::Type::CLIENT_JOINED || e->type==pse::BridgeSessionEvent::Type::RECONNECTED) {
                    if(e->connectionGeneration<=gen) continue;
                    if(mapGate.terminal()) { mapGate=map56::ClientGate{};lastState=map56::ClientGate::State::Disconnected;log("p3_map_gate_renewed","\"reason\":\"terminal gate before a new generation\""); }
                    if(!mapGate.welcome(e->slotId,e->connectionGeneration,e->mapEpoch,e->mapName)) { block("welcome_map_identity_or_epoch");continue; }
                    slot=e->slotId;gen=e->connectionGeneration;epoch=e->mapEpoch;active=true;readySent=baseline=false;reconnectPending=false;
                    if(e->reconnectKey) reconnectKey58=e->reconnectKey;
                    snapshots.reset(gen,epoch);
                    log("client_welcome","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"host_map\":\"%s\",\"local_map\":\"%s\",\"world_synchronized\":false,\"world\":%llu,\"reconnect_key_saved\":%s",slot,gen,epoch,mapGate.hostName().c_str(),localMap.name.c_str(),world58,reconnectKey58?"true":"false");
                    router58.configureSession(slot,pse::p3::SessionKey{gen,epoch,world58});log("proxy_session_configured","\"local_slot\":%u,\"generation\":%u,\"epoch\":%u,\"world\":%llu,\"spawn_enabled_by_session\":false,\"native_capacity\":1",slot,gen,epoch,world58);
                } else if(e->type==pse::BridgeSessionEvent::Type::LOCAL_SESSION_LOST) {
                    active=readySent=baseline=false;reconnectPending=true;
                    mapGate.lost();
                    disconnect58("LOCAL_SESSION_LOST");
                } else if(e->type==pse::BridgeSessionEvent::Type::REJECTED) {
                    active=readySent=baseline=false;block("session_rejected");
                    disconnect58("REJECTED");
                }
            } else if(const auto* map=std::get_if<pse::BridgeMapChange>(&m)) {
                if(map->connectionGeneration!=gen) continue;
                if(!mapGate.mapChange(map->connectionGeneration,map->newMapEpoch,map->mapName)) {
                    readySent=baseline=false;block("unsupported_map_change");
                    barrierWorld("unsupported_map_change",5);
                }
            } else if(const auto* b=std::get_if<pse::BridgeWorldBaseline>(&m)) {
                if(b->connectionGeneration!=gen) continue;
                if(mapGate.terminal()) { log("p3_baseline_after_invalidation_ignored","\"generation\":%u,\"epoch\":%u",b->connectionGeneration,b->mapEpoch);continue; }
                if(!active || !readySent || b->totalBatches!=1 || b->batchIndex!=0 || !b->entities.empty() || !mapGate.baseline(b->connectionGeneration,b->mapEpoch,b->mapName)) { block("map_baseline_contract");continue; }
                baseline=true;log("client_input_domain_ready","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"map\":\"%s\",\"entities\":0,\"state\":\"%s\",\"world_synchronized\":false,\"world\":%llu",slot,gen,epoch,mapGate.hostName().c_str(),map56::ClientGate::text(mapGate.state()),world58);
                const auto token=mapToken58(localMap,epoch,world58);router58.onNativeMapReady(epoch,token,world58);
                log("proxy_native_map_ready","\"epoch\":%u,\"map\":\"%s\",\"identity_token\":\"%016llx\",\"gate_state\":\"%s\",\"count_a\":%llu,\"count_b\":%llu,\"generation\":%u,\"world\":%llu,\"source\":\"ClientGate Ready after native 30-tick identity; separate from Welcome\"",epoch,localMap.name.c_str(),token,map56::ClientGate::text(mapGate.state()),localMap.countA,localMap.countB,gen,world58);
                logRouter("native_map_ready");
            } else if(const auto* s=std::get_if<pse::BridgePlayerSnapshot>(&m)) {
                const bool mapEligible=mapGate.snapshotEligible(s->connectionGeneration,s->mapEpoch);
                const auto verdict=snapshots.accept(*s,active && readySent && baseline && gameplay() && !g.failed && !g.stopped && mapEligible);
                if(verdict==snapshot55::Verdict::EntityMismatch || verdict==snapshot55::Verdict::Nonfinite) { block("snapshot_unexplained_entity_or_transform");continue; }
                if(verdict!=snapshot55::Verdict::Accept) { log("native_snapshot_rejected","\"slot\":%u,\"snapshot_seq\":%u,\"reason\":%u",s->slotId,s->sequence,static_cast<unsigned>(verdict));continue; }
                log("native_snapshot_received","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"bits\":[%u,%u,%u,%u,%u],\"player_writes\":0",
                    s->slotId,s->sequence,s->serverTick,s->mapEpoch,s->connectionGeneration,s->entityId,s->entityGeneration,s->posX,s->posY,s->posZ,s->yaw,s->pitch,snapshot55::bits(s->posX),snapshot55::bits(s->posY),snapshot55::bits(s->posZ),snapshot55::bits(s->yaw),snapshot55::bits(s->pitch));
                route58(*s);
            }
        }
        if(bridge.consumeResyncReasons()) { active=readySent=baseline=false;reconnectPending=true; }
        // P3 test-only: a reconnect of the live session through the accepted RECONNECT control (worker reconnectClient).
        if(reconnectRequest58.load(std::memory_order_acquire) && active && !reconnectPending) {
            pse::BridgeControl c;c.type=pse::BridgeControl::Type::RECONNECT;c.slotId=slot;c.connectionGeneration=gen;
            if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{c}))) { reconnectRequest58.store(0,std::memory_order_release);lastReconnectNs=steadyNs();log("p3_reconnect_requested","\"generation\":%u,\"world\":%llu,\"source\":\"PSE_P3_RequestReconnect\"",gen,world58); }
        }
        if(reconnectPending && steadyNs()-lastReconnectNs>=1000000000ULL) {
            pse::BridgeControl c;c.type=pse::BridgeControl::Type::RECONNECT;c.slotId=slot;c.connectionGeneration=gen;
            if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{c}))) { reconnectPending=false;lastReconnectNs=steadyNs();log("client_reconnect_requested","\"generation\":%u",gen); }
        }
        // MapReady only after the authenticated host identity equals the locked local native
        // identity for StableTicks consecutive stable ticks: never on connection or a timer.
        if(active && !readySent && !g.failed && gameplay() && g.localThisTick==1 && mapGate.shouldSendReady()) {
            pse::BridgeMapReady r;r.slotId=slot;r.connectionGeneration=gen;r.mapEpoch=epoch;
            if(pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{r}))) { readySent=true;mapGate.readyQueued();log("client_map_ready_queued","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"map\":\"%s\",\"stable_ticks\":%u,\"tick\":%llu,\"world\":%llu",slot,gen,epoch,localMap.name.c_str(),mapGate.stableTicks(),g.ticks,world58); }
        }
    }
    void sample(bool valid) {
        if(!active || stopped) return;
        pse::BridgeClientInputSample s;s.slotId=slot;s.connectionGeneration=gen;s.sampleOrdinal=++g.ordinal;s.sampledSteadyNs=steadyNs();s.simulationTick=g.ticks;
        s.valid=valid && baseline;s.moveForward=s.valid ? (g.forwardHeld?1.0f:0.0f)-(g.backHeld?1.0f:0.0f) : 0.0f;
        const bool queued=pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{s}));
        log("client_input_sampled","\"slot\":%u,\"generation\":%u,\"sample_ordinal\":%llu,\"tick\":%llu,\"sampled_ns\":%llu,\"valid\":%s,\"kex_buttons\":%u,\"held_forward\":%u,\"held_back\":%u,\"forward\":%.1f,\"bridge_queued\":%s",slot,gen,s.sampleOrdinal,g.ticks,s.sampledSteadyNs,s.valid?"true":"false",g.buttons,g.forwardHeld,g.backHeld,s.moveForward,queued?"true":"false");
    }
    void stop(const char* reason) {
        if(!started || stopped) return;
        bridge.assertSimulationThread();sample(false);stopped=true;
        logRouter("session_stop");if(!router58.quiescent()) log("proxy_not_quiescent_at_stop","\"reason\":\"%s\",\"native_calls_after\":\"none_permitted\"",reason);
        log("client_stop_begin","\"reason\":\"%s\",\"world\":%llu",reason,world58);worker.stop();
        pse::BridgeMessage m;std::size_t retired{};while(bridge.pollInbound(m)) ++retired;
        const auto s=worker.getStats();log("client_worker_joined","\"running\":%s,\"retired_inbound\":%zu,\"retired_outbound\":%zu,\"affinity_violations\":%llu,\"pending_peak\":%llu,\"lost_on_stop\":%llu,\"poll_loops\":%llu,\"reconnects\":%llu",worker.isRunning()?"true":"false",retired,bridge.getOutboundOccupancy(),bridge.getAffinityViolationCount(),s.pendingPeak,s.lostOnStop,s.pollLoops,s.reconnects);
        bridge.unregisterSimulationThread();bridge.resetForNewSession();active=baseline=readySent=false;
    }
};
void stop(const char* reason) {
    if(!thread()) { ++g.foreign;block("stop_affinity");return; }
    if(g.stopped) return;
    if(g.session) g.session->stop(reason);
    g.stopped=true;
    if(g.enabled[profile::Update]) {
        const auto rc=MH_DisableHook(at(profile::Update));g.enabled[profile::Update]=false;
        log("client_control_restored","\"exact16\":%s,\"status\":%d",prefix(profile::Update)?"true":"false",rc);
        if(rc!=MH_OK || !prefix(profile::Update)) block("restore_update");
    }
    log("client_quiesced","\"reason\":\"%s\",\"pinned\":true,\"player_writes\":0",reason);flushJournal();
}
void __fastcall hookUpdate(void* p) {
    reinterpret_cast<Unary>(g.original[profile::Update])(p);
    if(p!=primary() || g.stopped) return;
    if(!thread() || !g.inTick) { ++g.foreign;block("update_affinity");return; }
    ++g.localUpdates;if(++g.localThisTick!=1) { block("duplicate_local_update");return; }
    if(!gameplay() || g.failed) return;
    Byte* node=read<Byte*>(p,0xd8);Byte* actor=node?read<Byte*>(node,0x28):nullptr;
    if(!actor || read<Byte*>(actor,0x10)!=node || read<void*>(actor,0x1b8)!=p || read<Byte*>(actor)!=g.base+0x52d198) { block("local_player_graph");return; }
    if(read<std::int16_t>(actor,0x17a)<=0 || (read<unsigned>(actor,0x1c0)&0x200u)) return;
    g.buttons=read<std::uint32_t>(p);g.forwardHeld=read<std::uint32_t>(p,0xc);g.backHeld=read<std::uint32_t>(p,0x10);g.sampled=true;
}
void __fastcall hookTick(void* self) {
    if(g.stopped) { reinterpret_cast<Unary>(g.original[profile::Tick])(self);return; }
    if(self!=instance() || !thread() || g.inTick) { ++g.foreign;block("tick_affinity_or_nested");reinterpret_cast<Unary>(g.original[profile::Tick])(self);return; }
    if(!g.attested) { g.attested=true;log("simulation_attested","\"expected_tid\":%lu,\"observed_tid\":%lu",g.tid,GetCurrentThreadId()); }
    g.inTick=true;++g.ticks;g.localThisTick=0;g.sampled=false;g.buttons=g.forwardHeld=g.backHeld=0;
    seam58.tickEnter(g.ticks);
    reinterpret_cast<Unary>(g.original[profile::Tick])(self);
    if(!g.stopped) {
        if(!g.session && g.permit.load(std::memory_order_acquire) && !g.failed && gameplay() && g.localThisTick==1 && g.ticks>=g.nextSessionTick) {
            g.session=new Session;
            // Identity not yet derivable (transition edge): retry after 60 ticks instead of failing closed for good.
            if(!g.session->start()) { delete g.session;g.session=nullptr;g.nextSessionTick=g.ticks+60; }
        }
        if(g.session && g.session->started && !g.session->stopped) {
            g.session->consume();DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            g.session->sample(g.sampled && g.localThisTick==1 && gameplay() && !g.failed && foregroundPid==GetCurrentProcessId());
            if(seam58.attached()) g.session->phase58(seam58.worldReady(g.ticks));
        }
        if((g.localUpdates==1 && g.localThisTick==1) || g.ticks<=3 || g.ticks%120==0)
            log("tick_status","\"ticks\":%llu,\"state\":%d,\"pending\":%d,\"local_updates\":%llu,\"local_this_tick\":%u,\"world\":%llu",g.ticks,read<int>(instance(),0x24),read<int>(instance(),0x28),g.localUpdates,g.localThisTick,world58);
    }
    seam58.tickLeave(g.ticks,g.localThisTick);
    if(detach58.load(std::memory_order_acquire) && seam58.attached()) {
        // The adapter (and its callbacks) may be unloaded only after every proxy is quiescent.
        if(router58.quiescent()) {
            if(np::detach()) { seam58.detach(pse::seam::Detached);log("seam_detached","\"tick\":%llu,\"proxies_quiescent\":true",g.ticks); }
            else block("native_proxy_detach_restore");
        }
        else if(!detachDeferredLogged58) { detachDeferredLogged58=true;log("seam_detach_deferred","\"tick\":%llu,\"reason\":\"proxy_not_quiescent\"",g.ticks); }
    }
    if(seam58.faults()!=faultsLogged58) { faultsLogged58=seam58.faults();np::fault();block("seam_native_fault");log("seam_native_fault","\"faults\":%llu,\"seam_status\":%u,\"further_calls\":false",faultsLogged58,seam58.status()); }
    g.inTick=false;
}
// DirectQuit / process exit (P1 semantics): latch first, tell the adapter, restore exactly, never call it again.
void exit58(const char* reason,std::uint32_t code) {
    if(!thread()) return;
    router58.onPreUnload(world58);logRouter(reason);
    const bool wasAttached=seam58.attached();
    if(wasAttached) seam58.worldExit(code);
    np::exitBarrier(code); // also invalidate after an earlier fault detached the seam
    const bool restored=np::detach();
    if(!restored) block("native_proxy_exit_restore");
    seam58.detach(restored && !seam58.faults() ? pse::seam::ExitDetached : pse::seam::Faulted);
    log("seam_world_exit","\"reason\":\"%s\",\"was_attached\":%s,\"seam_status\":%u",reason,wasAttached?"true":"false",seam58.status());
}
// P3 world exit: barrier before Level::Unload, network Session ends (host sees the peer leave), hooks stay installed.
void beginWorldExit() {
    log("p3_world_exit_begin","\"world\":%llu,\"state\":%d,\"next\":%d,\"pending\":%d,\"session\":%s,\"tick\":%llu",world58,read<int>(instance(),0x24),read<int>(instance(),0x20),read<int>(instance(),0x28),g.session?"true":"false",g.ticks);
    barrierWorld("StateExit",1);
    if(g.session) g.session->stop("StateExit");
}
void finishWorldExit() {
    const auto v=np::teardownVerdict(world58);
    bool accepted=false;
    bool closedDead=false;
    if(v.armed) accepted=router58.onTeardownVerdict(pse::p3::Verdict{v.handle,v.arena,v.queue});
    else closedDead=router58.onTeardownMissing(); // DOC-78 F4: an orphaned slot without any adapter verdict dies visibly
    log("p3_world_exit_done","\"world\":%llu,\"verdict_armed\":%s,\"arena\":\"%s\",\"queue\":\"%s\",\"handle\":%llu,\"accepted\":%s,\"next_world\":%llu",world58,v.armed?"true":"false",
        v.arena==pse::proxy::EngineTeardownWitness::ArenaDestroyed?"ArenaDestroyed":v.arena==pse::proxy::EngineTeardownWitness::NotObserved?"NotObserved":"Unknown",pse::p3::toString(v.queue),v.handle,accepted?"true":"false",world58+1);
    if(closedDead) log("p3_teardown_verdict_missing","\"world\":%llu,\"slot_closed\":\"DeadNoWitness\"",world58);
    logRouter("teardown_verdict");
    delete g.session;g.session=nullptr; // worker joined in stop(); a new Session reconnects on the next gameplay world
    ++world58;++worldExits58;worldLostApplied58=false;publishStatus();flushJournal();
}
void __fastcall hookStateExit(void* self) {
    const bool world=thread() && self==instance() && read<int>(instance(),0x24)==3 && read<int>(instance(),0x20)!=3 && !g.stopped;
    if(world) beginWorldExit();
    reinterpret_cast<Unary>(g.original[profile::StateExit])(self);
    if(world) finishWorldExit();
}
void __fastcall hookShutdown(void* self) {
    exit58("DirectQuit",2);
    stop("DirectQuit");g.shutdown=true;reinterpret_cast<Unary>(g.original[profile::Shutdown])(self); }
void __fastcall hookEngineShutdown(void* self) { log("engine_shutdown_entry","\"quiesced\":%s",g.stopped?"true":"false");reinterpret_cast<Unary>(g.original[profile::EngineShutdown])(self);log("engine_shutdown_return"); }
void __fastcall hookFinal() { log("client_native_final_exit","\"quiesced\":%s,\"failed\":%s,\"foreign_calls\":%llu,\"module_pinned\":true,\"worlds_exited\":%llu",g.stopped?"true":"false",g.failed?"true":"false",g.foreign,worldExits58);flushJournal();reinterpret_cast<Final>(g.original[profile::FinalExit])(); }
LONG CALLBACK exceptions(EXCEPTION_POINTERS* e) { const auto c=e->ExceptionRecord->ExceptionCode;if(c==EXCEPTION_ACCESS_VIOLATION || c==EXCEPTION_ILLEGAL_INSTRUCTION || c==EXCEPTION_STACK_OVERFLOW) { log("native_fault","\"code\":%lu,\"address\":\"%p\"",c,e->ExceptionRecord->ExceptionAddress);flushJournal(); }return EXCEPTION_CONTINUE_SEARCH; }
void CALLBACK bootstrap(ULONG_PTR) {
    if(!thread()) { block("bootstrap_affinity");return; }
    const std::array<std::pair<profile::Index,void*>,7> hooks{{{profile::Tick,reinterpret_cast<void*>(&hookTick)},{profile::Update,reinterpret_cast<void*>(&hookUpdate)},{profile::StateExit,reinterpret_cast<void*>(&hookStateExit)},{profile::Shutdown,reinterpret_cast<void*>(&hookShutdown)},{profile::EngineShutdown,reinterpret_cast<void*>(&hookEngineShutdown)},{profile::FinalExit,reinterpret_cast<void*>(&hookFinal)},{profile::Getter,nullptr}}};
    for(const auto& [i,h]:hooks) if(h && !prefix(i)) { block("exact_prefix");return; }
    if(MH_Initialize()!=MH_OK) { block("minhook_init");return; }
    for(const auto& [i,h]:hooks) if(h) {
        if(MH_CreateHook(at(i),h,&g.original[i])!=MH_OK || MH_EnableHook(at(i))!=MH_OK) { block("hook_install");return; }g.enabled[i]=true;
    }
    if(!np::install(g.base,g.tid,nativeProxyJournal) || seam58.registerTable(&np::table())!=ERROR_SUCCESS) { block("native_proxy_install");return; }
    log("seam_registered","\"result\":0,\"abi\":\"58b00001\",\"size\":72,\"owner\":\"native_cpp\",\"external_callbacks\":false,\"before_permit\":true,\"p3\":true");
    AddVectoredExceptionHandler(0,exceptions);log("bootstrap_ready","\"game_calls\":0,\"hooks\":6,\"module_pinned\":true,\"p3_rearm\":true");
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
extern "C" __declspec(dllexport) DWORD WINAPI M11Prepare(const wchar_t* logfile,DWORD tid) {
    if(g.prepared.exchange(1)) return ERROR_ALREADY_INITIALIZED;
    wchar_t exe[32768]{},dll[32768]{},path[32768]{};
    if(!GetModuleFileNameW(nullptr,exe,32768) || _wcsicmp(exe,ClientExe)!=0 || !hashMatches(exe)) return ERROR_BAD_EXE_FORMAT;
    if(!GetModuleFileNameW(g.module,dll,32768) || !within(dll,profile::Workspace)) return ERROR_BAD_PATHNAME;
    const DWORD length=GetFullPathNameW(logfile,32768,path,nullptr);if(!length || length>=32768 || !within(path,profile::Workspace)) return ERROR_BAD_PATHNAME;
    HMODULE pinned{};if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&M11Prepare),&pinned)) return GetLastError();
    g.base=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr));g.tid=tid;
    // Permit the read-only isolation/acceptance observer while retaining sole
    // write ownership. Secure fopen's default sharing blocked the smoke gate.
    g.journal=_wfsopen(path,L"wx",_SH_DENYWR);
    if(!g.journal) return ERROR_OPEN_FAILED;std::setvbuf(g.journal,g.buffer,_IOFBF,sizeof(g.buffer));
    log("client_dll_prepared","\"binary_sha256\":\"%s\",\"image_base\":\"%p\",\"expected_primary_tid\":%lu,\"player_writes\":0,\"kex_calls\":0,\"package\":\"P3\"",profile::Sha256,g.base,tid);
    HANDLE th=OpenThread(THREAD_SET_CONTEXT|THREAD_QUERY_LIMITED_INFORMATION,FALSE,tid);if(!th) return GetLastError();
    if(GetProcessIdOfThread(th)!=GetCurrentProcessId()) { CloseHandle(th);return ERROR_INVALID_PARAMETER; }
    const DWORD queued=QueueUserAPC(bootstrap,th,0);const DWORD result=queued?ERROR_SUCCESS:GetLastError();CloseHandle(th);return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI M11PermitTest() { g.permit.store(1,std::memory_order_release);return ERROR_SUCCESS; }
// Frida-thread exports: they only publish atomics; every proxy/KEX action happens on the simulation thread.
extern "C" __declspec(dllexport) DWORD WINAPI PSE58B_RegisterSeam(const pse::seam::SeamTable* t) {
    // The adapter is internal. Retain the export for an explicit, machine-checkable refusal.
    (void)t;return ERROR_NOT_SUPPORTED;
}
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P1_PermitProxySpawn() { np::permitSpawn();return ERROR_SUCCESS; }
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P1_ReadObservation(np::Observation* out,std::uint32_t size) { return np::observe(out,size); }
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P1_IsPositionInterpolant(const void* candidate) { return np::isPositionInterpolant(candidate) ? 1u : 0u; }
extern "C" __declspec(dllexport) DWORD WINAPI PSE58B_RequestProxyShutdown() { proxyShutdown58.store(1,std::memory_order_release);return ERROR_SUCCESS; }
extern "C" __declspec(dllexport) DWORD WINAPI PSE58B_RequestDetach() { detach58.store(1,std::memory_order_release);return ERROR_SUCCESS; }
// seam status | proxies quiescent<<8 | (slot proxy state+1)<<16, as last published by the simulation thread.
extern "C" __declspec(dllexport) DWORD WINAPI PSE58B_Status() { return seam58.status()|status58.load(std::memory_order_acquire); }
// P3 test-only: ask the simulation thread to reconnect the live session (accepted RECONNECT control path).
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P3_RequestReconnect() { reconnectRequest58.store(1,std::memory_order_release);return ERROR_SUCCESS; }
// incarnation | closure<<8 | (world serial & 0xffff)<<16, as last published by the simulation thread.
extern "C" __declspec(dllexport) DWORD WINAPI PSE_P3_Status() { return statusP3.load(std::memory_order_acquire); }
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) { if(reason==DLL_PROCESS_ATTACH) g.module=module;return TRUE; }
