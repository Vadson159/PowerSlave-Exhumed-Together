#pragma once

#include <cstdint>
#include <array>
#include <bit>
#include <cmath>
#include "thread_bridge_dto.h"
#include "net_protocol.h"

// Reference DOC-55 Verdict enum without modifying frozen snapshot_contract.hpp
#include "../../native_snapshot/src/snapshot_contract.hpp"

namespace pse::snapshot {

// ============================================================================
// MultiSlotGuard<NumSlots>
// C++20, strictly noexcept, supporting up to 8 players (0..7) for PowerSlave Exhumed Together.
// Fully backward-compatible with DOC-55 validation semantics while eliminating the 2-slot limit.
// ============================================================================
template <size_t NumSlots = pse::MAX_PLAYERS>
struct MultiSlotGuard {
    static_assert(NumSlots >= 2 && NumSlots <= 32, "NumSlots must be between 2 and 32");

    struct Slot {
        std::uint32_t sequence{0};
        std::uint32_t tick{0};
        std::uint32_t entity{0};
        std::uint32_t generation{0};
    };

    std::array<Slot, NumSlots> slots{};
    std::uint32_t connection{0};
    std::uint32_t epoch{0};

    // Reset session tracking for all slots (strictly noexcept)
    void reset(std::uint32_t gen, std::uint32_t mapEpoch) noexcept {
        connection = gen;
        epoch = mapEpoch;
        slots.fill(Slot{});
    }

    // Evaluate incoming snapshot against multi-slot validation invariants
    snapshot55::Verdict accept(const pse::BridgePlayerSnapshot& s, bool ready) noexcept {
        if (!ready) {
            return snapshot55::Verdict::NotReady;
        }

        // Basic identity and boundary check across all NumSlots (0..7)
        if (!connection || !epoch || s.connectionGeneration != connection || s.mapEpoch != epoch ||
            s.slotId >= slots.size() || !s.sequence || !s.entityId || !s.entityGeneration) {
            return snapshot55::Verdict::Identity;
        }

        // Floating-point transform finiteness verification
        if (!std::isfinite(s.posX) || !std::isfinite(s.posY) || !std::isfinite(s.posZ) ||
            !std::isfinite(s.yaw) || !std::isfinite(s.pitch)) {
            return snapshot55::Verdict::Nonfinite;
        }

        auto& prior = slots[s.slotId];

        // Entity mismatch detection (respawn / pawn change on host)
        if (prior.generation && (s.entityId != prior.entity || s.entityGeneration != prior.generation)) {
            return snapshot55::Verdict::EntityMismatch;
        }

        // Replay and stale sequence / tick rejection
        if (s.sequence <= prior.sequence || (prior.sequence && s.serverTick < prior.tick)) {
            return snapshot55::Verdict::Stale;
        }

        // Accepted: update prior slot state
        prior = {s.sequence, s.serverTick, s.entityId, s.entityGeneration};
        return snapshot55::Verdict::Accept;
    }

    // Inspect slot tracking (read-only observer)
    const Slot& slot(size_t index) const noexcept {
        return slots[index < NumSlots ? index : 0];
    }

    size_t capacity() const noexcept {
        return NumSlots;
    }
};

using MultiSlotGuard8 = MultiSlotGuard<pse::MAX_PLAYERS>;

} // namespace pse::snapshot
