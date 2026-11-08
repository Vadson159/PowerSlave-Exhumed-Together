// DOC58B offline contract tests for the patched VisualProxyStateMachine/SnapshotRouter (no KEX, no network).
#include "visual_proxy.hpp"
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <thread>
#include <vector>

using namespace pse::proxy;
namespace {
struct Case { std::string id,name;bool pass; };
std::vector<Case> results;
void check(const char* id,const char* name,bool pass) { results.push_back({id,name,pass});std::printf("%-6s %-4s %s\n",id,pass?"PASS":"FAIL",name); }

struct MockSeam final : INativeVisualProxySeam {
    SpawnOutcome spawnResult{SpawnOutcome::Created};ProxyHandle nextHandle{7};
    UpdateOutcome updateResult{UpdateOutcome::Applied};
    RemoveOutcome removeResult{RemoveOutcome::Requested};
    std::vector<RetirementStatus> retire{RetirementStatus::Pending,RetirementStatus::Retired};
    int spawns{},updates{},removes{},polls{};ProxyCommand lastSpawn{},lastUpdate{};ProxyHandle lastHandle{};
    std::function<void()> during;
    SpawnOutcome spawn(const ProxyCommand& c,ProxyHandle& out) noexcept override { ++spawns;lastSpawn=c;if(during) during();out=spawnResult==SpawnOutcome::Created ? nextHandle : 0;return spawnResult; }
    UpdateOutcome update(ProxyHandle h,const ProxyCommand& c) noexcept override { ++updates;lastHandle=h;lastUpdate=c;if(during) during();return updateResult; }
    RemoveOutcome remove(ProxyHandle h) noexcept override { ++removes;lastHandle=h;if(during) during();return removeResult; }
    RetirementStatus retirement(ProxyHandle) noexcept override { const auto r=retire[std::min<std::size_t>(polls,retire.size()-1)];++polls;return r; }
};
pse::BridgePlayerSnapshot snap(std::uint32_t seq,std::uint32_t tick,float x=-7420.f,std::uint8_t slot=0,std::uint32_t gen=1,std::uint32_t epoch=3718259626u) {
    pse::BridgePlayerSnapshot s;s.slotId=slot;s.sequence=seq;s.serverTick=tick;s.connectionGeneration=gen;s.mapEpoch=epoch;s.entityId=0x5500u+slot;s.entityGeneration=1;
    s.posX=x;s.posY=-6775.f;s.posZ=1152.f;s.yaw=0.5f;s.pitch=0.f;return s;
}
constexpr TickContext Safe{true,true};
constexpr std::uint32_t Epoch=3718259626u;
struct Fixture {
    VisualProxyStateMachine sm{0};MockSeam seam;
    Fixture(bool ready=true) { sm.bindSimulationThread(GetCurrentThreadId());sm.configureSession(1,Epoch);if(ready) sm.onNativeMapReady(Epoch,0x1234); }
    void active() { sm.handleSnapshot(snap(1,10),true);sm.tick(&seam,Safe); }
};
}

int main(int argc,char** argv) {
    { Fixture f(false);f.sm.handleSnapshot(snap(1,10),true);const auto a=f.sm.tick(&f.seam,Safe);
      check("C01","configureSession alone never enables native Spawn (map identity not ready)",f.sm.state()==VisualProxyState::ABSENT && f.seam.spawns==0 && a==TickAction::None); }
    { Fixture f(false);const bool wrong=f.sm.onNativeMapReady(Epoch+1,5);const bool zero=f.sm.onNativeMapReady(Epoch,0);
      check("C02","native MapReady for another epoch or without identity token is refused",!wrong && !zero && !f.sm.mapReady()); }
    { Fixture f;f.sm.handleSnapshot(snap(1,10),true);const auto n1=f.sm.tick(&f.seam,{false,true});const auto n2=f.sm.tick(&f.seam,{true,false});const auto ok=f.sm.tick(&f.seam,Safe);
      check("C03","Spawn only in the attested safe phase with sector readiness; exactly one factory call",n1==TickAction::NotSafe && n2==TickAction::NotSafe && ok==TickAction::SpawnCreated && f.seam.spawns==1 && f.sm.state()==VisualProxyState::ACTIVE && f.sm.handle()==7); }
    { Fixture f;f.active();f.sm.handleSnapshot(snap(2,12,-7410),true);f.sm.handleSnapshot(snap(3,14,-7400),true);f.sm.handleSnapshot(snap(4,16,-7390),true);
      const int before=f.seam.updates;f.sm.tick(&f.seam,Safe);const auto again=f.sm.tick(&f.seam,Safe);
      check("C04","several snapshots between steps coalesce to ONE update with the latest sequence",f.seam.updates-before==1 && f.seam.lastUpdate.sequence==4 && f.seam.lastUpdate.coalesced==2 && again==TickAction::None); }
    { Fixture f;f.active();f.seam.updateResult=UpdateOutcome::Refused;f.sm.handleSnapshot(snap(2,12,-9000),true);const auto a=f.sm.tick(&f.seam,Safe);
      check("C05","adapter refusal (no write) keeps ACTIVE and does not record the refused transform as applied",a==TickAction::UpdateRefused && f.sm.state()==VisualProxyState::ACTIVE && f.sm.applied().sequence==1); }
    { Fixture f;f.active();f.seam.updateResult=UpdateOutcome::Stopped;f.sm.handleSnapshot(snap(2,12),true);f.sm.tick(&f.seam,Safe);const bool rp=f.sm.state()==VisualProxyState::REMOVE_PENDING;
      f.sm.handleSnapshot(snap(3,14),true);const auto r=f.sm.tick(&f.seam,Safe);const auto p=f.sm.tick(&f.seam,Safe);const auto d=f.sm.tick(&f.seam,Safe);
      check("C06","adapter fault -> only Remove; Remove return is NOT retirement (RETIRING until native witness)",rp && r==TickAction::RemoveRequested && p==TickAction::RetirementPending && d==TickAction::Retired && f.sm.state()==VisualProxyState::ABSENT && f.seam.removes==1 && f.seam.updates==1 && f.sm.handle()==0); }
    { Fixture f;f.active();f.seam.removeResult=RemoveOutcome::DefiniteNoCall;f.sm.onDisconnect();const auto a=f.sm.tick(&f.seam,Safe);for(int i=0;i<5;++i) f.sm.tick(&f.seam,Safe);
      check("C07","Remove refused before the native call -> QUARANTINED, no retry",a==TickAction::RemoveRefused && f.sm.state()==VisualProxyState::QUARANTINED && f.seam.removes==1); }
    { Fixture f;f.active();f.seam.removeResult=RemoveOutcome::Unknown;f.sm.requestShutdown();f.sm.tick(&f.seam,Safe);for(int i=0;i<5;++i) f.sm.tick(&f.seam,Safe);
      check("C08","Remove with unknown outcome (exception) -> QUARANTINED, never a blind second Remove",f.sm.state()==VisualProxyState::QUARANTINED && f.sm.quarantineReason()==QuarantineReason::RemoveUnknown && f.seam.removes==1 && f.sm.quiescent()); }
    { Fixture f;f.seam.spawnResult=SpawnOutcome::Unknown;f.sm.handleSnapshot(snap(1,10),true);const auto a=f.sm.tick(&f.seam,Safe);const auto rj=f.sm.handleSnapshot(snap(2,12),true);for(int i=0;i<5;++i) f.sm.tick(&f.seam,Safe);
      check("C09","Spawn exception/unknown: ownership undetermined -> QUARANTINED, no re-Spawn (Gemini kept SPAWN_PENDING)",a==TickAction::SpawnUnknown && f.sm.state()==VisualProxyState::QUARANTINED && rj==Rejection::Terminal && f.seam.spawns==1); }
    { Fixture f;f.seam.nextHandle=0;f.sm.handleSnapshot(snap(1,10),true);f.sm.tick(&f.seam,Safe);f.sm.handleSnapshot(snap(2,12),true);f.sm.tick(&f.seam,Safe);
      check("C10","Created without a handle (null-like) is not proof of no Actor -> QUARANTINED (Gemini returned to ABSENT and retried)",f.sm.state()==VisualProxyState::QUARANTINED && f.seam.spawns==1); }
    { Fixture f;f.seam.spawnResult=SpawnOutcome::Deferred;f.sm.handleSnapshot(snap(1,10),true);const auto a=f.sm.tick(&f.seam,Safe);f.seam.spawnResult=SpawnOutcome::Refused;const auto b=f.sm.tick(&f.seam,Safe);
      f.seam.spawnResult=SpawnOutcome::Created;f.sm.handleSnapshot(snap(2,12,-7400),true);const auto c=f.sm.tick(&f.seam,Safe);
      check("C11","Deferred/Refused (adapter proves no native call) keep SPAWN_PENDING; later Spawn uses the latest target",a==TickAction::SpawnDeferred && b==TickAction::SpawnRefused && c==TickAction::SpawnCreated && f.sm.spawnCalls()==1 && f.seam.lastSpawn.sequence==2); }
    { Fixture f;f.active();f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);f.sm.tick(&f.seam,Safe);f.sm.tick(&f.seam,Safe);const auto rj=f.sm.handleSnapshot(snap(9,40),true);
      check("C12","after a disconnect no new Spawn in the same session",f.sm.state()==VisualProxyState::ABSENT && rj==Rejection::SessionInactive && f.seam.spawns==1 && f.sm.retiredCount()==1); }
    { Fixture f;f.active();f.sm.onPreUnload();for(int i=0;i<5;++i) f.sm.tick(&f.seam,Safe);
      check("C13","PRE_UNLOAD with live Actor -> ORPHANED, no native Remove after barrier, never ABSENT (Gemini: LevelInvalidSkipped->ABSENT)",f.sm.state()==VisualProxyState::ORPHANED && f.seam.removes==0 && f.sm.orphanedHandle()==7 && f.sm.handle()==0); }
    { Fixture f;f.sm.handleSnapshot(snap(1,10),true);f.seam.during=[&] { f.sm.onPreUnload(); };f.sm.tick(&f.seam,Safe);f.seam.during=nullptr;for(int i=0;i<5;++i) f.sm.tick(&f.seam,Safe);
      check("C14","PRE_UNLOAD during an in-flight Spawn seam call is latched, not dropped; Actor orphaned after return",f.sm.state()==VisualProxyState::ORPHANED && f.sm.unloadDuringSeam()==1 && f.sm.teardownBarrier() && f.seam.removes==0 && f.seam.spawns==1); }
    { Fixture f;f.active();f.sm.handleSnapshot(snap(2,12),true);f.seam.during=[&] { f.sm.requestShutdown(); };f.sm.tick(&f.seam,Safe);f.seam.during=nullptr;const bool rp=f.sm.state()==VisualProxyState::REMOVE_PENDING;f.sm.tick(&f.seam,Safe);
      check("C15","shutdown requested inside an update seam call is latched -> REMOVE_PENDING -> one Remove",rp && f.seam.removes==1 && f.sm.state()==VisualProxyState::RETIRING); }
    { Fixture f;TickAction inner{};Rejection innerSnap{};f.seam.during=[&] { inner=f.sm.tick(&f.seam,Safe);innerSnap=f.sm.handleSnapshot(snap(5,30),true); };f.sm.handleSnapshot(snap(1,10),true);f.sm.tick(&f.seam,Safe);
      check("C16","reentrant tick/snapshot from inside a seam call are blocked without nested native calls",inner==TickAction::ReentrancyBlocked && innerSnap==Rejection::ReentrancyBlocked && f.seam.spawns==1 && f.sm.reentrancyBlocked()==2 && f.sm.state()==VisualProxyState::ACTIVE); }
    { Fixture f;f.active();Rejection r{};TickAction t{};std::thread([&] { r=f.sm.handleSnapshot(snap(2,12),true);t=f.sm.tick(&f.seam,Safe);f.sm.onPreUnload();f.sm.requestShutdown(); }).join();
      check("C17","calls from a non-simulation thread are rejected and change nothing",r==Rejection::AffinityViolation && t==TickAction::AffinityViolation && f.sm.state()==VisualProxyState::ACTIVE && !f.sm.teardownBarrier() && f.sm.affinityViolations()==4); }
    { VisualProxyStateMachine sm{0};const bool a=sm.bindSimulationThread(GetCurrentThreadId());const bool b=sm.bindSimulationThread(GetCurrentThreadId()+1);
      check("C18","simulation thread binds once; rebinding refused",a && !b); }
    { Fixture f;f.active();f.sm.configureSession(2,Epoch+7);const bool rp=f.sm.state()==VisualProxyState::REMOVE_PENDING;const auto old=f.sm.handleSnapshot(snap(2,12),true);
      check("C19","new session while ACTIVE: old Actor removed, not adopted; old-session snapshots rejected",rp && old!=Rejection::None && !f.sm.mapReady()); }
    { Fixture f;f.active();f.sm.onPreUnload();f.sm.configureSession(2,Epoch+7);const bool mr=f.sm.onNativeMapReady(Epoch+7,99);f.sm.handleSnapshot(snap(1,10,-7420,0,2,Epoch+7),true);f.sm.tick(&f.seam,Safe);
      check("C20","new session/epoch never lowers the teardown barrier and never re-Spawns over an orphan",!mr && f.sm.teardownBarrier() && f.sm.state()==VisualProxyState::ORPHANED && f.seam.spawns==1); }
    { Fixture f;f.active();const auto dup=f.sm.handleSnapshot(snap(1,10),true);const auto older=f.sm.handleSnapshot(snap(5,8),true);
      auto nan=snap(6,20);nan.posX=std::numeric_limits<float>::quiet_NaN();const auto nf=f.sm.handleSnapshot(nan,true);
      auto ent=snap(7,22);ent.entityGeneration=2;const auto em=f.sm.handleSnapshot(ent,true);
      check("C21","replayed/older-tick/nonfinite snapshots rejected; entity change while ACTIVE -> REMOVE_PENDING",dup==Rejection::StaleSequence && older==Rejection::StaleSequence && nf==Rejection::NonfiniteTransform && em==Rejection::EntityMismatch && f.sm.state()==VisualProxyState::REMOVE_PENDING); }
    { Fixture f;const auto wrong=f.sm.handleSnapshot(snap(1,10,-7420,1),true);const auto ng=f.sm.handleSnapshot(snap(1,10),false);
      check("C22","state machine accepts only its own slot and only in gameplay",wrong==Rejection::WrongSlot && ng==Rejection::NotGameplay && f.sm.state()==VisualProxyState::ABSENT); }
    { SnapshotRouter<1> r;r.bind(GetCurrentThreadId());Rejection rj{};const auto before=r.route(snap(1,10),true,rj);r.configureSession(1,1,Epoch);r.onNativeMapReady(Epoch,5);
      const auto local=r.route(snap(1,10,-7420,1),true,rj);const auto invalid=r.route(snap(2,12,-7420,8),true,rj);const auto host=r.route(snap(3,14,-7420,0),true,rj);const auto other=r.route(snap(4,16,-7420,2),true,rj);
      check("C23","router: no local slot before Welcome; local slot excluded; slot>=8 invalid; slot0 routed; capacity 1 defers other remote slots",
        before==RouteResult::NoLocalSlot && local==RouteResult::LocalExcluded && invalid==RouteResult::InvalidSlot && host==RouteResult::Routed && other==RouteResult::CapacityDeferred && r.proxy(1)==nullptr && r.proxy(0) && r.proxy(0)->state()==VisualProxyState::SPAWN_PENDING); }
    { SnapshotRouter<7> r;r.bind(GetCurrentThreadId());Rejection rj{};r.configureSession(3,1,Epoch);r.onNativeMapReady(Epoch,5);int routed=0;
      for(std::uint8_t s=0;s<8;++s) routed+=r.route(snap(10+s,20,-7420,s),true,rj)==RouteResult::Routed;
      bool own=true;for(std::uint8_t s=0;s<8;++s) if(s!=3) own=own && r.proxy(s) && r.proxy(s)->targetSlot()==s && r.proxy(s)->target().slot==s;
      check("C24","8-slot addressing: 7 remote slots demultiplex to their own proxies, local slot 3 has none",routed==7 && own && r.proxy(3)==nullptr); }
    { SnapshotRouter<1> r;r.bind(GetCurrentThreadId());Rejection rj{};r.configureSession(1,1,Epoch);r.onNativeMapReady(Epoch,5);r.route(snap(1,10),true,rj);
      r.configureSession(1,2,Epoch+1);const auto old=r.route(snap(2,12),true,rj);const auto oldRj=rj;
      check("C25","slot id reused by a new session: previous-session records rejected, new map readiness required",old==RouteResult::ProxyRejected && (oldRj==Rejection::IdentityMismatch || oldRj==Rejection::NotReady || oldRj==Rejection::Removing) && !r.proxy(0)->mapReady()); }
    { Fixture f;f.active();f.seam.retire={RetirementStatus::Pending,RetirementStatus::Failed};f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);f.sm.tick(&f.seam,Safe);f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);
      check("C26","retirement witness failure -> QUARANTINED; disconnect during RETIRING issues no second Remove",f.sm.state()==VisualProxyState::QUARANTINED && f.sm.quarantineReason()==QuarantineReason::RetirementFailed && f.seam.removes==1); }
    { Fixture f;f.sm.handleSnapshot(snap(1,10),true);f.sm.requestShutdown();f.sm.tick(&f.seam,Safe);
      check("C27","shutdown during SPAWN_PENDING: clean abort, no factory call",f.sm.state()==VisualProxyState::ABSENT && f.seam.spawns==0 && f.sm.quiescent()); }
    { Fixture f;f.active();f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);const int polls=f.seam.polls;f.sm.onPreUnload();for(int i=0;i<4;++i) f.sm.tick(&f.seam,Safe);
      check("C28","teardown while RETIRING -> ORPHANED, no further native witness/Remove calls",f.sm.state()==VisualProxyState::ORPHANED && f.seam.polls==polls && f.seam.removes==1); }
    { Fixture f;f.active();const bool q1=f.sm.quiescent();f.sm.onDisconnect();const bool q2=f.sm.quiescent();f.sm.tick(&f.seam,Safe);const bool q3=f.sm.quiescent();f.sm.tick(&f.seam,Safe);f.sm.tick(&f.seam,Safe);
      check("C29","quiescence (seam detach permission) only in ABSENT/terminal, never while ACTIVE/REMOVE_PENDING/RETIRING",!q1 && !q2 && !q3 && f.sm.quiescent() && f.sm.state()==VisualProxyState::ABSENT); }
    { Fixture f;f.active();f.sm.handleSnapshot(snap(2,12,-7400),true);f.sm.tick(&f.seam,{false,true});const bool held=f.sm.dirty();f.sm.tick(&f.seam,Safe);
      check("C30","no update outside a safe simulation step; pending target applied at the next one (background client)",held && !f.sm.dirty() && f.seam.updates==1 && f.seam.lastHandle==7 && f.seam.lastUpdate.sequence==2); }
    { Fixture f;f.active();const auto h=f.sm.handle();f.sm.onDisconnect();f.sm.tick(&f.seam,Safe);
      check("C31","seam only ever sees the opaque adapter handle",f.seam.lastHandle==h && h==7); }
    { Fixture f;f.active();f.sm.requestRemove();const auto rj=f.sm.handleSnapshot(snap(2,12),true);f.sm.tick(&f.seam,Safe);f.sm.requestRemove();f.sm.tick(&f.seam,Safe);f.sm.tick(&f.seam,Safe);
      check("C32","adapter-requested cleanup: no more intake, exactly one Remove, retirement witnessed",rj==Rejection::ShutdownLatched && f.seam.removes==1 && f.seam.updates==0 && f.sm.state()==VisualProxyState::ABSENT); }

    int failed=0;for(const auto& c:results) failed+=!c.pass;
    if(argc>1) {
        FILE* f{};if(fopen_s(&f,argv[1],"wx")==0 && f) {
            std::fprintf(f,"{\n  \"suite\": \"DOC58B proxy58b_contract\",\n  \"total\": %zu,\n  \"passed\": %zu,\n  \"failed\": %d,\n  \"cases\": [\n",results.size(),results.size()-failed,failed);
            for(std::size_t i=0;i<results.size();++i) std::fprintf(f,"    {\"id\": \"%s\", \"name\": \"%s\", \"status\": \"%s\"}%s\n",results[i].id.c_str(),results[i].name.c_str(),results[i].pass?"PASS":"FAIL",i+1<results.size()?",":"");
            std::fprintf(f,"  ]\n}\n");std::fclose(f);
        }
    }
    std::printf("TOTAL %zu FAILED %d\n",results.size(),failed);
    return failed ? 1 : 0;
}
