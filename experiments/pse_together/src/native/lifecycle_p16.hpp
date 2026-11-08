// Internal exact-build P16 native contract; no engine pointers on the wire.
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "build_profile.hpp"
#include "proxy_profile.hpp"
namespace pse::life_p16 {
using Byte=unsigned char;
using Journal=void(*)(const char*,const char*,...);
struct Point {float x{},y{},z{};};
struct Start {Point position{};float yaw{};int sector{};};
struct Graph {Byte* player{};Byte* actor{};Byte* node{};std::uint64_t allocation{};};
struct Story {
    std::uint16_t keys{},quest{},ankhs{},ankhMask{};
    std::uint32_t artifacts{},abilities{},dolls{};
    bool operator==(const Story&)const=default;
};
struct Receipt {
    Graph old{},fresh{};Story story{};Start start{};
    std::uint32_t epoch{},generation{};std::uint64_t probe{};
    std::uintptr_t probeReferenceEvidence{};bool awaitingKeys{},committed{};
};
template<class T>T get(const void* p,std::size_t off=0){return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+off);}
inline bool graph(Byte* base,Graph& g){
    if(!g.player)return false;
    g.node=get<Byte*>(g.player,0xd8);g.actor=g.node?get<Byte*>(g.node,0x28):nullptr;
    if(!g.actor||get<Byte*>(g.actor,0x10)!=g.node||get<Byte*>(g.actor,0x1b8)!=g.player||get<void*>(g.actor)!=base+0x52d198)return false;
    const auto* h=g.actor-0x60;
    if(get<std::uint32_t>(h)!=0x3151983u||get<void*>(h,0x18)!=base+0xaf4490)return false;
    g.allocation=get<std::uint64_t>(h,8);return g.allocation!=0;
}
inline bool sameGraph(Byte* base,const Graph& expected){
    Graph now{expected.player};return graph(base,now)&&now.actor==expected.actor&&now.node==expected.node&&now.allocation==expected.allocation;
}
inline Story story(Byte* p){return {get<std::uint16_t>(p,0x98),get<std::uint16_t>(p,0x9a),get<std::uint16_t>(p,0x84),get<std::uint16_t>(p,0x86),get<std::uint32_t>(p,0x90),get<std::uint32_t>(p,0x94),get<std::uint32_t>(p,0xa4)};}
inline bool world(Byte* base){return get<int>(base+0xaf33a0,0x20)==3&&get<int>(base+0xaf33a0,0x24)==3&&get<int>(base+0xaf33a0,0x28)==0;}
inline bool startGeometry(Byte* base,const Start& candidate,const Start& original,Journal log=nullptr){
    namespace pp=pse::native_proxy::profile;
    const auto& p=candidate.position;const auto& b=original.position;
    const auto fail=[&](const char* why){
        if(log)log("p16_start_geometry_rejected","\"why\":\"%s\",\"candidate\":[%.9g,%.9g,%.9g],\"candidate_yaw\":%.9g,\"candidate_sector\":%d,\"original\":[%.9g,%.9g,%.9g],\"original_yaw\":%.9g,\"original_sector\":%d",
            why,p.x,p.y,p.z,candidate.yaw,candidate.sector,b.x,b.y,b.z,original.yaw,original.sector);
        return false;};
    if(candidate.sector!=original.sector)return fail("sector");
    if(candidate.yaw!=original.yaw)return fail("yaw");
    if(!(std::fabs(p.z-b.z)<=4.0f))return fail("z");
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z))return fail("finite");
    if((p.x-b.x)*(p.x-b.x)+(p.y-b.y)*(p.y-b.y)>128.0f*128.0f)return fail("distance");
    const auto count=get<std::uint64_t>(base,0xa45970);auto* sectors=get<Byte*>(base,0xa45968);
    if(!sectors||count>4096||candidate.sector<0||static_cast<std::uint64_t>(candidate.sector)>=count)return fail("sector_table");
    for(const auto i:{pp::point_in_sector,pp::floor_height,pp::ceiling_height}){
        const auto& site=pp::Sites[i];if(!std::equal(std::begin(site.prefix),std::end(site.prefix),base+site.rva))return fail("native_prefix");
    }
    auto* sector=sectors+static_cast<std::size_t>(candidate.sector)*0x88;
    using NativePoint16=std::uint8_t(__fastcall*)(void*,const float*,float);using NativeHeight16=float(__fastcall*)(const float*,void*,std::uint8_t);
    const auto point=reinterpret_cast<NativePoint16>(base+pp::Sites[pp::point_in_sector].rva);
    const auto height=[&](const Point& at,std::uint8_t ceiling){return reinterpret_cast<NativeHeight16>(base+pp::Sites[ceiling?pp::ceiling_height:pp::floor_height].rva)(&at.x,sector,1);};
    if(point(sector,&p.x,0)!=1)return fail("candidate_not_in_sector");
    if(point(sector,&b.x,0)!=1)return fail("original_not_in_sector");
    // Both support heights come from the same native query at the two points;
    // cached actor fields are not trusted on the first client tick.
    const float floor=height(candidate.position,0),ceiling=height(candidate.position,1);
    const float originalFloor=height(original.position,0),originalCeiling=height(original.position,1);
    if(!std::isfinite(floor)||!std::isfinite(ceiling)||!std::isfinite(originalFloor)||!std::isfinite(originalCeiling))return fail("height_finite");
    if(floor!=originalFloor||ceiling!=originalCeiling){
        if(log)log("p16_start_support_mismatch","\"floor\":%.9g,\"original_floor\":%.9g,\"ceiling\":%.9g,\"original_ceiling\":%.9g",floor,originalFloor,ceiling,originalCeiling);
        return fail("support");
    }
    return true;
}
inline bool prefixes(Byte* base){
    for(const auto i:{profile::Reset,profile::Spawn,profile::WeaponReady})
        if(!std::equal(std::begin(profile::Sites[i].prefix),std::end(profile::Sites[i].prefix),base+profile::Sites[i].rva))return false;
    constexpr Byte remove[]{0x40,0x53,0x48,0x83,0xec,0x20,0x81,0xa1,0x6c,0x01,0x00,0x00,0x5d,0xfc,0xff,0xff};
    constexpr Byte give[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    constexpr Byte keys[]{0x48,0x8d,0x81,0x98,0x00,0x00,0x00,0xc3};
    constexpr Byte pack[]{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb7,0x81,0xfc,0x00,0x00};
    constexpr Byte restore[]{0x0f,0xb6,0x42,0x1c,0x88,0x81,0x0c,0x01,0x00,0x00,0x0f,0xb7,0x42,0x0c,0x66,0x89};
    return std::equal(std::begin(remove),std::end(remove),base+0x1a02b0)&&std::equal(std::begin(give),std::end(give),base+0x1e06d0)&&
        std::equal(std::begin(keys),std::end(keys),base+0x1ae0c0)&&std::equal(std::begin(pack),std::end(pack),base+0x1b5b80)&&
        std::equal(std::begin(restore),std::end(restore),base+0x1b5ce0);
}
inline bool kit(const Graph& g){
    if(get<std::int16_t>(g.actor,0x17a)!=200||(get<unsigned>(g.actor,0x1c0)&0x200u))return false;
    for(unsigned i=0;i<8;++i)if(get<Byte>(g.player,0xcc+i)!=(i<2?1:0)||get<std::uint16_t>(g.player,0xbc+2*i)!=(i==1?60:0))return false;
    return true;
}
// Called only by a Sim-thread owner with current map/provenance/start gates.
// Scope selects the existing external Player only during its native Spawn.
template<class Scope>bool revive(Byte* base,DWORD tid,Graph old,const Start& start,std::uint32_t epoch,std::uint32_t generation,
                               std::uint64_t probe,Scope scope,Receipt& r,Journal log){
    if(GetCurrentThreadId()!=tid||!world(base)||!prefixes(base)||!sameGraph(base,old)||!epoch||!generation||!probe||
       (get<std::int16_t>(old.actor,0x17a)>0&&!(get<unsigned>(old.actor,0x1c0)&0x200u))||
       !std::isfinite(start.position.x)||!std::isfinite(start.position.y)||!std::isfinite(start.position.z)||!std::isfinite(start.yaw)||start.sector<0)return false;
    if(get<void*>(get<void*>(old.actor),0x30)!=base+0x1a02b0||get<void*>(get<void*>(base+0x5a10a0),0x18)!=base+0x1d4780)return false;
    r={};r.old=old;r.story=story(old.player);r.start=start;r.epoch=epoch;r.generation=generation;r.probe=probe;
    using NativeUnary16=void(__fastcall*)(void*);using NativeGive16=Byte(__fastcall*)(void*,int,Byte);
    using NativeSpawn16=void*(__fastcall*)(void*,int,const Point*,float,int);using NativeReady16=void(__fastcall*)(void*,int);
    using NativeScalars16=void(__fastcall*)(void*,void*);using NativeKeys16=std::uint16_t*(__fastcall*)(void*);
    log("p16_native_respawn_begin","\"epoch\":%u,\"generation\":%u,\"old_actor\":\"%p\",\"old_node\":\"%p\",\"old_allocation\":%llu,\"player\":\"%p\"",epoch,generation,old.actor,old.node,old.allocation,old.player);
    if(!get<Byte>(old.actor,0x40))reinterpret_cast<NativeUnary16>(base+0x1a02b0)(old.actor);
    reinterpret_cast<NativeUnary16>(base+0x1dfa70)(old.player);
    reinterpret_cast<NativeGive16>(base+0x1e06d0)(old.player,1,0);
    if(!scope(true))return false;
    auto* created=static_cast<Byte*>(reinterpret_cast<NativeSpawn16>(base+0x1d4780)(base+0x5a10a0,0,&start.position,start.yaw,start.sector));
    const bool restored=scope(false);
    r.fresh.player=old.player;
    if(!restored||!created||!graph(base,r.fresh)||r.fresh.actor!=created||r.fresh.actor==old.actor||r.fresh.node==old.node||r.fresh.allocation==old.allocation)return false;
    reinterpret_cast<NativeReady16>(base+0x20f9e0)(old.player+0xe0,0);
    // Use the already accepted scalar pack/restore contract, not structure
    // copying. Preserve story fields; strip inaccessible heavy-weapon ammo.
    std::array<Byte,48> packed{};void* receiver=reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(old.player)-0x40u);
    reinterpret_cast<NativeScalars16>(base+0x1b5b80)(receiver,packed.data());
    const auto put=[&](unsigned at,std::uint32_t value,unsigned size){for(unsigned i=0;i<size;++i)packed[at+i]=static_cast<Byte>(value>>(8*i));};
    put(0,r.story.ankhs,2);put(2,r.story.ankhMask,2);put(4,r.story.artifacts,2);put(6,r.story.quest,2);put(8,r.story.abilities,2);put(10,0,2);put(36,r.story.dolls,4);
    for(unsigned i=0;i<8;++i)put(12+2*i,i==1?60u:0u,2);
    put(40,get<std::uint32_t>(old.player,0x8c),4);
    reinterpret_cast<NativeScalars16>(base+0x1b5ce0)(receiver,packed.data());
    if(!sameGraph(base,r.fresh)||!kit(r.fresh)||get<int>(created,0x3c)!=start.sector||
       get<float>(created,0x18)!=start.position.x||get<float>(created,0x1c)!=start.position.y||get<float>(created,0x20)!=start.position.z)return false;
    auto* const keyReference=reinterpret_cast<NativeKeys16>(base+0x1ae0c0)(old.player);
    if(reinterpret_cast<Byte*>(keyReference)!=old.player+0x98)return false;
    r.probeReferenceEvidence=reinterpret_cast<std::uintptr_t>(keyReference); // numeric audit witness only; never dereferenced later
    r.awaitingKeys=*keyReference!=r.story.keys;
    log("p16_keys_readonly_probe","\"probe\":%llu,\"epoch\":%u,\"generation\":%u,\"actor\":\"%p\",\"node\":\"%p\",\"allocation\":%llu,\"receiver\":\"%p\",\"reference\":\"%p\",\"expected\":\"%p\",\"value\":%u,\"desired\":%u,\"identity_match\":true,\"writes\":0",probe,epoch,generation,r.fresh.actor,r.fresh.node,r.fresh.allocation,old.player,keyReference,old.player+0x98,*keyReference,r.story.keys);
    return true;
}
inline bool finish(Byte* base,DWORD tid,Receipt& r,std::uint64_t approvedProbe,std::uint64_t review,Journal log){
    if(GetCurrentThreadId()!=tid||!world(base)||!prefixes(base)||!sameGraph(base,r.fresh)||!kit(r.fresh)||r.committed)return false;
    if(r.awaitingKeys){
        if(approvedProbe!=r.probe||!review||r.story.keys>15)return false;
        using Keys=std::uint16_t*(__fastcall*)(void*);auto* ref=reinterpret_cast<Keys>(base+0x1ae0c0)(r.fresh.player);
        if(reinterpret_cast<std::uintptr_t>(ref)!=r.probeReferenceEvidence||reinterpret_cast<Byte*>(ref)!=r.fresh.player+0x98)return false;
        log("p16_keys_gate_consumed","\"probe\":%llu,\"review\":%llu,\"epoch\":%u,\"generation\":%u,\"before\":%u,\"desired\":%u",approvedProbe,review,r.epoch,r.generation,*ref,r.story.keys);
        *ref=r.story.keys;
    }
    if(story(r.fresh.player)!=r.story)return false;
    r.committed=true;return true;
}
class RestartGate {
    Byte* base_{};DWORD tid_{};bool installed_{};
    static constexpr std::array<Byte,6> Original{0x0f,0x82,0xae,0x04,0x00,0x00},Patch{0xe9,0xaf,0x04,0x00,0x00,0x90};
    bool write(const auto& bytes){DWORD before{},unused{};auto* p=base_+0x1dffd6;if(!VirtualProtect(p,6,PAGE_EXECUTE_READWRITE,&before))return false;
        for(unsigned i=0;i<6;++i)p[i]=bytes[i];FlushInstructionCache(GetCurrentProcess(),p,6);return VirtualProtect(p,6,before,&unused)!=0;}
public:
    bool install(Byte* base,DWORD tid){if(installed_)return true;base_=base;tid_=tid;
        if(GetCurrentThreadId()!=tid||!std::equal(Original.begin(),Original.end(),base+0x1dffd6))return false;
        installed_=write(Patch);return installed_;}
    bool restore(Journal log){if(!installed_)return true;if(GetCurrentThreadId()!=tid_||!std::equal(Patch.begin(),Patch.end(),base_+0x1dffd6)||!write(Original))return false;
        const bool exact=std::equal(Original.begin(),Original.end(),base_+0x1dffd6);installed_=false;log("p16_restart_restore","\"exact6\":%s",exact?"true":"false");return exact;}
};
}
