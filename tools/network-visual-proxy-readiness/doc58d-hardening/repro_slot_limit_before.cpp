#include <iostream>
#include <cassert>
#include <windows.h>
#include "../remediation/remediated_visual_proxy_state_machine.hpp"

int main() {
    std::cout << "============================================================================\n";
    std::cout << "DOC-58D Repro: 3-8 Player Slot Limit on DOC-58B Candidate (BEFORE)\n";
    std::cout << "Target: remediated_visual_proxy_state_machine.hpp + snapshot55::Guard\n";
    std::cout << "============================================================================\n\n";

    pse::proxy::remediated::VisualProxyStateMachine sm(3);
    sm.registerSimulationThread();
    sm.configureSession(7365, 1);

    pse::BridgePlayerSnapshot s;
    s.slotId = 3; s.sequence = 1; s.serverTick = 100; s.mapEpoch = 7365;
    s.connectionGeneration = 1; s.entityId = 0x5503; s.entityGeneration = 1;
    s.posX = -7424.0f; s.posY = -6880.0f; s.posZ = 1240.0f;
    s.yaw = 0.0f; s.pitch = 0.0f; s.health = 200;

    std::cout << "[BEFORE] Dispatching valid Snapshot for Slot 3 to VisualProxyStateMachine(3)...\n";
    auto rejection = sm.handleSnapshot(s, true, true);

    std::cout << "[BEFORE] handleSnapshot returned rejection code: "
              << pse::proxy::remediated::toString(rejection) << "\n";
    std::cout << "[BEFORE] Resulting state machine state: "
              << pse::proxy::remediated::toString(sm.state()) << "\n\n";

    if (rejection == pse::proxy::remediated::ProxyRejectionReason::IdentityMismatch &&
        sm.state() == pse::proxy::remediated::VisualProxyState::ABSENT) {
        std::cout << "[CONFIRMED VULNERABILITY BEFORE FIX] Slot 3 rejected with IdentityMismatch by 2-slot Guard!\n";
        return 0; // Successfully reproduced failure
    }
    return 1;
}
