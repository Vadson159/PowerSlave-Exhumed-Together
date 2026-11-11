#pragma once
#include <array>
#include <cstdint>

// Exact-build experiment; callbacks are only used on the attested simulation TID.
namespace pse::p14 {
struct Member { void* player{}; void* actor{}; unsigned slot{}; };
using Roster = std::array<Member, 8>;
struct Host {
    bool (*eligible)();
    Roster (*roster)(); // current validated bindings; never cache engine pointers
    bool (*getter)(bool enable);
    void (*event)(const char* name, unsigned slot, unsigned value);
    void (*fault)(const char* reason);
    int (*heldWeapon)(unsigned slot); // weapon the player of a remote slot holds in its own game (0..7), -1 unknown; may be null
};
bool install(unsigned char* base, std::uint32_t tid, Host host);
void stop(); // disable + exact16 restore; trampolines/storage remain pinned
void sync(); // alive recipients only, missing bits only
void allowRemoteTouch(bool on); // health/ammo touches of remote bodies (slot>0) pass only while on (a client's pickup request)
void* scopedPlayer(std::uint32_t tid, const void* returnAddress);
#ifdef PSE_P14_OFFLINE
// Compiled only into the offline executable. No DLL exports or runtime control API.
struct OfflineNative {
    void* original[5];
    unsigned char (*see)(void*,void*);
    float (*angle)(void*,float);
    unsigned char (*give)(void*,unsigned);
    void (*consume)(void*,void*);
    int (*difficulty)(void*);
};
void offlineBind(unsigned char* base,std::uint32_t tid,Host host,OfflineNative native);
void offlineInvoke(unsigned kind,void* object,void* toucher=nullptr);
#endif
}
