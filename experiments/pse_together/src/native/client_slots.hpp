#pragma once
// Client-side bookkeeping for up to MAX_PLAYERS (8) slots: entity generation admission per slot, last death state, and
// stale-slot detection (a remote player that stops sending snapshots must not stay frozen as a proxy forever).
// Pure logic (no KEX, no clock): client.cpp feeds it the steady clock; tests/native/client_slots.cpp covers it offline.
#include "snapshot_contract.hpp"
#include "thread_bridge_dto.h"
#include <array>
#include <cstdint>

namespace pse::client_slots {

inline constexpr unsigned Slots = pse::MAX_PLAYERS;
inline constexpr std::uint64_t StaleNs = 8'000'000'000ULL; // no accepted snapshot for this long => the slot is gone

enum class Gen : std::uint8_t { Accept, Adopt, Wait };

struct Tracker {
    // Entity generation the client currently believes per slot. 0 = unknown (slot forgotten): any generation is adopted.
    std::array<std::uint32_t, Slots> expected{};
    std::array<std::uint32_t, Slots> lastDeath{};
    std::array<bool, Slots> lastDead{};
    std::array<std::uint64_t, Slots> lastAcceptNs{}; // 0 = not tracked (never accepted since the last forget/quiet)

    Tracker() noexcept { reset(); }
    // New world: everything back to the first generation.
    void reset() noexcept { expected.fill(1); lastDeath.fill(0); lastDead.fill(false); lastAcceptNs.fill(0); }
    // New network session (Welcome): generations and death state stay, but the staleness clocks restart.
    void quiet() noexcept { lastAcceptNs.fill(0); }

    // Own slot: only the generation the local respawn handshake produced (a new generation needs its confirm, which also
    // revives the local Actor). Other slots: a higher generation is adopted straight from the snapshot (late join, a
    // respawn whose confirm was missed); a lower one is an old in-flight record and waits/drops.
    Gen checkSnapshot(std::uint8_t slot, std::uint32_t generation, std::uint8_t ownSlot) const noexcept {
        if (slot >= Slots) return Gen::Wait;
        if (generation == expected[slot]) return Gen::Accept;
        if (slot != ownSlot && generation > expected[slot]) return Gen::Adopt;
        return Gen::Wait;
    }

    // BridgeRespawnConfirm for any slot (broadcast). Own slot: exactly the next generation. Other slots: any newer one.
    bool admitConfirm(const pse::BridgeRespawnConfirm& c, std::uint8_t ownSlot, std::uint32_t epoch) const noexcept {
        if (c.slotId >= Slots || c.mapEpoch != epoch || !c.deathSequence || c.newEntityId != 0x5500u + c.slotId || !(c.defaultWeapons & 1u)) return false;
        return c.slotId == ownSlot ? c.newGeneration == expected[c.slotId] + 1 : c.newGeneration > expected[c.slotId];
    }

    // What happened to one incoming snapshot (generation gate + the shared per-slot Guard + clock bookkeeping, in the order
    // client.cpp applies them).
    struct Intake {
        bool waited{};    // generation not admitted yet (old in-flight record / own slot ahead of its confirm): dropped
        bool adopted{};   // another slot's newer generation taken from the snapshot itself
        bool replaced{};  // ...and it replaced a known earlier generation (a new life): the old proxy body must go
        snapshot55::Verdict verdict{snapshot55::Verdict::NotReady};
    };
    Intake intake(snapshot55::Guard& guard, const pse::BridgePlayerSnapshot& s, bool ready, std::uint8_t ownSlot, std::uint64_t nowNs) noexcept {
        Intake r;
        const auto g = checkSnapshot(s.slotId, s.entityGeneration, ownSlot);
        if (g == Gen::Wait || (g == Gen::Adopt && s.entityId != 0x5500u + s.slotId)) { r.waited = true; return r; }
        if (g == Gen::Adopt) {
            auto& prior = guard.slots[s.slotId];
            r.adopted = true; r.replaced = prior.generation != 0;
            expected[s.slotId] = s.entityGeneration; prior.entity = s.entityId; prior.generation = s.entityGeneration;
        }
        r.verdict = guard.accept(s, ready);
        if (r.verdict == snapshot55::Verdict::Accept) accepted(s.slotId, s.deathSequence, s.lifeState == pse::LIFE_DEAD, nowNs);
        return r;
    }
    // An admitted confirm moves the slot (tracker and shared Guard) to the new generation.
    void applyConfirm(snapshot55::Guard& guard, const pse::BridgeRespawnConfirm& c) noexcept {
        if (c.slotId >= Slots) return;
        expected[c.slotId] = c.newGeneration;
        auto& prior = guard.slots[c.slotId]; prior.entity = c.newEntityId; prior.generation = c.newGeneration;
    }

    void setGeneration(std::uint8_t slot, std::uint32_t generation) noexcept { if (slot < Slots) expected[slot] = generation; }

    // A snapshot passed every gate for this slot.
    void accepted(std::uint8_t slot, std::uint32_t death, bool dead, std::uint64_t nowNs) noexcept {
        if (slot >= Slots) return;
        lastDeath[slot] = death; lastDead[slot] = dead; lastAcceptNs[slot] = nowNs ? nowNs : 1;
    }

    // Bit mask of remote slots (never the own one) that have been silent for StaleNs; each is forgotten (generation unknown,
    // clocks cleared) so a later snapshot, e.g. a new player reusing the slot, starts from scratch.
    std::uint32_t collectStale(std::uint64_t nowNs, std::uint8_t ownSlot) noexcept {
        std::uint32_t mask = 0;
        for (unsigned i = 0; i < Slots; ++i) {
            if (i == ownSlot || !lastAcceptNs[i] || nowNs < lastAcceptNs[i] || nowNs - lastAcceptNs[i] < StaleNs) continue;
            mask |= 1u << i; forget(static_cast<std::uint8_t>(i));
        }
        return mask;
    }
    // Stale slots: also wipe the Guard record (a new occupant's sequence numbers start over).
    std::uint32_t collectStale(snapshot55::Guard& guard, std::uint64_t nowNs, std::uint8_t ownSlot) noexcept {
        const auto mask = collectStale(nowNs, ownSlot);
        for (unsigned i = 0; i < Slots; ++i) if (mask >> i & 1u) guard.slots[i] = {};
        return mask;
    }
    void forget(std::uint8_t slot) noexcept {
        if (slot >= Slots) return;
        expected[slot] = 0; lastDeath[slot] = 0; lastDead[slot] = false; lastAcceptNs[slot] = 0;
    }
};

} // namespace pse::client_slots
