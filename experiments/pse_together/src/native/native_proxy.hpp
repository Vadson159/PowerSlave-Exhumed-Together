// P1: the DOC-58A/58B adapter lives inside the pinned client DLL.
#pragma once
#include "seam.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace pse::native_proxy {
using Journal = void(*)(const char* event, const char* json);
// remoteSlot: the player this proxy shows (client: the host, slot 0; host: the remote player, slot 1).
// hostMode: the host DLL owns the puppet-tick detour and forwards each call to notePuppetTick().
bool install(unsigned char* image, DWORD simulationTid, Journal journal, unsigned remoteSlot=0, bool hostMode=false);
void notePuppetTick(void* self); // hostMode only, simulation thread
void acceptLocalRespawn(); // the local player was respawned natively (P16): the next identity check re-baselines instead of faulting
bool rearm(); // fresh incarnation after a world transition or a fully retired Actor; no engine calls, hooks stay
const seam::SeamTable& table();
void permitSpawn(); // publishes an atomic only; no game calls
#ifdef PSE_FEATURE_P8
void stageAnimation(const pse::BridgePlayerSnapshot& snapshot); // already validated/routed, Sim TID; DTO copy only
DWORD previewAnimation(unsigned slot,unsigned pose); // diagnostic atomic only; slot 8 disables preview
#endif
bool detach();     // simulation thread, after quiescence or the teardown barrier
void fault();      // invalidate weak identities after an unknown native outcome
void exitBarrier(std::uint32_t reason); // idempotent, including a fault-detached seam
bool isPositionInterpolant(const void* candidate); // equality filter; never dereferences candidate

// Read-only, simulation/render thread only. No Actor/ObjectNode address crosses this API.
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
