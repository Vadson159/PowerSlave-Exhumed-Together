// Pure DOC-58A/58B bounds and one-Actor lifetime bookkeeping for the S0.4 DLL adapter.
// No KEX pointers are dereferenced here. Engine identity, thread/phase/geometry witnesses and every
// native call belong to the caller. These helpers neither establish runtime acceptance nor rearm.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace pse::bounded {

using Vec3 = std::array<float, 3>;
using Plane = std::array<double, 4>;

inline bool finite(const Vec3& p) noexcept {
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}

inline bool inside(std::span<const Plane> planes, const Vec3& p) noexcept {
    if (!finite(p)) return false;
    for (const auto& f : planes) {
        for (double v : f) if (!std::isfinite(v)) return false;
        const double margin = f[0] * static_cast<double>(p[0]) + f[1] * static_cast<double>(p[1]) +
            f[2] * static_cast<double>(p[2]) + f[3];
        if (!(margin >= 0.0)) return false;
    }
    return true;
}

inline bool fits(std::span<const Plane> planes, const Vec3& p) noexcept {
    if (!finite(p)) return false;
    for (const auto& f : planes) {
        for (double v : f) if (!std::isfinite(v)) return false;
        for (unsigned corner = 0; corner != 8; ++corner) {
            // JS adds AABB offsets to the widened float inputs in double precision. Casting corners
            // back to float before the plane test could incorrectly accept a point on a tight face.
            const double x = static_cast<double>(p[0]) + ((corner & 1u) ? 12.0 : -12.0);
            const double y = static_cast<double>(p[1]) + ((corner & 2u) ? 12.0 : -12.0);
            const double z = static_cast<double>(p[2]) + ((corner & 4u) ? 24.0 : -24.0);
            if (!(f[0] * x + f[1] * y + f[2] * z + f[3] >= 0.0)) return false;
        }
    }
    return true;
}

inline float normalizeYaw(float yaw) noexcept {
    // Literal numeric mapping from net_mapping58b.js, including the second remainder and the
    // single float32 rounding at the end. fmod has the JS remainder sign convention.
    constexpr double pi = 3.141592653589793238462643383279502884;
    constexpr double twoPi = 2.0 * pi;
    return static_cast<float>(std::fmod(std::fmod(static_cast<double>(yaw) + pi, twoPi) + twoPi, twoPi) - pi);
}

inline bool teleport(const Vec3& from, const Vec3& to) noexcept {
    // NaN/Infinity must never select native_step; JS mode() likewise rejects distance<=limit.
    if (!finite(from) || !finite(to)) return true;
    const double dx = static_cast<double>(from[0]) - static_cast<double>(to[0]);
    const double dy = static_cast<double>(from[1]) - static_cast<double>(to[1]);
    const double dz = static_cast<double>(from[2]) - static_cast<double>(to[2]);
    return !(std::hypot(dx, dy, dz) <= 160.0);
}

enum class Phase : std::uint32_t {
    Waiting = 0, Spawning = 1, Live = 2, Pending = 3,
    Retiring = 4, Retired = 5, Blocked = 6, Exit = 7
};

struct Lifetime {
    Phase phase{Phase::Waiting};
    std::uintptr_t actor{}, node{}, retireNode{};
    bool spawnAttempted{}, removeAttempted{};

    bool beginSpawn() noexcept {
        if (phase != Phase::Waiting || spawnAttempted) return false;
        spawnAttempted = true; // Budget is consumed before the factory may run or throw.
        phase = Phase::Spawning;
        return true;
    }

    bool acceptSpawn(std::uintptr_t a, std::uintptr_t n) noexcept {
        if (phase != Phase::Spawning || !spawnAttempted) return false;
        if (!a || !n) { block(); return false; }
        // These are already validated opaque tokens, not a substitute for native identity checks.
        actor = a;
        node = n;
        phase = Phase::Live;
        return true;
    }

    bool beginRemove() noexcept {
        if (phase != Phase::Live || !actor || !node || removeAttempted) return false;
        removeAttempted = true; // No retry after an exception or an indeterminate native result.
        phase = Phase::Pending;
        return true;
    }

    bool poolReturn(std::uintptr_t a, bool controlled) noexcept {
        if (!a || a != actor) return false;
        const auto oldNode = node;
        const bool canRetire = controlled && phase == Phase::Pending && removeAttempted && oldNode;
        // The caller invokes this BEFORE the PoolReturn trampoline. No later callback may observe
        // the Actor or its reciprocal node as live, even if that trampoline faults or reenters.
        actor = 0;
        node = 0;
        retireNode = 0;
        if (canRetire) {
            retireNode = oldNode;
            phase = Phase::Retiring;
        } else {
            phase = Phase::Blocked;
        }
        return true; // Matched our Actor; phase says whether retirement may be checked.
    }

    bool finishRetirement(bool payloadNull, int refs, int inUse, int occurrences) noexcept {
        const bool pending = phase == Phase::Retiring && retireNode != 0;
        retireNode = 0; // Consume the post-PoolReturn read token even if postconditions fail.
        if (!pending) return false;
        const bool retired = payloadNull && refs == 0 && inUse == 0 && occurrences == 0;
        phase = retired ? Phase::Retired : Phase::Blocked;
        return retired;
    }

    void block() noexcept {
        actor = node = retireNode = 0;
        phase = Phase::Blocked;
    }

    void worldExit() noexcept {
        actor = node = retireNode = 0;
        phase = Phase::Exit;
    }
};

} // namespace pse::bounded
