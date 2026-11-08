#include <windows.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include "gameplay_p14.hpp"
#include "gameplay_policy_p14.hpp"
#include "gameplay_profile_p14.hpp"

using namespace pse::p14;
namespace {
using Bytes=std::array<unsigned char,0x300>;
std::array<Bytes,8> players,actors,nodes;
Bytes ai{},pickup{},def{},foreign{};
std::vector<unsigned char> base(0xb00000);
Roster roster{};
unsigned checks{},failures{},gives{},consumed{},originalCalls{},faults{},scopeChanges{};
bool eligible=true,getterWorks=true,giveWorks=true,nested=false;
void* chosen{}; void* rejectedLOS{};
unsigned returnRva{};
template<class T> T& at(void* p,std::size_t n=0) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+n); }
void check(bool ok,const char* label) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n",label); } }
bool enabled() { return eligible; }
Roster members() { return roster; }
bool getter(bool) { ++scopeChanges; return getterWorks; }
void event(const char*,unsigned,unsigned) {}
void fault(const char*) { ++faults; }
void __fastcall originalAI(void*) {
    ++originalCalls;
    chosen=scopedPlayer(GetCurrentThreadId(),base.data()+returnRva);
    check(!scopedPlayer(GetCurrentThreadId(),base.data()+returnRva+1),"nested GetPlayer untouched");
    check(!scopedPlayer(GetCurrentThreadId()+1,base.data()+returnRva),"foreign GetPlayer untouched");
}
void __fastcall originalTouch(void*,void*) {
    originalAI(nullptr);
    if (nested) offlineInvoke(2,pickup.data(),actors[0].data());
}
unsigned char see(void*,void* target) { return target!=rejectedLOS; }
float angle(void* yaw,float bearing) { return std::remainder(bearing-at<float>(yaw),6.28318530718f); }
unsigned char give(void* player,unsigned mask) {
    ++gives;
    // Reproduce the dangerous native duplicate side effect, so an accidental call fails tests.
    if (at<std::uint16_t>(player,0x86)&mask) { at<std::int16_t>(player,0x84)=8; return 0; }
    ++at<std::int16_t>(player,0x84);
    if (!giveWorks) return 0;
    at<std::uint16_t>(player,0x86)|=static_cast<std::uint16_t>(mask);
    for (const auto& m:roster) if (m.player==player) at<std::int16_t>(m.actor,0x17a)=static_cast<std::int16_t>((at<std::int16_t>(player,0x84)+1)*200);
    return 1;
}
void consume(void* object,void*) { ++consumed; at<unsigned>(object,0x16c)&=~0x102u; }
int difficulty(void*) { return 0; }
void freshPickup(unsigned bit=0) {
    pickup={};def={};at<void*>(pickup.data(),0x170)=def.data();
    at<void*>(pickup.data(),0x10)=foreign.data();at<void*>(foreign.data(),0x28)=pickup.data();
    at<unsigned>(pickup.data(),0x16c)=0x102;at<int>(def.data(),0x144)=static_cast<int>(bit);
}
void reset() {
    players={};actors={};nodes={};roster={};ai={};foreign={};
    gives=consumed=originalCalls=faults=scopeChanges=0;chosen=nullptr;rejectedLOS=nullptr;
    eligible=getterWorks=giveWorks=true;nested=false;
    for (unsigned i=0;i<8;++i) {
        roster[i]={players[i].data(),actors[i].data(),i};
        at<std::int16_t>(actors[i].data(),0x17a)=100;
        at<float>(actors[i].data(),0x18)=float(i+1)*10;
    }
    at<float>(ai.data(),0x1cc)=1000;at<unsigned>(ai.data(),0x1d8)=2;
    at<float>(base.data(),0x53580c)=1.04719755f;
    base[0xaeb11d]=0;
    at<void*>(base.data(),0xaf33a0)=base.data()+0x52ad08;
    at<void*>(base.data(),0x52ad08+0x158)=base.data()+profile14::DifficultyRva;
    OfflineNative api{{reinterpret_cast<void*>(&originalAI),reinterpret_cast<void*>(&originalAI),
                       reinterpret_cast<void*>(&originalTouch),reinterpret_cast<void*>(&originalTouch),nullptr},see,angle,give,consume,difficulty};
    offlineBind(base.data(),GetCurrentThreadId(),{enabled,members,getter,event,fault},api);
    freshPickup();
}
void invocation(unsigned kind,void* toucher=nullptr) {
    returnRva=kind==0 ? 0x1955f4u : kind==1 ? 0x1972cau : kind==2 ? 0x1d8807u : 0x1d8638u;
    offlineInvoke(kind,kind<2 ? ai.data() : pickup.data(),toucher);
    check(!scopedPlayer(GetCurrentThreadId(),base.data()+returnRva),"scope closed");
}
}
int main() {
    // Exhaustive valid native Ankh progress combinations.
    for (unsigned mask=0;mask<65536;++mask)
        check(validProgress(mask,std::popcount(mask))==(mask<32),"Ankh mask domain");
    for (unsigned mask=0;mask<32;++mask)
        check(!validProgress(mask,std::popcount(mask)+1),"reject corrupted native count");
    for (unsigned slot=0;slot<8;++slot) {
        reset(); at<float>(actors[slot].data(),0x18)=1;
        invocation(0);check(chosen==players[slot].data(),"all eight slots selectable");
        check(scopeChanges==2,"scoped original idle");
        at<void*>(ai.data(),0x28)=nullptr;
        invocation(1);check(chosen==players[slot].data(),"chase fallback selects all slots");
        for (unsigned kind:{2u,3u}) {
            nested=true;invocation(kind,actors[slot].data());
            check(chosen==players[slot].data(),"touch to owner, nested touch rejected");
        }
    }
    reset();rejectedLOS=actors[0].data();invocation(0);
    check(chosen==players[1].data(),"closer occluded player rejected");
    reset();at<unsigned>(ai.data(),0x1d8)=0;
    at<float>(actors[0].data(),0x18)=0;at<float>(actors[0].data(),0x1c)=-1;
    at<float>(actors[1].data(),0x18)=0;at<float>(actors[1].data(),0x1c)=20;
    invocation(0);check(chosen==players[1].data(),"FOV before distance rank");
    reset();for (auto& actor:actors) at<std::int16_t>(actor.data(),0x17a)=0;
    invocation(0);invocation(1);check(originalCalls==0,"no candidate: no singleton or assertion");
    reset();base[0xaeb11d]=1;invocation(1);check(originalCalls==0,"native no-target respected");
    reset();invocation(2,foreign.data());check(originalCalls==0,"foreign toucher rejected before dereference");
    reset();getterWorks=false;invocation(3,actors[3].data());check(originalCalls==0 && faults==1,"scope failure stops native action");
    getterWorks=true;invocation(2,actors[0].data());invocation(0);invocation(1);
    check(originalCalls==3,"after a latched failure the stock handlers run (AI and pickups never freeze)");
    reset();invocation(0);const auto calls=originalCalls;eligible=false;invocation(1);invocation(2,actors[0].data());
    check(originalCalls==calls+2,"lost authority falls back to the stock handlers");
    reset();for (unsigned i=0;i<5;++i) {
        freshPickup(i);invocation(4,actors[i].data());
        check(gives==8*(i+1) && consumed==i+1,"team grant once per live player");
        invocation(4,actors[(i+1)%8].data());
        freshPickup(i);invocation(4,actors[(i+2)%8].data());
        check(gives==8*(i+1),"same bit never enters non-idempotent helper");
        // Two consumptions: original pickup and a separate duplicate item.
        consumed=i+1;
    }
    for (unsigned i=0;i<8;++i) check(validProgress(at<std::uint16_t>(players[i].data(),0x86),at<std::int16_t>(players[i].data(),0x84)),"all five bits consistent");
    check(faults==0,"five Ankhs without original Steam branch");
    reset();at<std::int16_t>(actors[7].data(),0x17a)=0;
    invocation(4,actors[0].data());check(gives==7 && at<std::int16_t>(actors[7].data(),0x17a)==0,"no resurrection by helper");
    sync();check(gives==7,"pending dead recipient stays pending");
    at<std::int16_t>(actors[7].data(),0x17a)=100;sync();check(gives==8,"missing bit delivered after live binding");
    sync();check(gives==8,"sync idempotent");
    reset();at<std::int16_t>(players[4].data(),0x84)=8;invocation(4,actors[0].data());
    check(!gives && !consumed && faults==1,"prevalidate entire roster before grant");
    reset();giveWorks=false;invocation(4,actors[0].data());sync();
    check(gives==1 && consumed==0 && faults==1,"failed helper not retried or consumed");
    reset();at<void*>(base.data(),0xaf33a0)=nullptr;invocation(4,actors[0].data());
    check(!gives && !consumed && faults==1,"wrong virtual target blocks before helper");
    reset();freshPickup(5);invocation(4,actors[0].data());check(!gives && !consumed && faults==1,"undefined Ankh rejected");
    reset();eligible=false;invocation(4,actors[0].data());check(!gives && !consumed,"Ankh held before host authority");
    std::array<Candidate,8> candidates{}; candidates[0]={true,3,4};candidates[1]={true,-3,-4};
    check(nearest(candidates,0,0,10,[](unsigned){return true;})==0,"ties stable by slot");
    candidates[0].x=std::numeric_limits<float>::quiet_NaN();
    check(nearest(candidates,0,0,10,[](unsigned){return true;})==1,"nonfinite player rejected");
    check(nearest(candidates,0,0,0,[](unsigned){return true;})==-1,"invalid sight rejected");
    std::printf("P14 CONTRACT checks=%u failures=%u; OFFLINE ONLY\n",checks,failures);
    return failures ? 1 : 0;
}
