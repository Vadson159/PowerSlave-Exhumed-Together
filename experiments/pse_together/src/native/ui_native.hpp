#pragma once
#include <windows.h>
#include <cstdint>
namespace pse::native_ui {
using Logger=void(*)(const char*,const char*,...);
bool install(unsigned char* base,DWORD simulationTid,bool host,Logger log);
void permit();
void enableInterface();
void worldExit();
void tick(bool ready,std::uint32_t epoch=0,std::uint8_t local=255,std::uint32_t generation=0);
// Host: what the lobby beacon says (map name, players in the game, true once the host is in a level and accepts connections). Sim thread.
void lobbyInfo(const char* map,unsigned players,bool open);
void stop();
struct Observation {
    std::uint32_t abi=0x50230001,size=sizeof(Observation);
    std::uint64_t presents{},overlays{},arrows{},markers{},menus{},submitted{},chats{},restores{},errors{};
    std::uint32_t renderTid{},simulationTid{},ready{},requested{};
};
DWORD observe(Observation* out,std::uint32_t size);
}
