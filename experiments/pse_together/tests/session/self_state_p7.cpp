// P7 offline checks of the scalar draft and authority admission. Engine doubles
// below do NOT establish native APIs, HUD equality, or KEX runtime acceptance.
#include "self_state_p7.hpp"
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>
using namespace pse::self_p7;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
constexpr NativeCapabilities Proven{true,true,true,true,true,true};
constexpr ApplyContext Safe{true,true,true,true,true};
constexpr Limits Bounded{1000,2000};
Admission completed(const Identity& i,std::uint64_t serial=1,std::uint64_t installed=1){return {i,serial,installed};}
State sample(std::uint32_t sequence=1) {
    State s;s.identity={1,4,7,21761,2,9,3};s.sequence=sequence;s.stateTick=sequence+100;s.stateTimeNs=1000000000ULL+sequence;s.clockGeneration=1;
    s.values.health=170;s.values.ammo={0,31,120,9,35,40,44,99};s.values.owned=0xff;s.values.weapon=1;s.values.pendingWeapon=7;
    s.values.keys=9;s.values.artifacts=0x21;s.values.abilities=3;s.values.questItems=0x34;
    s.values.ankhCount=5;s.values.ankhMask=0x1f;s.values.teamDolls=0x89abcdef;return s;
}
struct Native final:INativeState {
    Values actual{};unsigned reads{},writes{};bool readOk=true,applyOk=true,lie{},corruptSecondRead{};
    std::function<void()> onRead,onApply;
    bool read(Values& v) noexcept override {++reads;if(onRead)onRead();if(!readOk)return false;v=actual;if(corruptSecondRead&&reads==2)++v.ammo[0];return true;}
    bool apply(const Values& before,const Values& desired) noexcept override {
        ++writes;if(before!=actual)return false;if(onApply)onApply();if(!applyOk)return false;if(!lie)actual=desired;return true;
    }
};
struct Fixture {
    Authority a;Native native;State s=sample();
    Fixture(){a.admit(completed(s.identity),Proven,Bounded);}
    Result offer(std::uint64_t now=100){return a.offer(s,now,true);}
    Result step(std::uint64_t now=101){return a.step(native,Safe,now);}
};
void codec(){
    const auto s=sample();std::array<std::uint8_t,EncodedSize> bytes{};State decoded;
    check(encode(s,bytes)&&decode(bytes,decoded)&&s==decoded,"full scalar roundtrip");
    check(bytes[0]=='P'&&bytes[1]=='S'&&bytes[2]=='7'&&bytes[3]=='S'&&bytes[4]==0x12&&bytes[5]==1&&bytes[6]==1,"draft magic/version/kind independent of existing protocol");
    check(bytes[7]==1&&bytes[8]==4&&bytes[12]==7&&bytes[16]==1&&bytes[17]==0x55,"golden routing byte offsets and little endian");
    check(bytes[56]==170&&bytes[57]==0&&bytes[60]==31&&bytes[74]==0xff&&bytes[75]==1&&bytes[76]==7,"golden native value byte offsets");
    check(bytes[77]==9&&bytes[78]==0&&bytes[79]==0x21&&bytes[83]==3&&bytes[87]==0x34&&bytes[88]==0,"golden stock keys inventory and quest byte offsets");
    check(bytes[89]==5&&bytes[90]==0&&bytes[91]==0x1f&&bytes[92]==0&&bytes[93]==0xef&&bytes[94]==0xcd&&bytes[95]==0xab&&bytes[96]==0x89&&bytes[97]==0,"golden ankh count mask team dolls and life byte offsets");
    for(std::size_t n=0;n<bytes.size();++n) {State sentinel=sample(8);const auto before=sentinel;check(!decode(std::span(bytes).first(n),sentinel)&&sentinel==before,"every truncation rejects without output mutation");}
    auto larger=std::vector<std::uint8_t>(bytes.begin(),bytes.end());larger.push_back(0);check(!decode(larger,decoded),"trailing byte is rejected");
    for(std::size_t i=0;i<7;++i){auto damaged=bytes;damaged[i]^=0x80;check(!decode(damaged,decoded),"foreign magic version or kind rejected");}
    for(const auto i:{7u,75u,76u,97u}){auto damaged=bytes;damaged[i]=0xfe;check(!decode(damaged,decoded),"invalid slot weapon pending weapon or life rejected");}
    for(const auto i:{8u,12u,16u,20u,24u,28u,32u,52u}){auto damaged=bytes;for(unsigned j=0;j<4;++j)damaged[i+j]=0;check(!decode(damaged,decoded),"zero identity sequence or clock-generation rejected");}
    {auto invalid=s;invalid.values.health=0;check(!encode(invalid,bytes),"alive zero health rejected");}
    {auto invalid=s;invalid.values.owned=0;check(!encode(invalid,bytes),"unowned selected weapon rejected");}
    {auto invalid=s;invalid.values.artifacts=0x40;check(!encode(invalid,bytes),"unknown artifact bits rejected");}
    {auto invalid=s;invalid.values.abilities=4;check(!encode(invalid,bytes),"unknown ability bits rejected");}
    {auto invalid=s;invalid.values.keys=0x10;check(!encode(invalid,bytes),"unknown stock key bits rejected");}
    {auto invalid=s;invalid.values.questItems=0x100;check(!encode(invalid,bytes),"unknown stock quest item bits rejected");}
    {auto invalid=s;invalid.values.ankhCount=6;check(!encode(invalid,bytes),"corrupt duplicate ankh count above five rejected");}
    {auto invalid=s;invalid.values.ankhMask=0x3f;check(!encode(invalid,bytes),"sixth ankh bit absent from stock rejected");}
    {auto invalid=s;invalid.values.ankhCount=5;invalid.values.ankhMask=0;check(!encode(invalid,bytes),"ankh count must equal population of unique stock mask");}
    for(std::uint16_t mask=0;mask<=0x1f;++mask){auto validStock=s;validStock.values.ankhMask=mask;validStock.values.ankhCount=static_cast<std::uint16_t>(std::popcount(mask));
     check(encode(validStock,bytes)&&decode(bytes,decoded)&&decoded==validStock,"each valid stock ankh subset roundtrips with matching unique count");}
    {auto damaged=bytes;damaged[89]=6;check(!decode(damaged,decoded),"decoder rejects count six");}
    {auto damaged=bytes;damaged[91]=0x3f;check(!decode(damaged,decoded),"decoder rejects sixth stock ankh bit");}
    {auto damaged=bytes;damaged[91]=0;check(!decode(damaged,decoded),"decoder rejects count five with empty mask");}
    for(const auto i:{77u,78u,88u,89u,90u,91u,92u}){auto damaged=bytes;damaged[i]=0xff;check(!decode(damaged,decoded),"invalid stock key quest and ankh bytes rejected by decoder");}
    for(const auto hp:{std::numeric_limits<std::int16_t>::min(),static_cast<std::int16_t>(-1),static_cast<std::int16_t>(0)}) {
        auto dead=s;dead.values.life=Life::Dead;dead.values.health=hp;dead.values.deathSequence=7;
        check(encode(dead,bytes)&&decode(bytes,decoded)&&decoded==dead,"negative and zero dead health roundtrip explicitly");
    }
    {auto dead=s;dead.values.life=Life::Dead;dead.values.health=0;check(!encode(dead,bytes),"death requires event sequence");}
    {auto edge=s;edge.values.health=32767;edge.values.keys=15;edge.values.questItems=255;edge.values.teamDolls=0xffffffff;edge.values.ammo.fill(65535);edge.values.pendingWeapon=NoPendingWeapon;
     check(encode(edge,bytes)&&decode(bytes,decoded)&&edge==decoded,"scalar width boundaries without truncation");}
}
void admission(){
    Authority a;const auto s=sample();Native n;
    check(a.offer(s,100,true)==Result::NotReady&&a.step(n,Safe,101)==Result::NotReady&&n.reads==0,"unadmitted packets never call native");
    check(a.admit({},Proven,Bounded)==Result::Invalid&&!a.ready(),"zero admission rejected");
    check(a.admit(completed(s.identity),Proven,{0,2000})==Result::Invalid,"unbounded pending rejected");
    check(a.admit(completed(s.identity),Proven,{1000,0})==Result::Invalid,"unbounded host silence rejected");
    check(a.admit(completed(s.identity),Proven,Bounded)==Result::Accepted,"authenticated caller admission");
    check(a.admit(completed(s.identity),Proven,Bounded)==Result::Stale,"same local admission serial cannot clear watermarks");
    {auto moved=s.identity;++moved.slot;++moved.resetToken;check(a.admit(completed(moved,2),Proven,Bounded)==Result::IdentityMismatch,"reset cannot silently change recipient slot in same connection");}
    check(a.offer(s,100,false)==Result::Invalid&&a.highestSeen()==0,"unattested clock/age sample does not consume watermark");
    for(unsigned which=0;which<7;++which){auto foreign=s;
        switch(which){case 0:++foreign.identity.slot;break;case 1:++foreign.identity.connectionGeneration;break;case 2:++foreign.identity.mapEpoch;break;case 3:++foreign.identity.entityId;break;case 4:++foreign.identity.entityGeneration;break;case 5:++foreign.identity.resetToken;break;default:++foreign.identity.discontinuitySequence;}
        check(a.offer(foreign,100,true)==Result::IdentityMismatch&&a.highestSeen()==0,"all identity dimensions pinned");}
    check(a.offer(s,100,true)==Result::Accepted,"first admitted packet");a.disconnect();
    check(!a.hasPending()&&!a.applied()&&a.offer(s,101,true)==Result::NotReady,"disconnect invalidates pending and authority");
    auto identity=s.identity;++identity.resetToken;
    check(a.admit(completed(identity,2,110),Proven,Bounded)==Result::Stale,"reset token cannot reopen disconnected connection");
    ++identity.connectionGeneration;
    check(a.admit(completed(identity,2,110),Proven,Bounded)==Result::Accepted,"new connection may reopen after authenticated admission");
    a.barrier();++identity.resetToken;
    check(a.admit(completed(identity,3,120),Proven,Bounded)==Result::Stale,"unload barrier needs new world or connection");
    ++identity.mapEpoch;
    check(a.admit(completed(identity,3,120),Proven,Bounded)==Result::Accepted,"new map epoch may rearm after barrier");
    a.barrier();identity.mapEpoch=1;identity.resetToken=1;
    check(a.admit(completed(identity,4,130),Proven,Bounded)==Result::Accepted,"numerically smaller opaque map epoch and reset token accepted with fresh completed local serial");
    a.barrier();identity.mapEpoch=7;
    check(a.admit(completed(identity,3,140),Proven,Bounded)==Result::Stale,"replayed completed-handshake serial rejected regardless of epoch numeric value");
}
void apply(){
    {Fixture f;check(f.offer()==Result::Accepted&&f.a.highestSeen()==1&&f.a.lastApplied()==0,"ingress alone never commits state");
     check(f.step()==Result::Applied&&f.native.reads==2&&f.native.writes==1&&f.native.actual==f.s.values&&f.a.lastApplied()==1,"commit only after apply and exact native reread");
     check(f.step(102)==Result::Unchanged&&f.native.writes==1,"stable local native state avoids redundant operations");
     ++f.native.actual.ammo[3];--f.native.actual.health;f.native.actual.keys=0;
     check(f.step(103)==Result::Applied&&f.native.actual==f.s.values&&f.a.lastApplied()==1,"local mutation repaired from last committed authority without new packet");}
    {Fixture f;f.offer();f.native.actual=f.s.values;check(f.step()==Result::Unchanged&&f.native.writes==0&&f.a.lastApplied()==1,"equal actual state commits only after native read verification");}
    for(unsigned absent=0;absent<6;++absent) {
        Authority a;Native n;auto caps=Proven;
        switch(absent){case 0:caps.health=false;break;case 1:caps.allAmmo=false;break;case 2:caps.ownedAndSelectedWeapons=false;break;case 3:caps.keys=false;break;case 4:caps.inventory=false;break;default:caps.death=false;}
        const auto s=sample();a.admit(completed(s.identity),caps,Bounded);a.offer(s,100,true);
        check(a.step(n,Safe,101)==Result::CapabilitiesMissing&&n.reads==0&&n.writes==0&&a.lastApplied()==0,"each missing native capability blocks every native call");
    }
    for(unsigned unsafe=0;unsafe<5;++unsafe) {
        Fixture f;f.offer();auto context=Safe;
        switch(unsafe){case 0:context.simulationThread=false;break;case 1:context.liveBinding=false;break;case 2:context.gameplay=false;break;case 3:context.outsideUnloadBarrier=false;break;default:context.authenticated=false;}
        check(f.a.step(f.native,context,101)==Result::Unsafe&&f.native.reads==0&&f.a.lastApplied()==0,"unsafe context suppresses all native calls");
    }
    {Fixture f;f.offer();f.native.readOk=false;check(f.step()==Result::NativeFailure&&!f.a.ready()&&f.a.lastApplied()==0&&f.native.writes==0,"failed native read closes without writes or commit");}
    {Fixture f;f.offer();f.native.applyOk=false;check(f.step()==Result::NativeFailure&&!f.a.ready()&&f.a.lastApplied()==0,"failed native apply closes without commit");}
    {Fixture f;f.offer();f.native.lie=true;check(f.step()==Result::PostconditionFailure&&!f.a.ready()&&f.a.lastApplied()==0,"reported success with unchanged actual state fails closed");}
    {Fixture f;f.offer();f.native.corruptSecondRead=true;check(f.step()==Result::PostconditionFailure&&!f.a.ready()&&f.a.lastApplied()==0,"one divergent ammo field prevents commit");}
    {Fixture f;f.offer();f.native.onRead=[&]{f.a.barrier();};check(f.step()==Result::Interrupted&&f.native.writes==0&&f.native.reads==1&&f.a.lastApplied()==0,"unload reentry during read prevents apply");}
    {Fixture f;f.offer();f.native.onApply=[&]{f.a.disconnect();};check(f.step()==Result::Interrupted&&f.native.reads==1&&f.a.lastApplied()==0&&!f.a.hasPending(),"disconnect during native apply prevents reread and commit");}
    {Fixture f;f.offer();Result nested=Result::Accepted;f.native.onApply=[&]{nested=f.step();};check(f.step()==Result::Applied&&nested==Result::Busy&&f.native.writes==1,"reentrant native step blocked");}
    {Fixture f;f.offer();Result nested=Result::Accepted;f.native.onApply=[&]{nested=f.a.admit(completed(sample().identity),Proven,Bounded);};check(f.step()==Result::Applied&&nested==Result::Busy,"admission cannot reset authority inside native call");}
}
void sequenceAndDeadline(){
    {Fixture f;f.offer();auto later=sample(3);later.values.health=20;
     check(f.a.offer(later,101,true)==Result::PendingOccupied&&f.a.highestSeen()==3&&f.a.lastApplied()==0,"new arrivals cannot replace oldest pending");
     check(f.step(102)==Result::Applied&&f.a.lastApplied()==1&&f.native.actual.health==170,"pending uses application watermark independently of ingress");
     auto reordered=sample(2);check(f.a.offer(reordered,103,true)==Result::Stale,"lower ingress sequence rejected after occupied pending");
     check(f.a.offer(later,104,true)==Result::Stale,"retransmitted consumed ingress sequence rejected");
     later=sample(4);check(f.a.offer(later,105,true)==Result::Accepted&&f.step(106)==Result::Unchanged&&f.a.lastApplied()==4,"next fresh snapshot can commit");}
    {Fixture f;f.offer();auto backwards=sample(9);backwards.stateTick=1;check(f.a.offer(backwards,101,true)==Result::Stale&&f.a.highestSeen()==1,"server tick rollback cannot poison watermark");}
    {Fixture f;f.offer();auto backwards=sample(9);backwards.stateTimeNs=1;check(f.a.offer(backwards,101,true)==Result::Stale&&f.a.highestSeen()==1,"server time rollback cannot poison watermark");}
    {Fixture f;f.offer();auto changed=sample(2);++changed.clockGeneration;check(f.a.offer(changed,101,true)==Result::Invalid&&!f.a.ready()&&!f.a.hasPending(),"clock discontinuity needs fresh admitted reset");}
    {Fixture f;f.s.values.deathSequence=8;f.offer();auto rollback=sample(2);rollback.values.deathSequence=7;
     check(f.a.offer(rollback,101,true)==Result::Invalid&&!f.a.ready(),"fresh packet sequence cannot roll death event sequence backward");}
    {Fixture f;f.s.values.deathSequence=8;f.offer();auto reused=sample(2);reused.values.life=Life::Dead;reused.values.health=0;reused.values.deathSequence=8;
     check(f.a.offer(reused,101,true)==Result::Invalid&&!f.a.ready(),"a new death needs a new authoritative event sequence");}
    {Fixture f;f.s.values.life=Life::Dead;f.s.values.health=-1;f.s.values.deathSequence=8;f.offer();auto revive=sample(2);revive.values.deathSequence=8;
     check(f.a.offer(revive,101,true)==Result::Invalid&&!f.a.ready(),"same admitted generation cannot revive through scalar packet");}
    {Fixture f;f.offer();check(f.step(99)==Result::ClockRegressed&&!f.a.ready()&&f.native.reads==0,"local monotonic clock rollback closes without native calls");}
    {Fixture f;f.offer();check(f.a.offer(sample(2),99,true)==Result::ClockRegressed&&!f.a.ready(),"clock rollback on ingress also closes");}
    {Fixture f;f.offer();for(std::uint32_t i=2;i<20;++i)f.a.offer(sample(i),100+i,true);
     check(f.step(1100)==Result::Expired&&!f.a.ready()&&f.native.reads==0&&f.a.lastApplied()==0,"occupied pending expires at original deadline despite newer packets");}
    {Fixture f;f.offer();check(f.step(1099)==Result::Applied,"pending accepted immediately before deadline");}
    {Fixture f;f.offer();f.native.lie=true;f.step();auto id=f.s.identity;
     check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"same token cannot conceal failed postcondition");
     id.resetToken=1;check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Accepted,"fresh completed admission accepts numerically lower opaque reset token");
     check(f.a.offer(f.s,102,true)==Result::IdentityMismatch,"pre-reset packet rejected after rearm");}
    {Fixture f;f.offer();f.step();f.native.actual.health=1;const auto calls=f.native.reads+f.native.writes;
     check(f.step(2100)==Result::Expired&&!f.a.ready()&&f.native.reads+f.native.writes==calls,"host silence expires applied-state drift repair before every native call");}
    {Fixture f;f.offer();f.step();check(f.a.offer(sample(2),2100,true)==Result::Expired&&!f.a.ready(),"late packet cannot revive expired host lease without fresh reset admission");}
    {Fixture f;f.offer();f.step();f.s=sample(2);f.offer(2000);f.step(2001);f.native.actual.health=1;
     check(f.step(3999)==Result::Applied&&f.native.actual.health==170,"fresh admitted state renews finite host lease");
     const auto calls=f.native.reads+f.native.writes;
     check(f.step(4000)==Result::Expired&&f.native.reads+f.native.writes==calls,"native drift repairs do not extend host lease");}
    {Fixture f;check(f.step(2001)==Result::Expired&&f.native.reads==0,"no first snapshot after admission also has bounded host wait");}
}
void stickyClosureAndDeath(){
    for(unsigned order=0;order<2;++order) {
        Fixture f;f.offer();
        if(order==0){f.a.disconnect();f.a.barrier();}else{f.a.barrier();f.a.disconnect();}
        auto id=f.s.identity;id.resetToken=1;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"both closure orders preserve disconnect requirement against token-only rearm");
        id.mapEpoch=1;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"both closure orders preserve disconnect requirement against map-only rearm");
        ++id.connectionGeneration;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Accepted,"new authenticated connection satisfies both sticky closure requirements");
    }
    for(unsigned cause=0;cause<2;++cause)for(unsigned phase=0;phase<3;++phase) {
        Fixture f;f.offer();
        const auto close=[&]{if(cause==0)f.a.disconnect();else f.a.barrier();};
        if(phase==0)f.native.onRead=[&]{close();f.native.readOk=false;};
        if(phase==1)f.native.onApply=[&]{close();f.native.applyOk=false;};
        if(phase==2)f.native.onRead=[&]{if(f.native.reads==2){close();f.native.readOk=false;}};
        check(f.step()==Result::Interrupted&&!f.a.ready()&&!f.a.hasPending()&&!f.a.lastApplied(),"false native callback after disconnect/barrier is interrupted before generic failure");
        check(f.native.reads==(phase==2?2u:1u)&&f.native.writes==(phase==0?0u:1u),"interrupted false callback stops all subsequent native calls");
        auto id=f.s.identity;id.resetToken=1;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"false native callback cannot weaken sticky disconnect/barrier to generic failure");
        if(cause==0)++id.connectionGeneration;else id.mapEpoch=1;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Accepted,"proper fresh admission remains possible after interrupted false callback");
    }
    for(unsigned committed=0;committed<2;++committed) {
        Fixture f;f.s.values.life=Life::Dead;f.s.values.health=0;f.s.values.deathSequence=8;f.offer();if(committed)f.step();
        auto id=f.s.identity;id.resetToken=1;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"reset token alone cannot forget observed or committed death");
        ++id.connectionGeneration;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"connection token alone cannot revive same native lifetime in same world");
        id=f.s.identity;id.resetToken=1;++id.entityGeneration;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"entity generation alone cannot clear death without discontinuity");
        id=f.s.identity;id.resetToken=1;++id.discontinuitySequence;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Stale,"discontinuity alone cannot clear death without native lifetime advance");
        ++id.entityGeneration;
        check(f.a.admit(completed(id,2,102),Proven,Bounded)==Result::Accepted,"new native lifetime plus discontinuity permits explicit respawn admission");
        auto alive=sample(2);alive.identity=id;alive.values.deathSequence=8;
        check(f.a.offer(alive,103,true)==Result::Accepted&&f.step(104)==Result::Applied&&f.native.actual.life==Life::Alive,"alive authority only applies after fresh native incarnation admission");
    }
}
}
int main(){codec();admission();apply();sequenceAndDeadline();stickyClosureAndDeath();std::printf("P7 self-state checks=%u failures=%u; OFFLINE ONLY\n",checks,failures);return failures?1:0;}
