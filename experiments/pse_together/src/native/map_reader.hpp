// DOC56 read-only native map observation. Call only on the attested KEX simulation thread
// (both DLLs enforce this before calling). No KEX calls, no writes, bounded copies only.
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include "map_identity.hpp"

namespace map56 {
inline constexpr std::uint32_t GameInstance=0xaf33a0;
// Engine-owned bytes may be released by a concurrent native path only on this same thread,
// so a fault here means a contract violation: SEH refuses instead of crashing.
inline bool copyBytes(const char* src,std::size_t n,char* dst) {
    __try { for(std::size_t i=0;i<n;++i)dst[i]=src[i]; return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool copyUntilNul(const char* src,std::size_t cap,char* dst) {
    __try { for(std::size_t i=0;i<cap;++i) { dst[i]=src[i]; if(!dst[i])return true; } return false; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool readPointer(std::uintptr_t at,const char*& out) {
    __try { out=*reinterpret_cast<const char* const*>(at); return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template<class T> T field(const unsigned char* p,std::size_t off) { return *reinterpret_cast<const T*>(p+off); }
inline Observation observe(const unsigned char* base) {
    Observation o;const unsigned char* gi=base+GameInstance;
    o.state=field<int>(gi,0x24);o.next=field<int>(gi,0x20);o.pending=field<int>(gi,0x28);
    const auto* data=field<const char*>(gi,MapNameString);
    o.nameLength=field<std::uint64_t>(gi,MapNameString+8);o.nameCapacity=field<std::uint64_t>(gi,MapNameString+16);
    o.nameReadable=data && o.nameLength<=NameReadMax && copyBytes(data,static_cast<std::size_t>(o.nameLength)+1,o.name.data());
    o.mapInfo=field<std::uintptr_t>(gi,MapInfoEntry);
    const char* key{};
    o.keyReadable=o.mapInfo && readPointer(o.mapInfo+MapInfoKey,key) && key && copyUntilNul(key,KeyReadMax+1,o.key.data());
    const unsigned char* level=base+LevelObject;
    o.levelVtable=field<const unsigned char*>(level,0)==base+LevelVtable;
    if(o.levelVtable) { o.version=field<std::uint8_t>(level,LevelVersion);o.countA=field<std::uint64_t>(level,LevelCountA);o.countB=field<std::uint64_t>(level,LevelCountB); }
    return o;
}
inline std::uint32_t randomEpoch(std::uint32_t previous) {
    std::uint32_t r{};
    if(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(&r),sizeof(r),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)return 0;
    return nextEpoch(r,previous);
}
} // namespace map56
