// Seven VisualProxyStateMachines (one per remote slot of an 8-player session) driven through ONE SnapshotRouter<7>, ONE Seam
// and ONE fake multi-instance table that mirrors the contract of native_proxy.cpp: handle = (slot+1)<<48 | serial names
// exactly one instance, a handle of slot J can never act on slot K, one native spawn per instance incarnation.
// No KEX, no network. native_proxy.cpp itself needs the game and is only compile-checked (see CMake target pse_together).
#include "visual_proxy.hpp"
#include "seam.hpp"
#include <cstdio>
#include <functional>
#include <set>
#include <vector>

using namespace pse::proxy;
namespace {
int total=0,failures=0;
void check(const char* name,bool ok) { ++total;failures+=!ok;std::printf("%-4s %s\n",ok?"PASS":"FAIL",name); }
constexpr std::uint32_t Epoch=777;
constexpr TickContext Safe{true,true};

struct Fake {
    enum Phase { Waiting,Live,Retiring,Retired };
    struct Inst {
        std::uint64_t handle{},serial{};Phase phase{Waiting};
        int spawns{},updates{},removes{},polls{},wrongSlotRefusals{},staleRefusals{};
        ProxyCommand lastUpdate{};
        int spawnMode{0};  // 0 ok, 1 unknown (returns 0), 2 refused
        bool updateStops{};
    };
    Inst inst[8];
    int nativeCalls{};
    std::function<void()> during;
    bool crashOnUpdate{};
    Inst* byHandle(std::uint64_t h) { const auto s=h>>48;if(!s || s>8 || !(h&((1ull<<48)-1))) return nullptr;auto& i=inst[s-1];return i.handle==h ? &i : nullptr; }
    void rearmSlot(unsigned s) { const auto serial=inst[s].serial;inst[s]=Inst{};inst[s].serial=serial; }
} fk;
void fTickEnter(std::uint64_t) {}
std::int32_t fReady(std::uint64_t) { return pse::seam::StableWorld|pse::seam::LocalTicked|pse::seam::AdapterHealthy; }
std::int32_t fSpawn(const ProxyCommand* c,std::uint64_t* out) {
    auto& i=fk.inst[c->slot];if(fk.during) fk.during();
    if(i.spawns>=1 && i.phase!=Fake::Waiting) return 3; // one native spawn per incarnation
    if(i.spawnMode==2) return 3;
    ++fk.nativeCalls;++i.spawns;
    if(i.spawnMode==1) { i.phase=Fake::Live;return 0; } // an Actor may exist, outcome unknown
    i.handle=(static_cast<std::uint64_t>(c->slot+1)<<48)|++i.serial;i.phase=Fake::Live;*out=i.handle;return 1;
}
std::int32_t fUpdate(std::uint64_t h,const ProxyCommand* c) {
    if(fk.during) fk.during();
    if(fk.crashOnUpdate) RaiseException(0xE0000042u,0,0,nullptr);
    auto* i=fk.byHandle(h);if(!i) { for(auto& x:fk.inst) ++x.staleRefusals;return 3; } // stale/unknown handle: no instance touched
    if(c->slot+1!=(h>>48)) { ++i->wrongSlotRefusals;return 3; }
    if(i->phase!=Fake::Live) return 3;
    if(i->updateStops) return 4;
    ++fk.nativeCalls;++i->updates;i->lastUpdate=*c;return 1;
}
std::int32_t fRemove(std::uint64_t h) { auto* i=fk.byHandle(h);if(!i || i->phase!=Fake::Live) return 2;++fk.nativeCalls;++i->removes;i->phase=Fake::Retiring;return 1; }
std::int32_t fRetire(std::uint64_t h) {
    auto* i=fk.byHandle(h);if(!i) return 0;
    if(i->phase==Fake::Retiring) { if(++i->polls>=2) i->phase=Fake::Retired; }
    return i->phase==Fake::Retired ? 1 : 2;
}
void fLeave(std::uint64_t,std::uint32_t) {}
void fExit(std::uint32_t) {}
const pse::seam::SeamTable Table{pse::seam::Abi,sizeof(pse::seam::SeamTable),fTickEnter,fReady,fSpawn,fUpdate,fRemove,fRetire,fLeave,fExit};

pse::BridgePlayerSnapshot snap(std::uint8_t slot,std::uint32_t seq,float x,std::uint32_t gen=1) {
    pse::BridgePlayerSnapshot s;s.slotId=slot;s.sequence=seq;s.serverTick=seq*2;s.connectionGeneration=gen;s.mapEpoch=Epoch;
    s.entityId=0x6600u+slot;s.entityGeneration=1;s.posX=x;s.posY=-6775.f;s.posZ=1152.f;s.yaw=0.5f;return s;
}
struct World {
    SnapshotRouter<7> r;pse::seam::Seam seam;
    World(std::uint8_t local=0) {
        fk=Fake{};
        r.bind(GetCurrentThreadId());seam.registerTable(&Table);
        r.configureSession(local,1,Epoch);r.onNativeMapReady(Epoch,0x99);
    }
    void routeAll(std::uint32_t seq,float baseX) { Rejection rj{};for(std::uint8_t s=1;s<8;++s) r.route(snap(s,seq,baseX+s*10.f),true,rj); }
    void tick() { r.forEach([&](VisualProxyStateMachine& p) { p.tick(&seam,Safe); }); }
    VisualProxyState st(unsigned s) { auto* p=r.proxy(static_cast<std::uint8_t>(s));return p ? p->state() : VisualProxyState::ABSENT; }
    bool allActive() { for(unsigned s=1;s<8;++s) if(st(s)!=VisualProxyState::ACTIVE) return false;return true; }
};
}

int main() {
    // T01 spawn
    {   World w;w.routeAll(1,-7000);w.tick();
        std::set<std::uint64_t> handles;bool encoded=true,once=true;
        for(unsigned s=1;s<8;++s) { auto* p=w.r.proxy(static_cast<std::uint8_t>(s));handles.insert(p->handle());encoded&=(p->handle()>>48)==s+1;once&=fk.inst[s].spawns==1 && p->spawnCalls()==1; }
        check("T01 seven machines spawn once each in one tick pass, handles distinct and encode the slot",w.allActive() && handles.size()==7 && encoded && once && fk.nativeCalls==7 && w.r.proxy(0)==nullptr); }
    // T02 update
    {   World w;w.routeAll(1,-7000);w.tick();w.routeAll(2,-6000);w.tick();bool ok=true;
        for(unsigned s=1;s<8;++s) ok&=fk.inst[s].updates==1 && fk.inst[s].lastUpdate.slot==s && fk.inst[s].lastUpdate.x==-6000.f+s*10.f && fk.inst[s].wrongSlotRefusals==0;
        check("T02 every update reaches only its own instance with its own transform",ok && w.allActive()); }
    // T03 remove + retirement of one slot
    {   World w;w.routeAll(1,-7000);w.tick();w.r.proxy(3)->onDisconnect();w.routeAll(2,-6000);w.tick();w.tick();w.tick();
        bool others=true;for(unsigned s=1;s<8;++s) if(s!=3) others&=fk.inst[s].removes==0 && fk.inst[s].updates==1 && w.st(s)==VisualProxyState::ACTIVE;
        check("T03 one client leaving: only its instance is removed and retired, the other six keep running",
            fk.inst[3].removes==1 && fk.inst[3].phase==Fake::Retired && w.st(3)==VisualProxyState::ABSENT && w.r.proxy(3)->retiredCount()==1 && others && !w.r.quiescent()); }
    // T04 one slot faults on update (adapter stops it), others continue
    {   World w;w.routeAll(1,-7000);w.tick();fk.inst[5].updateStops=true;
        for(std::uint32_t seq=2;seq<6;++seq) { w.routeAll(seq,-6000+seq*100.f);w.tick(); }
        bool others=true;for(unsigned s=1;s<8;++s) if(s!=5) others&=w.st(s)==VisualProxyState::ACTIVE && fk.inst[s].updates==4 && fk.inst[s].removes==0;
        check("T04 slot 5 faults: its machine goes REMOVE_PENDING->RETIRING, the others apply all four updates",
            fk.inst[5].updates==0 && fk.inst[5].removes==1 && (w.st(5)==VisualProxyState::RETIRING || w.st(5)==VisualProxyState::ABSENT) && others); }
    // T05 spawn with unknown outcome quarantines that slot only
    {   World w;fk.inst[6].spawnMode=1;w.routeAll(1,-7000);w.tick();w.routeAll(2,-6900);w.tick();
        bool others=true;for(unsigned s=1;s<8;++s) if(s!=6) others&=w.st(s)==VisualProxyState::ACTIVE && fk.inst[s].updates==1;
        check("T05 slot 6 spawn unknown -> QUARANTINED, never re-spawned; the others are unaffected",
            w.st(6)==VisualProxyState::QUARANTINED && fk.inst[6].spawns==1 && others); }
    // T06 rejoin of one slot
    {   World w;w.routeAll(1,-7000);w.tick();const auto old=w.r.proxy(2)->handle();
        const auto h1=w.r.proxy(1)->handle(),h4=w.r.proxy(4)->handle();
        w.r.proxy(2)->onDisconnect();w.tick();w.tick();w.tick();
        const bool quiet=w.r.slotQuiescent(2);const bool reset=w.r.resetSlot(2);fk.rearmSlot(2);
        w.r.configureSessionSlot(2,5,Epoch);w.r.onNativeMapReady(Epoch,0x99);
        Rejection rj{};const auto stale=w.r.route(snap(2,3,-7000,1),true,rj); // old generation of the left client
        const auto fresh=w.r.route(snap(2,1,-7000,5),true,rj);w.tick();
        auto* p=w.r.proxy(2);
        check("T06 rejoin of slot 2 (new generation for that slot only): fresh machine, new handle, other handles unchanged",
            quiet && reset && stale!=RouteResult::Routed && fresh==RouteResult::Routed && p && p->state()==VisualProxyState::ACTIVE && p->handle()!=old &&
            (p->handle()>>48)==3 && fk.inst[2].spawns==1 && w.r.proxy(1)->handle()==h1 && w.r.proxy(4)->handle()==h4 && w.st(1)==VisualProxyState::ACTIVE);
        ProxyCommand sc{};sc.slot=2;
        check("T06b a handle of the previous incarnation can no longer act on the instance",
            w.seam.update(old,sc)==UpdateOutcome::Refused); }
    // T07 resetSlot preconditions
    {   World w;w.routeAll(1,-7000);w.tick();
        const bool live=w.r.resetSlot(4);const bool none=w.r.resetSlot(0);const bool bad=w.r.resetSlot(8);
        check("T07 resetSlot refuses a machine that may still own a native Actor, accepts an empty slot, rejects slot>=8",
            !live && w.r.proxy(4) && w.r.proxy(4)->state()==VisualProxyState::ACTIVE && none && !bad && !w.r.slotQuiescent(4) && w.r.slotQuiescent(0)); }
    // T08 cross-slot isolation at the seam
    {   World w;w.routeAll(1,-7000);w.tick();const auto h1=w.r.proxy(1)->handle(),h2=w.r.proxy(2)->handle();
        ProxyCommand c{};c.slot=2;const auto a=w.seam.update(h1,c);                       // slot 1 handle with a slot 2 command
        ProxyCommand c9{};c9.slot=2;const auto b=w.seam.update(h2+0x1000000ull,c9);        // forged serial
        const auto r=w.seam.remove((9ull<<48)|1);                                         // slot index out of range
        check("T08 handle/command slot mismatch, forged handle and out-of-range slot are refused without touching any instance",
            a==UpdateOutcome::Refused && b==UpdateOutcome::Refused && r==RemoveOutcome::DefiniteNoCall && fk.nativeCalls==7 && fk.inst[1].updates==0 && fk.inst[2].updates==0 && fk.inst[1].wrongSlotRefusals==1); }
    // T09 capacity
    {   SnapshotRouter<7> r;r.bind(GetCurrentThreadId());Rejection rj{};r.configureSession(3,1,Epoch);r.onNativeMapReady(Epoch,5);int routed=0;
        for(std::uint8_t s=0;s<8;++s) routed+=r.route(snap(s,1,-7000),true,rj)==RouteResult::Routed;
        // the session moves the local slot to 3: slot 3 has no machine; a machine of the NEW local slot would not count
        SnapshotRouter<7> q;q.bind(GetCurrentThreadId());q.configureSession(0,1,Epoch);q.onNativeMapReady(Epoch,5);
        for(std::uint8_t s=1;s<8;++s) q.route(snap(s,1,-7000),true,rj);
        q.configureSession(7,2,Epoch);q.onNativeMapReady(Epoch,5); // slot 7 becomes local, slot 0 is remote now (no machine yet)
        const auto zero=q.route(snap(0,1,-7000,2),true,rj);
        SnapshotRouter<2> two;two.bind(GetCurrentThreadId());two.configureSession(0,1,Epoch);two.onNativeMapReady(Epoch,5);
        const auto t1=two.route(snap(1,1,-7000),true,rj),t2=two.route(snap(2,1,-7000),true,rj),t3=two.route(snap(3,1,-7000),true,rj);
        check("T09 Capacity=7 serves all 7 remotes; a leftover machine of the local slot does not consume capacity; Capacity=2 defers the third",
            routed==7 && r.proxy(3)==nullptr && zero==RouteResult::Routed && t1==RouteResult::Routed && t2==RouteResult::Routed && t3==RouteResult::CapacityDeferred); }
    // T10 per-slot generations
    {   SnapshotRouter<7> r;r.bind(GetCurrentThreadId());Rejection rj{};r.configureSession(0,1,Epoch);
        r.configureSessionSlot(1,5,Epoch);r.configureSessionSlot(2,9,Epoch);r.onNativeMapReady(Epoch,5);
        const bool none=r.proxy(1)==nullptr && r.proxy(2)==nullptr; // creates nothing
        const auto a=r.route(snap(1,1,-7000,5),true,rj);const auto b=r.route(snap(2,1,-7000,9),true,rj);
        const auto cross=r.route(snap(1,2,-7000,9),true,rj);   // slot 1 snapshot with slot 2's generation
        const auto c=r.route(snap(3,1,-7000,1),true,rj);        // no per-slot value: router-wide generation 1
        r.configureSessionSlot(2,11,Epoch);                     // rejoin: only slot 2's existing machine is re-sessioned
        const auto oldGen=r.route(snap(2,2,-7000,9),true,rj);
        r.configureSessionSlot(9,1,1);                          // out of range: ignored
        check("T10 configureSessionSlot: per-slot generation, nothing created, later machines start with it, router-wide fallback, other slots untouched",
            none && a==RouteResult::Routed && b==RouteResult::Routed && cross==RouteResult::ProxyRejected && c==RouteResult::Routed && oldGen==RouteResult::ProxyRejected &&
            r.proxy(1)->sessionBound() && !r.proxy(2)->mapReady() && r.proxy(1)->mapReady()); }
    // T11 Seam: a call made from inside another machine's seam call is blocked (the per-machine flag cannot see it)
    {   World w;w.routeAll(1,-7000);w.tick();w.routeAll(2,-6000);const int before=fk.nativeCalls;
        UpdateOutcome inner{};ProxyCommand c{};c.slot=2;const auto h2=w.r.proxy(2)->handle();
        fk.during=[&] { inner=w.seam.update(h2,c); };
        w.r.proxy(1)->tick(&w.seam,Safe);fk.during=nullptr;
        check("T11 Seam in-call latch: nested update from another machine's callback makes no native call (Refused, counted); the outer one completes",
            inner==UpdateOutcome::Refused && w.seam.blocked()==1 && fk.nativeCalls==before+1 && fk.inst[1].updates==1 && fk.inst[2].updates==0); }
    {   World v;Rejection rj{};v.r.route(snap(1,1,-7000),true,rj);v.r.route(snap(2,1,-7000),true,rj);
        ProxyCommand sc{};sc.slot=2;ProxyHandle nested{};SpawnOutcome ns{};
        fk.during=[&] { ns=v.seam.spawn(sc,nested); };
        v.r.proxy(1)->tick(&v.seam,Safe);fk.during=nullptr;
        check("T11b nested spawn from inside another spawn is Deferred with no handle and no native spawn for slot 2",
            ns==SpawnOutcome::Deferred && nested==0 && fk.inst[2].spawns==0 && fk.inst[1].spawns==1 && v.seam.blocked()==1 && v.st(1)==VisualProxyState::ACTIVE);
        v.tick(); // the other machine proceeds normally on its own turn
        check("T11c the other machine spawns normally on its own turn",v.st(2)==VisualProxyState::ACTIVE && fk.inst[2].spawns==1); }
    // T12 Seam thread binding
    {   World w;w.seam.bindSimulationThread(GetCurrentThreadId()+1);w.routeAll(1,-7000);w.tick();
        check("T12 Seam bound to another thread: no native call, spawns Deferred",fk.nativeCalls==0 && w.seam.blocked()>=1 && w.st(1)==VisualProxyState::SPAWN_PENDING);
        w.seam.bindSimulationThread(GetCurrentThreadId());w.tick();
        check("T12b bound to the right thread the same machines spawn",w.allActive() && fk.nativeCalls==7); }
    // T13 teardown barrier for all
    {   World w;w.routeAll(1,-7000);w.tick();const int calls=fk.nativeCalls;w.r.forEach([](VisualProxyStateMachine& p) { p.onPreUnload(); });
        for(int i=0;i<4;++i) w.tick();bool all=true;for(unsigned s=1;s<8;++s) all&=w.st(s)==VisualProxyState::ORPHANED && w.r.proxy(static_cast<std::uint8_t>(s))->orphanedHandle()==fk.inst[s].handle;
        check("T13 world barrier: all seven ORPHANED with their own handles, no native call after the barrier",all && fk.nativeCalls==calls && w.r.quiescent()); }
    // T14 an SEH fault inside the table is a shared fault: the Seam stops for every machine
    {   World w;w.routeAll(1,-7000);w.tick();w.routeAll(2,-6000);fk.crashOnUpdate=true;const int calls=fk.nativeCalls;w.tick();fk.crashOnUpdate=false;
        const auto faults=w.seam.faults();w.tick();w.routeAll(3,-5000);w.tick();
        check("T14 native fault in a callback: Seam latches Faulted once, no further call for any slot, machines hold their state",
            faults==1 && w.seam.status()==pse::seam::Faulted && fk.nativeCalls==calls && w.seam.faults()==1); }
    // T15 per-tick work is bounded: at most one mutating call per machine per tick
    {   World w;w.routeAll(1,-7000);w.tick();const int before=fk.nativeCalls;w.routeAll(2,-6000);w.routeAll(3,-5900);w.routeAll(4,-5800);w.tick();
        check("T15 coalesced snapshots: exactly one native update per machine per tick (7 total)",fk.nativeCalls-before==7); }
    std::printf("TOTAL %d FAILED %d\n",total,failures);return failures?1:0;
}
