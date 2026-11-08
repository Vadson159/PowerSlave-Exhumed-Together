// P3 / S1.1-S1.2: the accepted P1 in-DLL adapter (native_proxy.hpp) plus an in-DLL engine-teardown witness, a
// same-live-tick Remove and per-world re-attestation, so one proxy can be retired and spawned again in one process.
// Built only into pse_dll_client_p3 (PSE_FEATURE_P3); accepted P1 targets keep native_proxy.cpp unchanged.
#pragma once
#include "seam.hpp"
#include "proxy_rearm_p3.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace pse::native_proxy_p3 {
using Journal = void(*)(const char* event, const char* json);
bool install(unsigned char* image, DWORD simulationTid, Journal journal);
const seam::SeamTable& table();
void permitSpawn(); // publishes an atomic only; no game calls
bool detach();     // simulation thread, after quiescence or the teardown barrier (DirectQuit/final only in P3)
void fault();      // invalidate weak identities after an unknown native outcome
void exitBarrier(std::uint32_t reason); // idempotent barrier; P3 keeps hooks and arms the teardown witness
bool isPositionInterpolant(const void* candidate); // equality filter; never dereferences candidate
// P3: the adapter refused an unstable attested world on its own (no ClientGate barrier yet): the caller must latch
// the automaton barrier before its next tick. Cleared when the next world is re-attested.
bool worldLost();
// P3: after the original StateExit returned, on the simulation thread. Consumes the teardown log of this world.
struct TeardownResult {
    std::uint64_t handle{};
    pse::proxy::EngineTeardownWitness arena{pse::proxy::EngineTeardownWitness::Unknown};
    pse::p3::QueueWitness queue{pse::p3::QueueWitness::Unknown};
    bool armed{};
};
TeardownResult teardownVerdict(std::uint64_t world);

// Read-only, simulation/render thread only. No Actor/ObjectNode address crosses this API (P1 ABI 0x50310001).
struct Observation {
    std::uint32_t size,abi,phase,identityValid;
    std::uint64_t handle;
    std::uint32_t sequence,serverTick,revision,engineStamp;
    std::uint64_t frameToken;
    std::array<float,3> origin,render,angles,previous;
    float yaw,scale;
    std::int32_t sector;
    std::uint32_t flags,simulationTid,observedTid,removeAttempted,faulted;
};
static_assert(sizeof(Observation)==128 && offsetof(Observation,frameToken)==40 && offsetof(Observation,faulted)==124);
DWORD observe(Observation* out, std::uint32_t size);
}
