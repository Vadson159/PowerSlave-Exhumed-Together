#include <iostream>
#include <cassert>
#include <windows.h>
#include "hardened_visual_proxy_state_machine.hpp"

namespace {

class ReentrantFaultSeam : public pse::proxy::hardened::INativeVisualProxySeamRemediated {
public:
    pse::proxy::hardened::VisualProxyStateMachine* sm{nullptr};
    pse::proxy::hardened::ISectorValidator* validator{nullptr};
    int spawnCount{0};
    pse::proxy::hardened::SpawnExecutionResult innerResult{pse::proxy::hardened::SpawnExecutionResult::Success};

    void* spawnVisualActor(uint32_t, float, float, float, float, int) override {
        spawnCount++;
        void* allocatedPtr = reinterpret_cast<void*>(static_cast<uintptr_t>(0x61A00000ULL + spawnCount * 0x1000));

        if (spawnCount == 1 && sm && validator) {
            std::cout << "[REENTRANT SEAM] Native hook triggered reentrant callback during outer spawn...\n";
            innerResult = sm->executePendingSpawn(this, validator, 1007, 259);
            std::cout << "[REENTRANT SEAM] Inner executePendingSpawn result: "
                      << pse::proxy::hardened::toString(innerResult) << "\n";
        }

        return allocatedPtr;
    }

    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    pse::proxy::hardened::RemoveOutcome removeVisualActorEx(void*) override {
        return pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess;
    }
};

class DummyValidator : public pse::proxy::hardened::ISectorValidator {
public:
    bool validateDestinationSector(int, float, float, float) const noexcept override {
        return true;
    }
};

} // namespace

int main() {
    std::cout << "============================================================================\n";
    std::cout << "DOC-58D Repro: Reentrancy Guard on DOC-58D Hardened Candidate (AFTER)\n";
    std::cout << "Target: hardened_visual_proxy_state_machine.hpp\n";
    std::cout << "============================================================================\n\n";

    pse::proxy::hardened::VisualProxyStateMachine sm(0);
    sm.registerSimulationThread();
    sm.configureSession(7365, 1);

    ReentrantFaultSeam seam;
    DummyValidator validator;
    seam.sm = &sm;
    seam.validator = &validator;

    pse::BridgePlayerSnapshot s;
    s.slotId = 0; s.sequence = 1; s.serverTick = 100; s.mapEpoch = 7365;
    s.connectionGeneration = 1; s.entityId = 0x5501; s.entityGeneration = 1;
    s.posX = -7424.0f; s.posY = -6880.0f; s.posZ = 1240.0f;
    s.health = 200;

    sm.handleSnapshot(s, true, true, &seam);
    assert(sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING);

    std::cout << "[AFTER] Calling outer executePendingSpawn()...\n";
    auto outerRes = sm.executePendingSpawn(&seam, &validator, 1007, 259);
    std::cout << "[AFTER] Outer executePendingSpawn result: " << pse::proxy::hardened::toString(outerRes) << "\n";
    std::cout << "[AFTER] Final activeActor in SM: " << sm.activeActor() << "\n";
    std::cout << "[AFTER] Total native spawns:      " << seam.spawnCount << "\n\n";

    bool pass = (seam.innerResult == pse::proxy::hardened::SpawnExecutionResult::ReentrancyBlocked) &&
                (outerRes == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                (seam.spawnCount == 1) && // ONLY 1 actor spawned!
                (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                (sm.activeActor() == reinterpret_cast<void*>(0x61A01000ULL));

    if (pass) {
        std::cout << "[PASS AFTER FIX] Reentrancy safely blocked with ReentrancyBlocked!\n";
        std::cout << "Exactly 1 native actor created in KEX; zero leaks, zero corruption.\n";
        return 0;
    } else {
        std::cout << "[FAIL AFTER FIX] Reentrancy was not properly guarded.\n";
        return 1;
    }
}
