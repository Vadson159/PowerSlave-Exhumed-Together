// Portable S0.4 numeric/lifetime contract. No KEX, Frida, DLL load, thread witness or native proof.
#include "bounded_proxy.hpp"

#include <bit>
#include <cstdio>
#include <limits>
#include <type_traits>

using namespace pse::bounded;

namespace {
unsigned checks{}, failures{};
void check(bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what); }
}
constexpr std::uintptr_t Actor = 0x1000, Node = 0x2000;
Lifetime live() {
    Lifetime l;
    check(l.beginSpawn() && l.acceptSpawn(Actor, Node), "fixture creates one logical live Actor");
    return l;
}
Lifetime retiring() {
    auto l = live();
    check(l.beginRemove() && l.poolReturn(Actor, true), "fixture consumes Remove then matching controlled PoolReturn");
    return l;
}
void geometry() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    check(finite({0.0f, -0.0f, 1.0f}), "finite accepts ordinary float32 values");
    for (unsigned axis = 0; axis != 3; ++axis) {
        Vec3 p{}; p[axis] = nan;
        check(!finite(p), "finite rejects NaN on each axis");
        p[axis] = inf;
        check(!finite(p), "finite rejects +Infinity on each axis");
        p[axis] = -inf;
        check(!finite(p), "finite rejects -Infinity on each axis");
    }
    const std::array<Plane, 6> box{{
        {1.0, 0.0, 0.0, 12.0}, {-1.0, 0.0, 0.0, 12.0},
        {0.0, 1.0, 0.0, 12.0}, {0.0, -1.0, 0.0, 12.0},
        {0.0, 0.0, 1.0, 24.0}, {0.0, 0.0, -1.0, 24.0}
    }};
    check(inside(box, {12.0f, -12.0f, 24.0f}), "inside includes exact plane equality");
    check(!inside(box, {12.0001f, 0.0f, 0.0f}), "inside rejects just beyond a face");
    check(!inside(box, {0.0f, nan, 0.0f}), "inside rejects nonfinite point");
    check(fits(box, {0.0f, 0.0f, 0.0f}), "AABB fits all six touching faces");
    check(!fits(box, {0.0001f, 0.0f, 0.0f}), "AABB rejects center-inside box with a leaking corner");
    check(!fits(box, {0.0f, 0.0f, nan}), "AABB rejects nonfinite origin");
    for (unsigned mask = 0; mask != 8; ++mask) {
        // A different oblique plane excludes exactly one of the eight AABB corners each time.
        const std::array<Plane, 1> slant{{{
            (mask & 1u) ? 1.0 : -1.0,
            (mask & 2u) ? 1.0 : -1.0,
            (mask & 4u) ? 1.0 : -1.0, 47.0
        }}};
        check(inside(slant, {0.0f, 0.0f, 0.0f}) && !fits(slant, {0.0f, 0.0f, 0.0f}),
            "all eight signed corner combinations are required");
    }
    // At 2^28 the float32 spacing exceeds the AABB half-width: float-rounded corners would lie
    // on this face and wrongly pass, while the JS/double corner extends past it by 12.
    const std::array<Plane, 1> tight{{{-1.0, 0.0, 0.0, 268435456.0}}};
    check(inside(tight, {268435456.0f, 0.0f, 0.0f}) && !fits(tight, {268435456.0f, 0.0f, 0.0f}),
        "AABB corner sums retain JS double precision");
    for (unsigned coefficient = 0; coefficient != 4; ++coefficient) {
        std::array<Plane, 1> bad{{{1.0, 0.0, 0.0, 12.0}}};
        bad[0][coefficient] = std::numeric_limits<double>::quiet_NaN();
        check(!inside(bad, {}) && !fits(bad, {}), "NaN plane data never attests bounds");
        bad[0][coefficient] = std::numeric_limits<double>::infinity();
        check(!inside(bad, {}) && !fits(bad, {}), "infinite plane data never attests bounds");
    }
    check(!teleport({}, {132.0f, 0.0f, 0.0f}), "accepted 132-unit native_step stays a step");
    check(!teleport({}, {160.0f, 0.0f, 0.0f}), "exactly 160 units remains native_step");
    check(!teleport({}, {96.0f, 128.0f, 0.0f}), "3D distance, not component maximum, includes exactly 160");
    check(teleport({}, {160.00002f, 0.0f, 0.0f}), "float32 value just beyond 160 teleports");
    check(teleport({}, {100.0f, 100.0f, 100.0f}), "diagonal 3D distance beyond 160 teleports");
    check(teleport({-std::numeric_limits<float>::max(), 0.0f, 0.0f},
                   {std::numeric_limits<float>::max(), 0.0f, 0.0f}), "distance subtraction is widened before overflow");
    check(teleport({}, {nan, 0.0f, 0.0f}) && teleport({inf, 0.0f, 0.0f}, {}),
        "invalid distance never selects native_step");
}
void yaw() {
    // Oracle: Float32Array input -> literal net_mapping58b.js normalizeYaw -> Uint32Array bits.
    // Cases include signed zero, float neighbors of +/-pi, large finite input and max float32.
    constexpr std::array<std::array<std::uint32_t, 2>, 18> vectors{{
        {0x00000000u, 0x00000000u}, {0x80000000u, 0x00000000u},
        {0x3e800000u, 0x3e800000u}, {0xc0600000u, 0x40321fb5u},
        {0x40e00000u, 0x3f37812bu}, {0x33d6bf95u, 0x33d6bf95u},
        {0xc0490fdbu, 0x40490fdau}, {0x40490fdbu, 0xc0490fdau},
        {0xc0c90fdbu, 0xb43bbd2eu}, {0x40c90fdbu, 0x343bbd2eu},
        {0xc1c90fdbu, 0xb53bbd2eu}, {0x41c90fdbu, 0x353bbd2eu},
        {0x40490fdau, 0x40490fdau}, {0xc0490fdau, 0xc0490fdau},
        {0x4cbebc20u, 0x3ff8aa3cu}, {0xccbebc20u, 0xbff8aa3cu},
        {0x7f7fffffu, 0xbfc8910au}, {0xff7fffffu, 0x3fc8910au}
    }};
    for (const auto& v : vectors)
        check(std::bit_cast<std::uint32_t>(normalizeYaw(std::bit_cast<float>(v[0]))) == v[1],
            "yaw matches independent JS float32 oracle bits");
    check(std::isnan(normalizeYaw(std::numeric_limits<float>::quiet_NaN())), "yaw NaN stays invalid");
    check(std::isnan(normalizeYaw(std::numeric_limits<float>::infinity())), "yaw infinity stays invalid");
}
void lifetime() {
    static_assert(std::is_same_v<std::underlying_type_t<Phase>, std::uint32_t>);
    static_assert(static_cast<std::uint32_t>(Phase::Waiting) == 0 && static_cast<std::uint32_t>(Phase::Exit) == 7);
    {
        Lifetime l;
        check(l.phase == Phase::Waiting && !l.actor && !l.node && !l.retireNode &&
            !l.spawnAttempted && !l.removeAttempted, "new lifetime starts empty and unattempted");
        check(!l.acceptSpawn(Actor, Node) && !l.beginRemove(), "no accept or Remove before a spawn budget");
        check(l.beginSpawn() && l.spawnAttempted && l.phase == Phase::Spawning, "factory budget consumed before call");
        check(!l.beginSpawn(), "second factory call forbidden while first is in progress");
        check(l.acceptSpawn(Actor, Node), "valid opaque tokens complete the sole spawn");
        check(!l.beginSpawn() && !l.acceptSpawn(Actor + 1, Node + 1) && l.actor == Actor && l.node == Node,
            "live generation cannot be replaced or spawned twice");
        check(l.beginRemove() && l.removeAttempted && l.phase == Phase::Pending, "Remove budget consumed before call");
        check(!l.beginRemove() && l.actor == Actor && l.node == Node, "pending Remove cannot be repeated");
        check(!l.poolReturn(Actor + 1, true) && !l.poolReturn(0, true) && l.actor == Actor && l.node == Node,
            "foreign PoolReturn leaves tracked identity intact");
        check(l.poolReturn(Actor, true) && !l.actor && !l.node && l.retireNode == Node && l.phase == Phase::Retiring,
            "matching controlled PoolReturn invalidates live tokens before trampoline");
        check(!l.poolReturn(Actor, true) && !l.beginRemove() && !l.beginSpawn(),
            "reentrant post-PoolReturn callbacks cannot rediscover Actor or issue native calls");
        check(l.finishRetirement(true, 0, 0, 0) && l.phase == Phase::Retired && !l.retireNode,
            "only complete postconditions retire generation and consume read token");
        check(!l.finishRetirement(true, 0, 0, 0) && !l.beginSpawn() && !l.beginRemove(),
            "retirement is terminal and cannot be used for respawn");
    }
    for (unsigned missing = 0; missing != 2; ++missing) {
        Lifetime l; check(l.beginSpawn(), "failed-factory fixture consumes budget");
        check(!l.acceptSpawn(missing ? Actor : 0, missing ? 0 : Node) && l.phase == Phase::Blocked &&
            !l.actor && !l.node && l.spawnAttempted && !l.beginSpawn(), "null factory/identity result blocks without retry");
    }
    {
        Lifetime l; check(l.beginSpawn(), "exception-factory fixture consumes budget"); l.block();
        check(l.spawnAttempted && !l.beginSpawn() && !l.acceptSpawn(Actor, Node), "factory exception or failed validation never retries");
        l = live(); check(l.beginRemove(), "exception-Remove fixture consumes budget"); l.block();
        check(l.removeAttempted && l.phase == Phase::Blocked && !l.actor && !l.node && !l.retireNode &&
            !l.beginRemove() && !l.beginSpawn(), "indeterminate Remove clears identity and cannot retry");
    }
    {
        auto l = live();
        check(l.poolReturn(Actor, true) && l.phase == Phase::Blocked && !l.actor && !l.node && !l.retireNode,
            "PoolReturn without pending Remove blocks even if caller labels it controlled");
        l = live(); check(l.beginRemove(), "uncontrolled-retirement fixture consumes Remove budget");
        check(l.poolReturn(Actor, false) && l.phase == Phase::Blocked && !l.actor && !l.node && !l.retireNode &&
            !l.finishRetirement(true, 0, 0, 0), "off-sweep or off-thread PoolReturn preserves no read token");
    }
    for (unsigned bad = 0; bad != 5; ++bad) {
        auto l = retiring();
        const bool ok = l.finishRetirement(bad != 0, bad == 1 ? 1 : 0, bad == 2 ? 1 : 0, bad == 3 ? 1 : bad == 4 ? -1 : 0);
        check(!ok && l.phase == Phase::Blocked && !l.actor && !l.node && !l.retireNode,
            "failed retirement postcondition consumes all identity/read tokens");
        check(!l.finishRetirement(true, 0, 0, 0) && !l.beginRemove() && !l.beginSpawn(),
            "failed retirement cannot be retried with later all-zero readings");
    }
    {
        auto l = retiring(); l.block();
        check(l.phase == Phase::Blocked && !l.actor && !l.node && !l.retireNode && l.spawnAttempted && l.removeAttempted,
            "block also invalidates a pending post-PoolReturn read token without resetting budgets");
    }
    for (unsigned state = 0; state != 4; ++state) {
        Lifetime l;
        if (state == 1) check(l.beginSpawn(), "world-exit fixture in factory scope");
        if (state == 2) l = live();
        if (state == 3) l = retiring();
        l.worldExit();
        check(l.phase == Phase::Exit && !l.actor && !l.node && !l.retireNode && !l.beginSpawn() &&
            !l.beginRemove() && !l.acceptSpawn(Actor, Node) && !l.poolReturn(Actor, true) &&
            !l.finishRetirement(true, 0, 0, 0), "worldExit invalidates every token and forbids later calls from every live phase");
    }
}
} // namespace

int main() {
    geometry();
    yaw();
    lifetime();
    std::printf("bounded_proxy_contract: %u checks, %u failed\n", checks, failures);
    return failures ? 1 : 0;
}
