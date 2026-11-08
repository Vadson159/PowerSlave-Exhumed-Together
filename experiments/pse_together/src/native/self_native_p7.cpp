// Exact EXE b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d.
// Addresses below are RVAs, not absolute VAs; raw offsets are RVA-0xc00.
// Evidence: DOC-03/05/80, ghidra_2022/{1b5b80,1b5ce0,1a1370,1dbd00,
// 19f2d0,194ac0}, m0_static/{20f9e0,210000}, P7/static_native_selected/*.
// Signatures are instruction-derived Win64 contracts; runtime acceptance is
// deliberately not implied by compilation. No game structure is cloned.
#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include "self_native_p7.hpp"

namespace pse::self_native_p7 {
namespace {
using Byte=unsigned char;
using Unary=void(__fastcall*)(void*);
using BaseTick=std::uint64_t(__fastcall*)(void*);
using Damage=void(__fastcall*)(void*,void*,int,std::int16_t);
using Reference=std::int16_t*(__fastcall*)(void*);
using Touch=void(__fastcall*)(void*,void*);
using Pack=void(__fastcall*)(void*,void*);
using Health=Byte(__fastcall*)(void*,int);
using Difficulty=int(__fastcall*)(void*);
using Ready=void(__fastcall*)(void*,int);
using Definition=void*(__fastcall*)(void*,const int*);
using Animation=void*(__fastcall*)(void*,const char*);
using Quad=std::uintptr_t(__fastcall*)(void*,void*,void*,void*);
using Binary=std::uintptr_t(__fastcall*)(void*,void*);
struct Site {const char* name;std::uint32_t rva;std::array<Byte,16> prefix;};
enum Index : unsigned {DamageEntry,BaseEntry,HealthReference,WeaponTouch,HealthTouch,AmmoTouch,
    AnkhTouch,KeyTouch,ArtifactTouch,QuestTouch,DollTouch,LevelUnload,ActorDtor,PoolReturn,HookCount};
constexpr Site sites[]={
 {"InflictDamage",0x1a1370,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57}},
 {"BaseActorTick",0x19f2d0,{0x40,0x55,0x53,0x48,0x8d,0x6c,0x24,0xa8,0x48,0x81,0xec,0x58,0x01,0x00,0x00,0x80}},
 {"MutableActorHealth",0x194ac0,{0x48,0x8d,0x81,0x7a,0x01,0x00,0x00,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
 {"WeaponOnTouch",0x1d8470,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00}},
 {"HealthOnTouch",0x1d8770,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00}},
 {"AmmoOnTouch",0x1d8590,{0x40,0x56,0x41,0x56,0x48,0x83,0xec,0x28,0x48,0x83,0xb9,0x70,0x01,0x00,0x00,0x00}},
 {"AnkhOnTouch",0x1d8870,{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00}},
 {"KeyOnTouch",0x1d8a00,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00}},
 {"ArtifactOnTouch",0x1d8ac0,{0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00}},
 {"QuestOnTouch",0x1d8dc0,{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00}},
 {"TeamDollOnTouch",0x1d8f30,{0x40,0x55,0x41,0x56,0x48,0x83,0xec,0x28,0x48,0x83,0xb9,0x70,0x01,0x00,0x00,0x00}},
 {"LevelUnload",0x1c90e0,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57}},
 {"ActorDtor",0x19f210,{0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8d,0x05,0xe8,0x93,0x38,0x00}},
 {"ActorPoolReturn",0x1d5000,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x02,0x48,0x8b,0xca}},
 {"ScalarPack",0x1b5b80,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb7,0x81,0xfc,0x00,0x00}},
 {"ScalarRestore",0x1b5ce0,{0x0f,0xb6,0x42,0x1c,0x88,0x81,0x0c,0x01,0x00,0x00,0x0f,0xb7,0x42,0x0c,0x66,0x89}},
 {"MutableKeys",0x1ae0c0,{0x48,0x8d,0x81,0x98,0x00,0x00,0x00,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
 {"GiveHealth",0x1e0850,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48}},
 {"Difficulty",0x1ae6a0,{0x8b,0x81,0xd8,0x01,0x00,0x00,0xc3,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc}},
 {"WeaponReady",0x20f9e0,{0x48,0x89,0x5c,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x8b,0xfa,0x48}},
 {"WeaponAdvance",0x210000,{0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x6c,0x24,0x20,0x56,0x57,0x41,0x54,0x41,0x55}},
 {"WeaponDefinition",0x1bb560,{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0x39,0x00,0x48,0x8b}},
 {"AnimationLookup",0x19ed10,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x48}},
};
static_assert(HookCount==14 && std::size(sites)==23);
struct State {
    Byte* base{};std::uint32_t tid{};Journal journal{};Binding binding{};
    void* original[HookCount]{};bool created[HookCount]{},enabled[HookCount]{};
    bool installed{},stopped{},faulted{},inTick{},authority{},busy{},hostEvent{},restored{},keys{};
    bool lifeSeen{},wasDead{};std::uint32_t deathSequence{};
    std::uint64_t applies{},damage{},pickups{},shadows{},probeSerial{},reviewSerial{};
    KeysProbe probe{};Binding probeBinding{};
    std::atomic<std::uint64_t> requestedProbe{0},requestedReview{0};
    std::atomic<std::uintptr_t> publishedActor{0};
    std::atomic<bool> publishedAuthority{false};
    std::atomic<unsigned> foreignOwnCalls{0};
    std::atomic<bool> barrier{false};
    Binding acceptedBinding{};std::uint64_t allocationSequence{};
    bool identityEstablished{},hadAuthority{},leaseLost{},graphFailureLogged{};
} s;
thread_local std::int16_t healthShadow{}; // own DLL storage, never engine memory

template<class T> T load(const void* p,std::size_t offset=0) noexcept {
    return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+offset);
}
template<class T> bool safeLoad(const void* p,std::size_t offset,T& out) noexcept {
    __try {out=load<T>(p,offset);return true;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void event(const char* name,const char* fmt="",...) noexcept {
    if(!s.journal)return;
    char detail[2400]{};va_list a;va_start(a,fmt);vsnprintf_s(detail,sizeof(detail),_TRUNCATE,fmt,a);va_end(a);
    s.journal(name,detail);
}
bool thread() noexcept {return s.tid && GetCurrentThreadId()==s.tid;}
void fail(const char* reason) noexcept {
    s.faulted=true;s.authority=false;s.keys=false;s.hostEvent=false;
    s.publishedAuthority.store(false,std::memory_order_release);
    event("p7_native_fault","\"reason\":\"%s\",\"tick\":%llu,\"native_retry\":false",reason,s.binding.tick);
}
bool sameIncarnation(const Binding& a,const Binding& b) noexcept {
    return a.game==b.game && a.player==b.player && a.node==b.node && a.actor==b.actor &&
        a.mapEpoch==b.mapEpoch && a.entityGeneration==b.entityGeneration;
}
bool sameNativeIdentity(const Binding& a,const Binding& b) noexcept {
    return a.game==b.game && a.player==b.player && a.node==b.node && a.actor==b.actor &&
        a.entityGeneration==b.entityGeneration;
}
bool graphBody() noexcept {
    const auto& b=s.binding;
    if(!s.installed || s.stopped || s.faulted || s.barrier.load(std::memory_order_acquire) || !thread() || !s.inTick || !b.gameplay || !b.outsideUnloadBarrier ||
        !b.tick || !b.mapEpoch || !b.entityGeneration || !b.node || !b.actor ||
        b.game!=reinterpret_cast<std::uintptr_t>(s.base+0xaf33a0) ||
        b.player!=reinterpret_cast<std::uintptr_t>(s.base+0xaf33e0))return false;
    const auto* g=reinterpret_cast<const Byte*>(b.game);
    const auto* p=reinterpret_cast<const Byte*>(b.player);
    const auto* n=reinterpret_cast<const Byte*>(b.node);
    const auto* a=reinterpret_cast<const Byte*>(b.actor);
    // DOC-58B.2/DOC-78: tagged heap payload-60, allocation sequence+8.
    // Scalar reads only. G_FUNC_1401db790 at this exact SHA sets Puppet
    // header+40 to LAB_1401e1000 (via DAT_140b30240), +48=1, +50=1e0.
    // Actor1007's d9980 destructor thunk is a different class contract.
    const auto* header=a-0x60;
    if(load<std::uint32_t>(header)!=0x3151983u || load<std::uint64_t>(header,0x10)!=0x1e0 ||
        load<const void*>(header,0x18)!=s.base+0xaf4490 || load<const void*>(header,0x40)!=s.base+0x1e1000 ||
        load<std::uint64_t>(header,0x48)!=1 || load<std::uint64_t>(header,0x50)!=0x1e0 ||
        load<Byte>(header,0x38) || !load<std::uint64_t>(header,8))return false;
    if(s.identityEstablished && (!sameIncarnation(b,s.acceptedBinding) || load<std::uint64_t>(header,8)!=s.allocationSequence))return false;
    return load<void*>(g)==s.base+0x52ad08 && load<int>(g,0x24)==3 && load<int>(g,0x28)==0 &&
        load<void*>(s.base+0x52ad08,0x158)==s.base+0x1ae6a0 &&
        load<const void*>(p,0xd8)==n && load<const void*>(n,0x28)==a &&
        load<const void*>(a,0x10)==n && load<const void*>(a,0x1b8)==p &&
        load<void*>(a)==s.base+0x52d198 && (!load<Byte>(a,0x40) || load<int>(n,0x40)>0);
}
bool graph() noexcept {
    __try {return graphBody();} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void graphFailureDiagnostic() noexcept {
    if(!thread() || s.graphFailureLogged)return;
    s.graphFailureLogged=true;
    const auto& b=s.binding;unsigned reads{},failed{};
    std::uint32_t magic{},actorFlags{},puppetFlags{};std::uint64_t allocation{},size{},ctorCount{},ctorSize{};
    std::uintptr_t heap{},dtor{},giVtable{},difficultyTarget{},playerNode{},nodeActor{},actorNode{},actorOwner{},actorVtable{};
    Byte locked{},actorRefMode{};int state{},pending{},nodeRefs{};
    const auto take=[&]<class T>(std::uintptr_t address,std::size_t offset,T& value) {
        if(address && safeLoad(reinterpret_cast<const void*>(address),offset,value))++reads;else ++failed;
    };
    const auto header=b.actor>=0x60?b.actor-0x60:0;
    take(header,0,magic);take(header,8,allocation);take(header,0x10,size);take(header,0x18,heap);
    take(header,0x38,locked);take(header,0x40,dtor);take(header,0x48,ctorCount);take(header,0x50,ctorSize);
    take(b.game,0,giVtable);take(b.game,0x24,state);take(b.game,0x28,pending);take(giVtable,0x158,difficultyTarget);
    take(b.player,0xd8,playerNode);take(b.node,0x28,nodeActor);take(b.node,0x40,nodeRefs);
    take(b.actor,0x10,actorNode);take(b.actor,0x1b8,actorOwner);take(b.actor,0,actorVtable);
    take(b.actor,0x40,actorRefMode);take(b.actor,0x16c,actorFlags);take(b.actor,0x1c0,puppetFlags);
    event("p7_native_graph_failure_readonly",
        "\"tick\":%llu,\"reads_ok\":%u,\"reads_failed\":%u,\"game\":\"%p\",\"player\":\"%p\",\"node\":\"%p\",\"actor\":\"%p\",\"header\":\"%p\","
        "\"magic\":%u,\"allocation_sequence\":%llu,\"payload_size\":%llu,\"heap\":\"%p\",\"locked\":%u,\"dtor\":\"%p\",\"ctor_count\":%llu,\"ctor_size\":%llu,"
        "\"gi_vtable\":\"%p\",\"difficulty_target\":\"%p\",\"state\":%d,\"pending\":%d,\"player_node\":\"%p\",\"node_actor\":\"%p\",\"actor_node\":\"%p\",\"actor_owner\":\"%p\",\"actor_vtable\":\"%p\","
        "\"node_refs\":%d,\"actor_ref_mode\":%u,\"actor_flags\":%u,\"puppet_flags\":%u,\"map_epoch\":%u,\"entity_generation\":%u,\"gameplay\":%s,\"outside_unload_barrier\":%s,\"installed\":%s,\"stopped\":%s,\"faulted\":%s,\"in_tick\":%s,\"barrier\":%s,\"identity_established\":%s,\"native_calls\":0,\"writes\":0",
        b.tick,reads,failed,reinterpret_cast<void*>(b.game),reinterpret_cast<void*>(b.player),reinterpret_cast<void*>(b.node),reinterpret_cast<void*>(b.actor),reinterpret_cast<void*>(header),
        magic,allocation,size,reinterpret_cast<void*>(heap),static_cast<unsigned>(locked),reinterpret_cast<void*>(dtor),ctorCount,ctorSize,
        reinterpret_cast<void*>(giVtable),reinterpret_cast<void*>(difficultyTarget),state,pending,reinterpret_cast<void*>(playerNode),reinterpret_cast<void*>(nodeActor),reinterpret_cast<void*>(actorNode),reinterpret_cast<void*>(actorOwner),reinterpret_cast<void*>(actorVtable),
        nodeRefs,static_cast<unsigned>(actorRefMode),actorFlags,puppetFlags,b.mapEpoch,b.entityGeneration,b.gameplay?"true":"false",b.outsideUnloadBarrier?"true":"false",
        s.installed?"true":"false",s.stopped?"true":"false",s.faulted?"true":"false",s.inTick?"true":"false",s.barrier.load(std::memory_order_acquire)?"true":"false",s.identityEstablished?"true":"false");
}
bool activeOwn(const void* actor) noexcept {
    // Receiver equality comes before any dereference of an intercepted argument.
    if(!thread() || !s.authority || !s.inTick || reinterpret_cast<std::uintptr_t>(actor)!=s.binding.actor)return false;
    if(!graph()) {fail("active_binding_lost");return true;} // refuse the own mutation even on loss
    return true;
}
bool foreignOwn(const void* actor) noexcept {
    // Wrong-thread interception compares atomic weak identities only. Never
    // validate/dereference engine objects or call a native own-mutator there.
    if(thread() || !s.publishedAuthority.load(std::memory_order_acquire) ||
        s.publishedActor.load(std::memory_order_acquire)!=reinterpret_cast<std::uintptr_t>(actor))return false;
    s.foreignOwnCalls.fetch_add(1,std::memory_order_relaxed);return true;
}
bool invokePack(std::uint32_t rva,void* buffer) noexcept {
    __try {reinterpret_cast<Pack>(s.base+rva)(reinterpret_cast<void*>(s.binding.game),buffer);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool invokeReady(int phase) noexcept {
    __try {reinterpret_cast<Ready>(s.base+0x20f9e0)(reinterpret_cast<void*>(s.binding.player+0xe0),phase);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool invokeAdvance() noexcept {
    __try {reinterpret_cast<Unary>(s.base+0x210000)(reinterpret_cast<void*>(s.binding.player+0xe0));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool invokeHealth(int amount) noexcept {
    __try {return reinterpret_cast<Health>(s.base+0x1e0850)(reinterpret_cast<void*>(s.binding.player),amount)!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool invokeDamage(std::int16_t amount) noexcept {
    __try {reinterpret_cast<Damage>(s.base+0x1a1370)(reinterpret_cast<void*>(s.binding.actor),nullptr,0,amount);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
std::int16_t* keyReference() noexcept {
    __try {return reinterpret_cast<Reference>(s.base+0x1ae0c0)(reinterpret_cast<void*>(s.binding.player));}
    __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
bool keyIdentity(std::int16_t* ref) noexcept {
    return ref && reinterpret_cast<std::uintptr_t>(ref)==s.binding.player+0x98 && graph();
}
bool readBody(self_p7::Values& v) noexcept {
    if(!graph()) {graphFailureDiagnostic();return false;}
    const auto* p=reinterpret_cast<const Byte*>(s.binding.player);
    const auto* a=reinterpret_cast<const Byte*>(s.binding.actor);
    const int current=load<int>(p,0x88),pending=load<int>(p,0x8c);
    if(current<0 || current>=8 || pending<0 || pending>=8)return false;
    v.health=load<std::int16_t>(a,0x17a);v.owned=0;
    for(unsigned i=0;i<8;++i) {
        v.ammo[i]=load<std::uint16_t>(p,0xbc+2*i);
        const Byte owned=load<Byte>(p,0xcc+i);if(owned>1)return false;
        if(owned)v.owned=static_cast<Byte>(v.owned|(1u<<i));
    }
    v.weapon=static_cast<Byte>(current);
    v.pendingWeapon=pending==current?self_p7::NoPendingWeapon:static_cast<Byte>(pending);
    auto* ref=keyReference();if(!keyIdentity(ref))return false;
    v.keys=static_cast<std::uint16_t>(*ref);
    v.artifacts=load<std::uint32_t>(p,0x90);v.abilities=load<std::uint32_t>(p,0x94);
    v.questItems=load<std::uint16_t>(p,0x9a);v.ankhCount=load<std::uint16_t>(p,0x84);
    v.ankhMask=load<std::uint16_t>(p,0x86);v.teamDolls=load<std::uint32_t>(p,0xa4);
    const bool dead=(load<std::uint32_t>(a,0x1c0)&0x200u)!=0;
    if(dead!=(v.health<=0))return false;
    if(dead && (!s.lifeSeen || !s.wasDead)) {
        if(s.deathSequence==UINT32_MAX)return false;
        ++s.deathSequence;
    }
    s.lifeSeen=true;s.wasDead=dead;v.life=dead?self_p7::Life::Dead:self_p7::Life::Alive;
    v.deathSequence=s.deathSequence;
    return self_p7::valid(v) && graph();
}
bool readProtected(self_p7::Values& v) noexcept {
    __try {return readBody(v);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// This is an opaque, native-produced 48-byte scalar record, not an engine object.
// Native pack includes current +194 at bytes44..47; we leave those bytes intact.
struct Scalars {alignas(4) std::array<Byte,48> bytes{};};
void put(Scalars& b,unsigned offset,std::uint32_t value,unsigned count) noexcept {
    for(unsigned i=0;i<count;++i)b.bytes[offset+i]=static_cast<Byte>(value>>(8*i));
}
bool scalarRestore(const self_p7::Values& desired,unsigned pending) noexcept {
    Scalars b;if(!graph() || !invokePack(0x1b5b80,b.bytes.data()) || !graph())return false;
    put(b,0,desired.ankhCount,2);put(b,2,desired.ankhMask,2);put(b,4,desired.artifacts,2);
    put(b,6,desired.questItems,2);put(b,8,desired.abilities,2);
    // zero is the native "no pending health" sentinel. Health applies below
    // through native damage/GiveHealth; no delayed next-tick overwrite remains.
    put(b,10,0,2);
    for(unsigned i=0;i<8;++i) {put(b,12+2*i,desired.ammo[i],2);put(b,28+i,(desired.owned>>i)&1u,1);}
    put(b,36,desired.teamDolls,4);put(b,40,pending,4);
    return invokePack(0x1b5ce0,b.bytes.data()) && graph();
}
void* definition(unsigned weapon) noexcept {
    __try {
        const int key=static_cast<int>(weapon);auto* manager=load<void*>(s.base+0xaf3330);
        if(!manager)return nullptr;
        auto* node=reinterpret_cast<Definition>(s.base+0x1bb560)(manager,&key);
        return node?static_cast<Byte*>(node)+8:nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
void* animation(void* definitionView,unsigned nameOffset) noexcept {
    __try {
        const auto* name=load<const char*>(definitionView,nameOffset);
        if(!name)return nullptr;
        unsigned length=0;while(length<256 && name[length])++length;
        if(!length || length==256)return nullptr;
        return reinterpret_cast<Animation>(s.base+0x19ed10)(nullptr,name);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return nullptr;}
}
bool harmlessAction(const void* action) noexcept {
    if(!action)return false;
    const auto* vt=load<const Byte*>(action);
    // RegisterActions19d5a0 -> constructors198b70/198fb0 -> these exact
    // vtables. Run198c60 only plays sound; Run1990a0 stops looping sound.
    // RegisterActions -> ctor199510 -> vt528358 -> Run199600 -> native
    // StopLoopingTactile1d39d0; the saved P7 dossiers prove this is tactile
    // presentation cleanup. It does not award progress or execute weapon fire.
    // No script action, ammo consumption, projectile, RNG, damage or Steam.
    if(vt==s.base+0x528400)return load<const void*>(vt,0x20)==s.base+0x1990a0;
    if(vt==s.base+0x528358)return load<const void*>(vt,0x20)==s.base+0x199600;
    if(vt!=s.base+0x528438 || load<const void*>(vt,0x20)!=s.base+0x198c60)return false;
    const auto* params=load<const void*>(action,0x18);
    return params && load<std::uint32_t>(action,0x20)>0 && load<const void*>(params)!=nullptr;
}
bool safeAnimationBody(const void* anim,bool whole) noexcept {
    if(!anim)return false;
    const unsigned count=load<unsigned>(anim,0x24);const auto* frames=load<const Byte*>(anim,0x18);
    if(!frames || !count || count>256)return false;
    for(unsigned f=0;f<(whole?count:1u);++f) {
        const auto* frame=frames+0x110*f;const unsigned delay=load<std::uint16_t>(frame);
        // Stock bomb_idle_blank/lower_blank contain zero-delay frames. Native
        // 210000 explicitly advances them and updates Actor+130. Preserve that
        // native path; the bounded action/branch checks below still apply.
        if(delay>256)return false;
        const unsigned actions=load<unsigned>(frame,0x14);const auto* array=load<const Byte*>(frame,8);
        if(actions>4 || (actions && !array))return false;
        for(unsigned j=0;j<actions;++j)if(!harmlessAction(load<const void*>(array,8*j))) {
            const auto* action=load<const void*>(array,8*j);
            event("p7_switch_action_refused","\"frame\":%u,\"action_index\":%u,\"vtable\":\"%p\"",f,j,action?load<const void*>(action):nullptr);
            return false;
        }
        // 210000 follows alternate animation names. During forced holster all
        // transitions must stay inside the admitted native phase machine.
        if(whole)for(unsigned off:{0x20u,0x38u}) {
            const auto* next=load<const char*>(frame,off);
            if(!next || next[0]!='-' || next[1]!=0)return false;
        }
    }
    return true;
}
bool safeAnimation(const void* anim,bool whole) noexcept {
    __try {return safeAnimationBody(anim,whole);} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool switchPreflight(unsigned current,unsigned desired) noexcept {
    if(current==desired)return true;
    const auto* p=reinterpret_cast<const Byte*>(s.binding.player);
    const auto* a=reinterpret_cast<const Byte*>(s.binding.actor);
    if((load<unsigned>(p)&1u) || (load<unsigned>(a,0x1c0)&0x200u))return false;
    auto* from=definition(current);auto* to=definition(desired);
    if(!from || !to || !graph())return false;
    // Underwater weapon routing has additional gameplay branches; this adapter
    // admits ordinary dry switching only and refuses the unsupported branch.
    if((load<unsigned>(a,0x16c)&0x400u) && !(load<unsigned>(a,0x1c0)&0x10u))return false;
    return safeAnimation(animation(from,0x30),true) && safeAnimation(animation(from,0x48),false) &&
        safeAnimation(animation(to,0x18),false) && safeAnimation(animation(to,0x48),false) && graph();
}
bool switchWeapon(unsigned desired) noexcept {
    const auto* p=reinterpret_cast<const Byte*>(s.binding.player);
    if(load<unsigned>(p,0x88)==desired)return true;
    if(!invokeReady(2) || !graph() || load<unsigned>(p,0xf0)!=2)return false;
    // At most 256 frames*256 ticks, but stock lowers are <100 ticks. Bound
    // work to 4096 native steps and stop immediately at the assignment branch.
    for(unsigned n=0;n<4096 && load<unsigned>(p,0x88)!=desired;++n) {
        if(load<unsigned>(p,0xf0)!=2 || !invokeAdvance() || !graph())return false;
    }
    if(load<unsigned>(p,0x88)!=desired)return false;
    // Idle uses only the separately admitted sound/no-action first frame.
    return invokeReady(0) && graph() && load<unsigned>(p,0x88)==desired;
}
void __fastcall damage(void* actor,void* instigator,int kind,std::int16_t amount) {
    if(foreignOwn(actor))return;
    if(activeOwn(actor) && !s.hostEvent) {
        ++s.damage;event("p7_damage_suppressed","\"tick\":%llu,\"kind\":%d,\"amount\":%d",s.binding.tick,kind,static_cast<int>(amount));return;
    }
    reinterpret_cast<Damage>(s.original[DamageEntry])(actor,instigator,kind,amount);
}
std::uint64_t __fastcall baseTick(void* actor) {
    if(foreignOwn(actor))return 0;
    if(activeOwn(actor)) {
        if(s.faulted || (load<unsigned>(actor,0x16c)&0x1000u)) {
            fail("unsupported_direct_health_flag1000");return 0;
        }
    }
    return reinterpret_cast<BaseTick>(s.original[BaseEntry])(actor);
}
std::int16_t* __fastcall healthReference(void* actor) {
    if(foreignOwn(actor)) {healthShadow=0;return &healthShadow;}
    if(activeOwn(actor) && !s.hostEvent) {
        if(!s.faulted)healthShadow=load<std::int16_t>(actor,0x17a);
        ++s.shadows;return &healthShadow;
    }
    return reinterpret_cast<Reference>(s.original[HealthReference])(actor);
}
void touch(unsigned index,void* pickup,void* actor) {
    if(foreignOwn(actor))return;
    if(activeOwn(actor)) {
        ++s.pickups;event("p7_pickup_suppressed","\"tick\":%llu,\"site\":\"%s\"",s.binding.tick,sites[index].name);return;
    }
    reinterpret_cast<Touch>(s.original[index])(pickup,actor);
}
void __fastcall weaponTouch(void* p,void* a) {touch(WeaponTouch,p,a);}
void __fastcall healthTouch(void* p,void* a) {touch(HealthTouch,p,a);}
void __fastcall ammoTouch(void* p,void* a) {touch(AmmoTouch,p,a);}
void __fastcall ankhTouch(void* p,void* a) {touch(AnkhTouch,p,a);}
void __fastcall keyTouch(void* p,void* a) {touch(KeyTouch,p,a);}
void __fastcall artifactTouch(void* p,void* a) {touch(ArtifactTouch,p,a);}
void __fastcall questTouch(void* p,void* a) {touch(QuestTouch,p,a);}
void __fastcall dollTouch(void* p,void* a) {touch(DollTouch,p,a);}
std::uintptr_t __fastcall levelUnload(void* a,void* b,void* c,void* d) {
    // Initial level setup can call Unload before a primary binding exists.
    // After attestation, latch before any original sector/arena destruction.
    if(s.publishedActor.load(std::memory_order_acquire))invalidate();
    return reinterpret_cast<Quad>(s.original[LevelUnload])(a,b,c,d);
}
std::uintptr_t __fastcall actorDtor(void* a,void* b,void* c,void* d) {
    if(s.publishedActor.load(std::memory_order_acquire)==reinterpret_cast<std::uintptr_t>(a) && a)invalidate();
    return reinterpret_cast<Quad>(s.original[ActorDtor])(a,b,c,d);
}
std::uintptr_t __fastcall poolReturn(void* factory,void* a) {
    if(s.publishedActor.load(std::memory_order_acquire)==reinterpret_cast<std::uintptr_t>(a) && a)invalidate();
    return reinterpret_cast<Binary>(s.original[PoolReturn])(factory,a);
}

bool fileHash() noexcept {
    constexpr std::array<Byte,32> expected={0xb8,0xf1,0x31,0x03,0x71,0x87,0xca,0x8d,0x0a,0x28,0x25,0x27,0xfb,0x6f,0x55,0xa2,
        0x01,0x22,0x41,0xd5,0xb9,0xda,0xc8,0xbf,0x20,0x38,0xba,0xf3,0x5b,0x71,0xa7,0x6d};
    wchar_t path[32768]{};const DWORD length=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    if(!length || length>=std::size(path))return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm{};BCRYPT_HASH_HANDLE hash{};
    std::array<Byte,4096> object{};std::array<Byte,65536> buffer{};std::array<Byte,32> digest{};
    DWORD objectSize{},received{};bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&received,0)>=0 && objectSize<=object.size();
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    while(ok) {
        DWORD count{};if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr)){ok=false;break;}
        if(!count)break;
        if(BCryptHashData(hash,buffer.data(),count,0)<0)ok=false;
    }
    if(ok)ok=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0 && digest==expected;
    if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);CloseHandle(file);
    return ok;
}
bool applyBody(const self_p7::Values& before,const self_p7::Values& desired) noexcept {
    if(!graph() || !s.authority || s.busy || !self_p7::valid(before) || !self_p7::valid(desired))return false;
    self_p7::Values actual;if(!readProtected(actual) || actual!=before)return false;
    auto* a=reinterpret_cast<Byte*>(s.binding.actor);auto* p=reinterpret_cast<Byte*>(s.binding.player);
    if(load<unsigned>(a,0x16c)&0x1000u)return false;
    if(before.life==self_p7::Life::Dead && desired.life!=self_p7::Life::Dead)return false;
    if(desired.deathSequence<before.deathSequence || (before.life==self_p7::Life::Alive && desired.life==self_p7::Life::Dead && desired.deathSequence<=before.deathSequence))return false;
    if(before.keys!=desired.keys && !s.keys)return false;
    if(desired.life==self_p7::Life::Dead && desired.weapon!=before.weapon)return false;
    if(!switchPreflight(before.weapon,desired.weapon)) {
        event("p7_switch_gate_refused","\"current\":%u,\"desired\":%u,\"buttons\":%u,\"actor_flags\":%u,\"puppet_flags\":%u",before.weapon,desired.weapon,load<unsigned>(p),load<unsigned>(a,0x16c),load<unsigned>(a,0x1c0));return false;
    }
    const int difference=static_cast<int>(desired.health)-static_cast<int>(before.health);
    if(difference<0 && ((load<unsigned>(a,0x1c0)&0x400u) || !(load<unsigned>(a,0x16c)&0x20u) || -difference>INT16_MAX))return false;
    if(before.life==self_p7::Life::Dead && difference!=0)return false;
    const int difficulty=reinterpret_cast<Difficulty>(s.base+0x1ae6a0)(reinterpret_cast<void*>(s.binding.game));
    const int cap=(static_cast<int>(desired.ankhCount)+1)*((difficulty==1 || difficulty==2)?100:200);
    // GiveHealth refuses to add when current>=cap; future pending-health direct
    // writers are not used as a workaround. Refuse impossible positive targets.
    if(difference>0 && (before.health>=cap || desired.health>cap))return false;
    s.busy=true;s.hostEvent=true;
    const unsigned finalPending=desired.pendingWeapon==self_p7::NoPendingWeapon?desired.weapon:desired.pendingWeapon;
    const unsigned firstPending=before.weapon==desired.weapon?finalPending:desired.weapon;
    bool ok=scalarRestore(desired,firstPending);
    if(ok && before.weapon!=desired.weapon)ok=switchWeapon(desired.weapon) && scalarRestore(desired,finalPending);
    if(ok && difference>0)ok=invokeHealth(difference*((difficulty==1 || difficulty==2)?2:1)) && graph();
    if(ok && difference<0)ok=invokeDamage(static_cast<std::int16_t>(-difference)) && graph();
    if(ok && before.keys!=desired.keys) {
        auto* ref=keyReference();ok=s.keys && keyIdentity(ref);
        if(ok) {*ref=static_cast<std::int16_t>(desired.keys);ok=keyIdentity(ref) && static_cast<std::uint16_t>(*ref)==desired.keys;}
    }
    // Native death may holster but must neither synthesize a revival nor be
    // silently retried when god/animation conditions refuse the event.
    if(ok)ok=load<std::int16_t>(a,0x17a)==desired.health &&
        ((load<unsigned>(a,0x1c0)&0x200u)!=0)==(desired.life==self_p7::Life::Dead) &&
        load<std::int16_t>(p,0x190)==0;
    if(ok) {s.deathSequence=desired.deathSequence;s.lifeSeen=true;s.wasDead=desired.life==self_p7::Life::Dead;}
    self_p7::Values after;if(ok)ok=readProtected(after) && after==desired;
    s.hostEvent=false;s.busy=false;
    if(ok) {++s.applies;event("p7_native_applied","\"tick\":%llu,\"health\":%d,\"weapon\":%u,\"keys\":%u,\"death_sequence\":%u",s.binding.tick,static_cast<int>(after.health),static_cast<unsigned>(after.weapon),static_cast<unsigned>(after.keys),after.deathSequence);}
    return ok;
}
} // namespace

bool install(Byte* image,std::uint32_t simulationTid,Journal journal) noexcept {
    if(s.installed || s.stopped || !image || image!=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr)) || !simulationTid || GetCurrentThreadId()!=simulationTid)return false;
    s.base=image;s.tid=simulationTid;s.journal=journal;
    if(!fileHash()) {fail("exe_sha256_mismatch");return false;}
    for(const auto& site:sites)if(!std::equal(site.prefix.begin(),site.prefix.end(),image+site.rva)) {
        event("p7_native_prefix_mismatch","\"site\":\"%s\",\"rva\":%u",site.name,site.rva);fail("exact_prefix");return false;
    }
    void* detours[]={reinterpret_cast<void*>(&damage),reinterpret_cast<void*>(&baseTick),reinterpret_cast<void*>(&healthReference),
        reinterpret_cast<void*>(&weaponTouch),reinterpret_cast<void*>(&healthTouch),reinterpret_cast<void*>(&ammoTouch),
        reinterpret_cast<void*>(&ankhTouch),reinterpret_cast<void*>(&keyTouch),reinterpret_cast<void*>(&artifactTouch),
        reinterpret_cast<void*>(&questTouch),reinterpret_cast<void*>(&dollTouch),reinterpret_cast<void*>(&levelUnload),
        reinterpret_cast<void*>(&actorDtor),reinterpret_cast<void*>(&poolReturn)};
    s.installed=true;
    for(unsigned i=0;i<HookCount;++i) {
        auto* address=image+sites[i].rva;
        if(MH_CreateHook(address,detours[i],&s.original[i])!=MH_OK) {fail("minhook_create");shutdown();return false;}
        s.created[i]=true;
        if(MH_EnableHook(address)!=MH_OK) {fail("minhook_enable");shutdown();return false;}
        s.enabled[i]=true;
    }
    event("p7_native_installed","\"hooks\":%u,\"sha256_verified\":true,\"keys_write\":false",HookCount);return true;
}
bool tick(const Binding& binding,bool authorityActive) noexcept {
    if(!thread() || !s.installed || s.stopped || s.faulted)return false;
    s.publishedAuthority.store(false,std::memory_order_release);
    if(s.barrier.load(std::memory_order_acquire))return false;
    if(s.foreignOwnCalls.load(std::memory_order_acquire)) {fail("foreign_thread_own_native_call");return false;}
    if(s.identityEstablished) {
        if(!sameNativeIdentity(binding,s.acceptedBinding)) {fail("native_incarnation_changed_no_rearm");return false;}
        if(binding.mapEpoch!=s.acceptedBinding.mapEpoch) {
            if(s.probe.serial || s.hadAuthority || s.keys) {fail("epoch_changed_after_native_attestation");return false;}
            s.acceptedBinding.mapEpoch=binding.mapEpoch; // bootstrap epoch -> authenticated first epoch only
        }
    }
    if(binding.tick<s.binding.tick) {fail("tick_regressed");return false;}
    if(s.hadAuthority && !authorityActive) {
        // A lost authority lease cannot inherit a prior read/review permit.
        s.keys=false;s.probe={};s.reviewSerial=0;s.leaseLost=true;
        s.requestedProbe.store(0,std::memory_order_release);s.requestedReview.store(0,std::memory_order_release);
    }
    if(authorityActive && s.leaseLost) {fail("authority_lease_lost_no_rearm");return false;}
    s.binding=binding;s.inTick=true;s.authority=false;
    if(!graph()) {graphFailureDiagnostic();return false;}
    if(!s.identityEstablished) {
        s.acceptedBinding=binding;s.allocationSequence=load<std::uint64_t>(reinterpret_cast<void*>(binding.actor-0x60),8);
        s.identityEstablished=true;
        event("p7_native_incarnation","\"actor\":\"%p\",\"node\":\"%p\",\"allocation_sequence\":%llu,\"entity_generation\":%u,\"rearm\":false",reinterpret_cast<void*>(binding.actor),reinterpret_cast<void*>(binding.node),s.allocationSequence,binding.entityGeneration);
    }
    unsigned flags{};if(!safeLoad(reinterpret_cast<void*>(binding.actor),0x16c,flags))return false;
    if(flags&0x1000u) {fail("unsupported_direct_health_flag1000");return false;}
    const auto review=s.requestedReview.load(std::memory_order_acquire);
    const auto probe=s.requestedProbe.load(std::memory_order_acquire);
    if(review && review==s.requestedReview.load(std::memory_order_acquire) && review>s.reviewSerial &&
        probe && probe==s.probe.serial && s.probe.referenceIdentity && sameIncarnation(s.probeBinding,binding)) {
        auto* ref=keyReference();
        if(!keyIdentity(ref)) {fail("keys_permit_reference_changed");return false;}
        s.keys=true;s.reviewSerial=review;
        event("p7_keys_permit_consumed","\"probe_serial\":%llu,\"independent_review_serial\":%llu,\"tick\":%llu",probe,review,binding.tick);
    }
    s.authority=authorityActive;s.hadAuthority|=authorityActive;s.publishedActor.store(binding.actor,std::memory_order_release);
    s.publishedAuthority.store(authorityActive,std::memory_order_release);return true;
}
void leaveTick() noexcept {if(thread()) {s.publishedAuthority.store(false,std::memory_order_release);s.inTick=false;s.authority=false;s.hostEvent=false;}}
void invalidate() noexcept {
    s.barrier.store(true,std::memory_order_release);s.publishedAuthority.store(false,std::memory_order_release);
    s.requestedProbe.store(0,std::memory_order_release);s.requestedReview.store(0,std::memory_order_release);
    if(!thread())return;
    s.authority=false;s.inTick=false;s.hostEvent=false;s.keys=false;s.probe={};s.reviewSerial=0;s.leaseLost=true;
    event("p7_native_invalidated","\"tick\":%llu,\"native_calls\":0,\"rearm\":false",s.binding.tick);
}
bool read(self_p7::Values& values) noexcept {
    if(!thread() || !graph())return false;
    self_p7::Values result;if(!readProtected(result))return false;values=result;return true;
}
bool apply(const self_p7::Values& before,const self_p7::Values& desired) noexcept {
    if(!thread())return false;
    bool ok{};
    __try {ok=applyBody(before,desired);} __except(EXCEPTION_EXECUTE_HANDLER) {ok=false;}
    s.hostEvent=false;s.busy=false;if(!ok && thread() && s.authority)fail("native_apply_or_postcondition");return ok;
}
bool probeKeys(KeysProbe& probe) noexcept {
    if(!thread() || !graph() || s.probeSerial==UINT64_MAX)return false;
    auto* ref=keyReference();std::int16_t value{};
    if(!keyIdentity(ref) || !safeLoad(ref,0,value) || value<0 || value>15 || !graph())return false;
    KeysProbe result;result.serial=++s.probeSerial;result.tick=s.binding.tick;
    result.mapEpoch=s.binding.mapEpoch;result.entityGeneration=s.binding.entityGeneration;
    result.value=static_cast<std::uint16_t>(value);result.referenceIdentity=true;
    s.probe=result;s.probeBinding=s.binding;probe=result;
    event("p7_keys_read_probe","\"serial\":%llu,\"tick\":%llu,\"map_epoch\":%u,\"entity_generation\":%u,\"player\":\"%p\",\"node\":\"%p\",\"actor\":\"%p\",\"reference\":\"%p\",\"expected_reference\":\"%p\",\"value\":%u,\"write\":false",
        result.serial,result.tick,result.mapEpoch,result.entityGeneration,reinterpret_cast<void*>(s.binding.player),reinterpret_cast<void*>(s.binding.node),reinterpret_cast<void*>(s.binding.actor),ref,reinterpret_cast<void*>(s.binding.player+0x98),static_cast<unsigned>(result.value));return true;
}
void permitKeys(std::uint64_t probeSerial,std::uint64_t independentReviewSerial) noexcept {
    if(!probeSerial || !independentReviewSerial)return;
    s.requestedReview.store(0,std::memory_order_release);s.requestedProbe.store(probeSerial,std::memory_order_release);
    s.requestedReview.store(independentReviewSerial,std::memory_order_release);
}
self_p7::NativeCapabilities capabilities() noexcept {
    const bool ready=thread() && s.installed && !s.stopped && !s.faulted;
    return {ready,ready,ready,ready&&s.keys,ready,ready};
}
Observation observe() noexcept {
    if(!thread())return {};
    return {s.binding.tick,s.probe.serial,s.reviewSerial,s.applies,s.damage,s.pickups,s.shadows,
        s.binding.mapEpoch,s.binding.entityGeneration,s.deathSequence,s.installed,graph(),s.authority,s.keys,s.faulted,s.restored};
}
bool shutdown() noexcept {
    if(!thread() || !s.base)return false;
    s.publishedAuthority.store(false,std::memory_order_release);
    s.authority=false;s.inTick=false;s.keys=false;s.hostEvent=false;s.stopped=true;bool all=true;
    for(unsigned i=0;i<HookCount;++i)if(s.created[i]) {
        auto* address=s.base+sites[i].rva;bool ok=true;
        if(s.enabled[i])ok=MH_DisableHook(address)==MH_OK;
        ok=ok && std::equal(sites[i].prefix.begin(),sites[i].prefix.end(),address);
        if(ok)s.enabled[i]=false;
        event("p7_native_hook_restore","\"site\":\"%s\",\"exact16\":%s,\"storage_pinned\":true",sites[i].name,ok?"true":"false");all=all&&ok;
    }
    s.restored=all;if(!all)fail("hook_exact_restore");return all;
}
} // namespace pse::self_native_p7
