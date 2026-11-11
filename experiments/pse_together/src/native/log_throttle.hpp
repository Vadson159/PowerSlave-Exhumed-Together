#pragma once
// The journal is a diagnostic tool, not a gameplay cost: per-tick events (snapshots, input samples, proxy updates ...) used to be written and
// flushed to disk one by one, hundreds of lines per second with a connected player (the host SSD sat at 100 %). Chatty events are now cut to
// the first few and then a handful per second; everything else (state changes, faults, joins, map changes) is always written.
// The caller holds its journal lock; the table is plain data.
#include <cstdint>
#include <cstring>

namespace pse::journal {
inline bool chatty(const char* e) {
    static constexpr const char* names[] = {
        "p7_native_scalars", "native_snapshot_captured", "network_native_update", "network_native_motion", "network_input_accepted",
        "network_input_rejected", "network_input_expired", "network_neutral", "native_primary_input", "p2_pose_applied", "p2_pose_refused",
        "p4_snapshot_buffered", "proxy_tick_action", "proxy_update", "native_proxy_stable", "snapshot_worker_send", "snapshot_worker_receive",
        "background_tick", "background_policy_status", "map_identity_status", "tick_status", "p23_overlay", "client_input_sampled",
        "client_input_dispatched", "native_snapshot_received", "proxy_snapshot_routed", "proxy_route_counts", "proxy_state", "p4_playout",
        "world_enemy_apply", "world_enemy_probe", "client_map_status", "p4_client_background_tick", "p8_animation", "world_own_health",
        "world_mover_local", "world_mover_applied", "client_loadout_applied", "client_input_gate", "p8_snapshot_art", "proxy_snapshot_rejected",
        "native_snapshot_rejected", "p16_snapshot_generation_wait", "client_player_dead", "proxy_safe_phase"};
    for (const char* n : names) if (std::strcmp(n, e) == 0) return true;
    return false;
}
struct Throttle {
    struct Slot { std::uint32_t hash{}; std::uint64_t windowStart{}; unsigned inWindow{}, total{}; };
    Slot slots[64]{};
    // true: write this line
    bool allow(const char* event, std::uint64_t nowMs) {
        if (!chatty(event)) return true;
        std::uint32_t h = 2166136261u;
        for (const char* p = event; *p; ++p) { h ^= static_cast<unsigned char>(*p); h *= 16777619u; }
        if (!h) h = 1;
        Slot* s = nullptr;
        for (auto& c : slots) { if (c.hash == h) { s = &c; break; } if (!c.hash && !s) s = &c; }
        if (!s) return true;
        if (!s->hash) s->hash = h;
        if (s->total < 30) { ++s->total; return true; }       // the first lines of every kind are kept in full
        if (nowMs - s->windowStart >= 1000) { s->windowStart = nowMs; s->inWindow = 0; }
        return ++s->inWindow <= 3;                              // afterwards: three lines per second per kind
    }
};
} // namespace pse::journal
