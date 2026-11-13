// P1 exact-build port of DOC-58B adapter58b.js + DOC-58A controller/one_actor.
// Native ABIs and offsets come from accepted DOC-57/58A evidence, not decompiler types.
// Up to 8 independent passive Actor1007 proxies (one Instance per remote slot 0..7), +88 presentation offset.
// Shared: image base, sim thread, journal, hooks, local-player baseline, sector geometry, stability checks.
// Per Instance: the Lifetime (one native spawn per Lifetime), handle, expected transform/sector, playout buffer, animation, fault state.
// No reconnect, LinkSector or teardown rearm except rearm()/rearmSlot() (no engine calls).
#include "native_proxy.hpp"
#include "proxy_profile.hpp"
#include "map_reader.hpp"
#include "bounded_proxy.hpp"
#ifdef PSE_FEATURE_P8
#include "sprite_binding.hpp"
#endif
#ifdef PSE_FEATURE_P4
#include "portal_plan.hpp"
#include "presentation_buffer.hpp"
#include <chrono>
#endif
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace pse::native_proxy {
namespace {
using Byte = unsigned char;
using bounded::Vec3;
using bounded::Plane;
using bounded::Phase;
using proxy::ProxyCommand;
using Unary = std::uintptr_t(__fastcall*)(void*);
using Binary = std::uintptr_t(__fastcall*)(void*,void*);
using NoArgs = std::uintptr_t(__fastcall*)();
constexpr unsigned MaxSlots=8;
// shared=true: the fault concerns state every proxy depends on (local player graph, world stability, sector lists/geometry,
// native call prefixes): it stops ALL proxies. shared=false: it concerns one proxy and stops only that one.
struct Error { const char* reason;bool shared; };
void require(bool ok,const char* reason) { if(!ok) throw Error{reason,false}; }
void requireShared(bool ok,const char* reason) { if(!ok) throw Error{reason,true}; }
template<class T> T read(const void* p,std::size_t offset=0) { return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+offset); }
template<class T> void store(void* p,std::size_t offset,T v) { *reinterpret_cast<T*>(static_cast<Byte*>(p)+offset)=v; }
Byte* ptr(std::uintptr_t p) { return reinterpret_cast<Byte*>(p); }
struct Local {
    Byte* node{};Byte* actor{};Byte* owner{};Byte* vtable{};Byte* ownNode{};
    Vec3 position{},render{},angles{};float yaw{},pitch{};
    std::array<Byte,128> input{};
    std::array<Byte,54> fields{}; // 12 accepted DOC57 fields, initialized content only
    int sector{};
    bool sameGraph(const Local& b) const { return node==b.node && actor==b.actor && owner==b.owner && vtable==b.vtable && ownNode==b.ownNode; }
    bool operator==(const Local&) const = default;
};
struct Sector { std::vector<Plane> planes;
#ifdef PSE_FEATURE_P4
    pse::portal::Sector portal;
#endif
};
// Everything that belongs to ONE proxy Actor. Instances never read or write each other (only the ACTOR_ALIAS check compares pointers).
struct Instance {
    unsigned slot{};
    bounded::Lifetime life;
    bool faulted{},removeRequest{},controllerStopped{};
    unsigned spawnCalls{},removeCalls{},revision{},actorTicks{},liveSteps{},postSteps{},waterSwitches{};
    std::uint64_t removeStep{},handle{},applied{},unchanged{},refused{};
    std::uint64_t serial{}; // handle counter; survives rearm so a stale handle can never match a new incarnation
    Vec3 expected{};float expectedYaw{},floor{},ceiling{};
    ProxyCommand command{};
#ifdef PSE_FEATURE_P8
    pse::player_art::Animation art;
    float artScale{1.5f};
#endif
#ifdef PSE_FEATURE_P4
    int expectedSector{259},plannedSector{259};bool relocate{};pse::presentation::Buffer buffer;
#endif
};
struct State {
    Byte* base{};std::atomic<DWORD> tid{};Journal journal{};
    void* original[profile::Count]{};bool enabled[profile::Count]{};
    std::atomic<unsigned> spawnPermit{};
    bool hostMode{}; // hostMode: the DLL already hooks the puppet tick
    bool installed{},inTick{},inSweep{},stopping{},sharedFault{},baseline{},area{};
    unsigned p1ThisTick{};
    std::uint64_t ticks{},tickSerial{},steps{};
    std::uint32_t lastStamp{},lastP1Stamp{};bool hasStamp{},hasP1Stamp{};
    Local local{};
    Instance inst[MaxSlots];
#ifdef PSE_FEATURE_P8
    std::atomic<unsigned> preview{0}; // 0=authoritative DTO, otherwise slot+pose encoded (diagnostic; applies to every proxy)
    bool resourcesAttested{};
#endif
    std::vector<Sector> sectors;
#ifdef PSE_FEATURE_P4
    bool geometryFresh{};std::uint64_t geometryTick{};
    std::uint64_t geometryRefreshes{},geometryMaxNs{};
#endif
    State() { for(unsigned i=0;i<MaxSlots;++i) inst[i].slot=i; }
} g;
void emit(const char* event,const char* fmt="",...) {
    if(!g.journal) return;
    char detail[2100]{};va_list args;va_start(args,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args);va_end(args);
    g.journal(event,detail);
}
#ifdef PSE_FEATURE_P4
int currentSector(const Instance& I) {
    return I.expectedSector;
}
#else
#define currentSector(I) 259
#endif
#ifdef PSE_FEATURE_P4
std::uint64_t nowNs() { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); }
#endif
bool thread() { return GetCurrentThreadId()==g.tid; }
std::uint32_t stamp() { return read<std::uint32_t>(g.base,0xaec390); }
Byte* player() { return g.base+0xaf33e0; }
void* at(profile::Index i) { return g.base+profile::Sites[i].rva; }
bool prefix(profile::Index i) { return std::equal(std::begin(profile::Sites[i].prefix),std::end(profile::Sites[i].prefix),static_cast<Byte*>(at(i))); }
bool stable() {
    if(!thread() || g.stopping) return false;
    const auto o=map56::observe(g.base);
    map56::Identity id;
    // Generic engine stability only. Map identity against the host is enforced by the ClientGate (Welcome name/structure).
    return map56::derive(o,id)==map56::Fault::None && o.state==3 && o.next==3 && !o.pending && !id.name.empty();
}
void scope() { requireShared(thread() && g.inTick && !g.stopping,"SIMULATION_SCOPE");requireShared(stable(),"WORLD_NOT_STABLE"); }
constexpr std::size_t FieldOffsets[]={0x84,0x86,0x90,0x94,0x98,0x9a,0xa4,0x88,0x8c,0xcc,0xbc,0x17a};
constexpr std::size_t FieldSizes[]={2,2,4,4,2,2,4,4,4,8,16,2};
static_assert(2+2+4+4+2+2+4+4+4+8+16+2==54);
Local local() {
    Local p;p.node=read<Byte*>(player(),0xd8);requireShared(p.node!=nullptr,"P1_NODE_NULL");
    p.actor=read<Byte*>(p.node,0x28);requireShared(p.actor!=nullptr,"P1_ACTOR_NULL");
    p.ownNode=read<Byte*>(p.actor,0x10);p.owner=read<Byte*>(p.actor,0x1b8);p.vtable=read<Byte*>(p.actor);
    requireShared(p.node==p.ownNode && p.owner==player() && p.vtable==g.base+0x52d198,"P1_GRAPH");
    p.position=read<Vec3>(p.actor,0x18);p.render=read<Vec3>(p.actor,0x6c);p.angles=read<Vec3>(p.actor,0x94);
    p.yaw=read<float>(p.actor,0x38);p.pitch=read<float>(p.actor,0x30);p.sector=read<int>(p.actor,0x3c);
    p.input=read<std::array<Byte,128>>(player());
    std::size_t out=0;
    for(std::size_t f=0;f<12;++f) for(std::size_t b=0;b<FieldSizes[f];++b)
        p.fields[out++]=read<Byte>(f==11?p.actor:player(),FieldOffsets[f]+b);
    return p;
}
bool localRespawned{}; // set by acceptLocalRespawn(): one deliberate change of the local player's Actor/node
void checkLocal(const Local& p) {
    if(localRespawned && g.baseline) { g.local=p;localRespawned=false;emit("p1_rebaselined","\"actor\":\"%p\",\"node\":\"%p\"",p.actor,p.node); }
    requireShared(g.baseline && p.sameGraph(g.local),"P1_OWNERSHIP_CHANGED");
    // Milestone 2: the game itself changes P1's inventory/health all the time (shooting, damage, keys, pickups). That is not a
    // proxy fault: the baseline follows the game, the ownership/graph check above still guards the actor identity.
    if(p.fields!=g.local.fields) g.local.fields=p.fields;
}
void snapshot(const Instance& I,const char* label) {
    const auto p=local();checkLocal(p);
    char hex[109]{};for(std::size_t i=0;i<p.fields.size();++i) sprintf_s(hex+2*i,3,"%02x",p.fields[i]);
    emit("p1_state_snapshot","\"slot\":%u,\"label\":\"%s\",\"fields_raw\":\"%s\",\"fields\":12,\"equal\":true,\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\",\"pos\":[%.9g,%.9g,%.9g],\"engine_stamp\":%u",I.slot,label,hex,p.actor,p.node,p.owner,p.position[0],p.position[1],p.position[2],stamp());
}
Byte* sector(int index) {
    const auto count=read<std::uint64_t>(g.base,0xa45970);auto* start=read<Byte*>(g.base,0xa45968);
    requireShared(start && index>=0 && count<=100000 && static_cast<std::uint64_t>(index)<count,"SECTOR_BOUNDS");
    return start+static_cast<std::size_t>(index)*0x88;
}
struct List { unsigned count{},found{},localFound{}; };
List list(int index,Byte* own=nullptr,const Local* p=nullptr) {
    Byte* head=read<Byte*>(sector(index),0x78);requireShared(head && !read<Byte*>(head),"SECTOR_HEAD");
    Byte* previous=head;Byte* n=read<Byte*>(head,8);List result;
    while(n && read<Byte*>(n,8)) {
        requireShared(result.count<4096 && read<Byte*>(n)==previous && read<Byte*>(previous,8)==n,"SECTOR_LINKS");
        ++result.count;if(n==own) ++result.found;
        if(p && n==p->node && read<Byte*>(n,0x28)==p->actor) ++result.localFound;
        previous=n;n=read<Byte*>(n,8);
    }
    requireShared(n && read<Byte*>(n)==previous,"SECTOR_TAIL");return result;
}
#ifdef PSE_FEATURE_P4
void geometry(int index,Sector& out) {
#else
Sector geometry(int index) {
#endif
    Byte* s=sector(index);requireShared(read<int>(s,0x80)==index,"SECTOR_SELF_INDEX");
    const auto first=read<std::uint16_t>(s),last=read<std::uint16_t>(s,2);
    const auto count=read<std::uint64_t>(g.base,0xa45988);auto* faces=read<Byte*>(g.base,0xa45980);
    requireShared(faces && first<=last && static_cast<std::uint64_t>(last)+2<count && last-first<=512,"FACE_BOUNDS");
#ifdef PSE_FEATURE_P4
    out.planes.clear();out.portal.faces.clear(); // Reuse attested capacities; reread mutable planes.
    out.portal.flags=read<std::uint16_t>(s,0x14);
#else
    Sector out;
#endif
    for(unsigned i=first;i<=static_cast<unsigned>(last)+2;++i) {
        auto* f=faces+i*0x108;const auto n=read<Vec3>(f,0xc);const double d=read<float>(f,0x18);
        requireShared(bounded::finite(n) && std::isfinite(d),"NONFINITE_PLANE");out.planes.push_back({n[0],n[1],n[2],d});
#ifdef PSE_FEATURE_P4
        out.portal.faces.push_back({out.planes.back(),read<std::int16_t>(f,6),(read<std::uint32_t>(f,0x1c)&0x2000)!=0});
#endif
    }
#ifndef PSE_FEATURE_P4
    return out;
#endif
}
#ifdef PSE_FEATURE_P4
Sector geometry(int index) { Sector out;geometry(index,out);return out; }
#endif
#ifdef PSE_FEATURE_P4
float height(profile::Index i,const Vec3& p,int index=259) {
#else
float height(profile::Index i,const Vec3& p) {
    constexpr int index=259;
#endif
    // DOC58A instructions: XMM0 return, RCX point*, RDX sector*, R8B mode.
    using Query=float(__fastcall*)(const float*,void*,std::uint8_t);
    return reinterpret_cast<Query>(at(i))(p.data(),sector(index),1);
}
#ifdef PSE_FEATURE_P4
bool point(const Vec3& p,int index=259) {
#else
bool point(const Vec3& p) {
    constexpr int index=259;
#endif
    using Query=std::uint8_t(__fastcall*)(void*,const float*,float);
    return reinterpret_cast<Query>(at(profile::point_in_sector))(sector(index),p.data(),0)==1;
}
bool unique(const Vec3& p) {
    unsigned hits=0;int own=-1;
    for(std::size_t i=0;i<g.sectors.size();++i) if(bounded::inside(g.sectors[i].planes,p)) { ++hits;own=static_cast<int>(i); }
    return hits==1 && own==259;
}
bool sameFloat(float a,float b) { return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b); }
void attestArea() {
    // Every sector of the loaded map (any level): a proxy is placed in whichever sector its player actually stands in.
    const auto count=read<std::uint64_t>(g.base,0xa45970);requireShared(count>0 && count<=4096,"SECTOR_COUNT");
    g.sectors.clear();for(int i=0;i<static_cast<int>(count);++i) g.sectors.push_back(geometry(i));
    g.area=true;
#ifdef PSE_FEATURE_P4
    g.geometryFresh=true;g.geometryTick=g.tickSerial;
#endif
    emit("attested_area","\"status\":\"ATTESTED\",\"sector_count\":%llu,\"generic\":true,\"z_offset\":88,\"native_cpp\":true",static_cast<unsigned long long>(count));
}
bool interpolation(Byte* a) {
    return read<Byte*>(a,0x58)==a+0x18 && read<Byte*>(a,0x80)==a+0x30 &&
        read<Byte*>(a,0x50)==g.base+0x52bdf0 && read<Byte*>(a,0x78)==g.base+0x52be68 &&
        bounded::finite(read<Vec3>(a,0x60)) && bounded::finite(read<Vec3>(a,0x6c)) &&
        bounded::finite(read<Vec3>(a,0x88)) && bounded::finite(read<Vec3>(a,0x94));
}
// Actor flags of the proxy (type 1007): 0x40 = fullbright (the 2022 EXE numbers actor flags one bit lower than the 2015 source: 0x2 solid,
// 0x20 shootable, 0x40 fullbright, 0x80 moveable, 0x100 touchable, 0x400 in water, 0x2000 hidden). Under water the proxy is lit and in water
// instead (see water()); nothing else may ever appear.
bool passiveFlags(unsigned f) { return f==0x40u || f==0x400u || f==0x440u; }
void actorIdentity(const Instance& I,Byte* a,Byte* expectedNode,bool strict) {
    require(a && a!=g.local.actor,"ACTOR_ALIAS");auto* n=read<Byte*>(a,0x10);
    // Several proxies may share a sector and even a position, but never an Actor or ObjectNode.
    for(const auto& o:g.inst) if(&o!=&I) require(!(o.life.actor && ptr(o.life.actor)==a) && !(o.life.node && n && ptr(o.life.node)==n),"ACTOR_ALIAS");
    require(n && n!=g.local.node && (!expectedNode || n==expectedNode),"ACTOR_NODE");
    require(read<Byte*>(a)==g.base+0x528608 && read<Byte*>(n,0x28)==a && read<Byte>(n,0x24)==1,"ACTOR_IDENTITY");
    auto* definition=read<Byte*>(a,0x170);
    require(definition && read<int>(definition)==1007 && read<int>(a,0x3c)==currentSector(I),"ACTOR_TYPE_SECTOR");
    require(read<Vec3>(a,0x18)==I.expected && sameFloat(read<float>(a,0x38),I.expectedYaw) && !read<Byte>(a,0x40),"ACTOR_TRANSFORM_REMOVED");
    const auto p=local();checkLocal(p);require(list(p.sector,nullptr,&p).localFound==1 && list(currentSector(I),n).found==1,"ACTOR_MEMBERSHIP");
    require(interpolation(a),"ACTOR_INTERPOLANTS");
    if(strict) require(passiveFlags(read<unsigned>(a,0x16c)) && read<int>(n,0x40)==0 && read<float>(a,0x124)==
#ifdef PSE_FEATURE_P8
        I.artScale &&
#else
        1.5f &&
#endif
        !read<Byte*>(a,0x28) && !read<Byte*>(a,0x148) && read<Vec3>(a,0xec)==Vec3{-12,-12,-24} &&
        read<Vec3>(a,0xf8)==Vec3{12,12,24},"ACTOR_PASSIVE_PROPERTIES");
}
#ifdef PSE_FEATURE_P4
const char* target(Instance& I,const Vec3& p,bool withActor) {
    if(!bounded::finite(p)||!g.area) return "P4_TARGET_UNATTESTED";
    // Refresh numeric geometry: doors/movers must not leave a stale plane proof. All proxies of one simulation tick share
    // one refresh (the engine tick that moves doors is over when the proxy phase runs).
    if(!g.geometryFresh || g.geometryTick!=g.tickSerial) {
        const auto began=nowNs();
        for(std::size_t i=0;i<g.sectors.size();++i) geometry(static_cast<int>(i),g.sectors[i]);
        const auto duration=nowNs()-began;g.geometryMaxNs=std::max(g.geometryMaxNs,duration);++g.geometryRefreshes;
        g.geometryFresh=true;g.geometryTick=g.tickSerial;
        if(g.geometryRefreshes<=3 || g.geometryRefreshes%60==0)emit("p4_geometry_refresh","\"sectors\":%zu,\"refreshes\":%llu,\"duration_ns\":%llu,\"max_ns\":%llu,\"capacity_reused\":true",g.sectors.size(),g.geometryRefreshes,duration,g.geometryMaxNs);
    }
    int found=-1;unsigned hits=0;
    for(std::size_t i=0;i<g.sectors.size();++i) if(bounded::inside(g.sectors[i].planes,p)) { found=static_cast<int>(i);++hits; }
    // Before the first spawn there is no current sector: a point on a shared boundary is retried on a later sample.
    const bool haveCurrent=I.life.actor!=0 && static_cast<std::size_t>(currentSector(I))<g.sectors.size();
    if(!hits || (hits>1 && !(haveCurrent && bounded::inside(g.sectors[static_cast<std::size_t>(currentSector(I))].planes,p)))) return "P4_SECTOR_AMBIGUOUS";
    if(hits>1) found=currentSector(I); // Retain exact boundary membership until the center crosses.
    if(!point(p,found)) return "P4_NATIVE_POINT_FALSE";
    const auto& dst=g.sectors[static_cast<std::size_t>(found)];
    I.relocate=false;
    if(withActor) {
        const auto& src=g.sectors[static_cast<std::size_t>(currentSector(I))];
        if(found!=currentSector(I)) {
            // No verified portal corridor (long jump, non-adjacent sector, coalesced snapshots): relocate with a native
            // LinkSector + interpolant reset instead of refusing, so the proxy always follows its player's real sector.
            if(!pse::portal::corridor(src.portal,currentSector(I),dst.portal,found,I.expected,p)) I.relocate=true;
        } else if(!bounded::fits(dst.planes,p)) {
            bool aperture=false;
            for(const auto& f:dst.portal.faces) if(f.portal && f.adjoin>=0 && static_cast<std::size_t>(f.adjoin)<g.sectors.size())
                aperture|=pse::portal::sweptAperture(dst.portal,found,g.sectors[static_cast<std::size_t>(f.adjoin)].portal,f.adjoin,I.expected,p);
            (void)aperture; // a proxy partly inside a wall is cosmetic only; the centre stays inside the sector
        }
        auto* a=ptr(I.life.actor);
        // Different floor/ceiling at the target: the engine's own height functions refresh the actor caches after the move.
        if(!sameFloat(height(profile::floor_height,p,found),read<float>(a,0x11c)) ||
           !sameFloat(height(profile::ceiling_height,p,found),read<float>(a,0x120))) I.relocate=true;
        if(read<unsigned>(a,0x16c)&0x80) return "PHYSICS_BIT7_SET";
    } // spawn: the centre is inside the sector (checked above) and native point-in-sector agrees; the box may touch a wall
    I.plannedSector=found;return nullptr;
}
#else
const char* target(Instance& I,const Vec3& p,bool withActor) {
    if(!bounded::finite(p)) return "NONFINITE_TARGET";
    if(!g.area) return "AREA_NOT_ATTESTED";
    if(!bounded::fits(g.sectors[259].planes,p)) return "OUT_OF_ATTESTED_AREA";
    if(!point(p)) return "NATIVE_POINT_IN_SECTOR_FALSE";
    if(!unique(p)) return "TARGET_NOT_UNIQUE";
    const float floor=height(profile::floor_height,p),ceiling=height(profile::ceiling_height,p);
    if(withActor) {
        auto* a=ptr(I.life.actor);
        if(!sameFloat(floor,read<float>(a,0x11c)) || !sameFloat(ceiling,read<float>(a,0x120))) return "FLOOR_CEILING_CACHE_MISMATCH";
        if(read<unsigned>(a,0x16c)&0x80) return "PHYSICS_BIT7_SET";
    } else if(!sameFloat(floor,I.floor) || !sameFloat(ceiling,I.ceiling)) return "FLOOR_CEILING_CACHE_MISMATCH";
    return nullptr;
}
#endif
// One proxy's fault: only that proxy stops (its weak pointers are nulled, no native call is made for it again).
void block(Instance& I,const char* reason) { I.faulted=true;I.controllerStopped=true;I.life.block();emit("native_proxy_fault","\"slot\":%u,\"scope\":\"slot\",\"reason\":\"%s\",\"all_weak_pointers_null\":true,\"retry\":false",I.slot,reason); }
// Shared-state fault (local player graph, world stability, hooks, prefixes, unknown native outcome): every proxy stops.
void blockAll(const char* reason) {
    g.sharedFault=true;
    for(auto& I:g.inst) { I.faulted=true;I.controllerStopped=true;I.life.block(); }
    emit("native_proxy_fault","\"scope\":\"shared\",\"reason\":\"%s\",\"all_weak_pointers_null\":true,\"retry\":false",reason);
}
// The world being briefly not in plain gameplay (a long frame, the engine loading or saving, a map fade) is not a fault of the proxies: it used to
// latch every proxy off for the rest of the world ("sprites of all players vanish" after a disk stall). Such a call is simply refused and retried.
bool soft(const Error& e) { return std::strcmp(e.reason,"WORLD_NOT_STABLE")==0; }
void fail(Instance* I,const Error& e) { if(soft(e)) return; if(e.shared || !I) blockAll(e.reason); else block(*I,e.reason); }
void reset(Byte* p) { reinterpret_cast<void(__fastcall*)(void*)>(at(profile::interpolant_reset))(p); }
#ifdef PSE_FEATURE_P8
void animate(Instance& I) {
    using namespace pse::player_art;
    scope();
    // A proxy whose controller just stopped (its Actor was moved or removed by someone else: it is being removed and replaced) is simply not
    // animated any more. This used to be a fault: the host's sprite went to quarantine for the rest of the level (friend's run 2026-11-12).
    if(I.life.phase!=Phase::Live || I.faulted || I.controllerStopped) return;
    const auto& c=I.command;
    // The pose follows the command on screen (I.command: the last transform actually applied, ~160 ms behind the newest snapshot because of
    // the presentation buffer; a refused out-of-area target keeps the previous one, and so does its pose).
    if(!I.art.sameEntity(c.slot,c.mapEpoch,c.connectionGeneration,c.entityId,c.entityGeneration)) return;
    const auto preview=g.preview.load(std::memory_order_acquire);
    const auto slot=preview ? ((preview-1)/4) : c.slot;
    const auto pose=preview ? static_cast<Pose>((preview-1)%4) : I.art.poseAt(c.sequence);
    auto* actor=ptr(I.life.actor);actorIdentity(I,actor,ptr(I.life.node),true);
    const auto before=local();bool changed=false;
    require(Binding::apply(g.base,actor,slot,pose,true,changed),"P8_RESOURCE_OR_BINDING");
    I.artScale=JohnScale;
    requireShared(local()==before,"P8_LOCAL_PLAYER_CHANGED");
    if(changed || I.liveSteps%60==0) {
        const auto text=resource(slot,pose,true);
        emit("p8_animation","\"slot\":%u,\"pose\":\"%s\",\"resource\":\"%s\",\"sequence\":%u,\"changed\":%s,\"frame\":%d,\"diagnostic_preview\":%s,\"p1_exact_unchanged\":true",I.slot,name(pose),text.data(),c.sequence,changed?"true":"false",read<short>(actor,0x178),preview?"true":"false");
    }
}
#endif
bool command(const ProxyCommand* c) { return c && c->abi==seam::Abi && c->slot<MaxSlots && std::isfinite(c->yaw) && std::abs(c->yaw)<=static_cast<float>(8*3.141592653589793); }
Vec3 position(const ProxyCommand& c) { return {c.x,c.y,static_cast<float>(static_cast<double>(c.z)+88.0)}; }
// Handle = (slot+1)<<48 | serial: it names exactly one Instance and one incarnation of it. Not an address.
constexpr unsigned HandleShift=48;
constexpr std::uint64_t HandleSerialMask=(1ull<<HandleShift)-1;
std::uint64_t makeHandle(Instance& I) { return (static_cast<std::uint64_t>(I.slot+1)<<HandleShift)|(++I.serial&HandleSerialMask); }
Instance* byHandle(std::uint64_t h) {
    const auto s=h>>HandleShift;
    if(!s || s>MaxSlots || !(h&HandleSerialMask)) return nullptr;
    auto& I=g.inst[s-1];
    return I.handle==h ? &I : nullptr;
}
Instance* byActor(const void* self) {
    const auto a=reinterpret_cast<std::uintptr_t>(self);
    if(!a) return nullptr;
    for(auto& I:g.inst) if(I.life.actor==a) return &I;
    return nullptr;
}
void tickEnter(std::uint64_t tick) {
    requireShared(thread() && !g.inTick,"TICK_AFFINITY_REENTRY");g.inTick=true;g.ticks=tick;++g.tickSerial;g.p1ThisTick=0;
}
std::int32_t worldReady(std::uint64_t) {
    try {
        scope();list(0);int flags=seam::StableWorld;
        if(g.p1ThisTick) {
            const auto p=local();if(!g.baseline) { g.local=p;g.baseline=true;emit("p1_baseline","\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\"",p.actor,p.node,p.owner); }
            checkLocal(p);flags|=seam::LocalTicked;if(!g.area) attestArea();
        }
#ifdef PSE_FEATURE_P8
        if(g.area && !g.sharedFault && !g.resourcesAttested) {
            using namespace pse::player_art;
            unsigned found=0;
            for(unsigned slot=0;slot<8;++slot) for(auto pose:{Pose::Idle,Pose::Walk,Pose::Shoot,Pose::Death})
                for(bool proxy:{false,true}) {
                    requireShared(Binding::lookup(g.base,slot,pose,proxy)!=nullptr,"P8_RESOURCE_GATE");++found;
                }
            g.resourcesAttested=found==64;
            emit("p8_resource_gate","\"validated_resources\":%u,\"distinct_resources\":16,\"archetypes\":2,\"actions\":0,\"external_transitions\":0",found);
        }
#endif
        // Healthy = the shared state is healthy. One faulted proxy never makes the others unhealthy.
        if(g.area && !g.sharedFault) flags|=seam::AdapterHealthy;
        for(const auto& I:g.inst) if(I.removeRequest && I.life.phase==Phase::Live) { flags|=seam::AdapterRequestsRemove;break; }
        return flags;
    } catch(const Error& e) {
        // No world yet is ordinary; once attested, a changed world is terminal in this P1 port.
        if(!soft(e) && g.area && !g.stopping) blockAll(e.reason);
        return 0;
    }
}
std::int32_t spawn(const ProxyCommand* c,std::uint64_t* out) {
    if(!g.spawnPermit.load(std::memory_order_acquire)) return 2;
    if(!command(c) || !out) return 3;
    auto& I=g.inst[c->slot];
    try {
        scope();require(g.p1ThisTick && !g.sharedFault && !I.faulted && !I.controllerStopped,"SPAWN_SCOPE");
        if(I.life.spawnAttempted) return 3;
        I.command=*c;const auto p=position(*c);if(const auto* why=target(I,p,false)) { if(++I.refused%60==1) emit("spawn_refused","\"slot\":%u,\"reason\":\"%s\",\"sequence\":%u",I.slot,why,c->sequence);return 3; }
        checkLocal(local());requireShared(prefix(profile::factory) && prefix(profile::remove),"NATIVE_CALL_PREFIX");
        auto* factory=g.base+0x5a10a0;requireShared(read<Byte*>(read<Byte*>(factory),0x18)==at(profile::factory),"FACTORY_VTABLE");
        snapshot(I,"BEFORE");I.expected=p;I.expectedYaw=0;
#ifdef PSE_FEATURE_P4
        I.expectedSector=I.plannedSector;
#endif
        require(I.life.beginSpawn(),"SPAWN_BUDGET");++I.spawnCalls;
#ifdef PSE_FEATURE_P4
        emit("p4_factory_placement","\"slot\":%u,\"sector\":%d,\"native_spawn_calls\":%u",I.slot,currentSector(I),I.spawnCalls);
#endif
#ifdef PSE_FEATURE_P4
        emit("native_factory_call","\"slot\":%u,\"actor_type\":1007,\"sequence\":%u,\"sector\":%d,\"target\":[%.9g,%.9g,%.9g]",I.slot,c->sequence,currentSector(I),p[0],p[1],p[2]);
#else
        emit("native_factory_call","\"slot\":%u,\"actor_type\":1007,\"sequence\":%u,\"sector\":259,\"target\":[%.9g,%.9g,%.9g]",I.slot,c->sequence,p[0],p[1],p[2]);
#endif
        using Factory=Byte*(__fastcall*)(void*,int,const float*,float,int);
        // Save the real return before any diagnostic access. Unknown/null exhausts the budget.
        Byte* a=reinterpret_cast<Factory>(at(profile::factory))(factory,1007,p.data(),0.0f,currentSector(I));
        I.life.actor=reinterpret_cast<std::uintptr_t>(a);
        emit("factory_return","\"slot\":%u,\"returned\":\"%p\",\"pointer_saved\":true,\"spawn_calls\":%u",I.slot,a,I.spawnCalls);
        require(a!=nullptr,"FACTORY_NULL_UNKNOWN");actorIdentity(I,a,nullptr,true);
        auto* n=read<Byte*>(a,0x10);require(I.life.acceptSpawn(reinterpret_cast<std::uintptr_t>(a),reinterpret_cast<std::uintptr_t>(n)),"SPAWN_IDENTITY_STATE");
        require(target(I,p,true)==nullptr,"SPAWN_CACHE_WITNESS");I.handle=makeHandle(I);*out=I.handle;
#ifdef PSE_FEATURE_P4
        require(I.buffer.push(*c,nowNs()),"P4_INITIAL_BUFFER_SAMPLE");
#endif
        emit("proxy_spawned","\"slot\":%u,\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"sequence\":%u,\"server_tick\":%u,\"target\":[%.9g,%.9g,%.9g],\"floor\":%.9g,\"ceiling\":%.9g,\"native_cpp\":true",I.slot,I.handle,a,n,c->sequence,c->serverTick,p[0],p[1],p[2],read<float>(a,0x11c),read<float>(a,0x120));return 1;
    } catch(const Error& e) { const bool attempted=I.life.spawnAttempted;fail(&I,e);return attempted?0:3; }
}
#ifdef PSE_FEATURE_P4
std::int32_t apply(Instance& I,std::uint64_t h,const ProxyCommand* c) {
#else
std::int32_t update(std::uint64_t h,const ProxyCommand* c) {
    auto& I=*byHandle(h);
#endif
    try {
        scope();if(!command(c) || c->slot!=I.slot) return 3;
        require(h==I.handle && I.life.phase==Phase::Live && !I.controllerStopped && !I.faulted,"UPDATE_IDENTITY_STATE");
        const auto p=position(*c);const float yaw=bounded::normalizeYaw(c->yaw);
        const bool moved=p!=I.expected,turned=!sameFloat(yaw,I.expectedYaw);
        if(!moved && !turned) { I.command=*c;++I.unchanged;return 2; }
        auto* a=ptr(I.life.actor);actorIdentity(I,a,ptr(I.life.node),true);
        if(moved) if(const auto* why=target(I,p,true)) { if(++I.refused%60==1) emit("update_refused","\"slot\":%u,\"reason\":\"%s\",\"sequence\":%u,\"target\":[%.9g,%.9g,%.9g]",I.slot,why,c->sequence,p[0],p[1],p[2]);return 3; }
        const auto before=local();const bool teleport=moved && (I.relocate || bounded::teleport(I.expected,p));
#ifdef PSE_FEATURE_P4
        if(moved && I.plannedSector!=I.expectedSector) {
            constexpr Byte linkPrefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48};
            requireShared(std::equal(std::begin(linkPrefix),std::end(linkPrefix),g.base+0x1d3d70),"P4_LINK_PREFIX");
            require(list(I.expectedSector,ptr(I.life.node)).found==1 && list(I.plannedSector,ptr(I.life.node)).found==0,"P4_LINK_BEFORE");
        }
#endif
        if(moved) {
            // Only the three accepted origin floats. No structure copy, refs, flags or links.
            for(std::size_t i=0;i<3;++i) store<float>(a,0x18+i*4,p[i]);
#ifdef PSE_FEATURE_P4
            if(I.plannedSector!=I.expectedSector) {
                // DOC58A native LinkSector, never manual sector/links/cache writes.
                const int from=I.expectedSector,to=I.plannedSector;
                auto* site=g.base+0x1d3d70;
                reinterpret_cast<void(__fastcall*)(void*,int)>(site)(a,to);I.expectedSector=to;
                require(list(from,ptr(I.life.node)).found==0 && list(to,ptr(I.life.node)).found==1,"P4_LINK_AFTER");
                emit("p4_portal_crossed","\"slot\":%u,\"from\":%d,\"to\":%d,\"native_lerp_preserved\":%s",I.slot,from,to,teleport?"false":"true");
            }
#endif
            if(I.relocate) {
                // Cache refresh with the engine's own floor/ceiling functions for the new sector (what a ticking actor would do).
                const float fl=height(profile::floor_height,p,I.expectedSector),ce=height(profile::ceiling_height,p,I.expectedSector);
                require(std::isfinite(fl) && std::isfinite(ce) && fl<ce,"RELOCATE_HEIGHTS");
                store<float>(a,0x11c,fl);store<float>(a,0x120,ce);
                emit("p4_relocated","\"slot\":%u,\"sector\":%d,\"floor\":%.9g,\"ceiling\":%.9g,\"target\":[%.9g,%.9g,%.9g]",I.slot,I.expectedSector,fl,ce,p[0],p[1],p[2]);
            }
            I.expected=p;if(teleport) reset(a+0x50);++I.revision;
        }
        if(turned) { store<float>(a,0x38,yaw);I.expectedYaw=yaw;reset(a+0x78);++I.revision; }
        actorIdentity(I,a,ptr(I.life.node),true);requireShared(before==local(),"P1_CHANGED_DURING_MUTATION");I.command=*c;++I.applied;
        emit("proxy_update","\"slot\":%u,\"handle\":%llu,\"sequence\":%u,\"server_tick\":%u,\"revision\":%u,\"engine_stamp\":%u,\"target\":[%.9g,%.9g,%.9g],\"actor_origin\":[%.9g,%.9g,%.9g],\"actor_yaw\":%.9g,\"mode\":\"%s\",\"p1_exact_unchanged\":true",I.slot,h,c->sequence,c->serverTick,I.revision,stamp(),p[0],p[1],p[2],read<float>(a,0x18),read<float>(a,0x1c),read<float>(a,0x20),read<float>(a,0x38),teleport?"teleport":"native_step");return 1;
    } catch(const Error& e) { if(soft(e)) return 3;I.controllerStopped=true;I.removeRequest=true;emit("native_proxy_controller_stopped","\"slot\":%u,\"reason\":\"%s\"",I.slot,e.reason);return 4; }
}
#ifdef PSE_FEATURE_P4
std::int32_t update(std::uint64_t h,const ProxyCommand* c) {
    auto* pI=byHandle(h);
    if(!pI || !command(c) || c->slot!=pI->slot) return 3; // unknown handle, or a handle of another slot's proxy
    auto& I=*pI;
    if(I.controllerStopped || g.stopping) return 4;
    if(!thread() || !g.inTick || I.faulted || I.life.phase!=Phase::Live) return 3;
    const auto received=nowNs();const bool queued=I.buffer.push(*c,received);
    emit("p4_snapshot_buffered","\"slot\":%u,\"sequence\":%u,\"server_tick\":%u,\"queued\":%s,\"depth\":%zu,\"delay_ms\":160,\"received_ns\":%llu,\"native_writes\":0",I.slot,c->sequence,c->serverTick,queued?"true":"false",I.buffer.size(),received);
    return queued ? 2 : 3; // Queued, no native mutation yet.
}
#endif
std::int32_t remove(std::uint64_t h) {
    auto* pI=byHandle(h);
    if(!pI) return 2;
    auto& I=*pI;
    if(I.life.phase!=Phase::Live || I.life.removeAttempted) return 2;
    try {
        scope();auto* a=ptr(I.life.actor);auto* n=ptr(I.life.node);actorIdentity(I,a,n,true);
        requireShared(prefix(profile::remove),"REMOVE_PREFIX");const auto before=local();
#ifdef PSE_FEATURE_P4
        I.buffer.clear();
#endif
        require(I.life.beginRemove(),"REMOVE_BUDGET");++I.removeCalls;I.removeStep=g.steps;I.controllerStopped=true;
        emit("native_remove_request","\"slot\":%u,\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"remove_calls\":%u,\"engine_stamp\":%u",I.slot,h,a,n,I.removeCalls,stamp());
        reinterpret_cast<void(__fastcall*)(void*)>(at(profile::remove))(a);
        requireShared(before==local(),"REMOVE_POSTCONDITION");
        require(read<Byte>(a,0x40)==1 && (read<unsigned>(a,0x16c)&0x2000),"REMOVE_POSTCONDITION");
        emit("native_remove_postcondition","\"slot\":%u,\"removed\":1,\"flags\":%u,\"refs\":%d,\"sector_occurrences\":%u,\"p1_exact_unchanged\":true,\"pending\":true",I.slot,read<unsigned>(a,0x16c),read<int>(n,0x40),list(currentSector(I),n).found);return 1;
    } catch(const Error& e) { const bool attempted=I.life.removeAttempted;fail(&I,e);return attempted?0:2; }
}
std::int32_t retirement(std::uint64_t h) {
    auto* pI=byHandle(h);
    if(!thread() || !pI) return 0;
    switch(pI->life.phase) { case Phase::Retired:return 1;case Phase::Pending:case Phase::Retiring:return 2;case Phase::Blocked:return 3;default:return 0; }
}
// A player's sprite under water. The engine draws actors of a water sector in its in-water pass, below the water surface and with the sector's
// light (enemies there look tinted); a moving actor's own tick sets the in-water bit 0x400 (0x19fda2) when it sinks below the surface of a water
// sector (sector +0x14 bit 0x100). The proxy never moves by itself, so it never got the bit, and it is fullbright: a player under water was drawn
// bright, on top of the water. In a water sector it is now lit and in water, elsewhere fullbright as before.
void water(Instance& I) {
    auto* a=ptr(I.life.actor);
    const bool wet=(read<std::uint16_t>(sector(currentSector(I)),0x14)&0x100u)!=0;
    const unsigned want=wet?0x400u:0x40u;
    if(read<unsigned>(a,0x16c)==want) return;
    store<unsigned>(a,0x16c,want);
    if(++I.waterSwitches<=40) emit("proxy_water","\"slot\":%u,\"in_water\":%s,\"sector\":%d,\"switches\":%u",I.slot,wet?"true":"false",currentSector(I),I.waterSwitches);
}
// One instance's per-simulation-step work. Instance faults are caught by the caller and stop only that instance.
void stepInstance(Instance& I,bool step) {
    if(step && I.life.phase==Phase::Live) {
#ifdef PSE_FEATURE_P8
        animate(I);
#endif
        water(I);
        actorIdentity(I,ptr(I.life.actor),ptr(I.life.node),true);++I.liveSteps;
        if(I.liveSteps==120) snapshot(I,"AFTER_SPAWN");
        if(I.liveSteps%1800==0) snapshot(I,"LIVE");
        if(I.liveSteps>=36000) I.removeRequest=true;
        if(I.liveSteps<=3 || I.liveSteps%60==0) emit("native_proxy_stable","\"slot\":%u,\"handle\":%llu,\"simulation_steps\":%u,\"actor_ticks\":%u,\"applied\":%llu,\"unchanged\":%llu,\"refused\":%llu,\"foreign_calls\":0",I.slot,I.handle,I.liveSteps,I.actorTicks,I.applied,I.unchanged,I.refused);
    } else if(I.life.phase==Phase::Pending && g.steps-I.removeStep>120) block(I,"RETIREMENT_DEADLINE");
    else if(step && I.life.phase==Phase::Retired) {
        if(!I.postSteps) snapshot(I,"AFTER_REMOVE");++I.postSteps;
        if(I.postSteps==60) snapshot(I,"POST_RETIREMENT");
        if(I.postSteps<=3 || I.postSteps%60==0) emit("post_retire","\"slot\":%u,\"post_retire_steps\":%u,\"all_weak_pointers_null\":%s,\"actor_reads\":false",I.slot,I.postSteps,(!I.life.actor && !I.life.node && !I.life.retireNode)?"true":"false");
    }
}
void tickLeave(std::uint64_t,std::uint32_t) {
    bool step=false;
    try {
        if(!thread() || !g.inTick || g.stopping || !g.baseline || !g.p1ThisTick || !stable()) { g.inTick=false;return; }
#ifdef PSE_FEATURE_P4
        for(auto& I:g.inst) if(I.life.phase==Phase::Live) {
            ProxyCommand c;const auto evaluated=nowNs();if(I.buffer.evaluate(evaluated,c)) {
                const auto result=apply(I,I.handle,&c);
                if(g.ticks%60==0) emit("p4_playout","\"slot\":%u,\"evaluated_ns\":%llu,\"cursor_tick\":%.9g,\"backlog_ticks\":%.9g,\"reanchors\":%u,\"discontinuities\":%u,\"sequence\":%u,\"result\":%d",I.slot,evaluated,I.buffer.cursorTick(),I.buffer.backlogTicks(),I.buffer.reanchors(),I.buffer.discontinuities(),c.sequence,result);
            }
        }
#endif
        checkLocal(local());const auto s=stamp();step=!g.hasStamp || s!=g.lastStamp;g.hasStamp=true;g.lastStamp=s;
    } catch(const Error& e) { if(!soft(e)) blockAll(e.reason);g.inTick=false;return; }
    for(auto& I:g.inst) {
        try { stepInstance(I,step); } catch(const Error& e) { fail(&I,e); }
    }
    g.inTick=false;
}
void worldExit(std::uint32_t reason) {
    require(thread(),"EXIT_AFFINITY");
#ifdef PSE_FEATURE_P8
    g.preview.store(0,std::memory_order_release);
#endif
    g.stopping=true;g.inTick=false;bool any=false;
    for(auto& I:g.inst) {
#ifdef PSE_FEATURE_P8
        I.art.reset();
#endif
#ifdef PSE_FEATURE_P4
        I.buffer.clear();
#endif
        const auto phase=I.life.phase;I.life.worldExit();
        if(phase!=Phase::Waiting) { any=true;emit("world_exit_observed","\"slot\":%u,\"reason\":%u,\"phase_before\":%u,\"all_weak_pointers_invalidated\":true,\"native_calls_after_barrier\":0,\"engine_retirement_claimed\":false",I.slot,reason,static_cast<unsigned>(phase)); }
    }
    if(!any) emit("world_exit_observed","\"slot\":-1,\"reason\":%u,\"phase_before\":%u,\"all_weak_pointers_invalidated\":true,\"native_calls_after_barrier\":0,\"engine_retirement_claimed\":false",reason,static_cast<unsigned>(Phase::Waiting));
}
// Guard only our observations; native original calls are outside this SEH boundary. Observers attribute instance faults themselves
// (fail(&I,e)); whatever reaches this boundary is unattributed and therefore shared.
void guardCpp(void(*fn)(void*),void* arg) { try { fn(arg); } catch(const Error& e) { if(!soft(e)) blockAll(e.reason); } catch(...) { blockAll("OBSERVER_CPP_EXCEPTION"); } }
void guard(void(*fn)(void*),void* arg=nullptr) {
    __try { guardCpp(fn,arg); } __except(EXCEPTION_EXECUTE_HANDLER) { blockAll("OBSERVER_SEH"); }
}
void observePuppet(void* self) {
    if(!thread() || !g.inTick || g.stopping) return;
    auto* n=read<Byte*>(player(),0xd8);if(!n || read<void*>(n,0x28)!=self) return;
    ++g.p1ThisTick;const auto s=stamp();if(!g.hasP1Stamp || s!=g.lastP1Stamp) { ++g.steps;g.lastP1Stamp=s;g.hasP1Stamp=true; }
}
std::uintptr_t __fastcall puppetTick(void* self) { guard(observePuppet,self);return reinterpret_cast<Unary>(g.original[profile::puppet_tick])(self); }

void observeActor(void* self) {
    auto* I=byActor(self);if(!I) return;
    try { require(thread() && g.inTick && !g.stopping,"ACTOR_TICK_AFFINITY");if(I->life.phase==Phase::Live) ++I->actorTicks; } catch(const Error& e) { fail(I,e); }
}
std::uintptr_t __fastcall actorTick(void* self) { guard(observeActor,self);return reinterpret_cast<Unary>(g.original[profile::actor_tick])(self); }
void onRemoveBefore(void* self) {
    if(auto* I=byActor(self)) {
        try { require(thread() && g.inTick && g.inSweep && I->life.phase==Phase::Pending,"ONREMOVE_SCOPE");emit("native_on_remove_entry","\"slot\":%u,\"handle\":%llu",I->slot,I->handle); } catch(const Error& e) { fail(I,e); }
    }
}
void onRemoveAfter(void* self) {
    if(auto* I=byActor(self)) {
        try { require(!read<Byte*>(self,0x10),"ONREMOVE_OWN_NODE_NOT_CLEARED");emit("native_on_remove_return","\"slot\":%u,\"own_node_cleared\":true",I->slot); } catch(const Error& e) { fail(I,e); }
    }
}
std::uintptr_t __fastcall onRemove(void* self) {
    guard(onRemoveBefore,self);const auto result=reinterpret_cast<Unary>(g.original[profile::on_remove])(self);guard(onRemoveAfter,self);return result;
}
void beforePool(void* self) {
    auto* I=byActor(self);if(!I) return;
    const bool controlled=thread() && g.inTick && g.inSweep && !g.stopping && I->life.phase==Phase::Pending && I->life.removeAttempted;
    bool facts=false;
    if(controlled) facts=!read<Byte*>(self,0x10) && read<int>(ptr(I->life.node),0x40)==0;
    // Invalidate BEFORE trampoline: no further Actor read, including on the return path.
    I->life.poolReturn(reinterpret_cast<std::uintptr_t>(self),controlled && facts);
    emit("native_pool_return_entry","\"slot\":%u,\"valid_scope\":%s,\"all_actor_weak_pointers_null\":true,\"actor_reads_after_entry\":false",I->slot,controlled&&facts?"true":"false");
    if(!controlled || !facts) block(*I,"POOL_RETURN_OUTSIDE_CONTROLLED_SWEEP");
}
bool keepPoolNumber=false; // test only: PSE_DEBUG_KEEP_POOL_ID=1 leaves the stale number, to reproduce the engine's behaviour (tools/smoke_r10.py)
// Is this pooled object a kexSlaveActor? The engine's own test (as in SpawnActor 0x1d4a37): the object's class info (vtable[0]) and its
// parents (+0x20) are compared by class number (+8) with kexSlaveActor's (class info 0xa432d8). The sweep returns EVERY kexSlaveObject to
// the pool, also movers that are much smaller than an Actor (a door is 0xf0 bytes, a lift 0xf8, an Actor 0x1b8): a write at +0x180 of
// those is a write past the end of their heap block (the first version of this fix did exactly that: heap corruption, a crash in ENet).
bool pooledActor(void* self) noexcept {
    __try {
        const int actorClass=read<int>(g.base,0xa432d8+8);
        auto* info=reinterpret_cast<Byte*(__fastcall*)(void*)>(read<void**>(self)[0])(self);
        for(unsigned depth=0;info && depth<16;++depth,info=read<Byte*>(info,0x20)) if(read<int>(info,8)==actorClass) return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
std::uintptr_t __fastcall poolReturn(void* factory,void* self) {
    // The engine keeps removed objects in a per-class queue (this function, called only by the end-of-tick sweep 0x1b6385) and hands
    // Actors out again in SpawnActor 0x1d4780 / 0x1d4b30 / 0x1d50b0 WITHOUT running the constructor: +0x180 (the map actor number, -1
    // for anything spawned during play) kept the number of the dead map actor. A vase scorpion that got the object of dead map enemy 62
    // was "map enemy 62" again: the host sent it as a map enemy the client no longer had (invisible enemy), the client kept its own such
    // scorpion as a map actor (a phantom without AI), a puff with a vase's number made the host report that vase "gone" over and over
    // (friend's run 2026-11-12). The number is the map's, not the object's: cleared here, so everything spawned during play has -1
    // again, as after the constructor (0x19f1ac). Actors only (see pooledActor).
    if(self && !keepPoolNumber && pooledActor(self)) *reinterpret_cast<int*>(static_cast<Byte*>(self)+0x180)=-1;
    guard(beforePool,self);return reinterpret_cast<Binary>(g.original[profile::pool_return])(factory,self);
}
void beginSweep(void*) { if(thread() && g.inTick && !g.stopping) { requireShared(!g.inSweep,"NESTED_SWEEP");g.inSweep=true; } }
void endSweep(void*) {
    if(!thread() || !g.inSweep) return;
    for(auto& I:g.inst) if(I.life.phase==Phase::Retiring && I.life.retireNode) {
        try {
            auto* n=ptr(I.life.retireNode);const bool empty=!read<Byte*>(n,0x28);const int refs=read<int>(n,0x40),inUse=read<Byte>(n,0x24);const auto occurrences=list(currentSector(I),n).found;
            const bool ok=I.life.finishRetirement(empty,refs,inUse,static_cast<int>(occurrences));
            emit("deferred_retired","\"slot\":%u,\"success\":%s,\"payload_null\":%s,\"refs\":%d,\"in_use\":%d,\"sector_occurrences\":%u,\"all_weak_pointers_null\":%s,\"actor_reads\":false",I.slot,ok?"true":"false",empty?"true":"false",refs,inUse,occurrences,(!I.life.actor&&!I.life.node&&!I.life.retireNode)?"true":"false");
            require(ok,"NODE_RETIREMENT_POSTCONDITION");
        } catch(const Error& e) { fail(&I,e); }
    }
    g.inSweep=false;
}
std::uintptr_t __fastcall sweep() { guard(beginSweep);const auto result=reinterpret_cast<NoArgs>(g.original[profile::sweep])();guard(endSweep);g.inSweep=false;return result; }
void drainBefore(void*) {
    if(thread() && g.inTick && !g.stopping) for(auto& I:g.inst) if(I.life.phase==Phase::Pending && I.life.node)
        emit("deferred_drain_entry","\"slot\":%u,\"refs\":%d,\"sector_occurrences\":%u",I.slot,read<int>(ptr(I.life.node),0x40),list(currentSector(I),ptr(I.life.node)).found);
}
void drainAfter(void*) {
    if(thread() && g.inTick && !g.stopping) for(auto& I:g.inst) if(I.life.phase==Phase::Pending && I.life.node)
        emit("deferred_drain_return","\"slot\":%u,\"refs\":%d,\"sector_occurrences\":%u",I.slot,read<int>(ptr(I.life.node),0x40),list(currentSector(I),ptr(I.life.node)).found);
}
std::uintptr_t __fastcall drain(void* self) { guard(drainBefore);const auto result=reinterpret_cast<Unary>(g.original[profile::drain])(self);guard(drainAfter);return result; }
constexpr seam::SeamTable Table{seam::Abi,sizeof(seam::SeamTable),tickEnter,worldReady,spawn,update,remove,retirement,tickLeave,worldExit};
DWORD observeImpl(unsigned slot,Observation* out,std::uint32_t size) {
    if(!out || size!=sizeof(Observation) || slot>=MaxSlots) return ERROR_INVALID_PARAMETER;
    *out={};out->size=sizeof(Observation);out->abi=0x50310001;
    out->simulationTid=g.tid.load(std::memory_order_acquire);out->observedTid=GetCurrentThreadId();
    // Reject foreign callers before reading any simulation-owned mutable state.
    if(!out->simulationTid || out->observedTid!=out->simulationTid) return ERROR_INVALID_STATE;
    auto& I=g.inst[slot];
    try {
        out->phase=static_cast<unsigned>(I.life.phase);out->handle=I.handle;
        out->faulted=I.faulted?1u:0u;out->removeAttempted=I.life.removeAttempted?1u:0u;
        if(!g.installed || g.stopping || I.faulted || I.controllerStopped || I.life.phase!=Phase::Live) return ERROR_INVALID_STATE;
        if(!stable()) return ERROR_INVALID_STATE;
        auto* a=ptr(I.life.actor);actorIdentity(I,a,ptr(I.life.node),true);
        auto* anim=read<Byte*>(a,0x108);const auto id=read<std::int16_t>(a,0x178);
        require(anim && id>=0 && static_cast<unsigned>(id)<read<unsigned>(anim,0x24),"FRAME_BOUNDS");
        auto* frames=read<Byte*>(anim,0x18);require(frames!=nullptr,"FRAME_NULL");
        out->frameToken=reinterpret_cast<std::uint64_t>(frames+static_cast<std::size_t>(id)*0x110);
        out->sequence=I.command.sequence;out->serverTick=I.command.serverTick;out->revision=I.revision;out->engineStamp=stamp();
        out->origin=read<Vec3>(a,0x18);out->render=read<Vec3>(a,0x6c);out->angles=read<Vec3>(a,0x94);out->previous=read<Vec3>(a,0x60);
        out->yaw=read<float>(a,0x38);out->scale=read<float>(a,0x124);out->sector=read<int>(a,0x3c);out->flags=read<unsigned>(a,0x16c);
        out->identityValid=1;return ERROR_SUCCESS;
    } catch(const Error& e) { fail(&I,e);return ERROR_INVALID_STATE; }
}
DWORD observeCpp(unsigned slot,Observation* out,std::uint32_t size) { try { return observeImpl(slot,out,size); } catch(...) { blockAll("OBSERVATION_EXCEPTION");return ERROR_INVALID_STATE; } }
// The lowest-slot proxy that is Live, else the lowest that ever left Waiting, else slot 0.
unsigned observedSlot() {
    for(unsigned s=0;s<MaxSlots;++s) if(g.inst[s].life.phase==Phase::Live) return s;
    for(unsigned s=0;s<MaxSlots;++s) if(g.inst[s].life.phase!=Phase::Waiting) return s;
    return 0;
}
// Fresh incarnation of one proxy. Keeps slot and handle serial; the caller has checked the preconditions.
void resetInstance(Instance& I) { const auto slot=I.slot;const auto serial=I.serial;I=Instance{};I.slot=slot;I.serial=serial; }
bool quiet(const Instance& I) { return I.life.phase==Phase::Retired || I.life.phase==Phase::Waiting; }
}
bool install(Byte* image,DWORD simulationTid,Journal journal,unsigned /*remoteSlot (legacy, ignored)*/,bool hostMode) {
    if(g.installed || !image || GetCurrentThreadId()!=simulationTid) return false;
    g.base=image;g.tid=simulationTid;g.journal=journal;g.hostMode=hostMode;
    { char v[4]{};keepPoolNumber=GetEnvironmentVariableA("PSE_DEBUG_KEEP_POOL_ID",v,sizeof(v))>0 && v[0]=='1'; }
    for(std::size_t i=0;i<profile::Count;++i) { if(hostMode && i==profile::puppet_tick) continue; /* already detoured by the host DLL */ if(!prefix(static_cast<profile::Index>(i))) { blockAll("NATIVE_PROXY_PREFIX");return false; } }
    std::vector<std::pair<profile::Index,void*>> hooks={
        {profile::actor_tick,reinterpret_cast<void*>(actorTick)},
        {profile::on_remove,reinterpret_cast<void*>(onRemove)},{profile::pool_return,reinterpret_cast<void*>(poolReturn)},
        {profile::sweep,reinterpret_cast<void*>(sweep)},{profile::drain,reinterpret_cast<void*>(drain)}};
    // On the host the DLL owns the puppet-tick detour and forwards each call to notePuppetTick().
    if(!hostMode) hooks.insert(hooks.begin(),{profile::puppet_tick,reinterpret_cast<void*>(puppetTick)});
    for(const auto& [i,hook]:hooks) {
        if(MH_CreateHook(at(i),hook,&g.original[i])!=MH_OK || MH_EnableHook(at(i))!=MH_OK) { blockAll("NATIVE_PROXY_HOOK_INSTALL");g.stopping=true;detach();return false; }
        g.enabled[i]=true;
    }
    g.installed=true;emit("native_proxy_installed","\"hooks\":6,\"capacity\":%u,\"sector\":259,\"new_injector\":false,\"external_callbacks\":false,\"module_pinned\":true",MaxSlots);return true;
}
unsigned char* actorOf(unsigned slot) {
    if(slot>=8 || !g.installed || g.stopping || g.sharedFault || !thread()) return nullptr;
    const auto& I=g.inst[slot];
    return I.life.phase==Phase::Live && !I.faulted && I.life.actor ? ptr(I.life.actor) : nullptr;
}
const seam::SeamTable& table() { return Table; }
void notePuppetTick(void* self) { guard(observePuppet,self); }
void permitSpawn() { g.spawnPermit.store(1,std::memory_order_release); }
// New incarnation of all proxies after a world transition (level restart / checkpoint reload: the engine purges the old
// proxy Actors itself) or after every previous Actor was fully retired in the same world. Only drops weak identities and
// per-world caches; hooks stay installed and no engine call is made. Refused while ANY Actor may still be alive.
void acceptLocalRespawn() { if(thread()) localRespawned=true; }
unsigned blockedRearms[MaxSlots]{}; // per world: how many times a slot came back after its own fault (rearmSlot)
bool rearm() {
    if(!g.installed || !thread()) return false;
    for(const auto& I:g.inst) if(!(g.stopping || quiet(I))) return false;
    unsigned previous[MaxSlots]{};for(unsigned s=0;s<MaxSlots;++s) previous[s]=static_cast<unsigned>(g.inst[s].life.phase);
    for(auto& I:g.inst) resetInstance(I);
    for(auto& n:blockedRearms) n=0;
    g.stopping=g.inTick=g.inSweep=g.sharedFault=g.baseline=g.area=false;
    g.p1ThisTick=0;g.steps=0;
    g.hasStamp=g.hasP1Stamp=false;g.local={};g.sectors.clear();
#ifdef PSE_FEATURE_P4
    g.geometryFresh=false;
#endif
#ifdef PSE_FEATURE_P8
    g.preview.store(0,std::memory_order_release);
#endif
    emit("native_proxy_rearmed","\"slot\":-1,\"previous_phase\":%u,\"previous_phases\":[%u,%u,%u,%u,%u,%u,%u,%u]",previous[0],previous[0],previous[1],previous[2],previous[3],previous[4],previous[5],previous[6],previous[7]);
    return true;
}
// One client rejoined in the same world: re-arm only its proxy (Lifetime Retired/Waiting, or the world barrier latched).
// Other proxies, the local-player baseline and the sector geometry are untouched.
bool rearmSlot(unsigned slot) {
    if(!g.installed || !thread() || slot>=MaxSlots) return false;
    auto& I=g.inst[slot];
    // A slot that faulted on its own (Blocked: every weak pointer of it is already null, no native call is owed) may start a fresh proxy:
    // otherwise that player stayed invisible until the next level. Not after a shared fault, at most 8 times per slot and world.
    const bool blockedSlot=I.life.phase==Phase::Blocked && !g.sharedFault && !I.life.actor && !I.life.node && blockedRearms[slot]<8;
    if(!(g.stopping || quiet(I) || blockedSlot)) return false;
    if(blockedSlot && !g.stopping && !quiet(I)) ++blockedRearms[slot];
    const auto phase=I.life.phase;
    resetInstance(I);
    emit("native_proxy_rearmed","\"slot\":%u,\"previous_phase\":%u",slot,static_cast<unsigned>(phase));
    return true;
}
std::uint32_t removeRequestMask() {
    std::uint32_t mask=0;
    if(thread()) for(unsigned s=0;s<MaxSlots;++s) if(g.inst[s].removeRequest && g.inst[s].life.phase==Phase::Live) mask|=1u<<s;
    return mask;
}
std::uint32_t faultedMask() {
    std::uint32_t mask=0;
    if(thread()) for(unsigned s=0;s<MaxSlots;++s) if(g.inst[s].faulted) mask|=1u<<s;
    return mask;
}
#ifdef PSE_FEATURE_P8
void stageAnimation(const pse::BridgePlayerSnapshot& s) {
    if(!thread() || !g.inTick || g.stopping || g.sharedFault || s.slotId>=MaxSlots) return;
    auto& I=g.inst[s.slotId];
    if(I.faulted || I.controllerStopped) return;
    if(!I.art.accept(s)) block(I,"P8_INVALID_ANIMATION_SNAPSHOT");
}
DWORD previewAnimation(unsigned slot,unsigned pose) {
    if(slot>8 || pose>3) return ERROR_INVALID_PARAMETER;
    g.preview.store(slot==8?0:1+slot*4+pose,std::memory_order_release);
    return ERROR_SUCCESS;
}
#endif
bool detach() {
    if(!thread()) return false;
    // Live/unknown identity can only be abandoned after the already latched engine teardown barrier.
    if(!g.stopping) for(const auto& I:g.inst) if(!quiet(I)) return false;
    bool ok=true;
    for(std::size_t n=0;n<profile::Count;++n) if(g.enabled[n]) {
        const auto i=static_cast<profile::Index>(n);const auto result=MH_DisableHook(at(i));const bool exact=prefix(i);ok&=result==MH_OK && exact;
        if(result==MH_OK) g.enabled[n]=false;
        emit("native_proxy_hook_restored","\"site\":\"%s\",\"rva\":%u,\"status\":%d,\"exact16\":%s",profile::Sites[n].name,profile::Sites[n].rva,result,exact?"true":"false");
    }
    if(!ok) blockAll("NATIVE_PROXY_HOOK_RESTORE");return ok;
}
void fault() { g.inTick=false;blockAll("SEAM_UNKNOWN_NATIVE_OUTCOME"); }
void exitBarrier(std::uint32_t reason) { if(thread() && !g.stopping) worldExit(reason); }
bool isPositionInterpolant(const void* candidate) {
    if(!thread() || !g.installed || g.stopping || g.sharedFault) return false;
    const auto c=reinterpret_cast<std::uintptr_t>(candidate);
    for(const auto& I:g.inst) if(!I.faulted && !I.controllerStopped && I.life.phase==Phase::Live && I.life.actor && c==I.life.actor+0x50) return true;
    return false;
}
DWORD observeSlot(unsigned slot,Observation* out,std::uint32_t size) {
    __try { return observeCpp(slot,out,size); } __except(EXCEPTION_EXECUTE_HANDLER) { blockAll("OBSERVATION_SEH");return ERROR_INVALID_STATE; }
}
DWORD observe(Observation* out,std::uint32_t size) {
    return observeSlot(thread() ? observedSlot() : 0,out,size);
}
}
