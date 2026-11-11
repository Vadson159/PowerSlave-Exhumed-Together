// P14 candidate. Existing dossiers establish call sites, not runtime acceptance.
#include <windows.h>
#include <MinHook.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include "gameplay_p14.hpp"
#include "gameplay_policy_p14.hpp"
#include "gameplay_profile_p14.hpp"

namespace pse::p14 {
namespace {
using Byte=unsigned char;
using Unary=void(__fastcall*)(void*);
using Touch=void(__fastcall*)(void*,void*);
using Give=unsigned char(__fastcall*)(void*,unsigned);
using See=unsigned char(__fastcall*)(void*,void*);
using Angle=float(__fastcall*)(void*,float);
using Difficulty=int(__fastcall*)(void*);
struct State {
    Byte* base{}; std::uint32_t tid{}; Host host{};
    void* original[5]{}; bool enabled[5]{};
    bool stopped{},busy{},armed{},remoteTouch{}; unsigned team{};
    void* scoped{}; std::uintptr_t returnAddress{};
    See see{}; Angle angle{}; Give give{}; Touch consume{};
    Difficulty difficulty{};
} s;
template<class T> T read(const void* p, std::size_t n=0) {
    return *reinterpret_cast<const T*>(static_cast<const Byte*>(p)+n);
}
bool thread() { return GetCurrentThreadId()==s.tid; }
bool active() {
    if (!thread() || s.stopped || !s.host.eligible()) return false;
    s.armed=true; return true;
}
void fail(const char* reason) { s.stopped=true; s.scoped=nullptr; s.returnAddress=0; s.host.fault(reason); }
bool alive(const Member& m) {
    return m.player && m.actor && read<std::int16_t>(m.actor,0x17a)>0 &&
           !(read<unsigned>(m.actor,0x1c0)&0x200);
}
bool progress(const Member& m) {
    return validProgress(read<std::uint16_t>(m.player,0x86),read<std::int16_t>(m.player,0x84));
}
struct Busy {
    Busy() { s.busy=true; }
    ~Busy() { s.busy=false; }
};
struct Scope {
    bool ok{};
    Scope(void* player, unsigned returnRva) {
        if (!s.scoped && s.host.getter(true)) {
            s.scoped=player; s.returnAddress=reinterpret_cast<std::uintptr_t>(s.base)+returnRva; ok=true;
        }
    }
    ~Scope() {
        if (ok) {
            s.scoped=nullptr; s.returnAddress=0;
            if (!s.host.getter(false)) fail("p14_getter_restore");
        }
    }
};
Member owner(void* actor, const Roster& roster) {
    // Match against validated native bindings BEFORE dereferencing a toucher.
    for (const auto& m:roster) if (m.actor==actor && alive(m)) return m;
    return {};
}
Member select(void* ai) {
    if (read<Byte>(s.base+0xaeb11d)) return {}; // native no-target flag
    const auto roster=s.host.roster(); std::array<Candidate,8> candidates{};
    for (unsigned i=0;i<roster.size();++i) {
        const auto& m=roster[i];
        if (alive(m)) candidates[i]={true,read<float>(m.actor,0x18),read<float>(m.actor,0x1c)};
    }
    const float x=read<float>(ai,0x18), y=read<float>(ai,0x1c);
    const float yaw=read<float>(ai,0x38), limit=read<float>(s.base+0x53580c);
    if (!std::isfinite(yaw) || !std::isfinite(limit) || limit<0) return {};
    const auto chosen=nearest(candidates,x,y,read<float>(ai,0x1cc),[&](unsigned i) {
        if (!(read<unsigned>(ai,0x1d8)&2)) {
            const float bearing=std::atan2(candidates[i].x-x,candidates[i].y-y);
            const float delta=s.angle(static_cast<Byte*>(ai)+0x38,bearing);
            if (!std::isfinite(delta) || std::abs(delta)>limit) return false;
        }
        return s.see(ai,roster[i].actor)!=0;
    });
    return chosen<0 ? Member{} : roster[static_cast<unsigned>(chosen)];
}
void acquire(void* ai, unsigned index, unsigned returnRva) {
    const auto m=select(ai);
    if (!m.player) { s.host.event("p14_no_visible_player",8,index); return; }
    Scope scope(m.player,returnRva);
    if (!scope.ok) { fail("p14_ai_scope"); return; }
    s.host.event("p14_ai_choice",m.slot,index);
    // Original function owns target refs, alert, animation and chase transitions.
    reinterpret_cast<Unary>(s.original[index])(ai);
}
void __fastcall idle(void* ai) {
    if (!active()) { reinterpret_cast<Unary>(s.original[0])(ai); return; } // no eligible P2: stock single-player AI, never freeze
    if (s.busy) return;
    Busy busy;
    if (read<void*>(ai,0x28)) { reinterpret_cast<Unary>(s.original[0])(ai); return; }
    acquire(ai,0,0x1955f4);
}
void __fastcall chase(void* ai) {
    if (!active()) { reinterpret_cast<Unary>(s.original[1])(ai); return; }
    if (s.busy) return;
    Busy busy;
    const auto node=read<void*>(ai,0x28);
    if (node && read<void*>(node,0x28)) { reinterpret_cast<Unary>(s.original[1])(ai); return; }
    // No visible candidate: wait for a later tick, never enter singleton/assert path.
    acquire(ai,1,0x1972ca);
}
void touch(void* pickup,void* actor,unsigned index,unsigned returnRva) {
    if (!active()) { reinterpret_cast<Touch>(s.original[index])(pickup,actor); return; }
    if (s.busy) return;
    Busy busy;
    const auto m=owner(actor,s.host.roster());
    if (!m.player) { s.host.event("p14_touch_rejected",8,index); return; }
    // A remote player's body takes a sphere only when its player touched it in its own game and asked (world_sync takePickup -> allowRemoteTouch):
    // the body is only re-placed to that player's pose here, so whether it touched a sphere was chance, and the player saw something else.
    if (m.slot && !s.remoteTouch) return;
    Scope scope(m.player,returnRva);
    if (!scope.ok) { fail("p14_touch_scope"); return; }
    s.host.event("p14_touch_owner",m.slot,index);
    // Ammo sphere: native OnTouch gives ammo to the weapon in Player+0x8c (pending). A remote player's body here switches by itself off an empty
    // weapon (empty pistol -> machete, which takes no ammo), while that player still holds the empty weapon in its own game: the sphere then gave
    // nothing and stayed under it. For the sphere the body's weapon is the one its player holds; the field is put back right after.
    int* pending=index==3 && m.slot && s.host.heldWeapon ? reinterpret_cast<int*>(static_cast<Byte*>(m.player)+0x8c) : nullptr;
    const int held=pending ? s.host.heldWeapon(m.slot) : -1, before=pending ? *pending : -1;
    const bool swap=held>0 && held<8 && held!=before && read<Byte>(m.player,0xcc+static_cast<unsigned>(held))==1;
    if (swap) *pending=held;
    reinterpret_cast<Touch>(s.original[index])(pickup,actor);
    if (swap && *pending==held) *pending=before;
    if (swap) s.host.event("p14_ammo_for_held_weapon",m.slot,static_cast<unsigned>(held));
    // Native OnTouch may have consumed pickup. Do not read it here.
}
void __fastcall health(void* pickup,void* actor) { touch(pickup,actor,2,0x1d8807); }
void __fastcall ammo(void* pickup,void* actor) { touch(pickup,actor,3,0x1d8638); }
bool apply(const Roster& roster,unsigned desired) {
    if (read<void*>(s.base+0xaf33a0)!=s.base+0x52ad08 ||
        read<void*>(s.base+0x52ad08,0x158)!=s.base+profile14::DifficultyRva) {
        fail("p14_difficulty_virtual_target"); return false;
    }
    for (const auto& m:roster) if (m.player && !progress(m)) { fail("p14_ankh_progress_invalid"); return false; }
    for (const auto& m:roster) {
        if (!alive(m)) continue; // helper would otherwise resurrect a dead Puppet
        for (unsigned bit=1;bit<=0x10;bit<<=1) {
            const unsigned mask=read<std::uint16_t>(m.player,0x86);
            if (!(desired&bit) || (mask&bit)) continue;
            const auto count=read<std::int16_t>(m.player,0x84);
            const auto difficulty=s.difficulty(s.base+0xaf33a0);
            const int expectedHealth=(count+2)*((difficulty==1 || difficulty==2) ? 100 : 200);
            const auto ok=s.give(m.player,bit);
            if (!ok || read<std::uint16_t>(m.player,0x86)!=(mask|bit) ||
                read<std::int16_t>(m.player,0x84)!=count+1 || read<std::int16_t>(m.actor,0x17a)!=expectedHealth) {
                fail("p14_ankh_helper_postcondition"); return false;
            }
            s.team|=bit; // commit observed grant; never retry a failed helper
            s.host.event("p14_ankh_granted",m.slot,bit);
        }
    }
    return true;
}
void __fastcall ankh(void* pickup,void* actor) {
    // Never execute original OnTouch: it calls Steam on the fifth Ankh (D4 absent).
    if (!active() || s.busy) return;
    Busy busy;
    const auto roster=s.host.roster(); const auto m=owner(actor,roster);
    if (!m.player || !pickup || !(read<unsigned>(pickup,0x16c)&0x100)) return;
    const auto def=read<void*>(pickup,0x170); if (!def) return;
    const auto node=read<void*>(pickup,0x10);
    if (!node || read<void*>(node,0x28)!=pickup ||
        (read<Byte>(pickup,0x40) && read<int>(node,0x40)<1)) return;
    const int bit=read<int>(def,0x144);
    if (bit<0 || bit>4) { fail("p14_ankh_definition"); return; }
    unsigned desired=s.team|(1u<<bit);
    for (const auto& recipient:roster) if (recipient.player) {
        if (!progress(recipient)) { fail("p14_ankh_progress_invalid"); return; }
        desired|=read<std::uint16_t>(recipient.player,0x86);
    }
    if (!apply(roster,desired)) return;
    s.team=desired;
    s.host.event("p14_ankh_consumed",m.slot,1u<<bit);
    // The stock pickup operation performs its own native persistence/removal.
    // No adapter Remove, refs, flags or stale-pointer reads after this call.
    s.consume(pickup,actor);
}
}
void allowRemoteTouch(bool on) { if (thread()) s.remoteTouch=on; }
void* scopedPlayer(std::uint32_t tid,const void* returnAddress) {
    return allowGetter(s.tid,tid,s.returnAddress,reinterpret_cast<std::uintptr_t>(returnAddress),!s.stopped && s.scoped!=nullptr)
        ? s.scoped : nullptr;
}
bool install(Byte* base,std::uint32_t tid,Host host) {
    s.base=base; s.tid=tid; s.host=host;
    s.see=reinterpret_cast<See>(base+0x195750); s.angle=reinterpret_cast<Angle>(base+0xce5a0);
    s.give=reinterpret_cast<Give>(base+0x1e0910); s.consume=reinterpret_cast<Touch>(base+0x1d8360);
    if (!thread()) return false;
    for (unsigned i=0;i<std::size(profile14::Sites);++i) {
        const auto& site=profile14::Sites[i];
        if (!std::equal(std::begin(site.prefix),std::end(site.prefix),base+site.rva)) {
            s.host.event("p14_profile_mismatch",i,site.rva); return false;
        }
    }
    // APC runs before C++ engine globals initialize; validate GI virtual target in apply().
    s.difficulty=reinterpret_cast<Difficulty>(base+profile14::DifficultyRva);
    void* hooks[]={reinterpret_cast<void*>(&idle),reinterpret_cast<void*>(&chase),reinterpret_cast<void*>(&health),
                  reinterpret_cast<void*>(&ammo),reinterpret_cast<void*>(&ankh)};
    for (unsigned i=0;i<5;++i) {
        auto address=base+profile14::Sites[i].rva;
        if (MH_CreateHook(address,hooks[i],&s.original[i])!=MH_OK || MH_EnableHook(address)!=MH_OK) {
            s.host.event("p14_minhook_failure",i,profile14::Sites[i].rva);
            stop(); return false;
        }
        s.enabled[i]=true;
    }
    return true;
}
void sync() {
    if (!active() || s.busy) return;
    Busy busy; const auto roster=s.host.roster(); unsigned desired=s.team;
    for (const auto& m:roster) if (m.player) {
        if (!progress(m)) { fail("p14_ankh_progress_invalid"); return; }
        desired|=read<std::uint16_t>(m.player,0x86);
    }
    if (apply(roster,desired)) s.team=desired;
}
void stop() {
    if (!thread()) { fail("p14_stop_thread"); return; }
    s.stopped=true;
    s.scoped=nullptr; s.returnAddress=0;
    for (unsigned i=0;i<5;++i) if (s.enabled[i]) {
        auto address=s.base+profile14::Sites[i].rva;
        const bool ok=MH_DisableHook(address)==MH_OK &&
            std::equal(std::begin(profile14::Sites[i].prefix),std::end(profile14::Sites[i].prefix),address);
        s.enabled[i]=!ok;
        s.host.event("p14_hook_restore",i,ok?1u:0u);
        if (!ok) fail("p14_exact_restore");
    }
}
#ifdef PSE_P14_OFFLINE
void offlineBind(Byte* base,std::uint32_t tid,Host host,OfflineNative native) {
    s=State{}; s.base=base; s.tid=tid; s.host=host;
    std::copy(std::begin(native.original),std::end(native.original),std::begin(s.original));
    s.see=native.see; s.angle=native.angle; s.give=native.give; s.consume=native.consume; s.difficulty=native.difficulty;
}
void offlineInvoke(unsigned kind,void* object,void* toucher) {
    switch(kind) {
    case 0: idle(object); break;
    case 1: chase(object); break;
    case 2: health(object,toucher); break;
    case 3: ammo(object,toucher); break;
    case 4: ankh(object,toucher); break;
    }
}
#endif
}
