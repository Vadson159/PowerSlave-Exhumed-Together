// P4 only. The user approved identity-only v1.11 on 2026-11-04.
#pragma once
#include "map_identity.hpp"
namespace map111 {
inline pse::NativeMapStructure structure(const map56::Identity& i) { return {i.version,i.countA,i.countB}; }
inline bool matches(const map56::Identity& i,const std::string& name,const pse::NativeMapStructure& s) {
    return pse::validNativeMapName(name)&&name==i.name&&s==structure(i);
}
}
