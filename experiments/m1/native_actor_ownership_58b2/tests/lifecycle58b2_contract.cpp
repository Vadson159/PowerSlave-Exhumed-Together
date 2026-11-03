// DOC58B.2 offline contract tests for the isolated engine-owned-unload patch (no KEX, no network).
// A mock seam stands in for the adapter; native retirement/teardown witnesses here are test inputs, never native proof.
#include "visual_proxy_58b2.hpp"
#include <cstdio>
#include <functional>
using namespace pse::proxy::doc58b2;
namespace {
int failures=0,total=0;
void check(const char* id,const char* name,bool ok){++total;failures+=!ok;std::printf("%-4s %s %s\n",id,ok?"PASS":"FAIL",name);}
constexpr std::uint32_t Epoch=77;constexpr TickContext Safe{true,true};
struct Seam final:INativeVisualProxySeam {
    int spawns{},updates{},removes{},polls{};ProxyHandle next=100;RemoveOutcome removeResult{RemoveOutcome::Requested};
    RetirementStatus retire{RetirementStatus::Pending};std::function<void()> during;
    SpawnOutcome spawn(const ProxyCommand&,ProxyHandle& h) noexcept override{++spawns;h=next;if(during)during();return SpawnOutcome::Created;}
    UpdateOutcome update(ProxyHandle,const ProxyCommand&) noexcept override{++updates;if(during)during();return UpdateOutcome::Applied;}
    RemoveOutcome remove(ProxyHandle) noexcept override{++removes;if(during)during();return removeResult;}
    RetirementStatus retirement(ProxyHandle) noexcept override{++polls;if(during)during();return retire;}
    int calls() const{return spawns+updates+removes+polls;}
};
pse::BridgePlayerSnapshot sample(std::uint32_t seq=1,std::uint32_t tick=10){pse::BridgePlayerSnapshot s;s.slotId=0;s.sequence=seq;s.serverTick=tick;s.connectionGeneration=1;s.mapEpoch=Epoch;s.entityId=11;s.entityGeneration=1;s.posX=-7420;s.posY=-6775;s.posZ=1152;return s;}
DWORD g_tid=0;DWORD fakeTid(){return g_tid;}
struct Fixture{VisualProxyStateMachine sm;Seam seam;explicit Fixture(VisualProxyStateMachine::ThreadIdFn f=&::GetCurrentThreadId):sm(0,f){sm.bindSimulationThread(f());sm.configureSession(1,Epoch);sm.onNativeMapReady(Epoch,1);}
    void active(){sm.handleSnapshot(sample(),true);sm.tick(&seam,Safe);}};
}
int main(){
    {Fixture f;f.active();const auto h=f.sm.handle();f.sm.onPreUnload();const int n=f.seam.calls();
     check("T01","live ACTIVE at barrier -> ORPHANED/LiveAtBarrier, no native call after barrier",f.sm.state()==VisualProxyState::ORPHANED&&f.sm.orphanCause()==OrphanCause::LiveAtBarrier&&f.sm.orphanedHandle()==h&&f.sm.handle()==0);
     f.sm.tick(&f.seam,Safe);f.sm.handleSnapshot(sample(2,11),true);f.sm.onDisconnect();f.sm.requestRemove();f.sm.requestShutdown();f.sm.tick(&f.seam,Safe);
     check("T02","no Spawn/Update/Remove/retirement poll is ever issued for the orphan",f.seam.calls()==n&&!f.sm.nativeQueueHazard());
     const bool ok=f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed);
     check("T03","attested arena witness -> ENGINE_RETIRED (terminal, quiescent, opaque handle invalid)",ok&&f.sm.state()==VisualProxyState::ENGINE_RETIRED&&f.sm.terminal()&&f.sm.quiescent()&&f.sm.handle()==0&&f.sm.engineRetiredCount()==1);
     check("T04","duplicate witness refused, still exactly one engine retirement",!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed)&&f.sm.engineRetiredCount()==1);
     f.sm.configureSession(2,Epoch+1);const bool ready=f.sm.onNativeMapReady(Epoch+1,9);auto s=sample();s.connectionGeneration=2;s.mapEpoch=Epoch+1;f.sm.handleSnapshot(s,true);f.sm.tick(&f.seam,Safe);
     check("T05","ENGINE_RETIRED does not rearm: new session/map cannot Spawn in this milestone",!ready&&f.seam.spawns==1&&f.seam.calls()==n);}
    {Fixture f;f.active();const auto h=f.sm.handle();
     check("T06","witness before the barrier is refused (Actor still proxy-owned)",!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed)&&f.sm.state()==VisualProxyState::ACTIVE);
     f.sm.onPreUnload();
     check("T07","witness for another handle is refused",!f.sm.onEngineTeardownWitness(h+1,EngineTeardownWitness::ArenaDestroyed)&&f.sm.state()==VisualProxyState::ORPHANED);
     check("T08","Unknown / NotObserved witness keeps ORPHANED (never ABSENT by default)",!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::Unknown)&&!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::NotObserved)&&f.sm.state()==VisualProxyState::ORPHANED&&f.sm.witnessRefused()==4); // T06 pre-barrier + T07 + two here
     check("T09","handle 0 is refused",!f.sm.onEngineTeardownWitness(0,EngineTeardownWitness::ArenaDestroyed));}
    {Fixture f;f.active();const auto h=f.sm.handle();f.sm.onDisconnect();const auto a=f.sm.tick(&f.seam,Safe);f.sm.onPreUnload();
     check("T10","Remove issued but not retired at barrier -> RemoveInFlightAtBarrier, nativeQueueHazard",a==TickAction::RemoveRequested&&f.sm.state()==VisualProxyState::ORPHANED&&f.sm.orphanCause()==OrphanCause::RemoveInFlightAtBarrier&&f.sm.nativeQueueHazard());
     check("T11","queued-Remove orphan can never be witnessed away (fail-closed)",!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed)&&f.sm.state()==VisualProxyState::ORPHANED&&f.seam.removes==1);}
    {Fixture f;f.active();const auto h=f.sm.handle();f.sm.onDisconnect();f.sm.onPreUnload();
     check("T12","Remove intent latched but no native call yet -> RemovePendingNoCall, zero Remove calls",f.sm.orphanCause()==OrphanCause::RemovePendingNoCall&&f.seam.removes==0&&!f.sm.nativeQueueHazard());
     check("T13","RemovePendingNoCall orphan accepts the engine witness",f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed)&&f.sm.state()==VisualProxyState::ENGINE_RETIRED&&f.seam.removes==0);}
    {Fixture f;f.active();const auto h=f.sm.handle();f.sm.onDisconnect();f.seam.during=[&]{f.sm.onPreUnload();};f.sm.tick(&f.seam,Safe);
     check("T14","barrier latched during the native Remove call -> hazard, intent not lost",f.sm.state()==VisualProxyState::ORPHANED&&f.sm.orphanCause()==OrphanCause::RemoveInFlightAtBarrier&&f.sm.unloadDuringSeam()==1&&!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed));}
    {Fixture f;f.active();const auto h=f.sm.handle();f.seam.removeResult=RemoveOutcome::Unknown;f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);f.sm.onPreUnload();
     check("T15","QUARANTINED at barrier -> QuarantinedAtBarrier, witness refused",f.sm.orphanCause()==OrphanCause::QuarantinedAtBarrier&&!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed)&&f.sm.state()==VisualProxyState::ORPHANED);}
    {Fixture f;f.sm.handleSnapshot(sample(),true);f.sm.onPreUnload();f.sm.tick(&f.seam,Safe);
     check("T16","SPAWN_PENDING at barrier -> ABSENT, no factory call, nothing to witness",f.sm.state()==VisualProxyState::ABSENT&&f.seam.spawns==0&&!f.sm.onEngineTeardownWitness(100,EngineTeardownWitness::ArenaDestroyed));}
    {Fixture f;f.active();const auto h=f.sm.handle();f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);f.seam.retire=RetirementStatus::Retired;f.sm.tick(&f.seam,Safe);f.sm.onPreUnload();
     check("T17","retired before the barrier stays ABSENT; teardown witness refused",f.sm.state()==VisualProxyState::ABSENT&&!f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed)&&f.sm.retiredCount()==1);}
    {g_tid=1;Fixture f(&fakeTid);f.active();const auto h=f.sm.handle();f.sm.onPreUnload();g_tid=2;const bool off=f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed);g_tid=1;
     check("T18","witness from a non-simulation thread is refused (affinity)",!off&&f.sm.state()==VisualProxyState::ORPHANED&&f.sm.affinityViolations()==1);
     check("T19","same witness on the attested simulation thread is accepted",f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed));}
    {Fixture f;f.active();const auto h=f.sm.handle();bool inner=true;f.sm.handleSnapshot(sample(2,11),true);f.seam.during=[&]{f.sm.onPreUnload();inner=f.sm.onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed);};f.sm.tick(&f.seam,Safe);
     check("T20","reentrant witness during a seam call is refused; barrier intent kept",!inner&&f.sm.state()==VisualProxyState::ORPHANED&&f.sm.reentrancyBlocked()==1);}
    {SnapshotRouter<1> r;r.bind(GetCurrentThreadId());r.configureSession(1,1,Epoch);r.onNativeMapReady(Epoch,1);Rejection rj;r.route(sample(),true,rj);Seam seam;auto* p=r.proxy(0);p->tick(&seam,Safe);const auto h=p->handle();
     r.forEach([](VisualProxyStateMachine& m){m.onPreUnload();});const bool before=r.quiescent();p->onEngineTeardownWitness(h,EngineTeardownWitness::ArenaDestroyed);
     check("T21","router quiescent for ORPHANED and ENGINE_RETIRED (shutdown never waits on KEX)",before&&r.quiescent()&&p->state()==VisualProxyState::ENGINE_RETIRED);}
    std::printf("TOTAL %d FAILED %d (offline; native witnesses are separate evidence)\n",total,failures);return failures?1:0;
}
