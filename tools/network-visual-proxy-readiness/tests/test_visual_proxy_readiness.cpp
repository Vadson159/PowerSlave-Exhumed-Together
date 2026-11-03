#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <thread>
#include <memory>
#include <cmath>
#include <limits>

#include "visual_proxy_state_machine.hpp"
#include "net_protocol.h"
#include "thread_bridge_dto.h"
#include "thread_bridge.h"

using namespace pse;
using namespace pse::proxy;

// Mock Seam for autonomous testing of state machine and lifecycle invariants
class MockVisualProxySeam : public INativeVisualProxySeam {
public:
    struct SpawnRecord {
        uint32_t actorType{0};
        float x{0.0f}, y{0.0f}, z{0.0f};
        float yaw{0.0f};
        int sector{0};
        void* returnedPtr{nullptr};
    };

    struct TransformRecord {
        void* actor{nullptr};
        float x{0.0f}, y{0.0f}, z{0.0f};
        float yaw{0.0f}, pitch{0.0f};
    };

    bool failNextSpawn{false};
    uint64_t simulatedActorCounter{0x61AA0000};
    std::vector<SpawnRecord> spawns;
    std::vector<TransformRecord> transforms;
    std::vector<int> sectorTransitions;
    std::vector<void*> removes;

    void* spawnVisualActor(uint32_t actorType, float x, float y, float z, float yaw, int sector) override {
        if (failNextSpawn) {
            failNextSpawn = false;
            return nullptr;
        }
        void* ptr = reinterpret_cast<void*>(simulatedActorCounter += 0x100);
        spawns.push_back({actorType, x, y, z, yaw, sector, ptr});
        return ptr;
    }

    bool updateVisualTransform(void* actor, float x, float y, float z, float yaw, float pitch) override {
        transforms.push_back({actor, x, y, z, yaw, pitch});
        return true;
    }

    bool transitionSector(void* /*actor*/, int newSector) override {
        sectorTransitions.push_back(newSector);
        return true;
    }

    bool removeVisualActor(void* actor) override {
        removes.push_back(actor);
        return true;
    }

    void reset() {
        failNextSpawn = false;
        spawns.clear();
        transforms.clear();
        sectorTransitions.clear();
        removes.clear();
    }
};

struct TestResult {
    int id;
    std::string name;
    std::string status; // PASS, FAIL, DEFERRED
    std::string detail;
};

static std::vector<TestResult> g_results;

void recordTest(int id, const std::string& name, const std::string& status, const std::string& detail) {
    g_results.push_back({id, name, status, detail});
    std::cout << "[" << status << "] Test " << id << ": " << name << " - " << detail << "\n";
}

// Helper to create a valid baseline Host P1 snapshot
BridgePlayerSnapshot makeSnapshot(uint8_t slotId, uint32_t seq, uint32_t tick, uint32_t epoch, uint32_t connGen,
                                 uint32_t entityId = 0x5500, uint32_t entityGen = 1,
                                 float x = -7420.0f, float y = -6775.0f, float z = 1152.0f,
                                 float yaw = 0.0f, float pitch = 0.0f) {
    BridgePlayerSnapshot s;
    s.slotId = slotId;
    s.sequence = seq;
    s.serverTick = tick;
    s.mapEpoch = epoch;
    s.connectionGeneration = connGen;
    s.entityId = entityId;
    s.entityGeneration = entityGen;
    s.posX = x;
    s.posY = y;
    s.posZ = z;
    s.yaw = yaw;
    s.pitch = pitch;
    s.health = 200;
    return s;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "PowerSlave Exhumed Together - Network Visual Proxy Readiness QA\n";
    std::cout << "Target: DOC-58B Preparation (Host P1 Snapshot -> Client B Proxy)\n";
    std::cout << "=================================================================\n\n";

    MockVisualProxySeam seam;
    const uint32_t EPOCH = 7365;
    const uint32_t CONN_GEN = 1;
    const uint32_t ACTOR_TYPE = 1007; // Stock Obj_Ramses_Body_Rest verified in DOC-57
    const int DEFAULT_SECTOR = 259;

    // ------------------------------------------------------------------------
    // Scenario 1: Первый корректный Host P1 snapshot
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        auto reason = sm.handleSnapshot(s, true, true, &seam);

        bool step1 = (reason == ProxyRejectionReason::None && sm.state() == VisualProxyState::SPAWN_PENDING);
        bool step2 = sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);
        bool step3 = (sm.state() == VisualProxyState::ACTIVE && sm.activeActor() != nullptr && seam.spawns.size() == 1);

        if (step1 && step2 && step3) {
            recordTest(1, "First valid Host P1 snapshot", "PASS", "ABSENT -> SPAWN_PENDING -> ACTIVE transition successful");
        } else {
            recordTest(1, "First valid Host P1 snapshot", "FAIL", "Failed state transition on valid initial snapshot");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 2: Duplicate snapshot
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s1 = makeSnapshot(0, 10, 1000, EPOCH, CONN_GEN);
        sm.handleSnapshot(s1, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        // Send identical snapshot (duplicate sequence 10)
        auto s_dup = makeSnapshot(0, 10, 1000, EPOCH, CONN_GEN, 0x5500, 1, -7400.0f, -6700.0f, 1152.0f);
        auto reason = sm.handleSnapshot(s_dup, true, true, &seam);

        if (reason == ProxyRejectionReason::StaleSequence && sm.state() == VisualProxyState::ACTIVE && seam.transforms.empty()) {
            recordTest(2, "Duplicate snapshot", "PASS", "Rejected duplicate sequence without modifying active transform");
        } else {
            recordTest(2, "Duplicate snapshot", "FAIL", "Duplicate snapshot was unexpectedly accepted");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 3: Out-of-order snapshot
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s1 = makeSnapshot(0, 20, 2000, EPOCH, CONN_GEN);
        sm.handleSnapshot(s1, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        // Send older sequence 15
        auto s_old = makeSnapshot(0, 15, 1900, EPOCH, CONN_GEN, 0x5500, 1, -7300.0f, -6600.0f, 1152.0f);
        auto reason = sm.handleSnapshot(s_old, true, true, &seam);

        if (reason == ProxyRejectionReason::StaleSequence && sm.state() == VisualProxyState::ACTIVE && seam.transforms.empty()) {
            recordTest(3, "Out-of-order snapshot", "PASS", "Out-of-order sequence dropped monotonically");
        } else {
            recordTest(3, "Out-of-order snapshot", "FAIL", "Out-of-order sequence modified active state");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 4: Wrong epoch
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s_wrong_epoch = makeSnapshot(0, 1, 100, EPOCH + 1, CONN_GEN);
        auto reason = sm.handleSnapshot(s_wrong_epoch, true, true, &seam);

        if (reason == ProxyRejectionReason::IdentityMismatch && sm.state() == VisualProxyState::ABSENT) {
            recordTest(4, "Wrong epoch", "PASS", "Snapshot with mismatched mapEpoch rejected by Guard");
        } else {
            recordTest(4, "Wrong epoch", "FAIL", "Mismatched epoch was not rejected");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 5: Wrong connection generation
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s_wrong_gen = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN + 1);
        auto reason = sm.handleSnapshot(s_wrong_gen, true, true, &seam);

        if (reason == ProxyRejectionReason::IdentityMismatch && sm.state() == VisualProxyState::ABSENT) {
            recordTest(5, "Wrong connection generation", "PASS", "Mismatched connectionGeneration rejected");
        } else {
            recordTest(5, "Wrong connection generation", "FAIL", "Mismatched connection generation was accepted");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 6: Wrong slot
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        // Slot 1 snapshot (Client's local puppet slot on host)
        auto s_slot1 = makeSnapshot(1, 1, 100, EPOCH, CONN_GEN);
        auto reason1 = sm.handleSnapshot(s_slot1, true, true, &seam);

        // Slot 2 snapshot (Third player)
        auto s_slot2 = makeSnapshot(2, 1, 100, EPOCH, CONN_GEN);
        auto reason2 = sm.handleSnapshot(s_slot2, true, true, &seam);

        if (reason1 == ProxyRejectionReason::WrongSlot && reason2 == ProxyRejectionReason::WrongSlot && sm.state() == VisualProxyState::ABSENT) {
            recordTest(6, "Wrong slot", "PASS", "Non-zero slots filtered out, preventing local puppet proxy duplication");
        } else {
            recordTest(6, "Wrong slot", "FAIL", "Non-zero slot triggered proxy state change");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 7: Snapshot before MapReady
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        // mapReady = false
        auto reason = sm.handleSnapshot(s, false, true, &seam);

        if (reason == ProxyRejectionReason::NotReady && sm.state() == VisualProxyState::ABSENT) {
            recordTest(7, "Snapshot before MapReady", "PASS", "Snapshot received before MapReady rejected by Guard");
        } else {
            recordTest(7, "Snapshot before MapReady", "FAIL", "Snapshot before MapReady initiated spawn");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 8: Disconnect while active
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);
        void* spawnedActor = sm.activeActor();

        // Disconnect occurs
        sm.onDisconnect();
        bool step1 = (sm.state() == VisualProxyState::REMOVE_PENDING);
        bool step2 = sm.executePendingRemove(&seam);
        bool step3 = (sm.state() == VisualProxyState::ABSENT && sm.activeActor() == nullptr && seam.removes.size() == 1 && seam.removes[0] == spawnedActor);

        if (step1 && step2 && step3) {
            recordTest(8, "Disconnect while active", "PASS", "ACTIVE -> REMOVE_PENDING -> ABSENT completed safely on disconnect");
        } else {
            recordTest(8, "Disconnect while active", "FAIL", "Disconnect did not cleanly retire active proxy actor");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 9: Stale queued DTO after disconnect
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        // Disconnect
        sm.onDisconnect();
        assert(sm.state() == VisualProxyState::ABSENT);

        // Stale snapshot arriving from queued buffer
        auto s_stale = makeSnapshot(0, 5, 500, EPOCH, CONN_GEN);
        auto reason = sm.handleSnapshot(s_stale, true, true, &seam);

        if (reason == ProxyRejectionReason::SessionInactive && sm.state() == VisualProxyState::ABSENT && seam.spawns.empty()) {
            recordTest(9, "Stale queued DTO after disconnect", "PASS", "Post-disconnect DTO dropped without resurrecting proxy");
        } else {
            recordTest(9, "Stale queued DTO after disconnect", "FAIL", "Post-disconnect DTO resurrected proxy or initiated spawn");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 10: Map invalidation
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        // Map invalidated (level unload or transition)
        sm.onMapInvalidated();
        bool step1 = (sm.state() == VisualProxyState::REMOVE_PENDING);
        sm.executePendingRemove(&seam);
        bool step2 = (sm.state() == VisualProxyState::ABSENT);

        if (step1 && step2) {
            recordTest(10, "Map invalidation", "PASS", "Map invalidation triggers immediate REMOVE_PENDING -> ABSENT");
        } else {
            recordTest(10, "Map invalidation", "FAIL", "Map invalidation failed to retire actor");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 11: Shutdown with pending removal
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        sm.onDisconnect(); // state is now REMOVE_PENDING
        assert(sm.state() == VisualProxyState::REMOVE_PENDING);

        // Force shutdown invoked during client tear-down
        sm.forceShutdown(&seam);

        if (sm.state() == VisualProxyState::ABSENT && sm.activeActor() == nullptr && seam.removes.size() == 1) {
            recordTest(11, "Shutdown with pending removal", "PASS", "forceShutdown synchronously cleaned up pending removal");
        } else {
            recordTest(11, "Shutdown with pending removal", "FAIL", "Shutdown left unremoved actor or invalid state");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 12: Repeated spawn request
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s1 = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s1, true, true, &seam); // state = SPAWN_PENDING

        // Second snapshot arrives before spawn is executed
        auto s2 = makeSnapshot(0, 2, 110, EPOCH, CONN_GEN, 0x5500, 1, -7410.0f, -6770.0f, 1152.0f);
        auto reason2 = sm.handleSnapshot(s2, true, true, &seam);

        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        // Third snapshot arrives after already ACTIVE
        auto s3 = makeSnapshot(0, 3, 120, EPOCH, CONN_GEN, 0x5500, 1, -7400.0f, -6760.0f, 1152.0f);
        auto reason3 = sm.handleSnapshot(s3, true, true, &seam);

        if (reason2 == ProxyRejectionReason::None && reason3 == ProxyRejectionReason::None &&
            sm.spawnCount() == 1 && seam.spawns.size() == 1) {
            recordTest(12, "Repeated spawn request", "PASS", "Multiple incoming snapshots resulted in exactly one native spawn");
        } else {
            recordTest(12, "Repeated spawn request", "FAIL", "Duplicate spawn was triggered");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 13: Double remove attempt
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        sm.onDisconnect();
        bool remove1 = sm.executePendingRemove(&seam);
        bool remove2 = sm.executePendingRemove(&seam); // Erroneous second remove call

        if (remove1 && !remove2 && sm.state() == VisualProxyState::ABSENT && seam.removes.size() == 1) {
            recordTest(13, "Double remove attempt", "PASS", "Second remove attempt was a safe no-op on null actor");
        } else {
            recordTest(13, "Double remove attempt", "FAIL", "Double remove attempted illegal dereference or operation");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 14: Actor generation mismatch
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s1 = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN, 0x5500, 1);
        sm.handleSnapshot(s1, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, DEFAULT_SECTOR);

        // Host P1 respawns: entityGeneration advances to 2
        auto s_respawn = makeSnapshot(0, 2, 200, EPOCH, CONN_GEN, 0x5500, 2);
        auto reason = sm.handleSnapshot(s_respawn, true, true, &seam);

        if (reason == ProxyRejectionReason::EntityMismatch && sm.state() == VisualProxyState::REMOVE_PENDING) {
            recordTest(14, "Actor generation mismatch", "PASS", "Entity generation change detected, scheduled old proxy removal");
        } else {
            recordTest(14, "Actor generation mismatch", "FAIL", "Entity generation mismatch was not caught");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 15: Invalid transform values
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        float nanVal = std::nanf("");
        float infVal = std::numeric_limits<float>::infinity();

        auto s_nan = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN, 0x5500, 1, nanVal, -6775.0f, 1152.0f);
        auto reason_nan = sm.handleSnapshot(s_nan, true, true, &seam);

        auto s_inf = makeSnapshot(0, 2, 110, EPOCH, CONN_GEN, 0x5500, 1, -7420.0f, infVal, 1152.0f);
        auto reason_inf = sm.handleSnapshot(s_inf, true, true, &seam);

        if (reason_nan == ProxyRejectionReason::NonfiniteTransform &&
            reason_inf == ProxyRejectionReason::NonfiniteTransform &&
            sm.state() == VisualProxyState::ABSENT) {
            recordTest(15, "Invalid transform values", "PASS", "NaN and Infinity transform payloads cleanly rejected");
        } else {
            recordTest(15, "Invalid transform values", "FAIL", "Nonfinite transform payload accepted");
        }
    }

    // ------------------------------------------------------------------------
    // Scenario 16: Worker/Simulation Thread ownership
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        bool workerCaughtAffinity = false;
        std::thread workerThread([&]() {
            // Simulated network worker thread attempting to touch the proxy state machine directly
            auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
            auto reason = sm.handleSnapshot(s, true, true, &seam);
            if (reason == ProxyRejectionReason::AffinityViolation) {
                workerCaughtAffinity = true;
            }
        });
        workerThread.join();

        if (workerCaughtAffinity && sm.state() == VisualProxyState::ABSENT) {
            recordTest(16, "Worker/Simulation Thread ownership", "PASS", "Direct worker thread mutation rejected by affinity guard");
        } else {
            recordTest(16, "Worker/Simulation Thread ownership", "FAIL", "Worker thread was permitted to mutate proxy state");
        }
    }

    // ------------------------------------------------------------------------
    // DEFERRED Scenario A: Live KEX Dynamic Sector Traversal & LinkSector
    // (Requires DOC-58A Native Movement Seam)
    // ------------------------------------------------------------------------
    recordTest(17, "Live KEX Dynamic Sector Traversal (DOC-58A Dependency)", "DEFERRED",
               "Requires live KEX LinkSector integration and sector boundary traversal from DOC-58A");

    // ------------------------------------------------------------------------
    // Summary Accounting
    // ------------------------------------------------------------------------
    std::cout << "\n=================================================================\n";
    std::cout << "TEST SUITE EXECUTION SUMMARY\n";
    std::cout << "=================================================================\n";
    int passCount = 0, failCount = 0, deferredCount = 0;
    for (const auto& r : g_results) {
        if (r.status == "PASS") passCount++;
        else if (r.status == "FAIL") failCount++;
        else if (r.status == "DEFERRED") deferredCount++;
    }
    std::cout << "Total Checks: " << g_results.size() << "\n";
    std::cout << "PASS: " << passCount << " | FAIL: " << failCount << " | DEFERRED: " << deferredCount << "\n";

    return (failCount == 0) ? 0 : 1;
}
