// Conservative AABB clearance for a previously observed native level spawn.
#pragma once
#include "bounded_proxy.hpp"
namespace pse::placement {
class WaitBudget {
    unsigned attempts_{};
public:
    static constexpr unsigned Limit=1800; // 30 seconds of eligible 60 Hz gameplay attempts.
    bool expired() const { return attempts_>=Limit; }
    bool deferred() { if(!expired())++attempts_;return expired(); }
    void clear() { attempts_=0; }
};
struct Box { bounded::Vec3 position{},lo{},hi{}; };
inline bool valid(const Box& b) {
    if(!bounded::finite(b.position)||!bounded::finite(b.lo)||!bounded::finite(b.hi)) return false;
    for(unsigned i=0;i<3;++i) if(b.lo[i]>b.hi[i]) return false;
    return true;
}
// A Puppet's origin is not the bottom of its diagnostic AABB (run01: originZ=floor=1152,
// boundsMinZ=-80). Revalidate the original native support values, not an invented center rule.
inline bool sameSupport(float oldFloor,float oldCeiling,float floor,float ceiling) {
    return std::isfinite(oldFloor)&&std::isfinite(oldCeiling)&&std::isfinite(floor)&&std::isfinite(ceiling)&&
        oldFloor==floor && oldCeiling==ceiling && floor<ceiling;
}
inline bool separate(const Box& a,const Box& b) {
    if(!valid(a)||!valid(b)) return false;
    // Touching is refused as well; use widened arithmetic for native float inputs.
    for(unsigned i=0;i<3;++i)
        if(static_cast<double>(a.position[i])+a.hi[i]+1 < static_cast<double>(b.position[i])+b.lo[i] ||
           static_cast<double>(b.position[i])+b.hi[i]+1 < static_cast<double>(a.position[i])+a.lo[i]) return true;
    return false;
}
}
