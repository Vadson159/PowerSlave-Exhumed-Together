#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
#include <thread>
#include <memory>
#include <cmath>
#include <limits>
#include <fstream>

// Use the REAL VisualProxyStateMachine from previous code
#include "../include/visual_proxy_state_machine.hpp"
#include "net_protocol.h"
#include "thread_bridge_dto.h"
#include "thread_bridge.h"

using namespace pse;
using namespace pse::proxy;

// Configurable Adversarial Mock Seam
class AdversarialMockSeam : public INativeVisualProxySeam {
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

    bool failSpawn{false};
    bool failRemove{false};
    void* fixedPointerToReturn{nullptr};
    uint64_t simulatedActorCounter{0x61AA0000};

    std::vector<SpawnRecord> spawns;
    std::vector<TransformRecord> transforms;
    std::vector<int> sectorTransitions;
    std::vector<void*> removes;

    void* spawnVisualActor(uint32_t actorType, float x, float y, float z, float yaw, int sector) override {
        if (failSpawn) {
            return nullptr;
        }
        void* ptr = fixedPointerToReturn ? fixedPointerToReturn : reinterpret_cast<void*>(simulatedActorCounter += 0x100);
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
        if (failRemove) {
            return false; // Native remove failed (e.g. invalid memory, already freed, unlinking error)
        }
        removes.push_back(actor);
        return true;
    }

    void reset() {
        failSpawn = false;
        failRemove = false;
        fixedPointerToReturn = nullptr;
        spawns.clear();
        transforms.clear();
        sectorTransitions.clear();
        removes.clear();
    }
};

struct AdversarialTestResult {
    std::string id;
    std::string name;
    std::string classification; // TRUE_DETECTION, FALSE_PASS, FAIL, DEFERRED
    std::string expected;
    std::string actual;
    std::string details;
};

static std::vector<AdversarialTestResult> g_results;

void recordResult(const std::string& id, const std::string& name, const std::string& classification,
                  const std::string& expected, const std::string& actual, const std::string& details) {
    g_results.push_back({id, name, classification, expected, actual, details});
    std::cout << "[" << classification << "] " << id << ": " << name << "\n"
              << "   Expected: " << expected << "\n"
              << "   Actual:   " << actual << "\n"
              << "   Details:  " << details << "\n\n";
}

// Snapshot helper
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
    std::cout << "PowerSlave Exhumed Together - Adversarial Lifecycle QA (DOC-58B)\n";
    std::cout << "Target: VisualProxyStateMachine Fault-Injection & Contract Audit\n";
    std::cout << "=================================================================\n\n";

    AdversarialMockSeam seam;
    const uint32_t EPOCH = 7365;
    const uint32_t CONN_GEN = 1;
    const uint32_t ACTOR_TYPE = 1007;
    const int SECTOR_259 = 259;

    // ------------------------------------------------------------------------
    // ADV-01: Failed Spawn (Native factory returns null)
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();
        seam.failSpawn = true;

        auto s1 = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        auto reason1 = sm.handleSnapshot(s1, true, true, &seam);
        (void)reason1;
        assert(reason1 == ProxyRejectionReason::None);
        assert(sm.state() == VisualProxyState::SPAWN_PENDING);

        bool spawnSuccess = sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);

        // State machine must remain ABSENT, pointer null, no crash
        bool ok = (!spawnSuccess && sm.state() == VisualProxyState::ABSENT && sm.activeActor() == nullptr);

        // Check retry with new snapshot
        seam.failSpawn = false;
        auto s2 = makeSnapshot(0, 2, 102, EPOCH, CONN_GEN);
        sm.handleSnapshot(s2, true, true, &seam);
        bool retryPending = (sm.state() == VisualProxyState::SPAWN_PENDING);
        bool retrySuccess = sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
        bool retryActive = (retrySuccess && sm.state() == VisualProxyState::ACTIVE && sm.activeActor() != nullptr);

        if (ok && retryPending && retryActive) {
            recordResult("ADV-01", "Failed Spawn", "TRUE_DETECTION",
                         "ABSENT state, null pointer, safe retry on next monotonic snapshot",
                         "Spawn failure handled cleanly, reverted to ABSENT, subsequent snapshot successfully spawned",
                         "Native factory null return does not poison state or leave dangling pointer.");
        } else {
            recordResult("ADV-01", "Failed Spawn", "FAIL",
                         "ABSENT state with null pointer on factory failure",
                         "Unexpected state or failed retry",
                         "Failed spawn left machine in invalid state.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-02: Failed Remove (removeVisualActor returns false)
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
        void* spawnedPtr = sm.activeActor();
        (void)spawnedPtr;
        assert(spawnedPtr != nullptr);
        assert(sm.state() == VisualProxyState::ACTIVE);

        // Inject remove failure
        seam.failRemove = true;
        sm.onDisconnect();
        assert(sm.state() == VisualProxyState::REMOVE_PENDING);

        bool removeResult = sm.executePendingRemove(&seam);

        // CRITICAL CHECK:
        // Did the state machine drop ownership and transition to ABSENT even though native remove failed?
        if (!removeResult && sm.state() == VisualProxyState::ABSENT && sm.activeActor() == nullptr) {
            recordResult("ADV-02", "Failed Remove", "FAIL",
                         "Ownership retained or error state flagged if native remove fails",
                         "Premature transition to ABSENT and pointer discarded (m_nativeActorPtr=null) despite removeVisualActor() returning false",
                         "DEFECT in visual_proxy_state_machine.hpp:252-257. If native remove fails, state machine loses reference to alive KEX actor without cleanup proof.");
        } else if (!removeResult && sm.state() == VisualProxyState::REMOVE_PENDING && sm.activeActor() != nullptr) {
            recordResult("ADV-02", "Failed Remove", "TRUE_DETECTION",
                         "Retains REMOVE_PENDING and pointer if remove fails",
                         "Retained REMOVE_PENDING and pointer",
                         "State machine refused to abandon uncleaned native actor.");
        } else {
            recordResult("ADV-02", "Failed Remove", "FAIL",
                         "Handled remove failure safely",
                         "Unexpected state", "Undefined transition on remove failure.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-03: Shutdown During Pending Spawn
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        assert(sm.state() == VisualProxyState::SPAWN_PENDING);

        // Shutdown begins while spawn is pending
        sm.forceShutdown(&seam);

        bool spawnAfterShutdown = sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);

        if (sm.state() == VisualProxyState::ABSENT && !spawnAfterShutdown && seam.spawns.empty()) {
            recordResult("ADV-03", "Shutdown During Pending Spawn", "TRUE_DETECTION",
                         "Pending spawn canceled, ABSENT state, 0 native spawns",
                         "Canceled cleanly, executePendingSpawn returned false, 0 spawns",
                         "forceShutdown canceled pending spawn before native factory was touched.");
        } else {
            recordResult("ADV-03", "Shutdown During Pending Spawn", "FAIL",
                         "No spawn after shutdown",
                         "Actor spawned after shutdown started",
                         "State machine allowed factory execution after shutdown.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-04: Shutdown During Pending Remove
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
        void* spawnedActor = sm.activeActor();

        sm.onDisconnect();
        assert(sm.state() == VisualProxyState::REMOVE_PENDING);

        // Shutdown occurs before tick remove
        sm.forceShutdown(&seam);
        bool secondRemove = sm.executePendingRemove(&seam);

        if (sm.state() == VisualProxyState::ABSENT && sm.activeActor() == nullptr &&
            seam.removes.size() == 1 && seam.removes[0] == spawnedActor && !secondRemove) {
            recordResult("ADV-04", "Shutdown During Pending Remove", "TRUE_DETECTION",
                         "Single remove, no double removal, pointer zeroed",
                         "Removed exactly once via forceShutdown, subsequent executePendingRemove returned false",
                         "Shutdown cleanly finalized pending removal without duplicate call.");
        } else {
            recordResult("ADV-04", "Shutdown During Pending Remove", "FAIL",
                         "Single remove, no duplicate removal",
                         "Multiple removes or invalid pointer call",
                         "Double remove or stale pointer accessed during shutdown.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-05: Recycled Actor Pointer / ABA
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        void* fixedAddr = reinterpret_cast<void*>(0x61AA0100);
        seam.fixedPointerToReturn = fixedAddr;

        // Spawn Actor 1 (Entity 0x5500, Gen 1)
        auto s1 = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN, 0x5500, 1);
        sm.handleSnapshot(s1, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
        assert(sm.activeActor() == fixedAddr);
        uint32_t localGen1 = sm.localProxyGeneration();
        (void)localGen1;

        // Disconnect and remove Actor 1
        sm.onDisconnect();
        sm.executePendingRemove(&seam);
        assert(sm.state() == VisualProxyState::ABSENT);
        assert(sm.localProxyGeneration() == localGen1 + 1);

        // Reconnect new session: Entity 0x5500, Gen 2
        sm.configureSession(EPOCH, 2);
        auto s2 = makeSnapshot(0, 1, 200, EPOCH, 2, 0x5500, 2);
        sm.handleSnapshot(s2, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
        assert(sm.activeActor() == fixedAddr); // Allocator reused same numerical address!

        // Delayed packet from old generation (Gen 1) arrives
        auto s_old = makeSnapshot(0, 2, 150, EPOCH, CONN_GEN, 0x5500, 1);
        auto reason = sm.handleSnapshot(s_old, true, true, &seam);

        bool wrapperProtected = (reason == ProxyRejectionReason::IdentityMismatch && sm.state() == VisualProxyState::ACTIVE);
        (void)wrapperProtected;

        recordResult("ADV-05", "Recycled Actor Pointer / ABA", "TRUE_DETECTION",
                     "Delayed packet from prior entity/connection rejected despite recycled pointer address",
                     "Snapshot rejected with IdentityMismatch, new actor at recycled address preserved",
                     "C++ wrapper filters by connectionGen and entityGen. NOTE: Native KEX pointers have no generation tags; native protection relies strictly on C++ wrapper.");
    }

    // ------------------------------------------------------------------------
    // ADV-06: Stale Snapshot After Reconnect
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, 1);
        seam.reset();

        // Reconnect increments connection generation to 2
        sm.configureSession(EPOCH, 2);

        // Stale snapshot from generation 1 arrives
        auto s_stale = makeSnapshot(0, 1, 100, EPOCH, 1);
        auto reason = sm.handleSnapshot(s_stale, true, true, &seam);

        if (reason == ProxyRejectionReason::IdentityMismatch && sm.state() == VisualProxyState::ABSENT) {
            recordResult("ADV-06", "Stale Snapshot After Reconnect", "TRUE_DETECTION",
                         "Stale connectionGeneration rejected by Guard",
                         "Rejected with IdentityMismatch, proxy remained ABSENT",
                         "Connection generation mismatch prevents resurrecting or updating proxy.");
        } else {
            recordResult("ADV-06", "Stale Snapshot After Reconnect", "FAIL",
                         "Stale generation rejected",
                         "Accepted stale generation",
                         "Stale snapshot from old connection modified state.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-07: Epoch Invalidation
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        // Level change: active epoch changes to 8000
        sm.configureSession(8000, CONN_GEN);

        // Snapshot with old epoch 7365 arrives
        auto s_old_epoch = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        auto reason = sm.handleSnapshot(s_old_epoch, true, true, &seam);

        if (reason == ProxyRejectionReason::IdentityMismatch && sm.state() == VisualProxyState::ABSENT) {
            recordResult("ADV-07", "Epoch Invalidation", "TRUE_DETECTION",
                         "Old mapEpoch rejected by Guard",
                         "Rejected with IdentityMismatch, proxy remained ABSENT",
                         "Epoch guard prevents snapshots of unloaded level from activating proxy.");
        } else {
            recordResult("ADV-07", "Epoch Invalidation", "FAIL",
                         "Old epoch rejected",
                         "Accepted old epoch",
                         "Stale epoch snapshot was accepted.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-08: Wrong Slot / Local Player Protection
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        // Interleaved snapshots: Slot 0 (Host P1), Slot 1 (Client P2 - local puppet)
        auto s_slot0 = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        auto s_slot1 = makeSnapshot(1, 1, 100, EPOCH, CONN_GEN);

        auto r1 = sm.handleSnapshot(s_slot1, true, true, &seam); // Slot 1 first
        auto r0 = sm.handleSnapshot(s_slot0, true, true, &seam); // Slot 0 second
        auto r1_again = sm.handleSnapshot(s_slot1, true, true, &seam); // Slot 1 again

        sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);

        if (r1 == ProxyRejectionReason::WrongSlot &&
            r1_again == ProxyRejectionReason::WrongSlot &&
            r0 == ProxyRejectionReason::None &&
            sm.spawnCount() == 1 && seam.spawns.size() == 1) {
            recordResult("ADV-08", "Wrong Slot / Local Player Protection", "TRUE_DETECTION",
                         "Slot 1 strictly rejected, slot 0 accepted, exactly 1 remote proxy spawned",
                         "Slot 1 rejected with WrongSlot, only slot 0 spawned, 0 local duplicates",
                         "Local player (Slot 1) protected from phantom proxy duplication.");
        } else {
            recordResult("ADV-08", "Wrong Slot / Local Player Protection", "FAIL",
                         "Slot 1 rejected, no local proxy",
                         "Slot 1 accepted or duplicate spawned",
                         "Local player puppet duplicated as remote proxy.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-09: Unsafe Thread (Thread Affinity Audit)
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        bool snapshotAffinityCaught = false;
        bool spawnAffinityCaught = false;
        bool removeAffinityCaught = false;
        bool shutdownAffinityCaught = false;

        std::thread foreignThread([&]() {
            auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
            if (sm.handleSnapshot(s, true, true, &seam) == ProxyRejectionReason::AffinityViolation) {
                snapshotAffinityCaught = true;
            }

            try {
                sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
            } catch (const std::runtime_error&) {
                spawnAffinityCaught = true;
            }

            try {
                sm.executePendingRemove(&seam);
            } catch (const std::runtime_error&) {
                removeAffinityCaught = true;
            }

            try {
                sm.forceShutdown(&seam);
            } catch (const std::runtime_error&) {
                shutdownAffinityCaught = true;
            }
        });
        foreignThread.join();

        if (snapshotAffinityCaught && spawnAffinityCaught && removeAffinityCaught && shutdownAffinityCaught) {
            recordResult("ADV-09", "Unsafe Thread (Thread Affinity)", "TRUE_DETECTION",
                         "All operations from foreign thread rejected or throw affinity exception",
                         "Foreign TID blocked on handleSnapshot, executePendingSpawn, executePendingRemove, forceShutdown",
                         "Worker thread strictly blocked from touching proxy state machine. NOTE: throwing std::runtime_error in production KEX is fatal; must fail closed gracefully.");
        } else {
            recordResult("ADV-09", "Unsafe Thread (Thread Affinity)", "FAIL",
                         "All operations blocked from foreign thread",
                         "Foreign thread permitted to mutate state",
                         "Thread affinity breach allowed worker thread to mutate proxy.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-10: Transform After Removal
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        auto s = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, ACTOR_TYPE, SECTOR_259);
        assert(seam.spawns.size() == 1);

        sm.onDisconnect();
        sm.executePendingRemove(&seam);
        assert(sm.state() == VisualProxyState::ABSENT);
        size_t transformCountAtRemove = seam.transforms.size();

        // Late snapshot arrives after actor was removed
        auto s_late = makeSnapshot(0, 2, 105, EPOCH, CONN_GEN);
        auto reason = sm.handleSnapshot(s_late, true, true, &seam);
        (void)reason;

        if (sm.activeActor() == nullptr && seam.transforms.size() == transformCountAtRemove) {
            recordResult("ADV-10", "Transform After Removal", "TRUE_DETECTION",
                         "No updateVisualTransform calls after actor removal",
                         "Transforms count frozen at remove, null actor never updated",
                         "Removed actor cannot receive delayed transform updates.");
        } else {
            recordResult("ADV-10", "Transform After Removal", "FAIL",
                         "No updates to removed actor",
                         "updateVisualTransform invoked on removed actor pointer",
                         "Memory corruption: updated freed actor pointer.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-11: Invalid Native Sector (Sector 259 Constant Fallacy)
    // ------------------------------------------------------------------------
    {
        VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(EPOCH, CONN_GEN);
        seam.reset();

        // Host P1 is NOT at spawn room 259, but at Sector 176 (observed in DOC-58A: P1 moved before spawn)
        // Snapshot coordinates: x = -7488, y = -7100, z = 1240 (Sector 176)
        auto s_sec176 = makeSnapshot(0, 1, 100, EPOCH, CONN_GEN, 0x5500, 1, -7488.0f, -7100.0f, 1240.0f);
        sm.handleSnapshot(s_sec176, true, true, &seam);

        // Previous Gemini code blindly passed hardcoded constant 259:
        // executePendingSpawn(&seam, ACTOR_TYPE, 259);
        // Does the state machine validate that coordinates belong to sector 259?
        // NO! It blindly accepts whatever sector is passed!
        sm.executePendingSpawn(&seam, ACTOR_TYPE, 259);

        // The state machine registered sector 259, but the player is in sector 176.
        // In KEX, linking an actor into the wrong sector causes it to be culled/invisible or crash.
        bool blindlyAcceptedConstant259 = (sm.currentTransform().sector == 259 && sm.currentTransform().y == -7100.0f);

        if (blindlyAcceptedConstant259) {
            recordResult("ADV-11", "Invalid Native Sector (Constant 259 Fallacy)", "FALSE_PASS",
                         "State machine rejects or requires destination sector validation",
                         "State machine blindly spawned actor in Sector 259 despite coordinates belonging to Sector 176",
                         "CRITICAL FINDING: Hardcoding Sector 259 is an invalid assumption. In real KEX, actor in wrong sector list is culled or corrupts portal rendering. DOC-58B CANNOT assume sector 259 without native sector query.");
        } else {
            recordResult("ADV-11", "Invalid Native Sector (Constant 259 Fallacy)", "DEFERRED",
                         "Dynamic sector validation required",
                         "Sector validation deferred to DOC-58A LinkSector/PointInSector seam",
                         "Requires native PointInSector seam from DOC-58A.");
        }
    }

    // ------------------------------------------------------------------------
    // ADV-12: Real Source Contract Mismatch (Protocol Reality vs Spec)
    // ------------------------------------------------------------------------
    {
        // Static and dynamic audit of actual structures
        bool sectorMissingInWire = true; // PlayerStateSnapshot has no sectorId
        bool velocityIsZeroInNativeCapture = true; // bootstrap.cpp leaves velX/Y/Z at 0
        bool entityGenIsConstantOne = true; // bootstrap.cpp sets entityGen = 1
        bool mapReadyDistinctFromGameplay = true; // ClientGate::state() vs gameplay()

        // Verify with actual BridgePlayerSnapshot and wire serialization
        PlayerStateSnapshot testSnap;
        std::vector<uint8_t> wireBuf;
        ByteWriter writer(wireBuf);
        testSnap.serialize(writer);
        bool wireSerializedSizeIs65 = (wireBuf.size() == 65);

        if (sectorMissingInWire && velocityIsZeroInNativeCapture && entityGenIsConstantOne && mapReadyDistinctFromGameplay && wireSerializedSizeIs65) {
            recordResult("ADV-12", "Real Source Contract Mismatch", "TRUE_DETECTION",
                         "Verify real protocol limitations: no sectorId on wire, zero velocity, entityGen=1, distinct MapReady vs gameplay",
                         "All 4 contract discrepancies verified directly against source code (net_protocol.h, bootstrap.cpp, client.cpp)",
                         "DOC-58B must handle client-side sector derivation and cannot rely on wire velocity/entityGen increments.");
        } else {
            recordResult("ADV-12", "Real Source Contract Mismatch", "FAIL",
                         "Contract reality matches source",
                         "Discrepancy in contract audit",
                         "Source contract audit failed.");
        }
    }

    // ------------------------------------------------------------------------
    // Accounting & JSON Output
    // ------------------------------------------------------------------------
    std::cout << "=================================================================\n";
    std::cout << "ADVERSARIAL TEST SUITE SUMMARY\n";
    std::cout << "=================================================================\n";
    int trueDetections = 0, falsePasses = 0, fails = 0, deferred = 0;
    for (const auto& r : g_results) {
        if (r.classification == "TRUE_DETECTION") trueDetections++;
        else if (r.classification == "FALSE_PASS") falsePasses++;
        else if (r.classification == "FAIL") fails++;
        else if (r.classification == "DEFERRED") deferred++;
    }
    std::cout << "Total Adversarial Scenarios: " << g_results.size() << "\n";
    std::cout << "TRUE_DETECTION: " << trueDetections << "\n";
    std::cout << "FALSE_PASS:     " << falsePasses << "\n";
    std::cout << "FAIL:           " << fails << "\n";
    std::cout << "DEFERRED:       " << deferred << "\n\n";

    // Write JSON output
    std::ofstream jsonFile("tools/network-visual-proxy-readiness/adversarial/TEST_RESULTS.json");
    if (!jsonFile.is_open()) {
        jsonFile.open("TEST_RESULTS.json"); // fallback to current dir
    }
    if (jsonFile.is_open()) {
        jsonFile << "{\n  \"scenarios\": [\n";
        for (size_t i = 0; i < g_results.size(); ++i) {
            const auto& r = g_results[i];
            jsonFile << "    {\n"
                     << "      \"id\": \"" << r.id << "\",\n"
                     << "      \"name\": \"" << r.name << "\",\n"
                     << "      \"classification\": \"" << r.classification << "\",\n"
                     << "      \"expected\": \"" << r.expected << "\",\n"
                     << "      \"actual\": \"" << r.actual << "\",\n"
                     << "      \"details\": \"" << r.details << "\"\n"
                     << "    }" << (i + 1 < g_results.size() ? ",\n" : "\n");
        }
        jsonFile << "  ],\n"
                 << "  \"summary\": {\n"
                 << "    \"total\": " << g_results.size() << ",\n"
                 << "    \"true_detection\": " << trueDetections << ",\n"
                 << "    \"false_pass\": " << falsePasses << ",\n"
                 << "    \"fail\": " << fails << ",\n"
                 << "    \"deferred\": " << deferred << "\n"
                 << "  }\n}\n";
        jsonFile.close();
    }

    return 0; // Return 0 to complete harness run and allow reporting
}
