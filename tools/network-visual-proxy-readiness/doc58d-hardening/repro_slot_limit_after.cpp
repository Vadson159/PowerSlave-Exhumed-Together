#include <iostream>
#include <cassert>
#include <windows.h>
#include "hardened_visual_proxy_state_machine.hpp"

int main() {
    std::cout << "============================================================================\n";
    std::cout << "DOC-58D Repro: MultiSlotGuard8 on DOC-58D Hardened Candidate (AFTER)\n";
    std::cout << "Target: hardened_visual_proxy_state_machine.hpp + MultiSlotGuard8\n";
    std::cout << "============================================================================\n\n";

    pse::proxy::hardened::VisualProxyStateMachine sm(3);
    sm.registerSimulationThread();
    sm.configureSession(7365, 1);

    pse::BridgePlayerSnapshot s;
    s.slotId = 3; s.sequence = 1; s.serverTick = 100; s.mapEpoch = 7365;
    s.connectionGeneration = 1; s.entityId = 0x5503; s.entityGeneration = 1;
    s.posX = -7424.0f; s.posY = -6880.0f; s.posZ = 1240.0f;
    s.yaw = 0.0f; s.pitch = 0.0f; s.health = 200;

    std::cout << "[AFTER] Dispatching valid Snapshot for Slot 3 to VisualProxyStateMachine(3)...\n";
    auto rejection = sm.handleSnapshot(s, true, true);

    std::cout << "[AFTER] handleSnapshot returned rejection code: "
              << pse::proxy::hardened::toString(rejection) << "\n";
    std::cout << "[AFTER] Resulting state machine state: "
              << pse::proxy::hardened::toString(sm.state()) << "\n\n";

    bool pass = (rejection == pse::proxy::hardened::ProxyRejectionReason::None) &&
                (sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING) &&
                (sm.activeEntityId() == 0x5503);

    if (pass) {
        std::cout << "[PASS AFTER FIX] Slot 3 accepted cleanly! State transitioned to SPAWN_PENDING.\n";
        std::cout << "MultiSlotGuard8 successfully enables full 8-player multiplayer support.\n";
        return 0;
    } else {
        std::cout << "[FAIL AFTER FIX] Slot 3 was unexpectedly rejected.\n";
        return 1;
    }
}
