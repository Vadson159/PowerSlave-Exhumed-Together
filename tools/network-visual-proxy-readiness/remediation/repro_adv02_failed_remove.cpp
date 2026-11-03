// Minimal Reproduction Fixture for ADV-02: Failed Remove Ownership Leak
// Contrasts Baseline defect (lines 252-257) vs Candidate remediation (Quarantine model)

#include <iostream>
#include <cassert>
#include "../include/visual_proxy_state_machine.hpp"
#include "remediated_visual_proxy_state_machine.hpp"

// Baseline Mock
class BaselineFailingSeam : public pse::proxy::INativeVisualProxySeam {
public:
    void* spawnVisualActor(uint32_t, float, float, float, float, int) override {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEFULL));
    }
    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    bool removeVisualActor(void*) override { return false; } // Native remove fails!
};

// Candidate Mock
class CandidateFailingSeam : public pse::proxy::remediated::INativeVisualProxySeamRemediated {
public:
    void* spawnVisualActor(uint32_t, float, float, float, float, int) override {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEFULL));
    }
    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    pse::proxy::remediated::RemoveOutcome removeVisualActorEx(void*) override {
        return pse::proxy::remediated::RemoveOutcome::DefiniteFailure; // Explicit definite failure!
    }
};

class DummyValidator : public pse::proxy::remediated::ISectorValidator {
public:
    bool validateDestinationSector(int, float, float, float) const noexcept override { return true; }
};

int main() {
    std::cout << "====================================================================\n";
    std::cout << "ADV-02 REPRODUCTION: Failed Remove Ownership Leak\n";
    std::cout << "====================================================================\n\n";

    // 1. Baseline Run
    std::cout << "[STEP 1] Testing Baseline State Machine...\n";
    {
        BaselineFailingSeam seam;
        pse::proxy::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        pse::BridgePlayerSnapshot s;
        s.slotId = 0; s.sequence = 1; s.serverTick = 100;
        s.mapEpoch = 7365; s.connectionGeneration = 1;
        s.entityId = 0x5500; s.entityGeneration = 1;
        s.posX = -7420.0f; s.posY = -6775.0f; s.posZ = 1152.0f;
        s.health = 200;

        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, 1007, 259);
        assert(sm.state() == pse::proxy::VisualProxyState::ACTIVE);
        assert(sm.activeActor() == reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEFULL)));

        sm.onDisconnect();
        assert(sm.state() == pse::proxy::VisualProxyState::REMOVE_PENDING);

        bool ok = sm.executePendingRemove(&seam);
        std::cout << "  Baseline executePendingRemove returned: " << (ok ? "true" : "false") << "\n";
        std::cout << "  Baseline resulting state: " << pse::proxy::toString(sm.state()) << "\n";
        std::cout << "  Baseline resulting pointer: " << sm.activeActor() << "\n";

        if (!ok && sm.state() == pse::proxy::VisualProxyState::ABSENT && sm.activeActor() == nullptr) {
            std::cout << "  [BASELINE BUG CONFIRMED] Pointer nullified and state set to ABSENT despite failure!\n"
                      << "  Actor 0xDEADBEEF is orphaned in KEX engine memory and sector actorList.\n\n";
        }
    }

    // 2. Candidate Run
    std::cout << "[STEP 2] Testing Candidate State Machine...\n";
    {
        CandidateFailingSeam seam;
        DummyValidator validator;
        pse::proxy::remediated::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        pse::BridgePlayerSnapshot s;
        s.slotId = 0; s.sequence = 1; s.serverTick = 100;
        s.mapEpoch = 7365; s.connectionGeneration = 1;
        s.entityId = 0x5500; s.entityGeneration = 1;
        s.posX = -7420.0f; s.posY = -6775.0f; s.posZ = 1152.0f;
        s.health = 200;

        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        assert(sm.state() == pse::proxy::remediated::VisualProxyState::ACTIVE);

        sm.onDisconnect();
        auto res = sm.executePendingRemove(&seam);
        std::cout << "  Candidate executePendingRemove returned: " << pse::proxy::remediated::toString(res) << "\n";
        std::cout << "  Candidate resulting state: " << pse::proxy::remediated::toString(sm.state()) << "\n";
        std::cout << "  Candidate resulting reason: " << pse::proxy::remediated::toString(sm.quarantineReason()) << "\n";
        std::cout << "  Candidate resulting pointer: " << sm.activeActor() << "\n";

        if (sm.state() == pse::proxy::remediated::VisualProxyState::QUARANTINED &&
            sm.activeActor() == reinterpret_cast<void*>(static_cast<uintptr_t>(0xDEADBEEFULL))) {
            std::cout << "  [CANDIDATE FIX CONFIRMED] Actor placed in QUARANTINED state!\n"
                      << "  Pointer preserved for attested cleanup; orphan leak prevented.\n";
            return 0;
        }
    }

    return 1;
}
