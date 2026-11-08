// P16 policy tests. No KEX, native pointers, hooks, or runtime acceptance.
#include "respawn_p16.hpp"
#include "net_protocol.h"
#include <cstdio>
#include <type_traits>
using namespace pse::respawn_p16;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
constexpr Gates Safe{true,true,true,true};
constexpr Identity Host{0,1,7},Client{1,9,7};
struct Fixture {
    Team team;Plan plan{};Confirmation cached{},confirmed{};
    Fixture(){team.map(7,true);team.connect(Host,1);team.connect(Client,1);}
    Result request(Identity id=Client,std::uint32_t death=1,Gates gates=Safe){return team.request({id,death},gates,plan,cached);}
    Result complete(bool ok=true,Loadout kit={}){return team.complete(plan,Safe,ok,kit,confirmed);}
};
void manual(){
    Fixture f;
    check(f.team.counts().alive==2&&!f.team.counts().totalPartyKill(),"two independently alive slots");
    check(f.request()==Result::Stale,"alive request with forged death counter rejected");
    check(f.team.observeDeath(Client,1)==Result::Accepted,"native death witness accepted");
    check(f.team.observeDeath(Client,1)==Result::Replay&&f.team.slot(1)->death==1,"repeated dead snapshot increments once");
    check(f.team.slot(1)->life==Life::Dead&&f.team.slot(0)->life==Life::Alive,"death alone never auto-respawns");
    check(f.request()==Result::Accepted&&f.plan.oldGeneration==1&&f.plan.newGeneration==2,"manual request creates new-generation plan");
    const auto issued=f.plan;
    check(f.request()==Result::Busy&&f.plan==issued,"duplicate pending request never issues second plan");
    check(f.complete()==Result::Accepted&&f.confirmed.generation==2,"native readback commits respawn");
    check(f.team.slot(1)->life==Life::Alive&&f.team.slot(1)->death==1,"death sequence retained across incarnation");
    check(f.team.slot(0)->generation==1&&f.team.slot(0)->death==0,"teammate identity unaffected");
    check(f.request()==Result::Replay&&f.cached==f.confirmed,"lost confirm replays exact cached result without native mutation");
    check(f.team.complete(issued,Safe,true,{},f.confirmed)==Result::Stale,"duplicate native completion rejected");
    check(f.team.observeDeath(Client,1)==Result::Stale,"late death observation for retired victim cannot kill fresh incarnation");
    check(f.team.observeDeath(Client,2)==Result::Accepted,"fresh incarnation death observed");
    check(f.request(Client,1)==Result::Stale,"previous death request cannot revive second death");
    check(f.request(Client,2)==Result::Accepted&&f.plan.ticket>issued.ticket&&f.plan.newGeneration==3,"second death uses distinct ticket");
    check(f.complete()==Result::Accepted,"second independent native respawn commits");
}
void routing(){
    Fixture f;f.team.observeDeath(Client,1);
    for(auto id:{Identity{1,8,7},Identity{1,10,7},Identity{1,9,6},Identity{1,9,8},Identity{8,9,7},Identity{1,0,7}})
        check(f.request(id)==Result::Stale&&f.team.slot(1)->life==Life::Dead,"stale/foreign request has no effect");
    check(f.request(Client,0)==Result::Stale&&f.request(Client,2)==Result::Stale,"zero/future death sequence rejected");
    for(auto g:{Gates{false,true,true,true},Gates{true,false,true,true},Gates{true,true,false,true},Gates{true,true,true,false}})
        check(f.request(Client,1,g)==Result::Unsafe&&f.team.slot(1)->life==Life::Dead,"thread/loading/unload/cinematic gates preserve dead state");
    check(f.request()==Result::Accepted,"safe retry after nonmutating gate refusal");
    auto forged=f.plan;++forged.ticket;
    check(f.team.complete(forged,Safe,true,{},f.confirmed)==Result::Stale,"forged native completion ticket rejected");
    check(f.team.disconnect({1,8,7})==Result::Stale,"foreign disconnect cannot evict occupant");
    check(f.team.disconnect(Client)==Result::Accepted,"disconnect retires pending plan");
    check(f.complete()==Result::Stale,"completion after disconnect ignored");
    check(f.team.connect(Client,2)==Result::Stale,"old connection generation cannot rejoin");
    const Identity fresh{1,10,7};
    check(f.team.connect(fresh,1)==Result::Stale&&f.team.connect(fresh,2)==Result::Stale,"slot reuse cannot reuse prior or interrupted native generation");
    check(f.team.connect(fresh,3)==Result::Accepted,"slot reuse requires fresh connection and incarnation");
    check(f.request()==Result::Stale&&f.team.slot(1)->life==Life::Alive,"old request cannot affect reused slot");
    check(f.team.map(8,false)==Result::Unsafe&&f.team.map(7,true)==Result::Stale,"epoch requires barrier and monotonic change");
    check(f.team.map(8,true)==Result::Accepted&&f.team.counts().connected==0,"map barrier retires all old roster entries");
    check(f.team.complete(f.plan,Safe,true,{},f.confirmed)==Result::Stale,"old map completion cannot revive new map");
}
void failure(){
    for(unsigned reason=0;reason<4;++reason){
        Fixture f;f.team.observeDeath(Client,1);f.request();
        auto kit=Loadout{};if(reason==1)kit.weapons=255;if(reason==2)kit.ammo[3]=1;
        auto gates=Safe;if(reason==3)gates.outsideUnload=false;
        check(f.team.complete(f.plan,gates,reason!=0,kit,f.confirmed)==Result::Failed,"partial native failure/loadout mismatch/unload fails closed");
        check(f.team.slot(1)->life==Life::Faulted&&f.request()==Result::Invalid,"failed native mutation is not retried");
        check(f.team.counts().faulted==1&&!f.team.counts().totalPartyKill(),"faulted slot is never reported alive or safely dead");
    }
    Team team;team.map(1,true);const Identity id{0,1,1};team.connect(id,UINT32_MAX);team.observeDeath(id,UINT32_MAX);
    Plan p;Confirmation c;
    check(team.request({id,1},Safe,p,c)==Result::Exhausted&&team.slot(0)->life==Life::Faulted,"generation exhaustion never wraps");
}
void totalPartyKill(){
    for(unsigned n=2;n<=Capacity;++n){
        Team team;team.map(4,true);
        for(unsigned slot=0;slot<n;++slot){
            const Identity id{static_cast<std::uint8_t>(slot),slot+1,4};
            check(team.connect(id,1)==Result::Accepted,"capacity join");
            check(team.observeDeath(id,1)==Result::Accepted,"capacity native death");
        }
        auto count=team.counts();
        check(count.connected==n&&count.dead==n&&count.alive==0&&count.totalPartyKill(),"2..8 TPK counted without a map reload");
        const Identity id{static_cast<std::uint8_t>(n-1),n,4};Plan p;Confirmation c;
        check(team.request({id,1},Safe,p,c)==Result::Accepted,"any player can request when entire team dead");
        check(team.counts().respawning==1&&team.counts().dead==n-1,"in-progress spawn not counted alive");
        check(team.complete(p,Safe,true,{},c)==Result::Accepted,"independent TPK recovery");
        check(team.counts().alive==1&&team.counts().dead==n-1,"remaining players stay dead until their own E");
        check(team.slot(0)->death==1,"TPK recovery does not reset teammate history");
    }
    Team one;one.map(1,true);one.connect({0,1,1},1);one.observeDeath({0,1,1},1);
    check(!one.counts().totalPartyKill(),"single-player diagnostic is not coop TPK evidence");
}
void input(){
    DeadInput input;input.bind(Client,1);
    check(!input.sample(true,1,true),"initial held E disarmed");
    input.sample(false,2,true);check(!input.sample(true,3,true),"E while alive does not send");
    input.observe(true,1);
    check(!input.sample(true,4,true),"held E through death does not send");
    input.sample(false,5,true);
    const auto request=input.sample(true,6,true);
    check(request&&*request==Request{Client,1},"fresh E edge requests exact own death");
    for(unsigned i=0;i<20;++i){input.sample(false,10+i*2,true);check(!input.sample(true,11+i*2,true),"spam while request pending suppressed");}
    check(!input.sample(true,2000,true),"timeout does not auto-request while held");
    input.sample(false,2001,true);check(input.sample(true,2002,true).has_value(),"new E edge after timeout retries same idempotent death");
    Confirmation c{{Client,1},2,{}};auto wrong=c;++wrong.request.who.connection;
    check(!input.confirm(wrong,true)&&!input.confirm(c,false),"foreign confirmation and missing native rebind refused");
    check(input.confirm(c,true)&&!input.confirm(c,true),"native confirmed incarnation accepted exactly once");
    input.observe(true,1);input.sample(false,2003,true);
    check(!input.sample(true,2004,true),"late old dead snapshot cannot reopen confirmed death");
    input.observe(true,2);check(!input.sample(true,2003,true),"held E across next death remains disarmed");
    input.sample(false,2004,true);check(input.sample(true,2005,true).has_value(),"next death accepts distinct edge");
    input.invalidate();input.sample(false,2006,true);check(!input.sample(true,2007,true),"disconnect closes E input");
    input.bind(Client,1);input.sample(false,3000,true);input.observe(true,1);input.sample(true,3001,true);input.sample(false,5000,true);
    check(input.confirm(c,true),"late valid confirm after timeout remains authorized by the sent death request");
    input.bind(Client,1);input.sample(false,3000,true);input.observe(true,1);
    check(!input.sample(true,3001,false),"menu/cinematic/focus unsafe input rejected");
    check(!input.sample(true,3002,true),"closing unsafe context never manufactures a held-key edge");
}
void weapons(){
    Fixture f;
    check(f.team.registerWeapon(7,44,3)==Result::Accepted,"verified original weapon identity registered");
    check(f.team.registerWeapon(7,44,3)==Result::Replay&&f.team.registerWeapon(7,44,4)==Result::Invalid,"pickup identity cannot change type");
    check(f.team.weaponAvailable(Host,44)&&f.team.weaponAvailable(Client,44),"weapon availability is per player");
    check(f.team.weaponGranted(Client,44,1,false)==Result::Unsafe&&f.team.weaponAvailable(Client,44),"no ledger grant without native touch/readback");
    check(f.team.weaponGranted(Client,44,1,true)==Result::Accepted,"first native grant recorded");
    check(!f.team.weaponAvailable(Client,44)&&f.team.weaponAvailable(Host,44),"same-life duplicate denied; teammate remains eligible");
    check(f.team.weaponGranted(Client,44,1,true)==Result::Unsafe,"duplicate grant cannot farm ammo");
    f.team.observeDeath(Client,1);check(!f.team.weaponAvailable(Client,44),"dead player cannot collect");
    f.request();f.complete();
    check(f.team.weaponAvailable(Client,44)&&f.team.weaponAvailable(Host,44),"BLK11 policy restores availability only after committed respawn");
    check(f.confirmed.loadout==Loadout{}&&f.confirmed.loadout.weapons==3,"respawn retains starter loadout, not previous heavy weapon");
    check(f.team.weaponGranted(Client,44,1,true)==Result::Stale,"old-life touch cannot consume new entitlement");
    check(f.team.weaponGranted(Client,44,2,true)==Result::Accepted,"reacquisition records new-life grant");
    check(!f.team.weaponAvailable(Client,44)&&!f.team.weaponAvailable(Client,999),"consumed and unknown originals unavailable");
    for(unsigned id=1000;id<1000+PickupCapacity-1;++id)
        check(f.team.registerWeapon(7,id,2)==Result::Accepted,"bounded pickup ledger fills");
    check(f.team.registerWeapon(7,9999,2)==Result::Exhausted,"capacity exhaustion fails without eviction");
    check(!f.team.weaponAvailable(Client,44),"full ledger preserves old consumption");
    check(f.team.registerWeapon(6,9998,2)==Result::Invalid&&f.team.registerWeapon(7,9998,8)==Result::Invalid,"old map and invalid weapon refused");
    f.team.map(8,true);const Identity next{1,10,8};f.team.connect(next,1);
    check(!f.team.weaponAvailable(next,44),"new map clears prior pickup provenance");
}
void wire(){
    // Existing accepted v1.12 messages are used verbatim. Native 200 HP is
    // NOT transported by MsgRespawnConfirm; full PlayerSnapshot must follow.
    pse::MsgRespawnRequest r;r.slotId=1;r.mapEpoch=7;r.deathSequence=3;
    std::vector<std::uint8_t> bytes;pse::ByteWriter w(bytes);r.serialize(w);pse::ByteReader reader(bytes.data(),bytes.size());pse::MsgRespawnRequest decoded;
    check(bytes.size()==9&&decoded.deserialize(reader)&&reader.remaining()==0&&decoded.slotId==1&&decoded.mapEpoch==7&&decoded.deathSequence==3,"existing v112 request golden size and values");
    pse::MsgRespawnConfirm c;c.slotId=1;c.mapEpoch=7;c.deathSequence=3;c.newEntityId=0x5501;c.entityGeneration=4;c.defaultWeapons=3;c.defaultAmmo=60;
    std::vector<std::uint8_t> cb;pse::ByteWriter cw(cb);c.serialize(cw);pse::ByteReader cr(cb.data(),cb.size());pse::MsgRespawnConfirm out;
    check(out.deserialize(cr)&&cr.remaining()==0&&out.entityGeneration==4&&out.defaultAmmo==60&&out.defaultWeapons==3,"existing v112 confirm accepts native starter ammo without wire change");
}
}
int main(){manual();routing();failure();totalPartyKill();input();weapons();wire();
    std::printf("P16 RESPAWN checks=%u failures=%u; OFFLINE POLICY ONLY, NOT KEX\n",checks,failures);
    return failures?1:0;
}
