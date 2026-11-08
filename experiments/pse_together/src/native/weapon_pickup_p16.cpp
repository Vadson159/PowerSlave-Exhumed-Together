// Exact SHA b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d.
// Saved evidence G_FUNC_1401d8470 and P7/static_native_selected/G_FUNC_1401e06d0.
// Preferred VA / RVA / file offset respectively:
// OnTouch: 0x1401d8470 / 0x1d8470 / 0x1d7870.
// GiveWeapon: 0x1401e06d0 / 0x1e06d0 / 0x1dfad0.
// Instruction-derived Win64 ABI: (pickup RCX, Puppet RDX),
// GiveWeapon(Player RCX, slot EDX, select R8B)->AL. No ammo operation.
#include "weapon_pickup_p16.hpp"
#include <algorithm>
#include <array>
#ifndef PSE_P16_OFFLINE
#include <windows.h>
#include <MinHook.h>
#endif
namespace pse::weapon_p16 {
namespace {
using Byte=unsigned char;
#ifndef PSE_P16_OFFLINE
using Touch=void(__fastcall*)(void*,void*);
using Give=std::uint8_t(__fastcall*)(void*,int,std::uint8_t);
#endif
constexpr std::array<Byte,16> TouchPrefix{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0xb9,0x70,0x01,0x00};
constexpr std::array<Byte,16> GivePrefix{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
struct State {Host host{};Touch original{};Give give{};Byte* base{};std::uint32_t tid{};bool installed{},busy{},faulted{},stopped{};} s;
template<class T>T read(const void* p,std::size_t off=0){return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+off);}
bool thread(){
#ifdef PSE_P16_OFFLINE
    return true;
#else
    return GetCurrentThreadId()==s.tid;
#endif
}
void event(const char* name,unsigned slot,unsigned value){if(s.host.event)s.host.event(name,slot,value);}
bool graph(void* actor,void* owner){
    if(!actor||!owner)return false;
    const auto* node=read<Byte*>(owner,0xd8);
    return node&&read<void*>(node,0x28)==actor&&read<void*>(actor,0x10)==node&&read<void*>(actor,0x1b8)==owner
#ifndef PSE_P16_OFFLINE
        &&read<void*>(actor)==s.base+0x52d198
#endif
        ;
}
void touch(void* pickup,void* puppet){
    // Every unowned/inactive call retains stock behavior, including after
    // disconnect/block/stop. Never repeat P14's frozen-host fallback defect.
    if(!thread()||s.stopped||!s.host.eligible||!s.host.eligible()) {s.original(pickup,puppet);return;}
    const auto roster=s.host.roster();Player recipient{};
    for(const auto& p:roster)if(p.puppet==puppet&&p.player){recipient=p;break;}
    if(!recipient.player){s.original(pickup,puppet);return;}
    if(!recipient.canCollect)return;
    if(s.busy||s.faulted)return;
    if(!graph(puppet,recipient.player)||!pickup||read<std::int16_t>(puppet,0x17a)<=0)return;
    const auto* node=read<Byte*>(pickup,0x10);const auto* def=read<Byte*>(pickup,0x170);
    if(!node||!def||read<void*>(node,0x28)!=pickup||(read<Byte>(pickup,0x40)&&read<int>(node,0x40)<1))return;
    // Exact native OnTouch loads weaponSlotToGive from definition+128.
    const auto weapon=read<int>(def,0x128);
    if(weapon<1||weapon>=8){event("p16_weapon_definition_rejected",recipient.slot,0);return;}
    if(read<Byte>(recipient.player,0xcc+weapon))return;
    std::array<std::uint16_t,8> ammo{};
    const auto removedBefore=read<Byte>(pickup,0x40);const auto* definitionBefore=def;
    for(unsigned i=0;i<8;++i)ammo[i]=read<std::uint16_t>(recipient.player,0xbc+i*2);
    s.busy=true;const auto given=s.give(recipient.player,weapon,1);s.busy=false;
    if(!given||!graph(puppet,recipient.player)||read<Byte>(recipient.player,0xcc+weapon)!=1){
        s.faulted=true;event("p16_weapon_grant_failed",recipient.slot,weapon);return;
    }
    for(unsigned i=0;i<8;++i)if(read<std::uint16_t>(recipient.player,0xbc+i*2)!=ammo[i]){
        s.faulted=true;event("p16_weapon_ammo_changed",recipient.slot,weapon);return;
    }
    const bool persistent=read<void*>(pickup,0x10)==node&&read<void*>(node,0x28)==pickup&&
        read<void*>(pickup,0x170)==definitionBefore&&read<Byte>(pickup,0x40)==removedBefore;
    if(!persistent){s.faulted=true;event("p16_weapon_lifetime_changed",recipient.slot,weapon);return;}
    // Deliberately do not call pickup-consume (1d8360) or original OnTouch:
    // the original weapon stays in the world, with untouched persistence,
    // refs and deletion state. Native owned[] prevents repeated same-life grants.
    event("p16_weapon_only_granted",recipient.slot,weapon);
    if(s.host.receipt)s.host.receipt({recipient.slot,static_cast<unsigned>(weapon),recipient.player,puppet,pickup,const_cast<Byte*>(node),ammo,persistent});
}
}
bool install(Byte* image,std::uint32_t tid,Host host){
#ifdef PSE_P16_OFFLINE
    (void)image;(void)tid;(void)host;return false;
#else
    if(s.installed||s.stopped||!image||image!=reinterpret_cast<Byte*>(GetModuleHandleW(nullptr))||
       GetCurrentThreadId()!=tid||!host.eligible||!host.roster||!host.event)return false;
    if(!std::equal(TouchPrefix.begin(),TouchPrefix.end(),image+0x1d8470)||
       !std::equal(GivePrefix.begin(),GivePrefix.end(),image+0x1e06d0))return false;
    s.base=image;s.tid=tid;s.host=host;s.give=reinterpret_cast<Give>(image+0x1e06d0);
    if(MH_CreateHook(image+0x1d8470,reinterpret_cast<void*>(&touch),reinterpret_cast<void**>(&s.original))!=MH_OK)return false;
    if(MH_EnableHook(image+0x1d8470)!=MH_OK)return false;
    s.installed=true;event("p16_weapon_hook_installed",0,0);return true;
#endif
}
bool stop(){
#ifdef PSE_P16_OFFLINE
    s.stopped=true;return true;
#else
    if(!s.installed){s.stopped=true;return true;}
    if(!thread())return false;
    s.stopped=true;
    if(MH_DisableHook(s.base+0x1d8470)!=MH_OK)return false;
    const bool exact=std::equal(TouchPrefix.begin(),TouchPrefix.end(),s.base+0x1d8470);
    event("p16_weapon_hook_restore",0,exact?1u:0u);s.installed=false;return exact;
#endif
}
#ifdef PSE_P16_OFFLINE
void bindForTest(Host host,Touch original,Give give){s={};s.host=host;s.original=original;s.give=give;}
void touchForTest(void* pickup,void* puppet){touch(pickup,puppet);}
#endif
}
