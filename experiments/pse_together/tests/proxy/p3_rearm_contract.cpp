// P3 offline contract: rearm layer (proxy_rearm_p3.hpp) over the unchanged DOC-58B.2 automaton, the pure teardown
// witness / anti-ABA rules (teardown_witness_p3.hpp), and the DOC-58C QA cases re-expressed against the current code.
// A mock seam stands in for the native adapter: witnesses here are test inputs, never native proof.
#include "proxy_rearm_p3.hpp"
#include "teardown_witness_p3.hpp"
#include "foreign_fault_p3.hpp"
#include <cstdio>
#include <functional>
#include <string_view>
#include <thread>
using namespace pse::proxy;
using namespace pse::p3;
namespace {
int failures=0,total=0;
void check(const char* id,const char* name,bool ok){++total;failures+=!ok;std::printf("%-4s %s %s\n",id,ok?"PASS":"FAIL",name);}
constexpr TickContext Safe{true,true};
struct Seam final:INativeVisualProxySeam {
    int spawns{},updates{},removes{},polls{};ProxyHandle next=1;bool fixedHandle{};RemoveOutcome removeResult{RemoveOutcome::Requested};
    SpawnOutcome spawnResult{SpawnOutcome::Created};RetirementStatus retire{RetirementStatus::Pending};std::function<void()> during;
    SpawnOutcome spawn(const ProxyCommand&,ProxyHandle& h) noexcept override{++spawns;h=fixedHandle?next:next++;if(during)during();return spawnResult;}
    UpdateOutcome update(ProxyHandle,const ProxyCommand&) noexcept override{++updates;if(during)during();return UpdateOutcome::Applied;}
    RemoveOutcome remove(ProxyHandle) noexcept override{++removes;if(during)during();return removeResult;}
    RetirementStatus retirement(ProxyHandle) noexcept override{++polls;if(during)during();return retire;}
    int calls() const{return spawns+updates+removes+polls;}
};
pse::BridgePlayerSnapshot snap(std::uint32_t gen,std::uint32_t epoch,std::uint32_t seq,std::uint8_t slot=0){
    pse::BridgePlayerSnapshot s;s.slotId=slot;s.sequence=seq;s.serverTick=10+seq;s.connectionGeneration=gen;s.mapEpoch=epoch;s.entityId=21760;s.entityGeneration=1;
    s.posX=-7420;s.posY=-6809;s.posZ=1152;s.yaw=0.5f;return s;}
DWORD g_tid=7;DWORD fakeTid(){return g_tid;}
constexpr std::uint32_t E=1245199520u;
SessionKey key(std::uint32_t gen,std::uint64_t world,std::uint32_t epoch=E){return SessionKey{gen,epoch,world};}
struct Fixture {
    RearmableProxy p{0,&fakeTid};Seam seam;std::uint32_t seq{};
    Fixture(){g_tid=7;p.bind(7);}
    void session(std::uint32_t gen,std::uint64_t world,bool ready=true){p.configureSession(key(gen,world));if(ready)p.onNativeMapReady(E,0x1000+gen,world);}
    TickAction feed(std::uint32_t gen){p.handleSnapshot(snap(gen,E,++seq),true);return p.tick(&seam,Safe);}
    void retireNow(){seam.retire=RetirementStatus::Retired;p.tick(&seam,Safe);seam.retire=RetirementStatus::Pending;}
    VisualProxyState state() const{return p.current()->state();}
};
}
int main(){
    // ---------- R: rearm rules (S1.1) ----------
    {Fixture f;f.session(1,1);const auto a=f.feed(1);
     check("R01","first incarnation: session + native MapReady + snapshot -> one Spawn, handle 1",a==TickAction::SpawnCreated&&f.state()==VisualProxyState::ACTIVE&&f.p.current()->handle()==1&&f.p.incarnation()==1);
     f.p.onDisconnect();f.p.tick(&f.seam,Safe);
     check("R02","disconnect latches the incarnation: Remove issued, RETIRING, no rearm while not retired",f.seam.removes==1&&f.state()==VisualProxyState::RETIRING&&f.p.closure()==Closure::Open);
     f.p.handleSnapshot(snap(1,E,++f.seq),true);f.p.tick(&f.seam,Safe);
     check("R03","old-session snapshots never respawn during retirement (no second factory call)",f.seam.spawns==1);
     f.p.configureSession(key(2,1));f.p.tick(&f.seam,Safe);
     check("R04","new session without native MapReady does not rearm (NoMapReady or NotClosed)",f.p.incarnation()==1&&f.seam.spawns==1);
     f.p.onNativeMapReady(E,0x1002,1);f.p.tick(&f.seam,Safe);
     check("R05","TC-09/TC-30: MapReady while PoolReturn/retirement still pending -> no rearm, no second Spawn",f.p.incarnation()==1&&f.seam.spawns==1&&f.p.lastRefusal()==RearmRefusal::NotClosed);
     f.retireNow();
     check("R06","native retirement witness closes the incarnation as Retired",f.p.closure()==Closure::Retired||f.p.incarnation()==2);
     f.p.tick(&f.seam,Safe);
     check("R07","Retired + newer session + MapReady after it -> incarnation 2 (fresh latches)",f.p.incarnation()==2&&f.p.rearms()==1&&f.state()==VisualProxyState::ABSENT&&f.p.closure()==Closure::Open);
     const auto b=f.feed(2);
     check("R08","second Spawn with a strictly newer opaque handle",b==TickAction::SpawnCreated&&f.p.current()->handle()==2&&f.seam.spawns==2&&f.p.lastHandle()==2);
     const auto rj=f.p.handleSnapshot(snap(1,E,++f.seq),true);
     check("R09","stale gen-1 snapshot is rejected by the new incarnation (IdentityMismatch)",rj==Rejection::IdentityMismatch);}
    {Fixture f;f.session(1,1);f.feed(1);f.p.onDisconnect();f.p.tick(&f.seam,Safe);f.retireNow();
     f.p.onNativeMapReady(E,0x1002,1);f.p.configureSession(key(2,1));f.p.tick(&f.seam,Safe);
     check("R10","MapReady received BEFORE the new session is configured never rearms",f.p.incarnation()==1&&f.p.lastRefusal()==RearmRefusal::NoMapReady);
     f.p.configureSession(key(1,1));f.p.onNativeMapReady(E,0x1001,1);f.p.tick(&f.seam,Safe);
     check("R11","session that is not strictly newer (same generation) is refused",f.p.incarnation()==1&&f.p.lastRefusal()==RearmRefusal::SessionNotNewer);}
    {Fixture f;f.session(1,1);f.feed(1);const auto h=f.p.current()->handle();const int n=f.seam.calls();f.p.onPreUnload(1);
     f.p.tick(&f.seam,Safe);f.p.handleSnapshot(snap(1,E,++f.seq),true);f.p.tick(&f.seam,Safe);
     check("R12","TC-03: live proxy at the barrier -> ORPHANED/LiveAtBarrier, zero seam calls after the barrier",f.state()==VisualProxyState::ORPHANED&&f.p.current()->orphanCause()==OrphanCause::LiveAtBarrier&&f.seam.calls()==n);
     f.session(2,1);f.p.tick(&f.seam,Safe);
     check("R13","no rearm before the engine teardown verdict",f.p.incarnation()==1&&f.p.lastRefusal()==RearmRefusal::NotClosed);
     check("R14","verdict for another handle refused",!f.p.onTeardownVerdict({h+5,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued})&&f.p.closure()==Closure::Open);
     check("R15","ArenaDestroyed for the orphaned handle -> ENGINE_RETIRED via the accepted rule",f.p.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued})&&f.state()==VisualProxyState::ENGINE_RETIRED&&f.p.closure()==Closure::EngineRetired);
     f.session(3,1);f.p.tick(&f.seam,Safe);
     check("R16","newer session on the SAME (torn-down) world is refused: WorldNotNewer",f.p.incarnation()==1&&f.p.lastRefusal()==RearmRefusal::WorldNotNewer&&f.seam.calls()==n);
     f.session(4,2);f.p.tick(&f.seam,Safe);const auto a=f.feed(4);
     check("R17","new world + newer session + MapReady -> incarnation 2 spawns (handle 2)",f.p.incarnation()==2&&a==TickAction::SpawnCreated&&f.p.current()->handle()==2);}
    {Fixture f;f.session(1,1);f.feed(1);const auto h=f.p.current()->handle();f.p.onPreUnload(1);
     f.p.onTeardownVerdict({h,EngineTeardownWitness::NotObserved,QueueWitness::NotIssued});f.session(2,2);f.p.tick(&f.seam,Safe);f.feed(2);
     check("R18","NotObserved verdict kills the slot: never rearmed, no second Spawn",f.p.closure()==Closure::DeadNoWitness&&f.p.dead()&&f.p.incarnation()==1&&f.seam.spawns==1);}
    {Fixture f;f.session(1,1);f.feed(1);const auto h=f.p.current()->handle();f.p.onDisconnect();f.p.tick(&f.seam,Safe);f.p.onPreUnload(1);
     check("R19","Remove requested (adapter armed) at the barrier -> RemoveInFlightAtBarrier",f.p.current()->orphanCause()==OrphanCause::RemoveInFlightAtBarrier);
     const bool ok=f.p.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued});f.session(2,2);f.p.tick(&f.seam,Safe);
     check("R20","queue witness NotIssued + ArenaDestroyed -> EngineRetiredQueueClear, rearm on the next world",ok&&f.p.incarnation()==2&&f.p.history(0).closure==Closure::EngineRetiredQueueClear);}
    {Fixture f;f.session(1,1);f.feed(1);const auto h=f.p.current()->handle();f.p.onDisconnect();f.p.tick(&f.seam,Safe);f.p.onPreUnload(1);
     const bool ok=f.p.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::IssuedDrained});
     check("R21","issued and drained in the same live tick + ArenaDestroyed is also queue-clear",ok&&f.p.closure()==Closure::EngineRetiredQueueClear);}
    {Fixture f;f.session(1,1);f.feed(1);const auto h=f.p.current()->handle();f.p.onDisconnect();f.p.tick(&f.seam,Safe);f.p.onPreUnload(1);
     f.p.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::IssuedQueued});f.session(2,2);f.p.tick(&f.seam,Safe);
     check("R22","undrained native Remove at the barrier -> DeadQueueHazard, never rearmed",f.p.closure()==Closure::DeadQueueHazard&&f.p.incarnation()==1);}
    {Fixture f;f.seam.spawnResult=SpawnOutcome::Unknown;f.session(1,1);f.feed(1);f.session(2,1);f.p.tick(&f.seam,Safe);
     check("R23","QUARANTINED (unknown Spawn) kills the slot",f.p.closure()==Closure::DeadQuarantined&&f.p.incarnation()==1&&f.seam.spawns==1);}
    {Fixture f;f.session(1,1);f.p.handleSnapshot(snap(1,E,1),true);f.p.onPreUnload(1);f.p.tick(&f.seam,Safe);
     check("R24","barrier during SPAWN_PENDING -> ABSENT without factory call, closed NeverSpawned",f.state()==VisualProxyState::ABSENT&&f.seam.spawns==0&&f.p.closure()==Closure::NeverSpawned);
     f.session(2,2);const auto a=f.feed(2);
     check("R25","NeverSpawned closure rearms on the next world (first Spawn handle 1)",f.p.incarnation()==2&&a==TickAction::SpawnCreated&&f.p.current()->handle()==1);}
    {Fixture f;f.session(1,1);f.feed(1);bool inner=false;std::uint32_t before=0;
     f.seam.during=[&]{before=f.p.incarnation();f.p.configureSession(key(2,2));f.p.onNativeMapReady(E,9,2);inner=f.p.onTeardownVerdict({1,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued});};
     f.p.handleSnapshot(snap(1,E,++f.seq),true);f.p.tick(&f.seam,Safe);
     check("R26","TC-26: reentrant session/MapReady/verdict during a seam call are refused; no incarnation swap inside the call",!inner&&before==1&&f.p.incarnation()==1&&f.p.reentrancyBlocked()>=3);}
    {Fixture f;f.session(1,1);g_tid=8;f.p.configureSession(key(2,2));const auto rj=f.p.handleSnapshot(snap(1,E,1),true);g_tid=7;
     check("R27","foreign-thread calls are refused before any state change",rj==Rejection::AffinityViolation&&f.p.affinityViolations()==2&&f.p.pendingKey().gen==1);}
    {Fixture f;f.seam.fixedHandle=true;f.seam.next=1;f.session(1,1);f.feed(1);f.p.onDisconnect();f.p.tick(&f.seam,Safe);f.retireNow();f.session(2,1);f.p.tick(&f.seam,Safe);
     f.feed(2);f.p.tick(&f.seam,Safe);f.retireNow();f.p.tick(&f.seam,Safe);f.session(3,1);f.p.tick(&f.seam,Safe);
     check("R28","reused opaque handle across incarnations -> violation, Remove requested, slot dead",f.p.handleViolations()==1&&f.p.closure()==Closure::DeadHandleReuse&&f.p.incarnation()==2&&f.seam.removes==2);}
    {Fixture f;f.session(1,1);f.feed(1);f.p.requestShutdown();f.p.tick(&f.seam,Safe);f.retireNow();f.session(2,2);f.p.tick(&f.seam,Safe);
     check("R29","shutdown is terminal for the slot (never rearmed)",f.p.closure()==Closure::DeadShutdown&&f.p.incarnation()==1);}
    {Fixture f;bool ok=true;std::uint32_t world=1,gen=1;f.session(gen,world);ok&=f.feed(gen)==TickAction::SpawnCreated;
     for(int i=0;i<10;++i) {
         const auto h=f.p.current()->handle();const int n=f.seam.calls();f.p.onPreUnload(world);f.p.tick(&f.seam,Safe);ok&=f.seam.calls()==n;
         ok&=f.p.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued});++world;++gen;f.session(gen,world);
         ok&=f.feed(gen)==TickAction::SpawnCreated && f.p.current()->handle()==h+1;}
     check("R30","S1.2 shape: 10 consecutive unload->witness->rearm cycles, 11 Spawns, strictly increasing handles, 0 calls after barriers",ok&&f.p.incarnation()==11&&f.seam.spawns==11&&f.seam.removes==0&&f.p.rearms()==10&&f.p.lastHandle()==11);}
    // ---------- DOC-78 fixes: latches inside seam calls (F1), lost-session pending state (F5), missing verdict (F4), once per incarnation (F2) ----------
    {Fixture f;f.session(1,1);f.feed(1);const auto h=f.p.current()->handle();
     f.p.handleSnapshot(snap(1,E,++f.seq),true);f.seam.during=[&]{f.p.onPreUnload(1);};f.p.tick(&f.seam,Safe);f.seam.during=nullptr;const int n=f.seam.calls();
     f.p.tick(&f.seam,Safe);
     check("R34","F1: barrier arriving INSIDE a seam update is latched (not dropped): ORPHANED after the call, no further seam call, accepted unload-during-seam counter",
           f.state()==VisualProxyState::ORPHANED&&f.p.current()->orphanCause()==OrphanCause::LiveAtBarrier&&f.seam.calls()==n&&f.p.intentsDuringSeam()==1&&f.p.current()->unloadDuringSeam()==1);
     f.session(2,1);f.p.tick(&f.seam,Safe);
     check("R35","F1: the wrapper barrier flag was set too: a newer session on the SAME world is refused until the verdict and a newer world",f.p.incarnation()==1&&f.p.lastRefusal()==RearmRefusal::NotClosed);
     const bool ok=f.p.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued});f.session(3,1);f.p.tick(&f.seam,Safe);
     check("R36","F1: after the verdict the same world is still refused (barrier world recorded), the next world rearms",ok&&f.p.incarnation()==1&&f.p.lastRefusal()==RearmRefusal::WorldNotNewer&&(f.session(4,2),f.p.tick(&f.seam,Safe),f.p.incarnation()==2));}
    {Fixture f;f.session(1,1);f.feed(1);f.p.handleSnapshot(snap(1,E,++f.seq),true);
     f.seam.during=[&]{f.p.requestShutdown();};f.p.tick(&f.seam,Safe);f.seam.during=nullptr;f.p.tick(&f.seam,Safe);f.retireNow();f.session(2,2);f.p.tick(&f.seam,Safe);
     check("R37","F1: shutdown arriving inside a seam call is latched: Remove, retirement, DeadShutdown, never rearmed",f.p.closure()==Closure::DeadShutdown&&f.p.incarnation()==1&&f.seam.removes==1&&f.p.intentsDuringSeam()==1);}
    {Fixture f;f.session(1,1);f.feed(1);f.p.handleSnapshot(snap(1,E,++f.seq),true);
     f.seam.during=[&]{f.p.onDisconnect();};f.p.tick(&f.seam,Safe);f.seam.during=nullptr;f.p.tick(&f.seam,Safe);
     check("R38","F1: disconnect arriving inside a seam call is latched: exactly one Remove follows",f.seam.removes==1&&f.state()==VisualProxyState::RETIRING&&f.p.intentsDuringSeam()==1);}
    {Fixture f;f.session(1,1);f.feed(1);f.p.onDisconnect();f.p.tick(&f.seam,Safe);
     f.session(2,1);f.p.onDisconnect();f.retireNow();f.p.tick(&f.seam,Safe);
     check("R39","F5: LOST(gen1) -> RECONNECTED(gen2)+MapReady -> LOST(gen2) while RETIRING: after Retired no incarnation is created for the lost gen2 session",f.p.incarnation()==1&&f.p.closure()==Closure::Retired&&f.p.lastRefusal()==RearmRefusal::NoSession&&f.seam.spawns==1);
     f.session(3,1);f.p.tick(&f.seam,Safe);const auto a=f.feed(3);
     check("R40","F5: a session configured and MapReady'd after the last loss rearms (gen 3, handle 2)",f.p.incarnation()==2&&f.p.currentKey().gen==3&&a==TickAction::SpawnCreated&&f.p.current()->handle()==2);}
    {Fixture f;f.session(1,1);f.feed(1);f.p.onPreUnload(1);f.p.tick(&f.seam,Safe);
     const bool first=f.p.onTeardownMissing();const bool second=f.p.onTeardownMissing();f.session(2,2);f.p.tick(&f.seam,Safe);
     check("R41","F4: ORPHANED without any adapter verdict is closed DeadNoWitness (visible, never left Open); idempotent; never rearmed",first&&!second&&f.p.closure()==Closure::DeadNoWitness&&f.p.incarnation()==1);
     Fixture g;g.session(1,1);g.feed(1);const bool none=g.p.onTeardownMissing();
     check("R42","F4: no effect on a live (not orphaned) incarnation",!none&&g.p.closure()==Closure::Open&&g.state()==VisualProxyState::ACTIVE);}
    {PerIncarnationOnce o;
     check("R43","F2: adapter life-cap request is accepted once per INCARNATION (1 yes, 1 no, 2 yes, 2 no, 0 never, 3 yes)",o.take(1)&&!o.take(1)&&o.take(2)&&!o.take(2)&&!o.take(0)&&o.take(3));}
    // ---------- Router (G9 contract kept) ----------
    {g_tid=7;RearmRouter<1> r(&fakeTid);r.bind(7);r.configureSession(1,key(1,1));r.onNativeMapReady(E,0x1001,1);Rejection rj{};Seam seam;
     const auto local=r.route(snap(1,E,1,1),true,rj);const auto own=r.route(snap(1,E,1,0),true,rj);const auto other=r.route(snap(1,E,1,3),true,rj);
     r.slot(0)->tick(&seam,Safe);
     check("R31","router: local slot excluded, first remote slot routed and spawns, second remote slot capacity-deferred (TC-27 bound)",local==RouteResult::LocalExcluded&&own==RouteResult::Routed&&other==RouteResult::CapacityDeferred&&seam.spawns==1);
     const auto h=r.proxy(0)->handle();r.onPreUnload(1);const bool q=r.quiescent();
     check("R32","router barrier + verdict by handle -> quiescent, ENGINE_RETIRED",q&&r.onTeardownVerdict({h,EngineTeardownWitness::ArenaDestroyed,QueueWitness::NotIssued})&&r.proxy(0)->state()==VisualProxyState::ENGINE_RETIRED);
     r.configureSession(1,key(2,2));r.onNativeMapReady(E,0x1002,2);r.route(snap(2,E,1,0),true,rj);r.slot(0)->tick(&seam,Safe);
     check("R33","router rearms the same slot on the next world (second Spawn)",r.slot(0)->incarnation()==2&&seam.spawns==2&&r.proxy(0)->state()==VisualProxyState::ACTIVE);}
    {g_tid=7;RearmRouter<1> r(&fakeTid);r.bind(7);r.configureSession(1,key(1,1));r.onNativeMapReady(E,0x1001,1);r.onDisconnect();Rejection rj{};Seam seam;
     r.route(snap(1,E,1,0),true,rj);r.slot(0)->tick(&seam,Safe);
     check("R44","F5 router: a lost session forgets the router's key/MapReady: a slot created afterwards is not configured and never spawns",seam.spawns==0&&r.slot(0)&&r.slot(0)->pendingKey().gen==0);}
    // ---------- DOC-58C QA cases re-expressed against the current automaton ----------
    {g_tid=7;RearmableProxy a(0,&fakeTid),b(2,&fakeTid);a.bind(7);b.bind(7);Seam sa,sb;
     a.configureSession(key(1,1));a.onNativeMapReady(E,1,1);b.configureSession(key(1,1));b.onNativeMapReady(E,1,1);
     a.handleSnapshot(snap(1,E,1,0),true);a.tick(&sa,Safe);b.handleSnapshot(snap(1,E,1,2),true);b.tick(&sb,Safe);
     b.onDisconnect();b.tick(&sb,Safe);sb.retire=RetirementStatus::Retired;b.tick(&sb,Safe);
     a.handleSnapshot(snap(1,E,2,0),true);const auto ua=a.tick(&sa,Safe);
     check("Q28","TC-28/29: per-slot generations are independent (slot 2 retires, slot 0 still updates; 8-slot guard accepts slot 2)",b.closure()==Closure::Retired&&ua==TickAction::UpdateApplied&&a.current()->state()==VisualProxyState::ACTIVE);
     sb.spawnResult=SpawnOutcome::Unknown;b.configureSession(key(2,1));b.onNativeMapReady(E,2,1);b.handleSnapshot(snap(2,E,1,2),true);b.tick(&sb,Safe);
     a.handleSnapshot(snap(1,E,3,0),true);const auto ua2=a.tick(&sa,Safe);
     check("Q31","TC-31: quarantine blast radius is one slot",b.closure()==Closure::DeadQuarantined&&ua2==TickAction::UpdateApplied);}
    // ---------- W: pure witness / anti-ABA rules ----------
    {using namespace pse::p3::witness;
     unsigned char raw[RawHeader]{};auto put=[&](std::size_t o,std::uint64_t v,std::size_t n){std::memcpy(raw+o,&v,n);};
     put(0,Magic,4);put(8,1459,8);put(0x10,ActorSize,8);put(0x18,0x140af4490,8);put(0x40,0x1400d9980,8);put(0x48,1,8);put(0x50,ActorSize,8);
     const auto h=decode(raw);
     check("W01","actor block header decode/attest (magic, af4490, dtor thunk, 0x1b8, ctor 1, alloc seq)",actorBlockOk(h,0x140af4490,0x1400d9980)&&h.allocSeq==1459);
     raw[0x38]=1;check("W02","locked block is not purge-owned",!actorBlockOk(decode(raw),0x140af4490,0x1400d9980));raw[0x38]=0;
     put(0x18,0x140af4600,8);check("W03","wrong heap refused",!actorBlockOk(decode(raw),0x140af4490,0x1400d9980));
     check("W04","node slot arithmetic matches 58B.2 (block 0x61ab5d00, slot 17 -> node 0x61ab6230)",nodeInBlock(0x61ab6230,0x61ab5d00)&&(0x61ab6230-(0x61ab5d00+NodeFirst))/NodeStride==17&&!nodeInBlock(0x61ab6231,0x61ab5d00)&&!nodeInBlock(0x61ab5d00+0x60+0x1210,0x61ab5d00));
     Identity a{0x615f0140,0x61ab6230,0x615f00e0,0x61ab5d00,1459,1,true};Identity b=a;b.incarnation=2;
     check("W05","same storage + same alloc seq after our PoolReturn = pool reuse",classify(a,true,b)==Reuse::PoolReuseAfterRetire);
     check("W06","same storage + same alloc seq after an engine purge = anomaly",classify(a,false,b)==Reuse::Anomaly);
     b.allocSeq=1460;check("W07","same address + new alloc seq = new block (ABA disambiguated)",classify(a,false,b)==Reuse::NewBlockSameAddress);
     b.actor=0x61000000;check("W08","other address = new address; first spawn = First",classify(a,false,b)==Reuse::NewAddress&&classify(Identity{},false,b)==Reuse::First);
     TeardownLog t;t.armed=t.barrierBeforeUnload=t.unloadBeforeActorPurge=t.actorPurgeBeforeNodePurge=true;t.unloadEntries=t.unloadReturns=t.actorPurgeEntries=t.actorPurgeReturns=t.nodePurgeEntries=t.nodePurgeReturns=1;
     t.actorBlockAtEntry=t.actorBlockAbsentAtReturn=t.actorDispatched=t.dispatchIdentityOk=t.dtorEntered=t.dtorReturned=t.nodeBlockAtEntry=t.nodeBlockAbsentAtReturn=true;
     check("W09","complete ordered chain -> ArenaDestroyed",verdict(a,t)==EngineTeardownWitness::ArenaDestroyed);
     auto u=t;u.actorPurgeEntries=0;check("W10","no af4490 purge observed -> NotObserved",verdict(a,u)==EngineTeardownWitness::NotObserved);
     u=t;u.nativeCallsAfterBarrier=1;check("W11","any proxy native call after the barrier -> Unknown",verdict(a,u)==EngineTeardownWitness::Unknown);
     u=t;u.barrierBeforeUnload=false;check("W12","barrier not before Level::Unload -> Unknown",verdict(a,u)==EngineTeardownWitness::Unknown);
     u=t;u.actorBlockAbsentAtReturn=false;check("W13","our block still listed after the purge -> Unknown",verdict(a,u)==EngineTeardownWitness::Unknown);
     u=t;u.unloadEntries=2;check("W14","double Unload -> Unknown",verdict(a,u)==EngineTeardownWitness::Unknown);
     Identity none;check("W15","no recorded identity -> Unknown",verdict(none,t)==EngineTeardownWitness::Unknown);}
    // ---------- F: DOC-64 N1 foreign-thread fault mailbox ----------
    {pse::p3::ForeignFault ff;bool first=false,second=true;
     std::thread([&]{first=ff.post("FOREIGN_A");second=ff.post("FOREIGN_B");}).join();
     check("F01","fault from a foreign thread is only posted (first reason wins, later posts counted)",first&&!second&&ff.pending()&&ff.posts()==2);
     const char* got=ff.take();
     check("F02","owner thread takes it exactly once",got&&std::string_view(got)=="FOREIGN_A"&&!ff.pending()&&ff.take()==nullptr);
     check("F03","nothing pending -> no fault applied; null reason refused",ff.take()==nullptr&&!ff.post(nullptr)&&!ff.pending());}
    std::printf("TOTAL %d FAILED %d (offline; native witnesses are separate evidence)\n",total,failures);return failures?1:0;
}
