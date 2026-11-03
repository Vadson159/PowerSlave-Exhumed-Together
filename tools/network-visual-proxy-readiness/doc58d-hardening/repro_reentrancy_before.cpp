#include <iostream>
#include <cassert>
#include <windows.h>
#include "../remediation/remediated_visual_proxy_state_machine.hpp"

namespace {

class ReentrantFaultSeam : public pse::proxy::remediated::INativeVisualProxySeamRemediated {
public:
    pse::proxy::remediated::VisualProxyStateMachine* sm{nullptr};
    pse::proxy::remediated::ISectorValidator* validator{nullptr};
    int spawnCount{0};
    void* leakedActor{nullptr};

    void* spawnVisualActor(uint32_t, float, float, float, float, int) override {
        spawnCount++;
        void* allocatedPtr = reinterpret_cast<void*>(static_cast<uintptr_t>(0x61A00000ULL + spawnCount * 0x1000));

        if (spawnCount == 1 && sm && validator) {
            std::cout << "[REENTRANT SEAM] Native hook triggered reentrant callback during outer spawn...\n";
            auto innerRes = sm->executePendingSpawn(this, validator, 1007, 259);
            std::cout << "[REENTRANT SEAM] Inner executePendingSpawn result: "
                      << pse::proxy::remediated::toString(innerRes) << "\n";
            leakedActor = sm->activeActor();
            std::cout << "[REENTRANT SEAM] Inner activeActor stored: " << leakedActor << "\n";
        }

        return allocatedPtr;
    }

    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    pse::proxy::remediated::RemoveOutcome removeVisualActorEx(void*) override {
        return pse::proxy::remediated::RemoveOutcome::ConfirmedSuccess;
    }
};

class DummyValidator : public pse::proxy::remediated::ISectorValidator {
public:
    bool validateDestinationSector(int, float, float, float) const noexcept override {
        return true;
    }
};

} // namespace

int main() {
    std::cout << "============================================================================\n";
    std::cout << "DOC-58D Repro: Reentrancy Leak on DOC-58B Candidate (BEFORE)\n";
    std::cout << "Target: remediated_visual_proxy_state_machine.hpp\n";
    std::cout << "============================================================================\n\n";

    pse::proxy::remediated::VisualProxyStateMachine sm(0);
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
    assert(sm.state() == pse::proxy::remediated::VisualProxyState::SPAWN_PENDING);

    std::cout << "[BEFORE] Calling outer executePendingSpawn()...\n";
    auto outerRes = sm.executePendingSpawn(&seam, &validator, 1007, 259);
    std::cout << "[BEFORE] Outer executePendingSpawn result: " << pse::proxy::remediated::toString(outerRes) << "\n";
    std::cout << "[BEFORE] Final activeActor in SM: " << sm.activeActor() << "\n";
    std::cout << "[BEFORE] Leaked inner actor:       " << seam.leakedActor << "\n";
    std::cout << "[BEFORE] Total native spawns:      " << seam.spawnCount << "\n\n";

    if (seam.spawnCount == 2 && sm.activeActor() != seam.leakedActor) {
        std::cout << "[CONFIRMED VULNERABILITY BEFORE FIX] Two native actors created in KEX, inner actor leaked!\n";
        return 0; // Successfully reproduced failure
    }
    return 1;
}
