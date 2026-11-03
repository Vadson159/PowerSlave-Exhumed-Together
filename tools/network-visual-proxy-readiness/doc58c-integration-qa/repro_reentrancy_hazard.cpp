#include <iostream>
#include <cassert>
#include <windows.h>
#include "remediated_visual_proxy_state_machine.hpp"

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
            // Reentrantly invoke executePendingSpawn
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
    std::cout << "DOC-58C Repro Fixture: Reentrancy Hazard in VisualProxyStateMachine\n";
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

    std::cout << "[REPRO] Calling outer executePendingSpawn()...\n";
    auto outerRes = sm.executePendingSpawn(&seam, &validator, 1007, 259);
    std::cout << "[REPRO] Outer executePendingSpawn result: " << pse::proxy::remediated::toString(outerRes) << "\n";
    std::cout << "[REPRO] Final activeActor in SM: " << sm.activeActor() << "\n";
    std::cout << "[REPRO] Leaked inner actor:       " << seam.leakedActor << "\n";
    std::cout << "[REPRO] Total native spawns:      " << seam.spawnCount << "\n\n";

    if (seam.spawnCount == 2 && sm.activeActor() != seam.leakedActor) {
        std::cout << "[VULNERABILITY CONFIRMED] Reentrant execution spawned two native actors in KEX,\n";
        std::cout << "but the outer call overwrote m_nativeActorPtr, permanently leaking the inner actor!\n";
        std::cout << "PROPOSED REMEDIATION: Add an atomic/boolean reentrancy guard (m_inSeamOperation)\n";
        std::cout << "or mandate in the Native Integration Contract that native hooks must defer\n";
        std::cout << "callbacks to an event queue and never reenter the state machine synchronously.\n";
        return 0; // Repro successfully demonstrates defect
    } else {
        std::cout << "[REPRO] Reentrancy was unexpectedly blocked.\n";
        return 1;
    }
}
