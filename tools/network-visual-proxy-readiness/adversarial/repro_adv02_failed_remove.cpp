// Minimal Reproduction Case for ADV-02: Failed Remove Ownership Leak
// Target: visual_proxy_state_machine.hpp (lines 252-257)

#include <iostream>
#include <cassert>
#include "../include/visual_proxy_state_machine.hpp"

using namespace pse::proxy;

class FailingRemoveMock : public INativeVisualProxySeam {
public:
    void* spawnVisualActor(uint32_t, float, float, float, float, int) override {
        return reinterpret_cast<void*>(0xDEADBEEF);
    }
    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    bool removeVisualActor(void*) override {
        // Simulate native KEX remove failure (level corrupted, already removed, or lock failure)
        return false;
    }
};

int main() {
    FailingRemoveMock seam;
    VisualProxyStateMachine sm(0);
    sm.registerSimulationThread();
    sm.configureSession(7365, 1);

    // 1. Initial snapshot -> SPAWN_PENDING -> ACTIVE
    pse::BridgePlayerSnapshot s;
    s.slotId = 0; s.sequence = 1; s.serverTick = 100;
    s.mapEpoch = 7365; s.connectionGeneration = 1;
    s.entityId = 0x5500; s.entityGeneration = 1;
    s.posX = -7420.0f; s.posY = -6775.0f; s.posZ = 1152.0f;
    s.health = 200;

    sm.handleSnapshot(s, true, true, &seam);
    sm.executePendingSpawn(&seam, 1007, 259);
    assert(sm.state() == VisualProxyState::ACTIVE);
    assert(sm.activeActor() == reinterpret_cast<void*>(0xDEADBEEF));

    // 2. Disconnect triggers REMOVE_PENDING
    sm.onDisconnect();
    assert(sm.state() == VisualProxyState::REMOVE_PENDING);

    // 3. Attempt remove
    bool ok = sm.executePendingRemove(&seam);

    std::cout << "[REPRO ADV-02] executePendingRemove returned: " << (ok ? "true" : "false") << "\n";
    std::cout << "[REPRO ADV-02] Resulting state: " << toString(sm.state()) << "\n";
    std::cout << "[REPRO ADV-02] Resulting pointer: " << sm.activeActor() << "\n";

    if (!ok && sm.state() == VisualProxyState::ABSENT && sm.activeActor() == nullptr) {
        std::cout << "[BUG CONFIRMED] State machine transitioned to ABSENT and discarded native pointer\n"
                  << "                even though removeVisualActor() failed! Native actor is orphaned in KEX.\n";
        return 1; // Exit 1 to demonstrate bug presence
    }

    return 0;
}
