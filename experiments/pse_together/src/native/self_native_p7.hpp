// P7 exact-build native adapter. Runtime acceptance is separate from this API.
// All engine receivers stay inside the native DLL; network DTOs contain scalars.
#pragma once
#include "self_state_p7.hpp"
#include <cstdint>

namespace pse::self_native_p7 {
using Journal=void(*)(const char* event,const char* json);
struct Binding {
    std::uintptr_t game{},player{},node{},actor{};
    std::uint32_t mapEpoch{},entityGeneration{};
    std::uint64_t tick{};
    bool gameplay{},outsideUnloadBarrier{};
};
struct KeysProbe {
    std::uint64_t serial{},tick{};
    std::uint32_t mapEpoch{},entityGeneration{};
    std::uint16_t value{};
    bool referenceIdentity{};
};
struct Observation {
    std::uint64_t tick{},keyProbeSerial{},keyReviewSerial{},applies{},suppressedDamage{},suppressedPickups{},shadowHealthReferences{};
    std::uint32_t mapEpoch{},entityGeneration{},deathSequence{};
    bool installed{},bindingLive{},authorityActive{},keysPermitted{},faulted{},restored{};
};

// Parent initializes MinHook and verifies its full EXE hash before install.
// install also checks the on-disk current process image hash and exact prefixes.
bool install(unsigned char* image,std::uint32_t simulationTid,Journal journal) noexcept;
// Call on native Sim entry, then again if admission/lease status changes. The
// current primary reciprocal graph, world identity and flags are rechecked.
bool tick(const Binding& binding,bool authorityActive) noexcept;
void leaveTick() noexcept;
// Session/disconnect/unload latch. No native calls or rearm; same-incarnation
// P7 is deliberately one-shot. Foreign callers publish an atomic barrier only.
void invalidate() noexcept;
bool read(self_p7::Values& values) noexcept;
bool apply(const self_p7::Values& before,const self_p7::Values& desired) noexcept;
bool probeKeys(KeysProbe& probe) noexcept; // getter call and scalar read only
inline bool probeKeys() noexcept {KeysProbe probe;return probeKeys(probe);}
// Mailbox only. The caller must supply its completed independent-review serial
// after reviewing a real read-only probe; a nonzero integer is not such evidence.
// The Sim tick consumes a permit only for that internally recorded incarnation.
void permitKeys(std::uint64_t readProbeSerial,std::uint64_t independentReviewSerial) noexcept;
self_p7::NativeCapabilities capabilities() noexcept;
Observation observe() noexcept; // Sim thread only
bool shutdown() noexcept; // disable own hooks, exact prefix restore; keep code pinned

class NativeState final : public self_p7::INativeState {
public:
    bool read(self_p7::Values& values) noexcept override {return self_native_p7::read(values);}
    bool apply(const self_p7::Values& before,const self_p7::Values& desired) noexcept override {
        return self_native_p7::apply(before,desired);
    }
};
} // namespace pse::self_native_p7
