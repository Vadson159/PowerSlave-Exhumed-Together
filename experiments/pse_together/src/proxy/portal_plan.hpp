// P4 numeric-only portal corridor proof. No sector IDs are whitelisted.
// Only one reciprocal coplanar portal per step; unknown cache/water transitions stay refused.
#pragma once
#include "bounded_proxy.hpp"
#include <vector>

namespace pse::portal {
struct Face { bounded::Plane plane; int adjoin{-1}; bool portal{}; };
struct Sector { std::vector<Face> faces; std::uint16_t flags{}; };
inline double margin(const bounded::Plane& f,const bounded::Vec3& p) {
    return f[0]*p[0]+f[1]*p[1]+f[2]*p[2]+f[3];
}
inline bool sweptAperture(const Sector& a,int ai,const Sector& b,int bi,
                     const bounded::Vec3& from,const bounded::Vec3& to) {
    if(!bounded::finite(from)||!bounded::finite(to)||ai==bi||((a.flags^b.flags)&0x100)) return false;
    const Face* ab=nullptr;const Face* ba=nullptr;
    for(const auto& f:a.faces) if(f.portal&&f.adjoin==bi) { if(ab) return false;ab=&f; }
    for(const auto& f:b.faces) if(f.portal&&f.adjoin==ai) { if(ba) return false;ba=&f; }
    if(!ab||!ba) return false;
    for(unsigned i=0;i<4;++i)
        if(!std::isfinite(ab->plane[i])||!std::isfinite(ba->plane[i])||std::abs(ab->plane[i]+ba->plane[i])>(i==3?0.01:0.0001)) return false;
    // Linear plane margins: endpoint corner tests bound the entire swept AABB.
    for(const auto* s:{&a,&b}) {
        const auto* skip=s==&a ? ab : ba;
        for(const auto& f:s->faces) if(&f!=skip) {
            const std::array<bounded::Plane,1> plane{f.plane};
            if(!bounded::fits(plane,from)||!bounded::fits(plane,to)) return false;
        }
    }
    return true;
}
inline bool corridor(const Sector& a,int ai,const Sector& b,int bi,const bounded::Vec3& from,const bounded::Vec3& to) {
    if(!sweptAperture(a,ai,b,bi,from,to)) return false;
    for(const auto& f:a.faces) if(f.portal&&f.adjoin==bi)
        return margin(f.plane,from)>=0 && margin(f.plane,to)<0;
    return false;
}
}
