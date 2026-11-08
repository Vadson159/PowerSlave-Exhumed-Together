// P16 persistent weapon-only pickup seam. Native acceptance is separate.
#pragma once
#include <array>
#include <cstdint>
namespace pse::weapon_p16 {
struct Player {void* player{};void* puppet{};unsigned slot{};bool canCollect=true;};
using Roster=std::array<Player,8>;
struct Grant {unsigned slot{},weapon{};void* player{};void* puppet{};void* pickup{};void* node{};
    std::array<std::uint16_t,8> ammo{};bool persistent{};};
struct Host {
    bool(*eligible)();
    Roster(*roster)();
    void(*event)(const char*,unsigned,unsigned);
    void(*receipt)(const Grant&)=nullptr;
};
bool install(unsigned char* image,std::uint32_t tid,Host host);
bool stop(); // exact16 restore, pinned code/trampoline
#ifdef PSE_P16_OFFLINE
using Touch=void(*)(void*,void*);
using Give=std::uint8_t(*)(void*,int,std::uint8_t);
void bindForTest(Host host,Touch original,Give give);
void touchForTest(void* pickup,void* puppet);
#endif
}
