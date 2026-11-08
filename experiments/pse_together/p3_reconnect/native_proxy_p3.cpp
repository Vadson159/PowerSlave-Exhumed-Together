// P3 fork of the accepted P1 adapter (native_proxy.cpp: DOC-61, accepted in DOC-64). Within one world the P1 contract
// is unchanged: factory 1d4780, three origin floats, normalized yaw, native lerp, Remove/Drain/OnRemove/PoolReturn/
// Sweep retirement, TOMB sector 259, z +88. P3 adds, for pse_dll_client_p3 only:
//  * an in-DLL engine-teardown witness: pass-through hooks on Level::Unload 1c90e0, tagged-heap purge c58b0, per-block
//    dispatch c5310 and kexSlaveActor dtor 19f210 (the DOC-58B.2 observer sites). Only live blocks are read: list walks
//    at purge entry/return and the dispatched block header while it is still allocated. No argument is changed.
//  * a same-live-tick Remove: the seam Remove only arms. The native Remove runs at the entry of the live-world Sweep
//    (StateTick 1b6ec0 live branch: Update -> actor ticks -> Sweep 1b6360 -> Drain 1b61d0), so the same tick's Drain
//    pops the removal-queue cell (af3380/af3388) before StateTick returns, as for stock in-tick Removes. A cell still
//    queued at tick end is a fault (QueueWitness::IssuedQueued).
//  * per-world re-attestation (P1 graph baseline, sector-259 area), strictly increasing opaque handles and a native
//    anti-ABA identity (tagged block alloc seq, DOC-58B.2 header +8).
// Native ABIs/offsets come from accepted DOC-57/58A/58B.2 evidence, not decompiler types.
#include "native_proxy_p3.hpp"
#include "proxy_profile.hpp"
#include "proxy_profile_p3.hpp"
#include "teardown_witness_p3.hpp"
#include "foreign_fault_p3.hpp"
#include "map_reader.hpp"
#include "bounded_proxy.hpp"
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

namespace pse::native_proxy_p3 {
namespace {
namespace profile = ::pse::native_proxy::profile;
namespace wit = ::pse::p3::witness;
using Byte = unsigned char;
using bounded::Vec3;
using bounded::Plane;
using bounded::Phase;
using proxy::ProxyCommand;
using pse::p3::QueueWitness;
using pse::proxy::EngineTeardownWitness;
using Unary = std::uintptr_t(__fastcall*)(void*);
using Binary = std::uintptr_t(__fastcall*)(void*,void*);
using NoArgs = std::uintptr_t(__fastcall*)();
using Quad = std::uintptr_t(__fastcall*)(void*,void*,void*,void*);
struct Error { const char* reason; };
void require(bool ok,const char* reason) { if(!ok) throw Error{reason}; }
template<class T> T read(const void* p,std::size_t offset=0) { return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+offset); }
template<class T> void store(void* p,std::size_t offset,T v) { *reinterpret_cast<T*>(static_cast<Byte*>(p)+offset)=v; }
Byte* ptr(std::uintptr_t p) { return reinterpret_cast<Byte*>(p); }
std::uintptr_t num(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }
struct Local {
    Byte* node{};Byte* actor{};Byte* owner{};Byte* vtable{};Byte* ownNode{};
    Vec3 position{},render{},angles{};float yaw{},pitch{};
    std::array<Byte,128> input{};
    std::array<Byte,54> fields{}; // 12 accepted DOC57 fields, initialized content only
    int sector{};
    bool sameGraph(const Local& b) const { return node==b.node && actor==b.actor && owner==b.owner && vtable==b.vtable && ownNode==b.ownNode; }
    bool operator==(const Local&) const = default;
};
struct Sector { std::vector<Plane> planes; };
struct State {
    Byte* base{};std::atomic<DWORD> tid{};Journal journal{};
    void* original[profile::Count]{};bool enabled[profile::Count]{};
    void* original3[profile3::Count]{};bool enabled3[profile3::Count]{};
    std::atomic<unsigned> spawnPermit{};
    bounded::Lifetime life;
    bool installed{},inTick{},inSweep{},stopping{},faulted{},removeRequest{},baseline{},area{},controllerStopped{};
    unsigned p1ThisTick{},spawnCalls{},removeCalls{},revision{},actorTicks{},liveSteps{},postSteps{};
    std::uint64_t ticks{},steps{},removeStep{},handle{},applied{},unchanged{},refused{};
    std::uint32_t lastStamp{},lastP1Stamp{};bool hasStamp{},hasP1Stamp{};
    Vec3 expected{};float expectedYaw{},floor{},ceiling{};
    ProxyCommand command{};
    Local local{};
    std::vector<Sector> sectors;
    // P3
    std::atomic<bool> teardownArmed{};
    wit::Identity id{},prevId{};bool prevPoolRetired{};
    wit::TeardownLog tlog{};
    int purge{}; // 1 = inside c58b0(af4490), 2 = inside c58b0(af4600)
    std::uint64_t handleSerial{},nativeCalls{},callsAtBarrier{},issueTick{},worldsEntered{1},unarmedPurges{};
    bool removeArmed{},removeIssued{},drained{},worldPending{},lost{},everFaulted{};
    QueueWitness queueAtBarrier{QueueWitness::NotIssued};
    pse::p3::ForeignFault foreign; // DOC-64 N1: faults seen off the Sim TID, applied on it
} g;
void emit(const char* event,const char* fmt="",...) {
    if(!g.journal) return;
    char detail[2100]{};va_list args;va_start(args,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,args);va_end(args);
    g.journal(event,detail);
}
bool thread() { return GetCurrentThreadId()==g.tid; }
std::uint32_t stamp() { return read<std::uint32_t>(g.base,0xaec390); }
Byte* player() { return g.base+0xaf33e0; }
void* at(profile::Index i) { return g.base+profile::Sites[i].rva; }
void* at3(profile3::Index i) { return g.base+profile3::Sites[i].rva; }
bool prefix(profile::Index i) { return std::equal(std::begin(profile::Sites[i].prefix),std::end(profile::Sites[i].prefix),static_cast<Byte*>(at(i))); }
bool prefix3(profile3::Index i) { return std::equal(std::begin(profile3::Sites[i].prefix),std::end(profile3::Sites[i].prefix),static_cast<Byte*>(at3(i))); }
std::uintptr_t actorHeap() { return num(g.base+profile3::ActorHeap); }
std::uintptr_t nodeHeap() { return num(g.base+profile3::NodeHeap); }
bool stableWorld() {
    if(!thread()) return false;
    const auto o=map56::observe(g.base);
    map56::Identity id;
    return map56::derive(o,id)==map56::Fault::None && o.state==3 && o.next==3 && !o.pending &&
        id.name=="maps/TOMB.MAP" && id.version==1 && id.countA==71 && id.countB==28608;
}
bool stable() { return !g.stopping && stableWorld(); }
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
    emit("p1_state_snapshot","\"label\":\"%s\",\"fields_raw\":\"%s\",\"fields\":12,\"equal\":true,\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\",\"pos\":[%.9g,%.9g,%.9g],\"engine_stamp\":%u,\"world\":%llu",label,hex,p.actor,p.node,p.owner,p.position[0],p.position[1],p.position[2],stamp(),g.worldsEntered);
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
Sector geometry(int index) {
    Byte* s=sector(index);require(read<int>(s,0x80)==index,"SECTOR_SELF_INDEX");
    const auto first=read<std::uint16_t>(s),last=read<std::uint16_t>(s,2);
    const auto count=read<std::uint64_t>(g.base,0xa45988);auto* faces=read<Byte*>(g.base,0xa45980);
    require(faces && first<=last && static_cast<std::uint64_t>(last)+2<count && last-first<=512,"FACE_BOUNDS");
    Sector out;
    for(unsigned i=first;i<=static_cast<unsigned>(last)+2;++i) {
        auto* f=faces+i*0x108;const auto n=read<Vec3>(f,0xc);const double d=read<float>(f,0x18);
        require(bounded::finite(n) && std::isfinite(d),"NONFINITE_PLANE");out.planes.push_back({n[0],n[1],n[2],d});
    }
    return out;
}
float height(profile::Index i,const Vec3& p) {
    // DOC58A instructions: XMM0 return, RCX point*, RDX sector*, R8B mode.
    using Query=float(__fastcall*)(const float*,void*,std::uint8_t);
    ++g.nativeCalls;return reinterpret_cast<Query>(at(i))(p.data(),sector(259),1);
}
bool point(const Vec3& p) {
    using Query=std::uint8_t(__fastcall*)(void*,const float*,float);
    ++g.nativeCalls;return reinterpret_cast<Query>(at(profile::point_in_sector))(sector(259),p.data(),0)==1;
}
bool unique(const Vec3& p) {
    unsigned hits=0;int own=-1;
    for(std::size_t i=0;i<g.sectors.size();++i) if(bounded::inside(g.sectors[i].planes,p)) { ++hits;own=static_cast<int>(i); }
    return hits==1 && own==259;
}
bool sameFloat(float a,float b) { return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b); }
void attestArea() {
    const auto count=read<std::uint64_t>(g.base,0xa45970);require(count==270,"UNATTESTED_SECTOR_COUNT");
    g.sectors.clear();for(int i=0;i<270;++i) g.sectors.push_back(geometry(i));
    unsigned samples=0;float floor=0,ceiling=0;
    for(int ix=0;ix<=8;++ix) for(int iy=0;iy<=8;++iy) {
        const Vec3 p{-7552.0f+32.0f*ix,-6912.0f+32.0f*iy,1240};
        if(!bounded::fits(g.sectors[259].planes,p)) continue;
        require(unique(p) && point(p),"AREA_IDENTITY");
        const float f=height(profile::floor_height,p),c=height(profile::ceiling_height,p);
        require(std::isfinite(f) && std::isfinite(c),"AREA_HEIGHT_NONFINITE");
        if(!samples) { floor=f;ceiling=c; }
        else require(sameFloat(f,floor) && sameFloat(c,ceiling),"AREA_NONFLAT");
        ++samples;
    }
    require(samples>=16,"AREA_INSUFFICIENT_SAMPLES");g.floor=floor;g.ceiling=ceiling;g.area=true;
    emit("attested_area","\"status\":\"ATTESTED\",\"sector\":259,\"sector_count\":270,\"samples\":%u,\"floor\":%.9g,\"ceiling\":%.9g,\"z_offset\":88,\"native_cpp\":true,\"world\":%llu",samples,floor,ceiling,g.worldsEntered);
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
    require(definition && read<int>(definition)==1007 && read<int>(a,0x3c)==259,"ACTOR_TYPE_SECTOR");
    require(read<Vec3>(a,0x18)==g.expected && sameFloat(read<float>(a,0x38),g.expectedYaw) && !read<Byte>(a,0x40),"ACTOR_TRANSFORM_REMOVED");
    const auto p=local();checkLocal(p);require(list(p.sector,nullptr,&p).localFound==1 && list(259,n).found==1,"ACTOR_MEMBERSHIP");
    require(interpolation(a),"ACTOR_INTERPOLANTS");
    if(strict) require(read<unsigned>(a,0x16c)==0x40 && read<int>(n,0x40)==0 && read<float>(a,0x124)==1.5f &&
        !read<Byte*>(a,0x28) && !read<Byte*>(a,0x148) && read<Vec3>(a,0xec)==Vec3{-12,-12,-24} &&
        read<Vec3>(a,0xf8)==Vec3{12,12,24},"ACTOR_PASSIVE_PROPERTIES");
}
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
void block(const char* reason) {
    // DOC-64 N1: never mutate simulation-owned state from a foreign thread; post, the Sim TID applies it next.
    if(!thread()) { g.foreign.post(reason);emit("p3_foreign_fault_posted","\"reason\":\"%s\",\"state_mutated\":false",reason);return; }
    g.faulted=true;g.everFaulted=true;g.controllerStopped=true;g.removeArmed=false;g.life.block();
    emit("native_proxy_fault","\"reason\":\"%s\",\"all_weak_pointers_null\":true,\"retry\":false,\"rearm_possible\":false",reason);
}
// Simulation thread entries: apply a fault that a foreign thread could only post.
void applyForeign() {
    if(!thread()) return;
    if(const char* r=g.foreign.take()) { emit("p3_foreign_fault_applied","\"reason\":\"%s\",\"posts\":%u,\"applied_on_sim_tid\":true",r,g.foreign.posts());block(r); }
}
void reset(Byte* p) { ++g.nativeCalls;reinterpret_cast<void(__fastcall*)(void*)>(at(profile::interpolant_reset))(p); }
bool command(const ProxyCommand* c) { return c && c->abi==seam::Abi && c->slot==0 && std::isfinite(c->yaw) && std::abs(c->yaw)<=static_cast<float>(8*3.141592653589793); }
Vec3 position(const ProxyCommand& c) { return {c.x,c.y,static_cast<float>(static_cast<double>(c.z)+88.0)}; }
const char* text(QueueWitness q) { return pse::p3::toString(q); }
const char* text(EngineTeardownWitness w) { return w==EngineTeardownWitness::ArenaDestroyed ? "ArenaDestroyed" : w==EngineTeardownWitness::NotObserved ? "NotObserved" : "Unknown"; }
// Per-incarnation bookkeeping; hooks, permit, handle serial and anti-ABA history persist for the process.
void resetIncarnation() {
    g.removeArmed=g.removeIssued=g.drained=false;g.queueAtBarrier=QueueWitness::NotIssued;g.issueTick=0;
    g.liveSteps=g.postSteps=g.actorTicks=g.revision=0;g.applied=g.unchanged=g.refused=0;g.hasStamp=false;
    g.expected={};g.expectedYaw=0;g.command={};g.controllerStopped=false;g.removeRequest=false;g.life=bounded::Lifetime{};
}
// A previous incarnation of this world was retired through our controlled PoolReturn: its storage may come back from
// the class pool with the same alloc seq (legal reuse); the new Spawn gets a new opaque handle.
void beginNextIncarnation() {
    if(g.id.valid) { g.prevId=g.id;g.prevPoolRetired=true; }
    g.id={};resetIncarnation();
    emit("p3_incarnation_reset","\"previous_handle\":%llu,\"previous_actor\":\"%p\",\"previous_alloc_seq\":%llu,\"reason\":\"pool_retired_in_this_world\"",g.handle,ptr(g.prevId.actor),g.prevId.allocSeq);
}
void tickEnter(std::uint64_t tick) {
    require(thread() && !g.inTick,"TICK_AFFINITY_REENTRY");applyForeign();g.inTick=true;g.ticks=tick;g.p1ThisTick=0;
}
void worldExit(std::uint32_t reason);
// The adapter observed its attested world become unstable before any external barrier (no ClientGate yet).
void loseWorld() { g.lost=true;worldExit(3);emit("p3_world_lost","\"reason\":\"WORLD_NOT_STABLE\",\"automaton_barrier_requested\":true"); }
bool enterWorld() {
    if(!thread() || !g.inTick || g.everFaulted || g.teardownArmed.load(std::memory_order_acquire) || !g.p1ThisTick || !stableWorld()) return false;
    g.stopping=false;g.worldPending=false;g.lost=false;g.baseline=false;g.area=false;g.sectors.clear();
    resetIncarnation();++g.worldsEntered;
    emit("p3_world_enter","\"world\":%llu,\"handle_serial\":%llu,\"previous_actor\":\"%p\",\"previous_alloc_seq\":%llu,\"previous_pool_retired\":%s,\"reattest\":true",g.worldsEntered,g.handleSerial,ptr(g.prevId.actor),g.prevId.allocSeq,g.prevPoolRetired?"true":"false");
    return true;
}
std::int32_t worldReady(std::uint64_t) {
    applyForeign();
    try {
        if(g.worldPending && !enterWorld()) return 0;
        scope();list(259);int flags=seam::StableWorld;
        if(g.p1ThisTick) {
            const auto p=local();if(!g.baseline) { g.local=p;g.baseline=true;emit("p1_baseline","\"actor\":\"%p\",\"node\":\"%p\",\"owner\":\"%p\",\"world\":%llu",p.actor,p.node,p.owner,g.worldsEntered); }
            checkLocal(p);flags|=seam::LocalTicked;if(!g.area) attestArea();
        }
        if(g.area && !g.faulted) flags|=seam::AdapterHealthy;
        if(g.removeRequest && g.life.phase==Phase::Live) flags|=seam::AdapterRequestsRemove;
        return flags;
    } catch(const Error& e) {
        // No world yet is ordinary. P3: an attested world becoming unstable is a transition, not a fault.
        if(g.area && !g.stopping) { if(std::strcmp(e.reason,"WORLD_NOT_STABLE")==0) loseWorld(); else block(e.reason); }
        return 0;
    }
}
std::int32_t spawn(const ProxyCommand* c,std::uint64_t* out) {
    applyForeign();
    if(!g.spawnPermit.load(std::memory_order_acquire)) return 2;
    if(!command(c) || !out) return 3;
    try {
        scope();
        if(g.life.phase==Phase::Retired && !g.life.actor && !g.life.node && !g.life.retireNode && !g.faulted) beginNextIncarnation();
        require(g.p1ThisTick && !g.faulted && !g.controllerStopped,"SPAWN_SCOPE");
        if(g.life.spawnAttempted || g.life.phase!=Phase::Waiting) return 3;
        g.command=*c;const auto p=position(*c);if(const auto* why=target(p,false)) { if(++g.refused%60==1) emit("spawn_refused","\"reason\":\"%s\",\"sequence\":%u",why,c->sequence);return 3; }
        checkLocal(local());require(prefix(profile::factory) && prefix(profile::remove),"NATIVE_CALL_PREFIX");
        auto* factory=g.base+0x5a10a0;require(read<Byte*>(read<Byte*>(factory),0x18)==at(profile::factory),"FACTORY_VTABLE");
        snapshot("BEFORE");g.expected=p;g.expectedYaw=0;
        require(g.life.beginSpawn(),"SPAWN_BUDGET");++g.spawnCalls;
        emit("native_factory_call","\"actor_type\":1007,\"sequence\":%u,\"sector\":259,\"target\":[%.9g,%.9g,%.9g],\"incarnation\":%llu",c->sequence,p[0],p[1],p[2],g.handleSerial+1);
        using Factory=Byte*(__fastcall*)(void*,int,const float*,float,int);
        // Save the real return before any diagnostic access. Unknown/null exhausts the budget.
        ++g.nativeCalls;Byte* a=reinterpret_cast<Factory>(at(profile::factory))(factory,1007,p.data(),0.0f,259);
        g.life.actor=num(a);
        emit("factory_return","\"returned\":\"%p\",\"pointer_saved\":true,\"spawn_calls\":%u",a,g.spawnCalls);
        require(a!=nullptr,"FACTORY_NULL_UNKNOWN");actorIdentity(a,nullptr,true);
        auto* n=read<Byte*>(a,0x10);require(g.life.acceptSpawn(num(a),num(n)),"SPAWN_IDENTITY_STATE");
        require(target(p,true)==nullptr,"SPAWN_CACHE_WITNESS");
        // P3 native identity: the Actor storage must be owned by the StateExit purge of af4490 (58B.2 E5).
        unsigned char raw[wit::RawHeader]{};std::memcpy(raw,a-wit::HeaderSize,sizeof(raw));const auto h=wit::decode(raw);
        require(wit::actorBlockOk(h,actorHeap(),num(g.base+profile3::DtorThunk)),"P3_ACTOR_BLOCK_UNATTESTED");
        const wit::Identity next{num(a),num(n),num(a)-wit::HeaderSize,0,h.allocSeq,static_cast<std::uint32_t>(g.handleSerial+1),true};
        const auto reuse=wit::classify(g.prevId,g.prevPoolRetired,next);
        require(reuse!=wit::Reuse::Anomaly,"P3_ABA_ANOMALY");
        g.id=next;g.handle=++g.handleSerial;*out=g.handle;
        emit("proxy_spawned","\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"sequence\":%u,\"server_tick\":%u,\"target\":[%.9g,%.9g,%.9g],\"floor\":%.9g,\"ceiling\":%.9g,\"native_cpp\":true,\"world\":%llu",g.handle,a,n,c->sequence,c->serverTick,p[0],p[1],p[2],read<float>(a,0x11c),read<float>(a,0x120),g.worldsEntered);
        emit("p3_spawn_identity","\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"block\":\"%p\",\"alloc_seq\":%llu,\"heap_af4490\":true,\"reuse\":\"%s\",\"previous_actor\":\"%p\",\"previous_alloc_seq\":%llu,\"previous_pool_retired\":%s,\"world\":%llu",
            g.handle,a,n,a-wit::HeaderSize,h.allocSeq,wit::toString(reuse),ptr(g.prevId.actor),g.prevId.allocSeq,g.prevPoolRetired?"true":"false",g.worldsEntered);
        return 1;
    } catch(const Error& e) { const bool attempted=g.life.spawnAttempted;block(e.reason);return attempted?0:3; }
}
std::int32_t update(std::uint64_t h,const ProxyCommand* c) {
    applyForeign();
    try {
        scope();if(!command(c)) return 3;
        require(h==g.handle && g.life.phase==Phase::Live && !g.controllerStopped && !g.faulted,"UPDATE_IDENTITY_STATE");
        const auto p=position(*c);const float yaw=bounded::normalizeYaw(c->yaw);
        const bool moved=p!=g.expected,turned=!sameFloat(yaw,g.expectedYaw);
        if(!moved && !turned) { g.command=*c;++g.unchanged;return 2; }
        auto* a=ptr(g.life.actor);actorIdentity(a,ptr(g.life.node),true);
        if(moved) if(const auto* why=target(p,true)) { if(++g.refused%60==1) emit("update_refused","\"reason\":\"%s\",\"sequence\":%u,\"target\":[%.9g,%.9g,%.9g]",why,c->sequence,p[0],p[1],p[2]);return 3; }
        const auto before=local();const bool teleport=moved && bounded::teleport(g.expected,p);
        if(moved) {
            // Only the three accepted origin floats. No structure copy, refs, flags or links.
            for(std::size_t i=0;i<3;++i) store<float>(a,0x18+i*4,p[i]);
            g.expected=p;if(teleport) reset(a+0x50);++g.revision;
        }
        if(turned) { store<float>(a,0x38,yaw);g.expectedYaw=yaw;reset(a+0x78);++g.revision; }
        actorIdentity(a,ptr(g.life.node),true);require(before==local(),"P1_CHANGED_DURING_MUTATION");g.command=*c;++g.applied;
        emit("proxy_update","\"handle\":%llu,\"sequence\":%u,\"server_tick\":%u,\"revision\":%u,\"engine_stamp\":%u,\"target\":[%.9g,%.9g,%.9g],\"actor_origin\":[%.9g,%.9g,%.9g],\"actor_yaw\":%.9g,\"mode\":\"%s\",\"p1_exact_unchanged\":true",h,c->sequence,c->serverTick,g.revision,stamp(),p[0],p[1],p[2],read<float>(a,0x18),read<float>(a,0x1c),read<float>(a,0x20),read<float>(a,0x38),teleport?"teleport":"native_step");return 1;
    } catch(const Error& e) { g.controllerStopped=true;g.removeRequest=true;emit("native_proxy_controller_stopped","\"reason\":\"%s\"",e.reason);return 4; }
}
// Seam Remove (post-tick phase): validates and ARMS only. Result 1 = requested; the native call follows in-tick.
std::int32_t remove(std::uint64_t h) {
    applyForeign();
    if(!h || h!=g.handle || g.life.phase!=Phase::Live || g.life.removeAttempted || g.removeArmed) return 2;
    try {
        scope();auto* a=ptr(g.life.actor);auto* n=ptr(g.life.node);actorIdentity(a,n,true);
        require(prefix(profile::remove),"REMOVE_PREFIX");
        g.removeArmed=true;g.controllerStopped=true;
        emit("p3_remove_armed","\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"native_remove_now\":false,\"issue_point\":\"entry of the next live-world Sweep (Drain follows in the same StateTick)\"",h,a,n);
        return 1;
    } catch(const Error& e) { block(e.reason);return 2; }
}
// Inside StateTick's live-world branch (our P1 PuppetTick ran this tick), right before Sweep; Drain follows Sweep.
void issueArmedRemoveCpp() {
    applyForeign(); // DOC-78 F7: a fault posted by a foreign thread since tickEnter cancels the Remove (block() clears removeArmed)
    if(!g.removeArmed || g.removeIssued || !thread() || !g.inTick || g.stopping || !g.inSweep || !g.p1ThisTick || g.life.phase!=Phase::Live) return;
    try {
        require(stable(),"P3_REMOVE_WORLD");auto* a=ptr(g.life.actor);auto* n=ptr(g.life.node);actorIdentity(a,n,true);
        require(prefix(profile::remove),"REMOVE_PREFIX");const auto before=local();
        require(g.life.beginRemove(),"REMOVE_BUDGET");++g.removeCalls;g.removeStep=g.steps;g.removeIssued=true;g.issueTick=g.ticks;
        emit("native_remove_request","\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"remove_calls\":%u,\"engine_stamp\":%u,\"in_tick\":true,\"site\":\"sweep_entry\",\"tick\":%llu",g.handle,a,n,g.removeCalls,stamp(),g.ticks);
        ++g.nativeCalls;reinterpret_cast<void(__fastcall*)(void*)>(at(profile::remove))(a);
        require(before==local() && read<Byte>(a,0x40)==1 && (read<unsigned>(a,0x16c)&0x2000),"REMOVE_POSTCONDITION");
        emit("native_remove_postcondition","\"removed\":1,\"flags\":%u,\"refs\":%d,\"sector_occurrences\":%u,\"p1_exact_unchanged\":true,\"pending\":true,\"queued_until_same_tick_drain\":true",read<unsigned>(a,0x16c),read<int>(n,0x40),list(259,n).found);
    } catch(const Error& e) { block(e.reason); }
}
// Same SEH boundary the P1 seam put around its Remove callback: a native fault becomes a terminal block.
void issueArmedRemove() { __try { issueArmedRemoveCpp(); } __except(EXCEPTION_EXECUTE_HANDLER) { block("P3_REMOVE_SEH"); } }
std::int32_t retirement(std::uint64_t h) {
    applyForeign();
    if(!thread() || h!=g.handle || !h) return 0;
    if(g.removeArmed && !g.removeIssued && g.life.phase==Phase::Live) return 2; // armed, waiting for a live Sweep
    switch(g.life.phase) { case Phase::Retired:return 1;case Phase::Pending:case Phase::Retiring:return 2;case Phase::Blocked:return 3;default:return 0; }
}
void tickLeave(std::uint64_t,std::uint32_t) {
    applyForeign();
    // The removal-queue cell of an in-tick Remove must be gone when StateTick returns.
    if(thread() && g.removeIssued && !g.drained && g.issueTick==g.ticks && !g.everFaulted) block("P3_REMOVE_NOT_DRAINED_IN_TICK");
    try {
        if(!thread() || !g.inTick || g.stopping || !g.baseline || !g.p1ThisTick || !stable()) { g.inTick=false;return; }
        checkLocal(local());const auto s=stamp();const bool step=!g.hasStamp || s!=g.lastStamp;g.hasStamp=true;g.lastStamp=s;
        if(step && g.life.phase==Phase::Live) {
            actorIdentity(ptr(g.life.actor),ptr(g.life.node),true);++g.liveSteps;
            if(g.liveSteps==120) snapshot("AFTER_SPAWN");
            if(g.liveSteps%1800==0) snapshot("LIVE");
            if(g.liveSteps>=36000) g.removeRequest=true;
            if(g.liveSteps<=3 || g.liveSteps%60==0) emit("native_proxy_stable","\"handle\":%llu,\"simulation_steps\":%u,\"actor_ticks\":%u,\"applied\":%llu,\"unchanged\":%llu,\"refused\":%llu,\"foreign_calls\":0",g.handle,g.liveSteps,g.actorTicks,g.applied,g.unchanged,g.refused);
        } else if(g.life.phase==Phase::Pending && g.steps-g.removeStep>120) block("RETIREMENT_DEADLINE");
        else if(step && g.life.phase==Phase::Retired) {
            if(!g.postSteps) snapshot("AFTER_REMOVE");++g.postSteps;
            if(g.postSteps==60) snapshot("POST_RETIREMENT");
            if(g.postSteps<=3 || g.postSteps%60==0) emit("post_retire","\"post_retire_steps\":%u,\"all_weak_pointers_null\":%s,\"actor_reads\":false,\"handle\":%llu",g.postSteps,(!g.life.actor && !g.life.node && !g.life.retireNode)?"true":"false",g.handle);
        }
    } catch(const Error& e) { block(e.reason); }
    g.inTick=false;
}
// Barrier: idempotent. Records the queue witness and arms the teardown witness for an identity that may still own
// storage in af4490; every weak pointer is invalidated; no native call is made by the proxy after this point.
void worldExit(std::uint32_t reason) {
    require(thread(),"EXIT_AFFINITY");
    applyForeign();
    if(g.stopping) return;
    const auto phase=g.life.phase;
    g.queueAtBarrier=!g.removeIssued ? QueueWitness::NotIssued : g.drained ? QueueWitness::IssuedDrained : QueueWitness::IssuedQueued;
    const bool owns=g.id.valid && (phase==Phase::Live || phase==Phase::Pending || phase==Phase::Retiring);
    g.tlog={};g.tlog.armed=owns;g.callsAtBarrier=g.nativeCalls;g.purge=0;
    g.teardownArmed.store(owns,std::memory_order_release);
    g.stopping=true;g.inTick=false;g.removeArmed=false;g.life.worldExit();
    emit("world_exit_observed","\"reason\":%u,\"phase_before\":%u,\"all_weak_pointers_invalidated\":true,\"native_calls_after_barrier\":0,\"engine_retirement_claimed\":false,\"queue_witness\":\"%s\",\"teardown_witness_armed\":%s,\"handle\":%llu,\"actor\":\"%p\",\"node\":\"%p\",\"alloc_seq\":%llu,\"world\":%llu",
        reason,static_cast<unsigned>(phase),text(g.queueAtBarrier),owns?"true":"false",g.handle,ptr(g.id.actor),ptr(g.id.node),g.id.allocSeq,g.worldsEntered);
}
// Guard only our observations; native original calls are outside this SEH boundary.
void guardCpp(void(*fn)(void*),void* arg) { try { fn(arg); } catch(const Error& e) { block(e.reason); } catch(...) { block("OBSERVER_CPP_EXCEPTION"); } }
void guard(void(*fn)(void*),void* arg=nullptr) {
    __try { guardCpp(fn,arg); } __except(EXCEPTION_EXECUTE_HANDLER) { block("OBSERVER_SEH"); }
}
void observePuppet(void* self) {
    // P3: also count while a torn-down world awaits re-attestation (enterWorld needs a real P1 tick of the new world).
    if(!thread() || !g.inTick || (g.stopping && !g.worldPending)) return;
    auto* n=read<Byte*>(player(),0xd8);if(!n || read<void*>(n,0x28)!=self) return;
    ++g.p1ThisTick;const auto s=stamp();if(!g.hasP1Stamp || s!=g.lastP1Stamp) { ++g.steps;g.lastP1Stamp=s;g.hasP1Stamp=true; }
}
std::uintptr_t __fastcall puppetTick(void* self) { guard(observePuppet,self);return reinterpret_cast<Unary>(g.original[profile::puppet_tick])(self); }
void observeActor(void* self) {
    if(num(self)!=g.life.actor || !g.life.actor) return;
    require(thread() && g.inTick && !g.stopping,"ACTOR_TICK_AFFINITY");if(g.life.phase==Phase::Live) ++g.actorTicks;
}
std::uintptr_t __fastcall actorTick(void* self) { guard(observeActor,self);return reinterpret_cast<Unary>(g.original[profile::actor_tick])(self); }
void onRemoveBefore(void* self) {
    if(num(self)==g.life.actor && g.life.actor) {
        require(thread() && g.inTick && g.inSweep && g.life.phase==Phase::Pending,"ONREMOVE_SCOPE");emit("native_on_remove_entry","\"handle\":%llu",g.handle);
    }
}
void onRemoveAfter(void* self) {
    if(num(self)==g.life.actor && g.life.actor) {
        require(!read<Byte*>(self,0x10),"ONREMOVE_OWN_NODE_NOT_CLEARED");emit("native_on_remove_return","\"own_node_cleared\":true");
    }
}
std::uintptr_t __fastcall onRemove(void* self) {
    guard(onRemoveBefore,self);const auto result=reinterpret_cast<Unary>(g.original[profile::on_remove])(self);guard(onRemoveAfter,self);return result;
}
void beforePool(void* self) {
    if(!g.life.actor || num(self)!=g.life.actor) return;
    // DOC-64 N1: a foreign-thread PoolReturn of our Actor is only posted; the Sim TID invalidates at its next entry.
    if(!thread()) { block("POOL_RETURN_FOREIGN_THREAD");return; }
    const bool controlled=thread() && g.inTick && g.inSweep && !g.stopping && g.life.phase==Phase::Pending && g.life.removeAttempted;
    bool facts=false;
    if(controlled) facts=!read<Byte*>(self,0x10) && read<int>(ptr(g.life.node),0x40)==0;
    // Invalidate BEFORE trampoline: no further Actor read, including on the return path.
    g.life.poolReturn(num(self),controlled && facts);
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
        auto* n=ptr(g.life.retireNode);const bool empty=!read<Byte*>(n,0x28);const int refs=read<int>(n,0x40),inUse=read<Byte>(n,0x24);const auto occurrences=list(259,n).found;
        const bool ok=g.life.finishRetirement(empty,refs,inUse,static_cast<int>(occurrences));
        emit("deferred_retired","\"success\":%s,\"payload_null\":%s,\"refs\":%d,\"in_use\":%d,\"sector_occurrences\":%u,\"all_weak_pointers_null\":%s,\"actor_reads\":false,\"handle\":%llu",ok?"true":"false",empty?"true":"false",refs,inUse,occurrences,(!g.life.actor&&!g.life.node&&!g.life.retireNode)?"true":"false",g.handle);
        require(ok,"NODE_RETIREMENT_POSTCONDITION");
    }
    g.inSweep=false;
}
std::uintptr_t __fastcall sweep() {
    guard(beginSweep);
    issueArmedRemove(); // native Remove outside the observer SEH boundary; its own checks throw into block()
    const auto result=reinterpret_cast<NoArgs>(g.original[profile::sweep])();guard(endSweep);if(thread()) g.inSweep=false;return result;
}
void drainBefore(void*) {
    if(thread() && g.inTick && !g.stopping && g.life.phase==Phase::Pending && g.life.node)
        emit("deferred_drain_entry","\"refs\":%d,\"sector_occurrences\":%u,\"tick\":%llu",read<int>(ptr(g.life.node),0x40),list(259,ptr(g.life.node)).found,g.ticks);
}
void drainAfter(void*) {
    if(thread() && g.inTick && !g.stopping && g.life.phase==Phase::Pending && g.life.node) {
        const int refs=read<int>(ptr(g.life.node),0x40);const auto occurrences=list(259,ptr(g.life.node)).found;
        const bool sameTick=g.removeIssued && g.issueTick==g.ticks;
        if(sameTick && !g.drained && refs==0 && occurrences==0) g.drained=true;
        emit("deferred_drain_return","\"refs\":%d,\"sector_occurrences\":%u,\"tick\":%llu,\"same_tick_as_remove\":%s,\"queue_cell_drained\":%s",refs,occurrences,g.ticks,sameTick?"true":"false",g.drained?"true":"false");
    }
}
std::uintptr_t __fastcall drain(void* self) { guard(drainBefore);const auto result=reinterpret_cast<Unary>(g.original[profile::drain])(self);guard(drainAfter);return result; }

// ---- P3 engine-teardown witness (pass-through; simulation thread only) ----
struct Walk { bool ok{},present{};std::uintptr_t containing{};unsigned blocks{}; };
// Walks a tagged heap's block list (heap+8, next at block+0x30) while every listed block is allocated.
Walk walkHeapImpl(std::uintptr_t heap,std::uintptr_t block,std::uintptr_t node) {
    Walk w;std::uintptr_t b=read<std::uintptr_t>(ptr(heap),8);
    for(;b;++w.blocks) {
        if(w.blocks>=65536 || read<std::uint32_t>(ptr(b))!=wit::Magic) return w;
        if(b==block) w.present=true;
        if(node && !w.containing && wit::nodeInBlock(node,b)) {
            unsigned char raw[wit::RawHeader]{};std::memcpy(raw,ptr(b),sizeof(raw));
            if(wit::nodeBlockOk(wit::decode(raw),heap)) w.containing=b;
        }
        b=read<std::uintptr_t>(ptr(b),0x30);
    }
    w.ok=true;return w;
}
Walk walkHeap(std::uintptr_t heap,std::uintptr_t block,std::uintptr_t node) {
    __try { return walkHeapImpl(heap,block,node); } __except(EXCEPTION_EXECUTE_HANDLER) { return Walk{}; }
}
std::uintptr_t __fastcall levelUnload(void* a,void* b,void* c,void* d) {
    const bool sim=thread(),armed=sim && g.teardownArmed.load(std::memory_order_acquire);
    if(armed) { ++g.tlog.unloadEntries;g.tlog.barrierBeforeUnload=g.stopping; }
    if(sim) emit("p3_level_unload_entry","\"armed\":%s,\"barrier_latched\":%s,\"sector_array\":\"%p\",\"sector_count\":%llu,\"this_is_level\":%s",armed?"true":"false",g.stopping?"true":"false",read<void*>(g.base,0xa45968),read<std::uint64_t>(g.base,0xa45970),a==g.base+map56::LevelObject?"true":"false");
    const auto r=reinterpret_cast<Quad>(g.original3[profile3::level_unload])(a,b,c,d);
    if(armed) ++g.tlog.unloadReturns;
    if(sim) emit("p3_level_unload_return","\"sector_array\":\"%p\",\"sector_count\":%llu",read<void*>(g.base,0xa45968),read<std::uint64_t>(g.base,0xa45970));
    return r;
}
std::uintptr_t __fastcall arenaPurge(void* heap,void* b,void* c,void* d) {
    const auto h=num(heap);const bool actor=h==actorHeap(),node=h==nodeHeap();
    if((!actor && !node) || !thread()) return reinterpret_cast<Quad>(g.original3[profile3::arena_purge])(heap,b,c,d);
    if(!g.teardownArmed.load(std::memory_order_acquire)) {
        // Purge of an owned heap without our barrier while our Actor is live: engine destruction we did not attest.
        if(g.life.actor && !g.stopping) block(actor ? "P3_UNEXPECTED_ACTOR_HEAP_PURGE" : "P3_UNEXPECTED_NODE_HEAP_PURGE");
        ++g.unarmedPurges;emit("p3_arena_purge_unarmed","\"heap\":\"%s\",\"barrier_latched\":%s",actor?"af4490":"af4600",g.stopping?"true":"false");
        return reinterpret_cast<Quad>(g.original3[profile3::arena_purge])(heap,b,c,d);
    }
    Walk entry;
    if(actor) {
        ++g.tlog.actorPurgeEntries;g.tlog.unloadBeforeActorPurge=g.tlog.unloadReturns>0;
        entry=walkHeap(h,g.id.actorBlock,0);g.tlog.actorBlockAtEntry=entry.ok && entry.present;if(!entry.ok) ++g.tlog.violations;g.purge=1;
    } else {
        ++g.tlog.nodePurgeEntries;g.tlog.actorPurgeBeforeNodePurge=g.tlog.actorPurgeReturns>0;
        entry=walkHeap(h,0,g.id.node);g.id.nodeBlock=entry.containing;g.tlog.nodeBlockAtEntry=entry.ok && entry.containing;if(!entry.ok) ++g.tlog.violations;g.purge=2;
    }
    emit("p3_arena_purge_entry","\"heap\":\"%s\",\"blocks\":%u,\"walk_ok\":%s,\"own_block\":\"%p\",\"own_block_listed\":%s,\"after_unload_return\":%s",actor?"af4490":"af4600",entry.blocks,entry.ok?"true":"false",
        ptr(actor?g.id.actorBlock:g.id.nodeBlock),(actor?g.tlog.actorBlockAtEntry:g.tlog.nodeBlockAtEntry)?"true":"false",g.tlog.unloadReturns?"true":"false");
    const auto r=reinterpret_cast<Quad>(g.original3[profile3::arena_purge])(heap,b,c,d);
    g.purge=0;
    // Only blocks still listed (locked) are read here; ours, if absent, were unlinked and freed by the purge loop.
    const auto done=walkHeap(h,actor?g.id.actorBlock:g.id.nodeBlock,0);if(!done.ok) ++g.tlog.violations;
    if(actor) { ++g.tlog.actorPurgeReturns;g.tlog.actorBlockAbsentAtReturn=done.ok && !done.present; }
    else { ++g.tlog.nodePurgeReturns;g.tlog.nodeBlockAbsentAtReturn=done.ok && !done.present && g.id.nodeBlock; }
    emit("p3_arena_purge_return","\"heap\":\"%s\",\"blocks_left\":%u,\"walk_ok\":%s,\"own_block_absent\":%s",actor?"af4490":"af4600",done.blocks,done.ok?"true":"false",done.ok && !done.present?"true":"false");
    return r;
}
void dispatchWitness(void* block) {
    if(g.tlog.actorDispatched) { ++g.tlog.violations;emit("p3_violation","\"what\":\"second_dispatch_of_own_block\"");return; }
    g.tlog.actorDispatched=true;
    // The block stays allocated until c58b0 frees it after this dispatch returns (static, 58B.2 E5).
    unsigned char raw[wit::RawHeader]{};std::memcpy(raw,block,sizeof(raw));const auto h=wit::decode(raw);
    const bool vt=read<Byte*>(ptr(g.id.actor))==g.base+profile3::SlaveActorVtable;
    // c58b0 may zero the owner slot (+0x20) before dispatch; heap/size/dtor/seq are still the Spawn values.
    g.tlog.dispatchIdentityOk=num(block)==g.id.actorBlock && wit::actorBlockOk(h,actorHeap(),num(g.base+profile3::DtorThunk)) && h.allocSeq==g.id.allocSeq && vt;
    emit("p3_actor_dtor_dispatch","\"block\":\"%p\",\"alloc_seq\":%llu,\"alloc_seq_matches_spawn\":%s,\"vtable_kexSlaveActor\":%s,\"identity_ok\":%s,\"after_unload_return\":%s",block,h.allocSeq,h.allocSeq==g.id.allocSeq?"true":"false",vt?"true":"false",g.tlog.dispatchIdentityOk?"true":"false",g.tlog.unloadReturns?"true":"false");
}
std::uintptr_t __fastcall blockDispatch(void* block,void* user,void* c,void* d) {
    if(g.purge==1 && thread() && g.teardownArmed.load(std::memory_order_acquire) && g.id.actor && num(user)==g.id.actor) guard(dispatchWitness,block);
    return reinterpret_cast<Quad>(g.original3[profile3::block_dispatch])(block,user,c,d);
}
void unexpectedDtor(void*) { block("P3_UNEXPECTED_ACTOR_DTOR"); }
std::uintptr_t __fastcall actorDtor(void* self,void* b,void* c,void* d) {
    bool mine=false;
    if(thread()) {
        const auto s=num(self);
        if(g.teardownArmed.load(std::memory_order_acquire) && g.id.actor && s==g.id.actor) {
            mine=true;if(g.purge!=1 || g.tlog.dtorEntered) ++g.tlog.violations;g.tlog.dtorEntered=true;
            emit("p3_actor_dtor_entry","\"actor\":\"%p\",\"in_actor_purge\":%s",self,g.purge==1?"true":"false");
        } else if(!g.stopping && g.life.actor && s==g.life.actor) guard(unexpectedDtor);
    }
    const auto r=reinterpret_cast<Quad>(g.original3[profile3::actor_dtor])(self,b,c,d);
    if(mine) { g.tlog.dtorReturned=true;emit("p3_actor_dtor_return","\"actor_reads_after\":false"); }
    return r;
}
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
    for(std::size_t i=0;i<profile3::Count;++i) if(!prefix3(static_cast<profile3::Index>(i))) { block("P3_WITNESS_PREFIX");return false; }
    const std::pair<profile::Index,void*> hooks[]={
        {profile::puppet_tick,reinterpret_cast<void*>(puppetTick)},{profile::actor_tick,reinterpret_cast<void*>(actorTick)},
        {profile::on_remove,reinterpret_cast<void*>(onRemove)},{profile::pool_return,reinterpret_cast<void*>(poolReturn)},
        {profile::sweep,reinterpret_cast<void*>(sweep)},{profile::drain,reinterpret_cast<void*>(drain)}};
    for(const auto& [i,hook]:hooks) {
        if(MH_CreateHook(at(i),hook,&g.original[i])!=MH_OK || MH_EnableHook(at(i))!=MH_OK) { block("NATIVE_PROXY_HOOK_INSTALL");g.stopping=true;detach();return false; }
        g.enabled[i]=true;
    }
    const std::pair<profile3::Index,void*> witness[]={
        {profile3::level_unload,reinterpret_cast<void*>(levelUnload)},{profile3::arena_purge,reinterpret_cast<void*>(arenaPurge)},
        {profile3::block_dispatch,reinterpret_cast<void*>(blockDispatch)},{profile3::actor_dtor,reinterpret_cast<void*>(actorDtor)}};
    for(const auto& [i,hook]:witness) {
        if(MH_CreateHook(at3(i),hook,&g.original3[i])!=MH_OK || MH_EnableHook(at3(i))!=MH_OK) { block("P3_WITNESS_HOOK_INSTALL");g.stopping=true;detach();return false; }
        g.enabled3[i]=true;
    }
    g.installed=true;emit("native_proxy_installed","\"hooks\":10,\"proxy_hooks\":6,\"teardown_witness_hooks\":4,\"capacity\":1,\"sector\":259,\"new_injector\":false,\"external_callbacks\":false,\"module_pinned\":true,\"p3\":true");return true;
}
const seam::SeamTable& table() { return Table; }
void permitSpawn() { g.spawnPermit.store(1,std::memory_order_release); }
bool detach() {
    if(!thread()) return false;
    applyForeign();
    // Live/unknown identity can only be abandoned after the already latched engine teardown barrier.
    if(!g.stopping && g.life.phase!=Phase::Retired && g.life.phase!=Phase::Waiting) return false;
    bool ok=true;
    for(std::size_t n=0;n<profile::Count;++n) if(g.enabled[n]) {
        const auto i=static_cast<profile::Index>(n);const auto result=MH_DisableHook(at(i));const bool exact=prefix(i);ok&=result==MH_OK && exact;
        if(result==MH_OK) g.enabled[n]=false;
        emit("native_proxy_hook_restored","\"site\":\"%s\",\"rva\":%u,\"status\":%d,\"exact16\":%s",profile::Sites[n].name,profile::Sites[n].rva,result,exact?"true":"false");
    }
    for(std::size_t n=0;n<profile3::Count;++n) if(g.enabled3[n]) {
        const auto i=static_cast<profile3::Index>(n);const auto result=MH_DisableHook(at3(i));const bool exact=prefix3(i);ok&=result==MH_OK && exact;
        if(result==MH_OK) g.enabled3[n]=false;
        emit("native_proxy_hook_restored","\"site\":\"%s\",\"rva\":%u,\"status\":%d,\"exact16\":%s,\"p3_witness\":true",profile3::Sites[n].name,profile3::Sites[n].rva,result,exact?"true":"false");
    }
    if(!ok) block("NATIVE_PROXY_HOOK_RESTORE");return ok;
}
void fault() { g.inTick=false;block("SEAM_UNKNOWN_NATIVE_OUTCOME"); }
void exitBarrier(std::uint32_t reason) { if(thread() && !g.stopping) worldExit(reason); }
bool worldLost() { return thread() && g.lost; }
TeardownResult teardownVerdict(std::uint64_t world) {
    TeardownResult r;
    if(!thread()) return r;
    applyForeign();
    r.armed=g.tlog.armed;r.handle=g.handle;r.queue=g.queueAtBarrier;
    g.tlog.nativeCallsAfterBarrier=static_cast<std::uint32_t>(g.nativeCalls-g.callsAtBarrier);
    r.arena=r.armed ? wit::verdict(g.id,g.tlog) : EngineTeardownWitness::Unknown;
    const auto& t=g.tlog;
    emit("p3_teardown_verdict","\"world\":%llu,\"armed\":%s,\"handle\":%llu,\"arena\":\"%s\",\"queue\":\"%s\",\"barrier_before_unload\":%s,\"unload\":[%u,%u],\"actor_purge\":[%u,%u],\"node_purge\":[%u,%u],\"unload_before_actor_purge\":%s,\"actor_purge_before_node_purge\":%s,"
        "\"actor_block_at_entry\":%s,\"dispatched\":%s,\"dispatch_identity_ok\":%s,\"dtor\":[%s,%s],\"actor_block_absent_after\":%s,\"node_block\":\"%p\",\"node_block_at_entry\":%s,\"node_block_absent_after\":%s,\"violations\":%u,\"native_calls_after_barrier\":%u,\"unarmed_purges\":%llu",
        world,r.armed?"true":"false",r.handle,text(r.arena),text(r.queue),t.barrierBeforeUnload?"true":"false",t.unloadEntries,t.unloadReturns,t.actorPurgeEntries,t.actorPurgeReturns,t.nodePurgeEntries,t.nodePurgeReturns,
        t.unloadBeforeActorPurge?"true":"false",t.actorPurgeBeforeNodePurge?"true":"false",t.actorBlockAtEntry?"true":"false",t.actorDispatched?"true":"false",t.dispatchIdentityOk?"true":"false",
        t.dtorEntered?"true":"false",t.dtorReturned?"true":"false",t.actorBlockAbsentAtReturn?"true":"false",ptr(g.id.nodeBlock),t.nodeBlockAtEntry?"true":"false",t.nodeBlockAbsentAtReturn?"true":"false",t.violations,t.nativeCallsAfterBarrier,g.unarmedPurges);
    g.teardownArmed.store(false,std::memory_order_release);g.tlog={};g.purge=0;
    g.lost=false; // DOC-78 F3: a world lost before this exit must not latch the barrier on the next world's incarnation
    // Every block of this world (ours and any pooled earlier incarnation) was purged: no same-seq reuse is legal now.
    if(g.id.valid) g.prevId=g.id;
    g.prevPoolRetired=false;g.id={};g.worldPending=true;
    return r;
}
bool isPositionInterpolant(const void* candidate) {
    return thread() && g.installed && !g.stopping && !g.faulted && !g.controllerStopped && g.life.phase==Phase::Live &&
        g.life.actor && num(candidate)==g.life.actor+0x50;
}
DWORD observe(Observation* out,std::uint32_t size) {
    __try { return observeCpp(out,size); } __except(EXCEPTION_EXECUTE_HANDLER) { block("OBSERVATION_SEH");return ERROR_INVALID_STATE; }
}
}
