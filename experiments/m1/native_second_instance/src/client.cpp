// Exact-build passive input client. KEX stays opaque; no Player writes/calls.
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
#include "../../native_bootstrap/src/build_profile.hpp"
#include "enet_worker.h"
#ifdef PSE_DOC55_SNAPSHOT
#include "../../native_snapshot/src/snapshot_contract.hpp"
#endif
#ifdef PSE_DOC56_MAPREADY
#include "../../native_map_ready/src/map_reader.hpp"
#endif

namespace {
using Byte=unsigned char;
using Unary=void(__fastcall*)(void*);
using Final=void(__fastcall*)();
using Clock=std::chrono::steady_clock;
constexpr wchar_t ClientExe[]=LR"(S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\experiments\m1\native_second_instance\game\anubis_Shipping_Steam_x64.exe)";
#ifdef PSE_DOC55_SNAPSHOT
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
    unsigned localThisTick{}; std::uint64_t ticks{},localUpdates{},ordinal{},logSeq{},lastFlush{},foreign{};
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

// Only the actual KEX simulation thread owns both SIMULATION bridge operations.
// Worker uses DTOs only and independently schedules the latest sample at 60 Hz.
struct Session {
    pse::ThreadBridge bridge;
    pse::ENetWorker worker{bridge,pse::ENetWorkerMode::CLIENT};
    bool started{},stopped{},active{},readySent{},baseline{},reconnectPending{};
    std::uint8_t slot{};std::uint32_t gen{},epoch{};
    std::uint64_t lastReconnectNs{};
#ifdef PSE_DOC55_SNAPSHOT
    snapshot55::Guard snapshots;
#endif
#ifdef PSE_DOC56_MAPREADY
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
        if(state!=lastState && state==map56::ClientGate::State::Invalidated) { readySent=baseline=false;block("native_map_invalidated"); }
        lastFault=fault;lastState=state;
    }
#endif
    bool start() {
        bridge.registerSimulationThread();
#if defined(PSE_DOC56_MAPREADY)
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
#elif defined(PSE_DOC55_SNAPSHOT)
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
        pse::ClientConfig cfg;cfg.hostPort=28745;cfg.joinToken="M12A_Loopback";cfg.participantId="native_second_instance_p2";cfg.displayName="KEX_Input_Client";
        if(!worker.startClient(cfg)) { bridge.unregisterSimulationThread();block("client_worker_start");return false; }
        started=true;log("client_worker_started","\"target_hz\":60,\"freshness_ms\":100,\"protocol\":272");return true;
    }
    void consume() {
        bridge.assertSimulationThread();pse::BridgeMessage m;
#ifdef PSE_DOC56_MAPREADY
        observeMap();
#endif
        for(std::size_t n=0;n<pse::THREAD_BRIDGE_INBOUND_CAPACITY && bridge.pollInbound(m);++n) {
            if(const auto* e=std::get_if<pse::BridgeSessionEvent>(&m)) {
                log("client_session","\"type\":%u,\"slot\":%u,\"generation\":%u,\"epoch\":%u",static_cast<unsigned>(e->type),e->slotId,e->connectionGeneration,e->mapEpoch);
                if(e->type==pse::BridgeSessionEvent::Type::CLIENT_JOINED || e->type==pse::BridgeSessionEvent::Type::RECONNECTED) {
                    if(e->connectionGeneration<=gen) continue;
#ifdef PSE_DOC56_MAPREADY
                    if(!mapGate.welcome(e->slotId,e->connectionGeneration,e->mapEpoch,e->mapName)) { block("welcome_map_identity_or_epoch");continue; }
                    slot=e->slotId;gen=e->connectionGeneration;epoch=e->mapEpoch;active=true;readySent=baseline=false;reconnectPending=false;
                    snapshots.reset(gen,epoch);
                    log("client_welcome","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"host_map\":\"%s\",\"local_map\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,mapGate.hostName().c_str(),localMap.name.c_str());
#else
                    if(!e->connectionGeneration || e->slotId==0 || e->slotId>=pse::MAX_PLAYERS || e->mapName!=Domain ||
#ifdef PSE_DOC55_SNAPSHOT
                        !e->mapEpoch
#else
                        e->mapEpoch!=1
#endif
                        ) { block("welcome_identity_or_input_domain");continue; }
                    slot=e->slotId;gen=e->connectionGeneration;epoch=e->mapEpoch;active=true;readySent=baseline=false;reconnectPending=false;
#ifdef PSE_DOC55_SNAPSHOT
                    snapshots.reset(gen,epoch);
#endif
                    log("client_welcome","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"domain\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,Domain);
#endif
                } else if(e->type==pse::BridgeSessionEvent::Type::LOCAL_SESSION_LOST) {
                    active=readySent=baseline=false;reconnectPending=true;
#ifdef PSE_DOC56_MAPREADY
                    mapGate.lost();
#endif
                } else if(e->type==pse::BridgeSessionEvent::Type::REJECTED) { active=readySent=baseline=false;block("session_rejected"); }
            } else if(const auto* map=std::get_if<pse::BridgeMapChange>(&m)) {
                if(map->connectionGeneration!=gen) continue;
#ifdef PSE_DOC56_MAPREADY
                if(!mapGate.mapChange(map->connectionGeneration,map->newMapEpoch,map->mapName)) { readySent=baseline=false;block("unsupported_map_change"); }
#else
                if(map->mapName!=Domain || map->newMapEpoch!=epoch) block("unsupported_map_domain");
#endif
            } else if(const auto* b=std::get_if<pse::BridgeWorldBaseline>(&m)) {
                if(b->connectionGeneration!=gen) continue;
#ifdef PSE_DOC56_MAPREADY
                if(!active || !readySent || b->totalBatches!=1 || b->batchIndex!=0 || !b->entities.empty() || !mapGate.baseline(b->connectionGeneration,b->mapEpoch,b->mapName)) { block("map_baseline_contract");continue; }
                baseline=true;log("client_input_domain_ready","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"map\":\"%s\",\"entities\":0,\"state\":\"%s\",\"world_synchronized\":false",slot,gen,epoch,mapGate.hostName().c_str(),map56::ClientGate::text(mapGate.state()));
#else
                if(!active || !readySent || b->mapEpoch!=epoch || b->mapName!=Domain || b->totalBatches!=1 || b->batchIndex!=0 || !b->entities.empty()) { block("input_baseline_contract");continue; }
                baseline=true;log("client_input_domain_ready","\"slot\":%u,\"generation\":%u,\"epoch\":%u,\"entities\":0,\"world_synchronized\":false",slot,gen,epoch);
#endif
#ifdef PSE_DOC55_SNAPSHOT
            } else if(const auto* s=std::get_if<pse::BridgePlayerSnapshot>(&m)) {
#ifdef PSE_DOC56_MAPREADY
                const bool mapEligible=mapGate.snapshotEligible(s->connectionGeneration,s->mapEpoch);
#else
                const bool mapEligible=true;
#endif
                const auto verdict=snapshots.accept(*s,active && readySent && baseline && gameplay() && !g.failed && !g.stopped && mapEligible);
                if(verdict==snapshot55::Verdict::EntityMismatch || verdict==snapshot55::Verdict::Nonfinite) { block("snapshot_unexplained_entity_or_transform");continue; }
                if(verdict!=snapshot55::Verdict::Accept) { log("native_snapshot_rejected","\"slot\":%u,\"snapshot_seq\":%u,\"reason\":%u",s->slotId,s->sequence,static_cast<unsigned>(verdict));continue; }
                log("native_snapshot_received","\"slot\":%u,\"snapshot_seq\":%u,\"server_tick\":%u,\"map_epoch\":%u,\"connection_generation\":%u,\"entity_id\":%u,\"entity_generation\":%u,\"pos\":[%.9g,%.9g,%.9g],\"yaw\":%.9g,\"pitch\":%.9g,\"bits\":[%u,%u,%u,%u,%u],\"player_writes\":0",
                    s->slotId,s->sequence,s->serverTick,s->mapEpoch,s->connectionGeneration,s->entityId,s->entityGeneration,s->posX,s->posY,s->posZ,s->yaw,s->pitch,snapshot55::bits(s->posX),snapshot55::bits(s->posY),snapshot55::bits(s->posZ),snapshot55::bits(s->yaw),snapshot55::bits(s->pitch));
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
#ifdef PSE_DOC56_MAPREADY
        // MapReady only after the authenticated host identity equals the locked local native
        // identity for StableTicks consecutive stable ticks: never on connection or a timer.
        if(active && !readySent && !g.failed && gameplay() && g.localThisTick==1 && mapGate.shouldSendReady()) {
            pse::BridgeMapReady r;r.slotId=slot;r.connectionGeneration=gen;r.mapEpoch=epoch;
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
        pse::BridgeClientInputSample s;s.slotId=slot;s.connectionGeneration=gen;s.sampleOrdinal=++g.ordinal;s.sampledSteadyNs=steadyNs();s.simulationTick=g.ticks;
        s.valid=valid && baseline;s.moveForward=s.valid ? (g.forwardHeld?1.0f:0.0f)-(g.backHeld?1.0f:0.0f) : 0.0f;
        const bool queued=pse::isSuccess(bridge.pushOutbound(pse::BridgeMessage{s}));
        log("client_input_sampled","\"slot\":%u,\"generation\":%u,\"sample_ordinal\":%llu,\"tick\":%llu,\"sampled_ns\":%llu,\"valid\":%s,\"kex_buttons\":%u,\"held_forward\":%u,\"held_back\":%u,\"forward\":%.1f,\"bridge_queued\":%s",slot,gen,s.sampleOrdinal,g.ticks,s.sampledSteadyNs,s.valid?"true":"false",g.buttons,g.forwardHeld,g.backHeld,s.moveForward,queued?"true":"false");
    }
    void stop(const char* reason) {
        if(!started || stopped) return;
        bridge.assertSimulationThread();sample(false);stopped=true;
        log("client_stop_begin","\"reason\":\"%s\"",reason);worker.stop();
        pse::BridgeMessage m;std::size_t retired{};while(bridge.pollInbound(m)) ++retired;
        const auto s=worker.getStats();log("client_worker_joined","\"running\":%s,\"retired_inbound\":%zu,\"retired_outbound\":%zu,\"affinity_violations\":%llu,\"pending_peak\":%llu,\"lost_on_stop\":%llu,\"poll_loops\":%llu",worker.isRunning()?"true":"false",retired,bridge.getOutboundOccupancy(),bridge.getAffinityViolationCount(),s.pendingPeak,s.lostOnStop,s.pollLoops);
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
    reinterpret_cast<Unary>(g.original[profile::Tick])(self);
    if(!g.stopped) {
        if(!g.session && g.permit.load(std::memory_order_acquire) && !g.failed && gameplay() && g.localThisTick==1) { g.session=new Session;g.session->start(); }
        if(g.session && g.session->started && !g.session->stopped) {
            g.session->consume();DWORD foregroundPid{};GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
            g.session->sample(g.sampled && g.localThisTick==1 && gameplay() && !g.failed && foregroundPid==GetCurrentProcessId());
        }
        if((g.localUpdates==1 && g.localThisTick==1) || g.ticks<=3 || g.ticks%120==0)
            log("tick_status","\"ticks\":%llu,\"state\":%d,\"pending\":%d,\"local_updates\":%llu,\"local_this_tick\":%u",g.ticks,read<int>(instance(),0x24),read<int>(instance(),0x28),g.localUpdates,g.localThisTick);
    }
    g.inTick=false;
}
void __fastcall hookStateExit(void* self) {
    if(thread() && self==instance() && read<int>(instance(),0x24)==3 && read<int>(instance(),0x20)!=3) stop("StateExit");
    reinterpret_cast<Unary>(g.original[profile::StateExit])(self);
}
void __fastcall hookShutdown(void* self) { stop("DirectQuit");g.shutdown=true;reinterpret_cast<Unary>(g.original[profile::Shutdown])(self); }
void __fastcall hookEngineShutdown(void* self) { log("engine_shutdown_entry","\"quiesced\":%s",g.stopped?"true":"false");reinterpret_cast<Unary>(g.original[profile::EngineShutdown])(self);log("engine_shutdown_return"); }
void __fastcall hookFinal() { log("client_native_final_exit","\"quiesced\":%s,\"failed\":%s,\"foreign_calls\":%llu,\"module_pinned\":true",g.stopped?"true":"false",g.failed?"true":"false",g.foreign);flushJournal();reinterpret_cast<Final>(g.original[profile::FinalExit])(); }
LONG CALLBACK exceptions(EXCEPTION_POINTERS* e) { const auto c=e->ExceptionRecord->ExceptionCode;if(c==EXCEPTION_ACCESS_VIOLATION || c==EXCEPTION_ILLEGAL_INSTRUCTION || c==EXCEPTION_STACK_OVERFLOW) { log("native_fault","\"code\":%lu,\"address\":\"%p\"",c,e->ExceptionRecord->ExceptionAddress);flushJournal(); }return EXCEPTION_CONTINUE_SEARCH; }
void CALLBACK bootstrap(ULONG_PTR) {
    if(!thread()) { block("bootstrap_affinity");return; }
    const std::array<std::pair<profile::Index,void*>,7> hooks{{{profile::Tick,reinterpret_cast<void*>(&hookTick)},{profile::Update,reinterpret_cast<void*>(&hookUpdate)},{profile::StateExit,reinterpret_cast<void*>(&hookStateExit)},{profile::Shutdown,reinterpret_cast<void*>(&hookShutdown)},{profile::EngineShutdown,reinterpret_cast<void*>(&hookEngineShutdown)},{profile::FinalExit,reinterpret_cast<void*>(&hookFinal)},{profile::Getter,nullptr}}};
    for(const auto& [i,h]:hooks) if(h && !prefix(i)) { block("exact_prefix");return; }
    if(MH_Initialize()!=MH_OK) { block("minhook_init");return; }
    for(const auto& [i,h]:hooks) if(h) {
        if(MH_CreateHook(at(i),h,&g.original[i])!=MH_OK || MH_EnableHook(at(i))!=MH_OK) { block("hook_install");return; }g.enabled[i]=true;
    }
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
    log("client_dll_prepared","\"binary_sha256\":\"%s\",\"image_base\":\"%p\",\"expected_primary_tid\":%lu,\"player_writes\":0,\"kex_calls\":0",profile::Sha256,g.base,tid);
    HANDLE th=OpenThread(THREAD_SET_CONTEXT|THREAD_QUERY_LIMITED_INFORMATION,FALSE,tid);if(!th) return GetLastError();
    if(GetProcessIdOfThread(th)!=GetCurrentProcessId()) { CloseHandle(th);return ERROR_INVALID_PARAMETER; }
    const DWORD queued=QueueUserAPC(bootstrap,th,0);const DWORD result=queued?ERROR_SUCCESS:GetLastError();CloseHandle(th);return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI M11PermitTest() { g.permit.store(1,std::memory_order_release);return ERROR_SUCCESS; }
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) { if(reason==DLL_PROCESS_ATTACH) g.module=module;return TRUE; }
