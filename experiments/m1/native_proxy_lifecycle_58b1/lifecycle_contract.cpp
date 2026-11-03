// Characterization of accepted DOC58B, never a native acceptance claim.
#include "visual_proxy_58b.hpp"
#include <cstdio>
#include <functional>
using namespace pse::proxy::doc58b;
constexpr std::uint32_t Epoch=77;
int failures=0,total=0;
void check(const char* name,bool ok){++total;failures+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",name);}
struct Seam final:INativeVisualProxySeam {
    int spawns{},updates{},removes{},polls{};ProxyHandle next=100;
    std::function<void()> during;
    SpawnOutcome spawn(const ProxyCommand&,ProxyHandle& h) noexcept override{++spawns;h=next;if(during)during();return SpawnOutcome::Created;}
    UpdateOutcome update(ProxyHandle,const ProxyCommand&) noexcept override{++updates;if(during)during();return UpdateOutcome::Applied;}
    RemoveOutcome remove(ProxyHandle) noexcept override{++removes;if(during)during();return RemoveOutcome::Requested;}
    RetirementStatus retirement(ProxyHandle) noexcept override{++polls;if(during)during();return RetirementStatus::Retired;}
    int writes() const{return spawns+updates+removes;}
};
pse::BridgePlayerSnapshot sample(std::uint32_t seq=1,std::uint32_t tick=10){pse::BridgePlayerSnapshot s;s.slotId=0;s.sequence=seq;s.serverTick=tick;s.connectionGeneration=1;s.mapEpoch=Epoch;s.entityId=11;s.entityGeneration=1;s.posX=-7420;s.posY=-6775;s.posZ=1152;return s;}
struct Fixture{VisualProxyStateMachine sm{0};Seam seam;Fixture(){sm.bindSimulationThread(GetCurrentThreadId());sm.configureSession(1,Epoch);sm.onNativeMapReady(Epoch,1);}void active(std::uint32_t entityGeneration=1){auto s=sample();s.entityGeneration=entityGeneration;sm.handleSnapshot(s,true);sm.tick(&seam,{true,true});}};
int main(){
    {Fixture f;f.sm.handleSnapshot(sample(),true);f.sm.onPreUnload();f.sm.tick(&f.seam,{true,true});check("pending_cancel_no_factory",f.seam.spawns==0&&f.sm.state()==VisualProxyState::ABSENT);}
    {Fixture f;f.seam.during=[&]{f.sm.onPreUnload();f.sm.requestShutdown();};f.active();check("inflight_spawn_preserves_both_intents",f.sm.state()==VisualProxyState::ORPHANED&&f.sm.teardownBarrier()&&f.sm.shutdownLatched()&&f.sm.orphanedHandle()==100);const int n=f.seam.writes();f.sm.tick(&f.seam,{true,true});check("orphan_no_late_remove",n==f.seam.writes());}
    {Fixture f;f.active();f.sm.handleSnapshot(sample(2,11),true);f.seam.during=[&]{f.sm.onPreUnload();};f.sm.tick(&f.seam,{true,true});check("inflight_update_intent_latched",f.sm.state()==VisualProxyState::ORPHANED&&f.sm.unloadDuringSeam()==1);}
    {Fixture f;f.active();f.sm.onDisconnect();check("remove_requested",f.sm.tick(&f.seam,{true,true})==TickAction::RemoveRequested&&f.sm.state()==VisualProxyState::RETIRING);check("witness_before_absent",f.sm.tick(&f.seam,{true,true})==TickAction::Retired&&f.sm.state()==VisualProxyState::ABSENT&&f.sm.handle()==0);f.sm.configureSession(2,Epoch);f.sm.onNativeMapReady(Epoch,2);auto s=sample();s.connectionGeneration=2;const auto r=f.sm.handleSnapshot(s,true);f.sm.tick(&f.seam,{true,true});check("CHARACTERIZED_reconnect_blocker_disconnect_latched",r==Rejection::SessionInactive&&f.seam.spawns==1);}
    {Fixture f;f.active();f.sm.onPreUnload();f.sm.configureSession(2,Epoch+1);check("map_transition_cannot_rearm_orphan",!f.sm.onNativeMapReady(Epoch+1,2)&&f.sm.state()==VisualProxyState::ORPHANED);}
    {Fixture f;f.sm.configureSession(2,Epoch+1);auto s=sample();s.connectionGeneration=2;s.mapEpoch=Epoch+1;const auto r=f.sm.handleSnapshot(s,true);f.sm.tick(&f.seam,{true,true});check("before_new_mapready_no_writes",r==Rejection::NotReady&&f.seam.writes()==0);}
    const char* names[]={"old_session_intake_no_writes","old_map_epoch_intake_no_writes","previous_entity_generation_intake_no_writes","duplicate_sequence_intake_no_writes","older_tick_intake_no_writes","post_disconnect_intake_no_writes","during_teardown_intake_no_writes"};
    for(int kind=0;kind<7;++kind){Fixture f;f.active(kind==2?2u:1u);auto s=sample(2,11);if(kind==0){f.sm.configureSession(2,Epoch);s.connectionGeneration=1;}if(kind==1){f.sm.configureSession(1,Epoch+1);f.sm.onNativeMapReady(Epoch+1,2);s.mapEpoch=Epoch;}if(kind==3)s.sequence=1;if(kind==4)s.serverTick=9;if(kind==5)f.sm.onDisconnect();if(kind==6)f.sm.onPreUnload();const int before=f.seam.writes();auto r=f.sm.handleSnapshot(s,true);check(names[kind],r!=Rejection::None&&f.seam.writes()==before);}
    {Fixture f;f.active();f.sm.onDisconnect();f.seam.during=[&]{f.sm.onPreUnload();f.sm.requestShutdown();};f.sm.tick(&f.seam,{true,true});const int n=f.seam.writes();f.sm.tick(&f.seam,{true,true});check("inflight_remove_does_not_drop_unload",f.sm.state()==VisualProxyState::ORPHANED&&f.sm.shutdownLatched()&&f.seam.removes==1&&f.seam.writes()==n);}
    {Fixture f;f.active();const auto h=f.sm.handle();f.sm.onDisconnect();f.sm.tick(&f.seam,{true,true});f.seam.during=[&]{f.sm.onPreUnload();};f.sm.tick(&f.seam,{true,true});check("retirement_witness_before_reentrant_unload",f.sm.state()==VisualProxyState::ABSENT&&f.sm.handle()==0&&f.sm.retiredCount()==1&&h!=0&&f.sm.teardownBarrier());}
    // The baseline has no rearm API; exercising two incarnations and native ABA is blocked.
    std::printf("TOTAL %d FAILED %d; second incarnation and native ABA NOT_EXECUTED\n",total,failures);return failures?1:0;
}
