#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pse::p14 {
inline constexpr unsigned AnkhBits = 0x1f;
inline bool validProgress(unsigned mask, int count) {
    return !(mask & ~AnkhBits) && count == std::popcount(mask);
}
struct Candidate { bool alive{}; float x{}, y{}; };
// Native acquisition uses an axis-aligned sight bound, then FOV and LOS.
// Rank eligible candidates by horizontal Euclidean distance, stable by slot.
template<class Visible>
int nearest(const std::array<Candidate, 8>& candidates, float x, float y,
            float sight, Visible visible) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(sight) || sight <= 0) return -1;
    double best = std::numeric_limits<double>::infinity(); int selected = -1;
    for (unsigned i=0; i<candidates.size(); ++i) {
        const auto& c=candidates[i];
        if (!c.alive || !std::isfinite(c.x) || !std::isfinite(c.y)) continue;
        const double dx=double(c.x)-x, dy=double(c.y)-y;
        if (std::abs(dx)>=sight || std::abs(dy)>=sight) continue;
        const double distance=dx*dx+dy*dy;
        if (distance<best && visible(i)) { best=distance; selected=static_cast<int>(i); }
    }
    return selected;
}
// A scope redirects precisely one known call site, never nested HUD/cleanup calls.
inline bool allowGetter(std::uint32_t expectedTid, std::uint32_t actualTid,
                        std::uintptr_t expectedReturn, std::uintptr_t actualReturn,
                        bool scoped) {
    return scoped && expectedTid==actualTid && expectedReturn==actualReturn;
}
}
