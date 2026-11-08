// P3 teardown-witness sites. RVAs and 16-byte prefixes are the exact-build values verified against both test-copy EXE
// files in DOC-58B.2 (docs/evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/PRE_NATIVE_GATE.json, observer_site_prefix_*),
// where a passive observer hooked the same four sites. The P3 pre-native gate re-checks them by file bytes; install()
// re-checks them in memory before MinHook. Hooks are pass-through: no argument or return value is changed.
#pragma once
#include <cstdint>
#include <cstddef>
namespace pse::native_proxy_p3::profile3 {
struct Site { const char* name; std::uint32_t rva; unsigned char prefix[16]; };
enum Index : std::size_t { level_unload, arena_purge, block_dispatch, actor_dtor, Count };
inline constexpr Site Sites[] = {
    {"level_unload", 0x1c90e0, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57}},
    {"arena_purge", 0xc58b0, {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x20,0x44,0x89,0x44,0x24,0x18,0x48}},
    {"block_dispatch", 0xc5310, {0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x83,0x79,0x40,0x00,0x48}},
    {"actor_dtor", 0x19f210, {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8d,0x05,0xe8,0x93,0x38,0x00}},
};
// Static data (VERIFIED static, DOC-58B.2): tagged heaps purged by StateExit and the Actor dtor thunk.
inline constexpr std::uint32_t ActorHeap=0xaf4490, NodeHeap=0xaf4600, DtorThunk=0xd9980, SlaveActorVtable=0x528608;
}
