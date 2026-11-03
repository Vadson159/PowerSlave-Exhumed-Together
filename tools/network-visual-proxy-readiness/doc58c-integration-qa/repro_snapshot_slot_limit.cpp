#include <iostream>
#include <cassert>
#include <windows.h>
#include "remediated_visual_proxy_state_machine.hpp"

int main() {
    std::cout << "============================================================================\n";
    std::cout << "DOC-58C Repro Fixture: 3-8 Player Slot Rejection Defect\n";
    std::cout << "Target: remediated_visual_proxy_state_machine.hpp + snapshot55::Guard\n";
    std::cout << "============================================================================\n\n";

    // Scenario: Client B joins a 4-player game (Players 0, 1, 2, 3)
    // Client B runs a VisualProxyStateMachine to represent remote Player 3 (slot 3)
    pse::proxy::remediated::VisualProxyStateMachine sm(3);
    sm.registerSimulationThread();
    sm.configureSession(7365, 1);

    // Create a valid, fully formed BridgePlayerSnapshot for Slot 3
    pse::BridgePlayerSnapshot s;
    s.slotId = 3;
    s.sequence = 1;
    s.serverTick = 100;
    s.mapEpoch = 7365;
    s.connectionGeneration = 1;
    s.entityId = 0x5503;
    s.entityGeneration = 1;
    s.posX = -7424.0f;
    s.posY = -6880.0f;
    s.posZ = 1240.0f;
    s.yaw = 0.0f;
    s.pitch = 0.0f;
    s.health = 200;

    std::cout << "[REPRO] Dispatching valid Snapshot for Slot 3 to VisualProxyStateMachine(3)...\n";
    auto rejection = sm.handleSnapshot(s, true /* mapReady */, true /* isGameplay */);

    std::cout << "[REPRO] handleSnapshot returned rejection code: "
              << pse::proxy::remediated::toString(rejection) << "\n";
    std::cout << "[REPRO] Resulting state machine state: "
              << pse::proxy::remediated::toString(sm.state()) << "\n\n";

    if (rejection == pse::proxy::remediated::ProxyRejectionReason::IdentityMismatch &&
        sm.state() == pse::proxy::remediated::VisualProxyState::ABSENT) {
        std::cout << "[VULNERABILITY CONFIRMED] Remote player visual proxies for Slots 2..7 are\n";
        std::cout << "permanently blocked from spawning or updating!\n";
        std::cout << "ROOT CAUSE: VisualProxyStateMachine embeds snapshot55::Guard m_guard,\n";
        std::cout << "which defines 'std::array<Slot, 2> slots;' hardcoding a 2-player limit.\n";
        std::cout << "When s.slotId >= slots.size() (slot >= 2), Guard::accept() returns Verdict::Identity,\n";
        std::cout << "which the state machine translates to ProxyRejectionReason::IdentityMismatch.\n";
        std::cout << "PROPOSED REMEDIATION: Update snapshot guard to support pse::MAX_PLAYERS (8 slots),\n";
        std::cout << "e.g. 'std::array<Slot, pse::MAX_PLAYERS> slots;' in snapshot contract.\n";
        return 0; // Successfully reproduced candidate defect
    } else {
        std::cout << "[REPRO] Unexpected result: snapshot was not rejected by Guard.\n";
        return 1;
    }
}
