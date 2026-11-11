// Offline contract of the client's per-slot bookkeeping for up to 8 players (no KEX, no network, no clock).
#include "client_slots.hpp"
#include <cmath>
#include <cstdio>
namespace cs=pse::client_slots;
using snapshot55::Verdict;
namespace {
unsigned checks{},failures{};
void check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
constexpr std::uint32_t Conn=7,Epoch=3;
constexpr std::uint64_t Ms=1'000'000ULL;
pse::BridgePlayerSnapshot snap(unsigned slot,std::uint32_t seq,std::uint32_t generation=1){
    pse::BridgePlayerSnapshot s;
    s.connectionGeneration=Conn;s.mapEpoch=Epoch;s.slotId=static_cast<std::uint8_t>(slot);s.entityId=0x5500u+slot;s.entityGeneration=generation;
    s.sequence=seq;s.serverTick=seq;s.posX=1;s.posY=2;s.posZ=3;
    return s;
}
pse::BridgeRespawnConfirm confirm(unsigned slot,std::uint32_t generation,std::uint32_t death=1){
    pse::BridgeRespawnConfirm c;c.slotId=static_cast<std::uint8_t>(slot);c.mapEpoch=Epoch;c.deathSequence=death;c.newEntityId=0x5500u+slot;c.newGeneration=generation;
    return c;
}
snapshot55::Guard freshGuard(){snapshot55::Guard g;g.reset(Conn,Epoch);return g;}
}
int main(){
    // --- shared Guard: all 8 slots tracked independently --------------------------------------------------------------
    static_assert(cs::Slots==8);
    {
        auto g=freshGuard();bool all=true;
        for(unsigned i=0;i<8;++i) all=all&&g.accept(snap(i,5),true)==Verdict::Accept;
        check(all&&g.slots.size()==8,"guard accepts a first snapshot for each of slots 0..7");
        check(g.accept(snap(7,5),true)==Verdict::Stale&&g.accept(snap(7,6),true)==Verdict::Accept,"guard sequence is per slot (slot 7)");
        check(g.accept(snap(3,4),true)==Verdict::Stale&&g.accept(snap(4,6),true)==Verdict::Accept,"one slot's stale record does not affect another");
        check(g.accept(snap(8,9),true)==Verdict::Identity,"slot 8 is outside the guard");
        auto other=snap(6,9);other.entityId=0x5500+5;
        check(g.accept(other,true)==Verdict::EntityMismatch,"entity mismatch is detected for a high slot");
    }
    // --- generation gate ------------------------------------------------------------------------------------------------
    {
        cs::Tracker t;bool initial=true;
        for(unsigned i=0;i<8;++i) initial=initial&&t.expected[i]==1;
        check(initial,"every slot starts at generation 1");
        check(t.checkSnapshot(2,1,5)==cs::Gen::Accept,"matching generation accepted");
        check(t.checkSnapshot(2,3,5)==cs::Gen::Adopt,"another slot's newer generation is adopted");
        check(t.checkSnapshot(5,3,5)==cs::Gen::Wait,"the own slot never adopts: it waits for its confirm");
        t.setGeneration(2,4);
        check(t.checkSnapshot(2,3,5)==cs::Gen::Wait,"an older generation waits");
        check(t.checkSnapshot(8,1,0)==cs::Gen::Wait&&t.checkSnapshot(255,1,0)==cs::Gen::Wait,"invalid slots wait");
    }
    // --- respawn confirm admission for any slot -------------------------------------------------------------------------
    {
        cs::Tracker t;bool own=true,remote=true;
        for(unsigned i=0;i<8;++i){
            own=own&&t.admitConfirm(confirm(i,2),static_cast<std::uint8_t>(i),Epoch);        // as the own slot: exactly gen+1
            remote=remote&&t.admitConfirm(confirm(i,2),static_cast<std::uint8_t>((i+1)%8),Epoch); // as another slot's confirm
        }
        check(own,"own-slot confirm admitted for every slot id 0..7");
        check(remote,"remote confirm admitted for every slot id 0..7");
        check(!t.admitConfirm(confirm(8,2),0,Epoch)&&!t.admitConfirm(confirm(255,2),0,Epoch),"confirm for slot >= 8 refused");
        check(!t.admitConfirm(confirm(3,3),3,Epoch),"own confirm must be exactly the next generation");
        check(t.admitConfirm(confirm(3,3),1,Epoch),"a remote confirm may skip generations (missed confirm)");
        check(!t.admitConfirm(confirm(3,1),1,Epoch)&&!t.admitConfirm(confirm(3,2),3,Epoch+1),"old generation / wrong epoch refused");
        auto c=confirm(3,2);c.newEntityId=0x5500+4;check(!t.admitConfirm(c,1,Epoch),"wrong entity id refused");
        c=confirm(3,2);c.defaultWeapons=0;check(!t.admitConfirm(c,1,Epoch),"missing default weapon refused");
        check(!t.admitConfirm(confirm(3,2,0),1,Epoch),"zero death sequence refused");
        auto g=freshGuard();t.applyConfirm(g,confirm(6,2));
        check(t.expected[6]==2&&g.slots[6].generation==2&&g.slots[6].entity==0x5506,"applyConfirm moves tracker and guard of that slot only");
        check(t.expected[5]==1&&g.slots[5].generation==0,"other slots untouched by a confirm");
    }
    // --- per-slot flow: late join, respawn of others, own slot waits --------------------------------------------------------
    {
        cs::Tracker t;auto g=freshGuard();const std::uint8_t own=2;
        auto r=t.intake(g,snap(0,10,1),true,own,100*Ms);
        check(!r.waited&&!r.adopted&&r.verdict==Verdict::Accept,"first snapshot of the host player accepted");
        r=t.intake(g,snap(5,10,4),true,own,100*Ms);
        check(!r.waited&&r.adopted&&!r.replaced&&r.verdict==Verdict::Accept&&t.expected[5]==4,"late join: slot 5 already at generation 4 is adopted, nothing to replace");
        r=t.intake(g,snap(5,11,4),true,own,110*Ms);
        check(!r.adopted&&r.verdict==Verdict::Accept,"after adoption it is an ordinary accept");
        r=t.intake(g,snap(5,12,3),true,own,120*Ms);
        check(r.waited,"an older generation of slot 5 waits (does not block, does not fail)");
        r=t.intake(g,snap(0,11,2),true,own,130*Ms);
        check(r.adopted&&r.replaced&&r.verdict==Verdict::Accept,"slot 0 respawned and its confirm was missed: newer generation adopted, proxy body to be replaced");
        r=t.intake(g,snap(own,10,2),true,own,130*Ms);
        check(r.waited,"the own slot's new generation waits until its own confirm");
        t.applyConfirm(g,confirm(own,2));
        r=t.intake(g,snap(own,11,2),true,own,140*Ms);
        check(!r.waited&&r.verdict==Verdict::Accept,"the own slot continues once its confirm is applied");
        // confirm first, snapshot after (the normal order) for another slot
        t.applyConfirm(g,confirm(1,2));
        r=t.intake(g,snap(1,5,2),true,own,150*Ms);
        check(!r.waited&&!r.adopted&&r.verdict==Verdict::Accept,"remote confirm then snapshot: plain accept");
        r=t.intake(g,snap(1,6,1),true,own,150*Ms);
        check(r.waited,"old-generation snapshot after the confirm waits");
        r=t.intake(g,snap(4,5,1),false,own,150*Ms);
        check(r.verdict==Verdict::NotReady&&t.lastAcceptNs[4]==0,"a not-ready snapshot is neither accepted nor starts a clock");
        auto bad=snap(6,5,1);bad.posX=NAN;
        r=t.intake(g,bad,true,own,150*Ms);
        check(r.verdict==Verdict::Nonfinite&&t.lastAcceptNs[6]==0,"nonfinite transform rejected per slot");
        auto wrongEntity=snap(7,5,6);wrongEntity.entityId=0x5500+3;
        r=t.intake(g,wrongEntity,true,own,150*Ms);
        check(r.waited&&t.expected[7]==1,"an adopted generation must carry the slot's own entity id");
        r=t.intake(g,snap(8,5,1),true,own,150*Ms);
        check(r.waited,"slot 8 never creates anything");
        check(t.lastDeath[0]==0&&!t.lastDead[0],"death state recorded per slot");
        auto dead=snap(3,5,1);dead.lifeState=pse::LIFE_DEAD;dead.deathSequence=2;
        t.intake(g,dead,true,own,160*Ms);
        check(t.lastDead[3]&&t.lastDeath[3]==2&&!t.lastDead[4],"slot 3 dead does not mark slot 4");
    }
    // --- stale-slot detection ---------------------------------------------------------------------------------------------
    {
        cs::Tracker t;auto g=freshGuard();const std::uint8_t own=3;
        for(unsigned i:{0u,2u,3u,5u,7u}) t.intake(g,snap(i,50,1),true,own,1000*Ms);
        check(t.collectStale(g,1000*Ms+cs::StaleNs-Ms,own)==0,"nothing stale before the timeout");
        t.intake(g,snap(2,51,1),true,own,1000*Ms+cs::StaleNs/2); // slot 2 keeps sending
        const auto mask=t.collectStale(g,1000*Ms+cs::StaleNs+Ms,own);
        check(mask==((1u<<0)|(1u<<5)|(1u<<7)),"silent remote slots are reported; the sender and the own slot are not");
        check(t.collectStale(g,1000*Ms+cs::StaleNs/2+cs::StaleNs+Ms,own)==((1u<<2)),"a slot is reported once; the later-silent slot follows");
        check(t.collectStale(g,1000*Ms+10*cs::StaleNs,own)==0,"nothing reported twice, the own slot is never stale");
        check(t.expected[0]==0&&g.slots[0].generation==0&&g.slots[0].sequence==0,"a stale slot is forgotten (generation unknown, guard wiped)");
        // the slot is reused by a new player: generation restarts at 1, sequence numbers restart low
        auto r=t.intake(g,snap(0,3,1),true,own,100000*Ms);
        check(!r.waited&&r.adopted&&r.verdict==Verdict::Accept,"a reused stale slot starts from scratch (low sequence, generation 1)");
        r=t.intake(g,snap(5,1,6),true,own,100000*Ms);
        check(r.adopted&&r.verdict==Verdict::Accept&&t.expected[5]==6,"a returning slot adopts whatever generation it now has");
        check(t.collectStale(g,100000*Ms+Ms,own)==0,"fresh slots are not stale");
        // new session: the clocks restart, generations stay
        t.quiet();
        check(t.collectStale(g,500000*Ms,own)==0&&t.expected[5]==6,"quiet() restarts staleness without losing generations");
        t.intake(g,snap(5,2,6),true,own,500000*Ms);
        check(t.collectStale(g,500000*Ms+cs::StaleNs/2,own)==0,"clock restarts from the first snapshot after quiet()");
        // world transition
        t.reset();
        bool initial=true;for(unsigned i=0;i<8;++i) initial=initial&&t.expected[i]==1&&!t.lastAcceptNs[i]&&!t.lastDead[i]&&!t.lastDeath[i];
        check(initial,"reset() returns every slot to the first generation");
    }
    std::printf("CLIENT SLOTS checks=%u failures=%u; OFFLINE LOGIC ONLY, NOT KEX\n",checks,failures);
    return failures?1:0;
}
