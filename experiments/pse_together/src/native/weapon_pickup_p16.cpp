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
#include <cstring>
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
// The host's own player (slot 0) gets the weapon here without the native consume 0x1d8360, so the stock pickup feedback never showed for the
// host (friend run 2026-11-12: slot 0 weapons 1 and 2 silent, the client saw its own). Same feedback as consume, minus everything that removes
// or persists the pickup: PlaySound(toucher, def+0xf8) when def+0x100 > 0, HUD +0x18 message Localize(def+0x110) when def+0x118 > 0, HUD +0x28
// pickup flash (0x1d8398 / 0x1d83c0..0x1d83cf / 0x1d8448..0x1d8456). Client bodies keep the silent grant (their own machine shows it).
void localNotice(void* pickup,void* puppet){
#ifndef PSE_P16_OFFLINE
    // The body of consume that is repeated here (its entry is hooked by world_sync on the host, so not the prefix): message from def+0x110 /
    // +0x118 through HUD vtable +0x18 (0x1d839d..0x1d83d1) and the pickup flash HUD +0x28 (0x1d8448..0x1d8459).
    constexpr std::array<Byte,0x34> Message{0x48,0x8b,0x97,0x70,0x01,0x00,0x00,0x48,0x83,0xba,0x18,0x01,0x00,0x00,0x00,0x76,0x23,0x48,0x8b,0x05,
        0x6b,0xba,0x86,0x00,0x48,0x8b,0x92,0x10,0x01,0x00,0x00,0x48,0x8b,0x58,0x18,0xe8,0xdb,0x0e,0xee,0xff,0x48,0x8b,0xd0,0x48,0x8d,0x0d,0x51,
        0xba,0x86,0x00,0xff,0xd3};
    constexpr std::array<Byte,0x11> Flash{0x48,0x8b,0x05,0xd1,0xb9,0x86,0x00,0x48,0x8d,0x0d,0xca,0xb9,0x86,0x00,0xff,0x50,0x28};
    if(!std::equal(Message.begin(),Message.end(),s.base+0x1d839d)||!std::equal(Flash.begin(),Flash.end(),s.base+0x1d8448)){event("p16_weapon_notice_skipped",0,0);return;}
    const auto* def=read<Byte*>(pickup,0x170);auto* hud=s.base+0xa43e20;auto* vt=read<Byte**>(hud);
    if(!def)return;
    if(vt&&(reinterpret_cast<Byte*>(vt)<s.base+0x482000||reinterpret_cast<Byte*>(vt)>=s.base+0x59f000))vt=nullptr; // HUD not built (.rdata)
    unsigned shown=0; // bits: 1 sound, 2 message, 4 flash (logged)
    if(read<std::uint64_t>(def,0x100)&&read<const char*>(def,0xf8)) {
        shown|=1;reinterpret_cast<void(__fastcall*)(void*,const char*,std::uint8_t)>(s.base+0x1d3b30)(puppet,read<const char*>(def,0xf8),0);
    }
    if(vt&&read<std::uint64_t>(def,0x118)&&read<const char*>(def,0x110)){
        const auto* text=reinterpret_cast<const char*(__fastcall*)(void*,const char*)>(s.base+0xb92a0)(nullptr,read<const char*>(def,0x110));
        reinterpret_cast<void(__fastcall*)(void*,const char*)>(vt[3])(hud,text);
        shown|=2|(text?static_cast<unsigned>(strnlen(text,255))<<8:0u); // + message length
    }
    if(vt) {reinterpret_cast<void(__fastcall*)(void*)>(vt[5])(hud);shown|=4;}
    event("p16_weapon_local_notice",0,shown);
#else
    (void)pickup;(void)puppet;
#endif
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
    if(recipient.slot==0)localNotice(pickup,puppet);
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
