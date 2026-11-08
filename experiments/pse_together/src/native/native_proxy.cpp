// P1 exact-build port of DOC-58B adapter58b.js + DOC-58A controller/one_actor.
// Native ABIs and offsets come from accepted DOC-57/58A evidence, not decompiler types.
// Single Actor1007, sector259, +88 presentation offset. No reconnect, LinkSector or teardown rearm.
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
struct Error { const char* reason; };
void require(bool ok,const char* reason) { if(!ok) throw Error{reason}; }
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
struct State {
    Byte* base{};std::atomic<DWORD> tid{};Journal journal{};
    void* original[profile::Count]{};bool enabled[profile::Count]{};
    std::atomic<unsigned> spawnPermit{};
    bounded::Lifetime life;
    bool installed{},inTick{},inSweep{},stopping{},faulted{},removeRequest{},baseline{},area{},controllerStopped{};
    unsigned p1ThisTick{},spawnCalls{},removeCalls{},revision{},actorTicks{},liveSteps{},postSteps{};
    std::uint64_t ticks{},steps{},removeStep{},handle{},applied{},unchanged{},refused{};
    std::uint32_t lastStamp{},lastP1Stamp{};bool hasStamp{},hasP1Stamp{};
    Vec3 expected{};float expectedYaw{},floor{},ceiling{};
    ProxyCommand command{};
    Local local{};
#ifdef PSE_FEATURE_P8
    pse::player_art::Animation art;
    std::atomic<unsigned> preview{0}; // 0=authoritative DTO, otherwise slot+pose encoded
    bool resourcesAttested{};
    float artScale{1.5f};
#endif
    std::vector<Sector> sectors;
#ifdef PSE_FEATURE_P4
    int expectedSector{259},plannedSector{259};bool relocate{};pse::presentation::Buffer buffer;
    std::uint64_t geometryRefreshes{},geometryMaxNs{};
#endif
} g;
void emit(const char* event,const char* fmt="",...) {
    if(!g.journal) return;
    char detail[2100]{};va_list args;va_start(args,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args);va_end(args);
    g.journal(event,detail);
}
#ifdef PSE_FEATURE_P4
int currentSector() {
    return g.expectedSector;
}
#else
#define currentSector() 259
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
void scope() { require(thread() && g.inTick && !g.stopping,"SIMULATION_SCOPE");require(stable(),"WORLD_NOT_STABLE"); }
constexpr std::size_t FieldOffsets[]={0x84,0x86,0x90,0x94,0x98,0x9a,0xa4,0x88,0x8c,0xcc,0xbc,0x17a};
constexpr std::size_t FieldSizes[]={2,2,4,4,2,2,4,4,4,8,16,2};
static_assert(2+2+4+4+2+2+4+4+4+8+16+2==54);
Local local() {
    Local p;p.node=read<Byte*>(player(),0xd8);require(p.node!=nullptr,"P1_NODE_NULL");
    p.actor=read<Byte*>(p.node,0x28);require(p.actor!=nullptr,"P1_ACTOR_NULL");
    p.ownNode=read<Byte*>(p.actor,0x10);p.owner=read<Byte*>(p.actor,0x1b8);p.vtable=read<Byte*>(p.actor);
    require(p.node==p.ownNode && p.owner==player() && p.vtable==g.base+0x52d198,"P1_GRAPH");
    p.position=read<Vec3>(p.actor,0x18);p.render=read<Vec3>(p.actor,0x6c);p.angles=read<Vec3>(p.actor,0x94);
    p.yaw=read<float>(p.actor,0x38);p.pitch=read<float>(p.actor,0x30);p.sector=read<int>(p.actor,0x3c);
    p.input=read<std::array<Byte,128>>(player());
    std::size_t out=0;
    for(std::size_t f=0;f<12;++f) for(std::size_t b=0;b<FieldSizes[f];++b)
        p.fields[out++]=read<Byte>(f==11?p.actor:player(),FieldOffsets[f]+b);
    return p;
}
void checkLocal(const Local& p) {
    require(g.baseline && p.sameGraph(g.local),"P1_OWNERSHIP_CHANGED");
    require(p.fields==g.local.fields,"P1_INVENTORY_CHANGED");
}
void snapshot(const char* label) {
    const auto p=local();checkLocal(p);
    char hex[109]{};for(std::size_t i=0;i<p.fields.size();++i) sprintf_s(hex+2*i,3,"%02x",p.fields[i]);
    emit("p1_state_snapshot","\"label\":\"%s\",\"fields_raw\":\"%s\",\"fields\":12,\"equal\":true,\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\",\"pos\":[%.9g,%.9g,%.9g],\"engine_stamp\":%u",label,hex,p.actor,p.node,p.owner,p.position[0],p.position[1],p.position[2],stamp());
}
Byte* sector(int index) {
    const auto count=read<std::uint64_t>(g.base,0xa45970);auto* start=read<Byte*>(g.base,0xa45968);
    require(start && index>=0 && count<=100000 && static_cast<std::uint64_t>(index)<count,"SECTOR_BOUNDS");
    return start+static_cast<std::size_t>(index)*0x88;
}
struct List { unsigned count{},found{},localFound{}; };
List list(int index,Byte* own=nullptr,const Local* p=nullptr) {
    Byte* head=read<Byte*>(sector(index),0x78);require(head && !read<Byte*>(head),"SECTOR_HEAD");
    Byte* previous=head;Byte* n=read<Byte*>(head,8);List result;
    while(n && read<Byte*>(n,8)) {
        require(result.count<4096 && read<Byte*>(n)==previous && read<Byte*>(previous,8)==n,"SECTOR_LINKS");
        ++result.count;if(n==own) ++result.found;
        if(p && n==p->node && read<Byte*>(n,0x28)==p->actor) ++result.localFound;
        previous=n;n=read<Byte*>(n,8);
    }
    require(n && read<Byte*>(n)==previous,"SECTOR_TAIL");return result;
}
#ifdef PSE_FEATURE_P4
void geometry(int index,Sector& out) {
#else
Sector geometry(int index) {
#endif
    Byte* s=sector(index);require(read<int>(s,0x80)==index,"SECTOR_SELF_INDEX");
    const auto first=read<std::uint16_t>(s),last=read<std::uint16_t>(s,2);
    const auto count=read<std::uint64_t>(g.base,0xa45988);auto* faces=read<Byte*>(g.base,0xa45980);
    require(faces && first<=last && static_cast<std::uint64_t>(last)+2<count && last-first<=512,"FACE_BOUNDS");
#ifdef PSE_FEATURE_P4
    out.planes.clear();out.portal.faces.clear(); // Reuse attested capacities; reread mutable planes.
    out.portal.flags=read<std::uint16_t>(s,0x14);
#else
    Sector out;
#endif
    for(unsigned i=first;i<=static_cast<unsigned>(last)+2;++i) {
        auto* f=faces+i*0x108;const auto n=read<Vec3>(f,0xc);const double d=read<float>(f,0x18);
        require(bounded::finite(n) && std::isfinite(d),"NONFINITE_PLANE");out.planes.push_back({n[0],n[1],n[2],d});
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
    // Every sector of the loaded map (any level): the proxy is placed in whichever sector the host actually stands in.
    const auto count=read<std::uint64_t>(g.base,0xa45970);require(count>0 && count<=4096,"SECTOR_COUNT");
    g.sectors.clear();for(int i=0;i<static_cast<int>(count);++i) g.sectors.push_back(geometry(i));
    g.area=true;
    emit("attested_area","\"status\":\"ATTESTED\",\"sector_count\":%llu,\"generic\":true,\"z_offset\":88,\"native_cpp\":true",static_cast<unsigned long long>(count));
}
bool interpolation(Byte* a) {
    return read<Byte*>(a,0x58)==a+0x18 && read<Byte*>(a,0x80)==a+0x30 &&
        read<Byte*>(a,0x50)==g.base+0x52bdf0 && read<Byte*>(a,0x78)==g.base+0x52be68 &&
        bounded::finite(read<Vec3>(a,0x60)) && bounded::finite(read<Vec3>(a,0x6c)) &&
        bounded::finite(read<Vec3>(a,0x88)) && bounded::finite(read<Vec3>(a,0x94));
}
void actorIdentity(Byte* a,Byte* expectedNode,bool strict) {
    require(a && a!=g.local.actor,"ACTOR_ALIAS");auto* n=read<Byte*>(a,0x10);
    require(n && n!=g.local.node && (!expectedNode || n==expectedNode),"ACTOR_NODE");
    require(read<Byte*>(a)==g.base+0x528608 && read<Byte*>(n,0x28)==a && read<Byte>(n,0x24)==1,"ACTOR_IDENTITY");
    auto* definition=read<Byte*>(a,0x170);
    require(definition && read<int>(definition)==1007 && read<int>(a,0x3c)==currentSector(),"ACTOR_TYPE_SECTOR");
    require(read<Vec3>(a,0x18)==g.expected && sameFloat(read<float>(a,0x38),g.expectedYaw) && !read<Byte>(a,0x40),"ACTOR_TRANSFORM_REMOVED");
    const auto p=local();checkLocal(p);require(list(p.sector,nullptr,&p).localFound==1 && list(currentSector(),n).found==1,"ACTOR_MEMBERSHIP");
    require(interpolation(a),"ACTOR_INTERPOLANTS");
    if(strict) require(read<unsigned>(a,0x16c)==0x40 && read<int>(n,0x40)==0 && read<float>(a,0x124)==
#ifdef PSE_FEATURE_P8
        g.artScale &&
#else
        1.5f &&
#endif
        !read<Byte*>(a,0x28) && !read<Byte*>(a,0x148) && read<Vec3>(a,0xec)==Vec3{-12,-12,-24} &&
        read<Vec3>(a,0xf8)==Vec3{12,12,24},"ACTOR_PASSIVE_PROPERTIES");
}
#ifdef PSE_FEATURE_P4
const char* target(const Vec3& p,bool withActor) {
    if(!bounded::finite(p)||!g.area) return "P4_TARGET_UNATTESTED";
    // Refresh numeric geometry: doors/movers must not leave a stale plane proof.
    const auto began=nowNs();
    for(std::size_t i=0;i<g.sectors.size();++i) geometry(static_cast<int>(i),g.sectors[i]);
    const auto duration=nowNs()-began;g.geometryMaxNs=std::max(g.geometryMaxNs,duration);++g.geometryRefreshes;
    if(g.geometryRefreshes<=3 || g.geometryRefreshes%60==0)emit("p4_geometry_refresh","\"sectors\":%zu,\"refreshes\":%llu,\"duration_ns\":%llu,\"max_ns\":%llu,\"capacity_reused\":true",g.sectors.size(),g.geometryRefreshes,duration,g.geometryMaxNs);
    int found=-1;unsigned hits=0;
    for(std::size_t i=0;i<g.sectors.size();++i) if(bounded::inside(g.sectors[i].planes,p)) { found=static_cast<int>(i);++hits; }
    // Before the first spawn there is no current sector: a point on a shared boundary is retried on a later sample.
    const bool haveCurrent=g.life.actor!=0 && static_cast<std::size_t>(currentSector())<g.sectors.size();
    if(!hits || (hits>1 && !(haveCurrent && bounded::inside(g.sectors[static_cast<std::size_t>(currentSector())].planes,p)))) return "P4_SECTOR_AMBIGUOUS";
    if(hits>1) found=currentSector(); // Retain exact boundary membership until the center crosses.
    if(!point(p,found)) return "P4_NATIVE_POINT_FALSE";
    const auto& dst=g.sectors[static_cast<std::size_t>(found)];
    g.relocate=false;
    if(withActor) {
        const auto& src=g.sectors[static_cast<std::size_t>(currentSector())];
        if(found!=currentSector()) {
            // No verified portal corridor (long jump, non-adjacent sector, coalesced snapshots): relocate with a native
            // LinkSector + interpolant reset instead of refusing, so the proxy always follows the host's real sector.
            if(!pse::portal::corridor(src.portal,currentSector(),dst.portal,found,g.expected,p)) g.relocate=true;
        } else if(!bounded::fits(dst.planes,p)) {
            bool aperture=false;
            for(const auto& f:dst.portal.faces) if(f.portal && f.adjoin>=0 && static_cast<std::size_t>(f.adjoin)<g.sectors.size())
                aperture|=pse::portal::sweptAperture(dst.portal,found,g.sectors[static_cast<std::size_t>(f.adjoin)].portal,f.adjoin,g.expected,p);
            (void)aperture; // a proxy partly inside a wall is cosmetic only; the centre stays inside the sector
        }
        auto* a=ptr(g.life.actor);
        // Different floor/ceiling at the target: the engine's own height functions refresh the actor caches after the move.
        if(!sameFloat(height(profile::floor_height,p,found),read<float>(a,0x11c)) ||
           !sameFloat(height(profile::ceiling_height,p,found),read<float>(a,0x120))) g.relocate=true;
        if(read<unsigned>(a,0x16c)&0x80) return "PHYSICS_BIT7_SET";
    } // spawn: the centre is inside the sector (checked above) and native point-in-sector agrees; the box may touch a wall
    g.plannedSector=found;return nullptr;
}
#else
const char* target(const Vec3& p,bool withActor) {
    if(!bounded::finite(p)) return "NONFINITE_TARGET";
    if(!g.area) return "AREA_NOT_ATTESTED";
    if(!bounded::fits(g.sectors[259].planes,p)) return "OUT_OF_ATTESTED_AREA";
    if(!point(p)) return "NATIVE_POINT_IN_SECTOR_FALSE";
    if(!unique(p)) return "TARGET_NOT_UNIQUE";
    const float floor=height(profile::floor_height,p),ceiling=height(profile::ceiling_height,p);
    if(withActor) {
        auto* a=ptr(g.life.actor);
        if(!sameFloat(floor,read<float>(a,0x11c)) || !sameFloat(ceiling,read<float>(a,0x120))) return "FLOOR_CEILING_CACHE_MISMATCH";
        if(read<unsigned>(a,0x16c)&0x80) return "PHYSICS_BIT7_SET";
    } else if(!sameFloat(floor,g.floor) || !sameFloat(ceiling,g.ceiling)) return "FLOOR_CEILING_CACHE_MISMATCH";
    return nullptr;
}
#endif
void block(const char* reason) { g.faulted=true;g.controllerStopped=true;g.life.block();emit("native_proxy_fault","\"reason\":\"%s\",\"all_weak_pointers_null\":true,\"retry\":false",reason); }
void reset(Byte* p) { reinterpret_cast<void(__fastcall*)(void*)>(at(profile::interpolant_reset))(p); }
#ifdef PSE_FEATURE_P8
void animate() {
    using namespace pse::player_art;
    scope();require(g.life.phase==Phase::Live && !g.faulted && !g.controllerStopped,"P8_LIVE_SCOPE");
    const auto& c=g.command;
    // The bounded movement seam may refuse an out-of-area target while keeping
    // the last valid transform. A newer staged pose must then wait as well.
    if(!g.art.matches(c.slot,c.sequence,c.mapEpoch,c.connectionGeneration,c.entityId,c.entityGeneration)) return;
    const auto preview=g.preview.load(std::memory_order_acquire);
    const auto slot=preview ? ((preview-1)/4) : c.slot;
    const auto pose=preview ? static_cast<Pose>((preview-1)%4) : g.art.pose();
    auto* actor=ptr(g.life.actor);actorIdentity(actor,ptr(g.life.node),true);
    const auto before=local();bool changed=false;
    require(Binding::apply(g.base,actor,slot,pose,true,changed),"P8_RESOURCE_OR_BINDING");
    g.artScale=JohnScale;
    require(local()==before,"P8_LOCAL_PLAYER_CHANGED");
    if(changed || g.liveSteps%60==0) {
        const auto text=resource(slot,pose,true);
        emit("p8_animation","\"slot\":%u,\"pose\":\"%s\",\"resource\":\"%s\",\"sequence\":%u,\"changed\":%s,\"frame\":%d,\"diagnostic_preview\":%s,\"p1_exact_unchanged\":true",slot,name(pose),text.data(),c.sequence,changed?"true":"false",read<short>(actor,0x178),preview?"true":"false");
    }
}
#endif
bool command(const ProxyCommand* c) { return c && c->abi==seam::Abi && c->slot==0 && std::isfinite(c->yaw) && std::abs(c->yaw)<=static_cast<float>(8*3.141592653589793); }
Vec3 position(const ProxyCommand& c) { return {c.x,c.y,static_cast<float>(static_cast<double>(c.z)+88.0)}; }
void tickEnter(std::uint64_t tick) {
    require(thread() && !g.inTick,"TICK_AFFINITY_REENTRY");g.inTick=true;g.ticks=tick;g.p1ThisTick=0;
}
std::int32_t worldReady(std::uint64_t) {
    try {
        scope();list(0);int flags=seam::StableWorld;
        if(g.p1ThisTick) {
            const auto p=local();if(!g.baseline) { g.local=p;g.baseline=true;emit("p1_baseline","\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\"",p.actor,p.node,p.owner); }
            checkLocal(p);flags|=seam::LocalTicked;if(!g.area) attestArea();
        }
#ifdef PSE_FEATURE_P8
        if(g.area && !g.faulted && !g.resourcesAttested) {
            using namespace pse::player_art;
            unsigned found=0;
            for(unsigned slot=0;slot<8;++slot) for(auto pose:{Pose::Idle,Pose::Walk,Pose::Shoot,Pose::Death})
                for(bool proxy:{false,true}) {
                    require(Binding::lookup(g.base,slot,pose,proxy)!=nullptr,"P8_RESOURCE_GATE");++found;
                }
            g.resourcesAttested=found==64;
            emit("p8_resource_gate","\"validated_resources\":%u,\"distinct_resources\":16,\"archetypes\":2,\"actions\":0,\"external_transitions\":0",found);
        }
#endif
        if(g.area && !g.faulted) flags|=seam::AdapterHealthy;
        if(g.removeRequest && g.life.phase==Phase::Live) flags|=seam::AdapterRequestsRemove;
        return flags;
    } catch(const Error& e) {
        // No world yet is ordinary; once attested, a changed world is terminal in this P1 port.
        if(g.area && !g.stopping) block(e.reason);
        return 0;
    }
}
std::int32_t spawn(const ProxyCommand* c,std::uint64_t* out) {
    if(!g.spawnPermit.load(std::memory_order_acquire)) return 2;
    if(!command(c) || !out) return 3;
    try {
        scope();require(g.p1ThisTick && !g.faulted && !g.controllerStopped,"SPAWN_SCOPE");
        if(g.life.spawnAttempted) return 3;
        g.command=*c;const auto p=position(*c);if(const auto* why=target(p,false)) { if(++g.refused%60==1) emit("spawn_refused","\"reason\":\"%s\",\"sequence\":%u",why,c->sequence);return 3; }
        checkLocal(local());require(prefix(profile::factory) && prefix(profile::remove),"NATIVE_CALL_PREFIX");
        auto* factory=g.base+0x5a10a0;require(read<Byte*>(read<Byte*>(factory),0x18)==at(profile::factory),"FACTORY_VTABLE");
        snapshot("BEFORE");g.expected=p;g.expectedYaw=0;
#ifdef PSE_FEATURE_P4
        g.expectedSector=g.plannedSector;
#endif
        require(g.life.beginSpawn(),"SPAWN_BUDGET");++g.spawnCalls;
#ifdef PSE_FEATURE_P4
        emit("p4_factory_placement","\"sector\":%d,\"native_spawn_calls\":%u",currentSector(),g.spawnCalls);
#endif
#ifdef PSE_FEATURE_P4
        emit("native_factory_call","\"actor_type\":1007,\"sequence\":%u,\"sector\":%d,\"target\":[%.9g,%.9g,%.9g]",c->sequence,currentSector(),p[0],p[1],p[2]);
#else
        emit("native_factory_call","\"actor_type\":1007,\"sequence\":%u,\"sector\":259,\"target\":[%.9g,%.9g,%.9g]",c->sequence,p[0],p[1],p[2]);
#endif
        using Factory=Byte*(__fastcall*)(void*,int,const float*,float,int);
        // Save the real return before any diagnostic access. Unknown/null exhausts the budget.
        Byte* a=reinterpret_cast<Factory>(at(profile::factory))(factory,1007,p.data(),0.0f,currentSector());
        g.life.actor=reinterpret_cast<std::uintptr_t>(a);
        emit("factory_return","\"returned\":\"%p\",\"pointer_saved\":true,\"spawn_calls\":%u",a,g.spawnCalls);
        require(a!=nullptr,"FACTORY_NULL_UNKNOWN");actorIdentity(a,nullptr,true);
        auto* n=read<Byte*>(a,0x10);require(g.life.acceptSpawn(reinterpret_cast<std::uintptr_t>(a),reinterpret_cast<std::uintptr_t>(n)),"SPAWN_IDENTITY_STATE");
        require(target(p,true)==nullptr,"SPAWN_CACHE_WITNESS");g.handle=1;*out=g.handle;
#ifdef PSE_FEATURE_P4
        require(g.buffer.push(*c,nowNs()),"P4_INITIAL_BUFFER_SAMPLE");
#endif
        emit("proxy_spawned","\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"sequence\":%u,\"server_tick\":%u,\"target\":[%.9g,%.9g,%.9g],\"floor\":%.9g,\"ceiling\":%.9g,\"native_cpp\":true",g.handle,a,n,c->sequence,c->serverTick,p[0],p[1],p[2],read<float>(a,0x11c),read<float>(a,0x120));return 1;
    } catch(const Error& e) { const bool attempted=g.life.spawnAttempted;block(e.reason);return attempted?0:3; }
}
#ifdef PSE_FEATURE_P4
std::int32_t apply(std::uint64_t h,const ProxyCommand* c) {
#else
std::int32_t update(std::uint64_t h,const ProxyCommand* c) {
#endif
    try {
        scope();if(!command(c)) return 3;
        require(h==g.handle && g.life.phase==Phase::Live && !g.controllerStopped && !g.faulted,"UPDATE_IDENTITY_STATE");
        const auto p=position(*c);const float yaw=bounded::normalizeYaw(c->yaw);
        const bool moved=p!=g.expected,turned=!sameFloat(yaw,g.expectedYaw);
        if(!moved && !turned) { g.command=*c;++g.unchanged;return 2; }
        auto* a=ptr(g.life.actor);actorIdentity(a,ptr(g.life.node),true);
        if(moved) if(const auto* why=target(p,true)) { if(++g.refused%60==1) emit("update_refused","\"reason\":\"%s\",\"sequence\":%u,\"target\":[%.9g,%.9g,%.9g]",why,c->sequence,p[0],p[1],p[2]);return 3; }
        const auto before=local();const bool teleport=moved && (g.relocate || bounded::teleport(g.expected,p));
#ifdef PSE_FEATURE_P4
        if(moved && g.plannedSector!=g.expectedSector) {
            constexpr Byte linkPrefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48};
            require(std::equal(std::begin(linkPrefix),std::end(linkPrefix),g.base+0x1d3d70),"P4_LINK_PREFIX");
            require(list(g.expectedSector,ptr(g.life.node)).found==1 && list(g.plannedSector,ptr(g.life.node)).found==0,"P4_LINK_BEFORE");
        }
#endif
        if(moved) {
            // Only the three accepted origin floats. No structure copy, refs, flags or links.
            for(std::size_t i=0;i<3;++i) store<float>(a,0x18+i*4,p[i]);
#ifdef PSE_FEATURE_P4
            if(g.plannedSector!=g.expectedSector) {
                // DOC58A native LinkSector, never manual sector/links/cache writes.
                const int from=g.expectedSector,to=g.plannedSector;
                auto* site=g.base+0x1d3d70;
                reinterpret_cast<void(__fastcall*)(void*,int)>(site)(a,to);g.expectedSector=to;
                require(list(from,ptr(g.life.node)).found==0 && list(to,ptr(g.life.node)).found==1,"P4_LINK_AFTER");
                emit("p4_portal_crossed","\"from\":%d,\"to\":%d,\"native_lerp_preserved\":%s",from,to,teleport?"false":"true");
            }
#endif
            if(g.relocate) {
                // Cache refresh with the engine's own floor/ceiling functions for the new sector (what a ticking actor would do).
                const float fl=height(profile::floor_height,p,g.expectedSector),ce=height(profile::ceiling_height,p,g.expectedSector);
                require(std::isfinite(fl) && std::isfinite(ce) && fl<ce,"RELOCATE_HEIGHTS");
                store<float>(a,0x11c,fl);store<float>(a,0x120,ce);
                emit("p4_relocated","\"sector\":%d,\"floor\":%.9g,\"ceiling\":%.9g,\"target\":[%.9g,%.9g,%.9g]",g.expectedSector,fl,ce,p[0],p[1],p[2]);
            }
            g.expected=p;if(teleport) reset(a+0x50);++g.revision;
        }
        if(turned) { store<float>(a,0x38,yaw);g.expectedYaw=yaw;reset(a+0x78);++g.revision; }
        actorIdentity(a,ptr(g.life.node),true);require(before==local(),"P1_CHANGED_DURING_MUTATION");g.command=*c;++g.applied;
        emit("proxy_update","\"handle\":%llu,\"sequence\":%u,\"server_tick\":%u,\"revision\":%u,\"engine_stamp\":%u,\"target\":[%.9g,%.9g,%.9g],\"actor_origin\":[%.9g,%.9g,%.9g],\"actor_yaw\":%.9g,\"mode\":\"%s\",\"p1_exact_unchanged\":true",h,c->sequence,c->serverTick,g.revision,stamp(),p[0],p[1],p[2],read<float>(a,0x18),read<float>(a,0x1c),read<float>(a,0x20),read<float>(a,0x38),teleport?"teleport":"native_step");return 1;
    } catch(const Error& e) { g.controllerStopped=true;g.removeRequest=true;emit("native_proxy_controller_stopped","\"reason\":\"%s\"",e.reason);return 4; }
}
#ifdef PSE_FEATURE_P4
std::int32_t update(std::uint64_t h,const ProxyCommand* c) {
    if(g.controllerStopped || g.stopping) return 4;
    if(!thread() || !g.inTick || g.faulted || g.life.phase!=Phase::Live || h!=g.handle || !command(c)) return 3;
    const auto received=nowNs();const bool queued=g.buffer.push(*c,received);
    emit("p4_snapshot_buffered","\"sequence\":%u,\"server_tick\":%u,\"queued\":%s,\"depth\":%zu,\"delay_ms\":160,\"received_ns\":%llu,\"native_writes\":0",c->sequence,c->serverTick,queued?"true":"false",g.buffer.size(),received);
    return queued ? 2 : 3; // Queued, no native mutation yet.
}
#endif
std::int32_t remove(std::uint64_t h) {
    if(!h || h!=g.handle || g.life.phase!=Phase::Live || g.life.removeAttempted) return 2;
    try {
        scope();auto* a=ptr(g.life.actor);auto* n=ptr(g.life.node);actorIdentity(a,n,true);
        require(prefix(profile::remove),"REMOVE_PREFIX");const auto before=local();
#ifdef PSE_FEATURE_P4
        g.buffer.clear();
#endif
        require(g.life.beginRemove(),"REMOVE_BUDGET");++g.removeCalls;g.removeStep=g.steps;g.controllerStopped=true;
        emit("native_remove_request","\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"remove_calls\":%u,\"engine_stamp\":%u",h,a,n,g.removeCalls,stamp());
        reinterpret_cast<void(__fastcall*)(void*)>(at(profile::remove))(a);
        require(before==local() && read<Byte>(a,0x40)==1 && (read<unsigned>(a,0x16c)&0x2000),"REMOVE_POSTCONDITION");
        emit("native_remove_postcondition","\"removed\":1,\"flags\":%u,\"refs\":%d,\"sector_occurrences\":%u,\"p1_exact_unchanged\":true,\"pending\":true",read<unsigned>(a,0x16c),read<int>(n,0x40),list(currentSector(),n).found);return 1;
    } catch(const Error& e) { const bool attempted=g.life.removeAttempted;block(e.reason);return attempted?0:2; }
}
std::int32_t retirement(std::uint64_t h) {
    if(!thread() || h!=g.handle || !h) return 0;
    switch(g.life.phase) { case Phase::Retired:return 1;case Phase::Pending:case Phase::Retiring:return 2;case Phase::Blocked:return 3;default:return 0; }
}
void tickLeave(std::uint64_t,std::uint32_t) {
    try {
        if(!thread() || !g.inTick || g.stopping || !g.baseline || !g.p1ThisTick || !stable()) { g.inTick=false;return; }
#ifdef PSE_FEATURE_P4
        if(g.life.phase==Phase::Live) {
            ProxyCommand c;const auto evaluated=nowNs();if(g.buffer.evaluate(evaluated,c)) {
                const auto result=apply(g.handle,&c);
                if(g.ticks%60==0) emit("p4_playout","\"evaluated_ns\":%llu,\"cursor_tick\":%.9g,\"backlog_ticks\":%.9g,\"reanchors\":%u,\"discontinuities\":%u,\"sequence\":%u,\"result\":%d",evaluated,g.buffer.cursorTick(),g.buffer.backlogTicks(),g.buffer.reanchors(),g.buffer.discontinuities(),c.sequence,result);
            }
        }
#endif
        checkLocal(local());const auto s=stamp();const bool step=!g.hasStamp || s!=g.lastStamp;g.hasStamp=true;g.lastStamp=s;
        if(step && g.life.phase==Phase::Live) {
#ifdef PSE_FEATURE_P8
            animate();
#endif
            actorIdentity(ptr(g.life.actor),ptr(g.life.node),true);++g.liveSteps;
            if(g.liveSteps==120) snapshot("AFTER_SPAWN");
            if(g.liveSteps%1800==0) snapshot("LIVE");
            if(g.liveSteps>=36000) g.removeRequest=true;
            if(g.liveSteps<=3 || g.liveSteps%60==0) emit("native_proxy_stable","\"handle\":%llu,\"simulation_steps\":%u,\"actor_ticks\":%u,\"applied\":%llu,\"unchanged\":%llu,\"refused\":%llu,\"foreign_calls\":0",g.handle,g.liveSteps,g.actorTicks,g.applied,g.unchanged,g.refused);
        } else if(g.life.phase==Phase::Pending && g.steps-g.removeStep>120) block("RETIREMENT_DEADLINE");
        else if(step && g.life.phase==Phase::Retired) {
            if(!g.postSteps) snapshot("AFTER_REMOVE");++g.postSteps;
            if(g.postSteps==60) snapshot("POST_RETIREMENT");
            if(g.postSteps<=3 || g.postSteps%60==0) emit("post_retire","\"post_retire_steps\":%u,\"all_weak_pointers_null\":%s,\"actor_reads\":false",g.postSteps,(!g.life.actor && !g.life.node && !g.life.retireNode)?"true":"false");
        }
    } catch(const Error& e) { block(e.reason); }
    g.inTick=false;
}
void worldExit(std::uint32_t reason) {
    require(thread(),"EXIT_AFFINITY");
#ifdef PSE_FEATURE_P8
    g.art.reset();g.preview.store(0,std::memory_order_release);
#endif
#ifdef PSE_FEATURE_P4
    g.buffer.clear();
#endif
    g.stopping=true;g.inTick=false;const auto phase=g.life.phase;g.life.worldExit();
    emit("world_exit_observed","\"reason\":%u,\"phase_before\":%u,\"all_weak_pointers_invalidated\":true,\"native_calls_after_barrier\":0,\"engine_retirement_claimed\":false",reason,static_cast<unsigned>(phase));
}
// Guard only our observations; native original calls are outside this SEH boundary.
void guardCpp(void(*fn)(void*),void* arg) { try { fn(arg); } catch(const Error& e) { block(e.reason); } catch(...) { block("OBSERVER_CPP_EXCEPTION"); } }
void guard(void(*fn)(void*),void* arg=nullptr) {
    __try { guardCpp(fn,arg); } __except(EXCEPTION_EXECUTE_HANDLER) { block("OBSERVER_SEH"); }
}
void observePuppet(void* self) {
    if(!thread() || !g.inTick || g.stopping) return;
    auto* n=read<Byte*>(player(),0xd8);if(!n || read<void*>(n,0x28)!=self) return;
    ++g.p1ThisTick;const auto s=stamp();if(!g.hasP1Stamp || s!=g.lastP1Stamp) { ++g.steps;g.lastP1Stamp=s;g.hasP1Stamp=true; }
}
std::uintptr_t __fastcall puppetTick(void* self) { guard(observePuppet,self);return reinterpret_cast<Unary>(g.original[profile::puppet_tick])(self); }
void observeActor(void* self) {
    if(reinterpret_cast<std::uintptr_t>(self)!=g.life.actor || !g.life.actor) return;
    require(thread() && g.inTick && !g.stopping,"ACTOR_TICK_AFFINITY");if(g.life.phase==Phase::Live) ++g.actorTicks;
}
std::uintptr_t __fastcall actorTick(void* self) { guard(observeActor,self);return reinterpret_cast<Unary>(g.original[profile::actor_tick])(self); }
void onRemoveBefore(void* self) {
    if(reinterpret_cast<std::uintptr_t>(self)==g.life.actor && g.life.actor) {
        require(thread() && g.inTick && g.inSweep && g.life.phase==Phase::Pending,"ONREMOVE_SCOPE");emit("native_on_remove_entry","\"handle\":%llu",g.handle);
    }
}
void onRemoveAfter(void* self) {
    if(reinterpret_cast<std::uintptr_t>(self)==g.life.actor && g.life.actor) {
        require(!read<Byte*>(self,0x10),"ONREMOVE_OWN_NODE_NOT_CLEARED");emit("native_on_remove_return","\"own_node_cleared\":true");
    }
}
std::uintptr_t __fastcall onRemove(void* self) {
    guard(onRemoveBefore,self);const auto result=reinterpret_cast<Unary>(g.original[profile::on_remove])(self);guard(onRemoveAfter,self);return result;
}
void beforePool(void* self) {
    if(!g.life.actor || reinterpret_cast<std::uintptr_t>(self)!=g.life.actor) return;
    const bool controlled=thread() && g.inTick && g.inSweep && !g.stopping && g.life.phase==Phase::Pending && g.life.removeAttempted;
    bool facts=false;
    if(controlled) facts=!read<Byte*>(self,0x10) && read<int>(ptr(g.life.node),0x40)==0;
    // Invalidate BEFORE trampoline: no further Actor read, including on the return path.
    g.life.poolReturn(reinterpret_cast<std::uintptr_t>(self),controlled && facts);
    emit("native_pool_return_entry","\"valid_scope\":%s,\"all_actor_weak_pointers_null\":true,\"actor_reads_after_entry\":false",controlled&&facts?"true":"false");
    if(!controlled || !facts) block("POOL_RETURN_OUTSIDE_CONTROLLED_SWEEP");
}
std::uintptr_t __fastcall poolReturn(void* factory,void* self) {
    guard(beforePool,self);return reinterpret_cast<Binary>(g.original[profile::pool_return])(factory,self);
}
void beginSweep(void*) { if(thread() && g.inTick && !g.stopping) { require(!g.inSweep,"NESTED_SWEEP");g.inSweep=true; } }
void endSweep(void*) {
    if(!thread() || !g.inSweep) return;
    if(g.life.phase==Phase::Retiring && g.life.retireNode) {
        auto* n=ptr(g.life.retireNode);const bool empty=!read<Byte*>(n,0x28);const int refs=read<int>(n,0x40),inUse=read<Byte>(n,0x24);const auto occurrences=list(currentSector(),n).found;
        const bool ok=g.life.finishRetirement(empty,refs,inUse,static_cast<int>(occurrences));
        emit("deferred_retired","\"success\":%s,\"payload_null\":%s,\"refs\":%d,\"in_use\":%d,\"sector_occurrences\":%u,\"all_weak_pointers_null\":%s,\"actor_reads\":false",ok?"true":"false",empty?"true":"false",refs,inUse,occurrences,(!g.life.actor&&!g.life.node&&!g.life.retireNode)?"true":"false");
        require(ok,"NODE_RETIREMENT_POSTCONDITION");
    }
    g.inSweep=false;
}
std::uintptr_t __fastcall sweep() { guard(beginSweep);const auto result=reinterpret_cast<NoArgs>(g.original[profile::sweep])();guard(endSweep);g.inSweep=false;return result; }
void drainBefore(void*) {
    if(thread() && g.inTick && !g.stopping && g.life.phase==Phase::Pending && g.life.node)
        emit("deferred_drain_entry","\"refs\":%d,\"sector_occurrences\":%u",read<int>(ptr(g.life.node),0x40),list(currentSector(),ptr(g.life.node)).found);
}
void drainAfter(void*) {
    if(thread() && g.inTick && !g.stopping && g.life.phase==Phase::Pending && g.life.node)
        emit("deferred_drain_return","\"refs\":%d,\"sector_occurrences\":%u",read<int>(ptr(g.life.node),0x40),list(currentSector(),ptr(g.life.node)).found);
}
std::uintptr_t __fastcall drain(void* self) { guard(drainBefore);const auto result=reinterpret_cast<Unary>(g.original[profile::drain])(self);guard(drainAfter);return result; }
constexpr seam::SeamTable Table{seam::Abi,sizeof(seam::SeamTable),tickEnter,worldReady,spawn,update,remove,retirement,tickLeave,worldExit};
DWORD observeImpl(Observation* out,std::uint32_t size) {
    if(!out || size!=sizeof(Observation)) return ERROR_INVALID_PARAMETER;
    *out={};out->size=sizeof(Observation);out->abi=0x50310001;
    out->simulationTid=g.tid.load(std::memory_order_acquire);out->observedTid=GetCurrentThreadId();
    // Reject foreign callers before reading any simulation-owned mutable state.
    if(!out->simulationTid || out->observedTid!=out->simulationTid) return ERROR_INVALID_STATE;
    out->phase=static_cast<unsigned>(g.life.phase);out->handle=g.handle;
    out->faulted=g.faulted?1u:0u;out->removeAttempted=g.life.removeAttempted?1u:0u;
    if(!g.installed || g.stopping || g.faulted || g.controllerStopped || g.life.phase!=Phase::Live) return ERROR_INVALID_STATE;
    if(!stable()) return ERROR_INVALID_STATE;
    auto* a=ptr(g.life.actor);actorIdentity(a,ptr(g.life.node),true);
    auto* anim=read<Byte*>(a,0x108);const auto id=read<std::int16_t>(a,0x178);
    require(anim && id>=0 && static_cast<unsigned>(id)<read<unsigned>(anim,0x24),"FRAME_BOUNDS");
    auto* frames=read<Byte*>(anim,0x18);require(frames!=nullptr,"FRAME_NULL");
    out->frameToken=reinterpret_cast<std::uint64_t>(frames+static_cast<std::size_t>(id)*0x110);
    out->sequence=g.command.sequence;out->serverTick=g.command.serverTick;out->revision=g.revision;out->engineStamp=stamp();
    out->origin=read<Vec3>(a,0x18);out->render=read<Vec3>(a,0x6c);out->angles=read<Vec3>(a,0x94);out->previous=read<Vec3>(a,0x60);
    out->yaw=read<float>(a,0x38);out->scale=read<float>(a,0x124);out->sector=read<int>(a,0x3c);out->flags=read<unsigned>(a,0x16c);
    out->identityValid=1;return ERROR_SUCCESS;
}
DWORD observeCpp(Observation* out,std::uint32_t size) { try { return observeImpl(out,size); } catch(const Error& e) { block(e.reason);return ERROR_INVALID_STATE; } catch(...) { block("OBSERVATION_EXCEPTION");return ERROR_INVALID_STATE; } }
}
bool install(Byte* image,DWORD simulationTid,Journal journal) {
    if(g.installed || !image || GetCurrentThreadId()!=simulationTid) return false;
    g.base=image;g.tid=simulationTid;g.journal=journal;
    for(std::size_t i=0;i<profile::Count;++i) if(!prefix(static_cast<profile::Index>(i))) { block("NATIVE_PROXY_PREFIX");return false; }
    const std::pair<profile::Index,void*> hooks[]={
        {profile::puppet_tick,reinterpret_cast<void*>(puppetTick)},{profile::actor_tick,reinterpret_cast<void*>(actorTick)},
        {profile::on_remove,reinterpret_cast<void*>(onRemove)},{profile::pool_return,reinterpret_cast<void*>(poolReturn)},
        {profile::sweep,reinterpret_cast<void*>(sweep)},{profile::drain,reinterpret_cast<void*>(drain)}};
    for(const auto& [i,hook]:hooks) {
        if(MH_CreateHook(at(i),hook,&g.original[i])!=MH_OK || MH_EnableHook(at(i))!=MH_OK) { block("NATIVE_PROXY_HOOK_INSTALL");g.stopping=true;detach();return false; }
        g.enabled[i]=true;
    }
    g.installed=true;emit("native_proxy_installed","\"hooks\":6,\"capacity\":1,\"sector\":259,\"new_injector\":false,\"external_callbacks\":false,\"module_pinned\":true");return true;
}
const seam::SeamTable& table() { return Table; }
void permitSpawn() { g.spawnPermit.store(1,std::memory_order_release); }
#ifdef PSE_FEATURE_P8
void stageAnimation(const pse::BridgePlayerSnapshot& s) {
    if(!thread() || !g.inTick || g.stopping || g.faulted || g.controllerStopped) return;
    if(!g.art.accept(s)) block("P8_INVALID_ANIMATION_SNAPSHOT");
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
    if(!g.stopping && g.life.phase!=Phase::Retired && g.life.phase!=Phase::Waiting) return false;
    bool ok=true;
    for(std::size_t n=0;n<profile::Count;++n) if(g.enabled[n]) {
        const auto i=static_cast<profile::Index>(n);const auto result=MH_DisableHook(at(i));const bool exact=prefix(i);ok&=result==MH_OK && exact;
        if(result==MH_OK) g.enabled[n]=false;
        emit("native_proxy_hook_restored","\"site\":\"%s\",\"rva\":%u,\"status\":%d,\"exact16\":%s",profile::Sites[n].name,profile::Sites[n].rva,result,exact?"true":"false");
    }
    if(!ok) block("NATIVE_PROXY_HOOK_RESTORE");return ok;
}
void fault() { g.inTick=false;block("SEAM_UNKNOWN_NATIVE_OUTCOME"); }
void exitBarrier(std::uint32_t reason) { if(thread() && !g.stopping) worldExit(reason); }
bool isPositionInterpolant(const void* candidate) {
    return thread() && g.installed && !g.stopping && !g.faulted && !g.controllerStopped && g.life.phase==Phase::Live &&
        g.life.actor && reinterpret_cast<std::uintptr_t>(candidate)==g.life.actor+0x50;
}
DWORD observe(Observation* out,std::uint32_t size) {
    __try { return observeCpp(out,size); } __except(EXCEPTION_EXECUTE_HANDLER) { block("OBSERVATION_SEH");return ERROR_INVALID_STATE; }
}
}
