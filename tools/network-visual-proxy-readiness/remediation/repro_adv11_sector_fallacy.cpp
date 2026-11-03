// Minimal Reproduction Fixture for ADV-11: Hardcoded Sector 259 Fallacy
// Contrasts Baseline defect (accepting hardcoded 259) vs Candidate remediation (Sector Validator contract)

#include <iostream>
#include <cassert>
#include "../include/visual_proxy_state_machine.hpp"
#include "remediated_visual_proxy_state_machine.hpp"

// Baseline Mock
class BaselineSectorMock : public pse::proxy::INativeVisualProxySeam {
public:
    int capturedSector{-1};
    float capturedX{0.0f}, capturedY{0.0f}, capturedZ{0.0f};

    void* spawnVisualActor(uint32_t, float x, float y, float z, float, int sector) override {
        capturedSector = sector;
        capturedX = x; capturedY = y; capturedZ = z;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(0xCAFEBABEEULL));
    }
    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    bool removeVisualActor(void*) override { return true; }
};

// Candidate Mock
class CandidateSectorMock : public pse::proxy::remediated::INativeVisualProxySeamRemediated {
public:
    int capturedSector{-1};
    float capturedX{0.0f}, capturedY{0.0f}, capturedZ{0.0f};

    void* spawnVisualActor(uint32_t, float x, float y, float z, float, int sector) override {
        capturedSector = sector;
        capturedX = x; capturedY = y; capturedZ = z;
        return reinterpret_cast<void*>(static_cast<uintptr_t>(0xCAFEBABEEULL));
    }
    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    pse::proxy::remediated::RemoveOutcome removeVisualActorEx(void*) override {
        return pse::proxy::remediated::RemoveOutcome::ConfirmedSuccess;
    }
};

// Realistic Tomb of Ramses Validator
class RealisticSectorValidator : public pse::proxy::remediated::ISectorValidator {
public:
    bool validateDestinationSector(int sectorId, float x, float y, float) const noexcept override {
        // Sector 259: Spawn Room (-7500..-7300, -6850..-6700)
        if (sectorId == 259) {
            return (x >= -7500.0f && x <= -7300.0f && y >= -6850.0f && y <= -6700.0f);
        }
        // Sector 176: Corridor (-7600..-7400, -7200..-7000)
        if (sectorId == 176) {
            return (x >= -7600.0f && x <= -7400.0f && y >= -7200.0f && y <= -7000.0f);
        }
        return false;
    }
};

int main() {
    std::cout << "====================================================================\n";
    std::cout << "ADV-11 REPRODUCTION: Hardcoded Sector 259 Fallacy\n";
    std::cout << "====================================================================\n\n";

    // Host P1 moved to Sector 176 (Tomb of Ramses hallway) before client connected
    // Coordinates: x = -7488, y = -7100, z = 1240 (inside Sector 176, NOT 259)
    pse::BridgePlayerSnapshot s;
    s.slotId = 0; s.sequence = 1; s.serverTick = 100;
    s.mapEpoch = 7365; s.connectionGeneration = 1;
    s.entityId = 0x5500; s.entityGeneration = 1;
    s.posX = -7488.0f; s.posY = -7100.0f; s.posZ = 1240.0f;
    s.health = 200;

    // 1. Baseline Test
    std::cout << "[STEP 1] Testing Baseline State Machine...\n";
    {
        BaselineSectorMock seam;
        pse::proxy::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        sm.handleSnapshot(s, true, true, &seam);
        assert(sm.state() == pse::proxy::VisualProxyState::SPAWN_PENDING);

        // Baseline caller naively hardcodes 259
        sm.executePendingSpawn(&seam, 1007, 259);

        std::cout << "  Baseline spawned coordinates: [" << seam.capturedX << ", "
                  << seam.capturedY << ", " << seam.capturedZ << "]\n";
        std::cout << "  Baseline linked sector: " << seam.capturedSector << "\n";

        if (seam.capturedSector == 259 && seam.capturedY == -7100.0f) {
            std::cout << "  [BASELINE BUG CONFIRMED] Actor linked into sector 259 actorList,\n"
                      << "  even though coordinates belong to sector 176! Causes invisible sprite in KEX.\n\n";
        }
    }

    // 2. Candidate Test
    std::cout << "[STEP 2] Testing Candidate State Machine with Sector Validator...\n";
    {
        CandidateSectorMock seam;
        RealisticSectorValidator validator;
        pse::proxy::remediated::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        sm.handleSnapshot(s, true, true, &seam);
        assert(sm.state() == pse::proxy::remediated::VisualProxyState::SPAWN_PENDING);

        // Attempting to spawn in sector 259 when coords are in 176
        auto res = sm.executePendingSpawn(&seam, &validator, 1007, 259);

        std::cout << "  Candidate executePendingSpawn with sector 259 returned: "
                  << pse::proxy::remediated::toString(res) << "\n";
        std::cout << "  Candidate resulting state: " << pse::proxy::remediated::toString(sm.state()) << "\n";

        if (res == pse::proxy::remediated::SpawnExecutionResult::SectorValidationFailed &&
            sm.state() == pse::proxy::remediated::VisualProxyState::SPAWN_PENDING) {
            std::cout << "  [CANDIDATE FIX CONFIRMED] Spawn rejected due to coordinate/sector mismatch!\n";

            // Now provide verified sector 176
            auto resCorrect = sm.executePendingSpawn(&seam, &validator, 1007, 176);
            std::cout << "  Candidate executePendingSpawn with verified sector 176 returned: "
                      << pse::proxy::remediated::toString(resCorrect) << "\n";
            std::cout << "  Candidate linked sector: " << seam.capturedSector << "\n";

            if (resCorrect == pse::proxy::remediated::SpawnExecutionResult::Success &&
                seam.capturedSector == 176) {
                std::cout << "  [CANDIDATE VERIFIED] Successfully spawned with matching sector 176!\n";
                return 0;
            }
        }
    }

    return 1;
}
