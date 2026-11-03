// Minimal Reproduction Case for ADV-11: Hardcoded Sector 259 Fallacy
// Demonstrates that spawning in Sector 259 when coordinates belong to Sector 176
// creates an invalid spatial linkage without error detection in the state machine.

#include <iostream>
#include <cassert>
#include "../include/visual_proxy_state_machine.hpp"

using namespace pse::proxy;

class SectorCapturingMock : public INativeVisualProxySeam {
public:
    int capturedSector{-1};
    float capturedX{0.0f}, capturedY{0.0f}, capturedZ{0.0f};

    void* spawnVisualActor(uint32_t, float x, float y, float z, float, int sector) override {
        capturedSector = sector;
        capturedX = x; capturedY = y; capturedZ = z;
        return reinterpret_cast<void*>(0xCAFEBABE);
    }
    bool updateVisualTransform(void*, float, float, float, float, float) override { return true; }
    bool transitionSector(void*, int) override { return true; }
    bool removeVisualActor(void*) override { return true; }
};

int main() {
    SectorCapturingMock seam;
    VisualProxyStateMachine sm(0);
    sm.registerSimulationThread();
    sm.configureSession(7365, 1);

    // Host P1 moved to Sector 176 (Tomb of Ramses hallway) before client connected
    // Coordinates: x = -7488, y = -7100, z = 1240 (inside Sector 176, NOT 259)
    pse::BridgePlayerSnapshot s;
    s.slotId = 0; s.sequence = 1; s.serverTick = 100;
    s.mapEpoch = 7365; s.connectionGeneration = 1;
    s.entityId = 0x5500; s.entityGeneration = 1;
    s.posX = -7488.0f; s.posY = -7100.0f; s.posZ = 1240.0f;
    s.health = 200;

    sm.handleSnapshot(s, true, true, &seam);
    assert(sm.state() == VisualProxyState::SPAWN_PENDING);

    // Naive caller uses previous Gemini recommendation: hardcode sector 259
    const int HARDCODED_SECTOR_259 = 259;
    sm.executePendingSpawn(&seam, 1007, HARDCODED_SECTOR_259);

    std::cout << "[REPRO ADV-11] Spawned coordinates: [" << seam.capturedX << ", "
              << seam.capturedY << ", " << seam.capturedZ << "]\n";
    std::cout << "[REPRO ADV-11] Linked sector: " << seam.capturedSector << "\n";

    if (seam.capturedSector == 259 && seam.capturedY == -7100.0f) {
        std::cout << "[BUG CONFIRMED] Actor linked into sector 259 actorList, but coordinates are in sector 176!\n"
                  << "                In KEX engine, this causes invisible sprite when viewing sector 176,\n"
                  << "                and portal plane traversal assertion failures when rendering sector 259.\n";
        return 1; // Exit 1 to demonstrate assumption failure
    }

    return 0;
}
