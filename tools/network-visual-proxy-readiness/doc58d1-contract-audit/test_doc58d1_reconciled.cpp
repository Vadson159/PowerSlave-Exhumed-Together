#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <thread>
#include <chrono>
#include <array>
#include <type_traits>
#include <memory>
#include <functional>
#include <cassert>
#include <assert.h>
#include <windows.h>

// Include DOC-58D.1 Hardened Candidate & Multi-Slot Guard
#include "doc58d1_hardened_visual_proxy_state_machine.hpp"
#include "multi_slot_snapshot_guard.hpp"

namespace pse::doc58d {

// ============================================================================
// Mock KEX Engine Environment
// ============================================================================

struct MockKexSector {
    int sectorId{0};
    float minX{0.0f}, maxX{0.0f};
    float minY{0.0f}, maxY{0.0f};
    float minZ{0.0f}, maxZ{0.0f};
    std::vector<void*> actorList;
};

struct MockKexActorNode {
    void* payload{nullptr};
    uint32_t refs{0};
    bool in_use{false};
    int sectorId{-1};
    uint32_t actorType{1007};
    float x{0.0f}, y{0.0f}, z{0.0f};
    float yaw{0.0f}, pitch{0.0f};
    bool isMarkedForRemoval{false};
    bool isDrained{false};
    bool isSwept{false};
};

class MockKexLevelEnvironment {
public:
    bool isLevelLoaded{true};
    bool sectorsDeallocated{false};
    bool inUnload{false};
    int numSectors{270};
    std::vector<MockKexSector> sectors;
    std::vector<std::unique_ptr<MockKexActorNode>> actorPool;

    MockKexLevelEnvironment() {
        initTombOfRamses();
    }

    void initTombOfRamses() {
        sectors.clear();
        sectors.resize(numSectors);
        for (int i = 0; i < numSectors; ++i) {
            sectors[i].sectorId = i;
            sectors[i].minX = -10000.0f; sectors[i].maxX = 10000.0f;
            sectors[i].minY = -10000.0f; sectors[i].maxY = 10000.0f;
            sectors[i].minZ = -1000.0f; sectors[i].maxZ = 5000.0f;
        }

        // Sector 259: Ramses Spawn / Home Sector
        sectors[259].minX = -7550.0f; sectors[259].maxX = -7350.0f;
        sectors[259].minY = -6912.0f; sectors[259].maxY = -6700.0f; // Portal at Y = -6912
        sectors[259].minZ = 1100.0f;  sectors[259].maxZ = 1400.0f;

        // Sector 123: Ramses Corridor Sector across portal face
        sectors[123].minX = -7550.0f; sectors[123].maxX = -7350.0f;
        sectors[123].minY = -7150.0f; sectors[123].maxY = -6912.0f; // Portal at Y = -6912
        sectors[123].minZ = 1100.0f;  sectors[123].maxZ = 1700.0f;

        // Sector 176: Disjoint Corridor (ADV-11 witness)
        sectors[176].minX = -7650.0f; sectors[176].maxX = -7450.0f;
        sectors[176].minY = -7250.0f; sectors[176].maxY = -7050.0f;
        sectors[176].minZ = 1200.0f;  sectors[176].maxZ = 1350.0f;

        isLevelLoaded = true;
        sectorsDeallocated = false;
        inUnload = false;
    }

    void simulateEngineLevelUnload() {
        inUnload = true;
        sectors.clear();
        sectorsDeallocated = true;
        isLevelLoaded = false;
        inUnload = false;
    }

    bool isPointInSector(int sectorId, float x, float y, float z, float eps = 0.01f) const {
        if (!isLevelLoaded || sectorsDeallocated) return false;
        if (sectorId < 0 || sectorId >= static_cast<int>(sectors.size())) return false;

        const auto& s = sectors[sectorId];
        return (x >= s.minX - eps && x <= s.maxX + eps &&
                y >= s.minY - eps && y <= s.maxY + eps &&
                z >= s.minZ - eps && z <= s.maxZ + eps);
    }
};

// ============================================================================
// Comprehensive Mock Native Seam Adapter
// ============================================================================

class MockNativeVisualProxySeam : public pse::proxy::hardened::INativeVisualProxySeamRemediated {
public:
    MockKexLevelEnvironment* env{nullptr};

    int spawnCalls{0};
    int updateCalls{0};
    int transitionCalls{0};
    int removeCalls{0};

    pse::proxy::hardened::RemoveOutcome programmedRemoveOutcome{pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess};
    bool shouldThrowOnSpawn{false};
    bool shouldThrowOnUpdate{false};
    bool shouldThrowOnRemove{false};
    bool simulateDeferredDestruction{false};

    // Reentrancy fault injection hooks
    bool reentrantCallbackOnSpawn{false};
    bool reentrantCallbackOnRemove{false};
    bool reentrantCallbackOnUpdate{false};
    std::function<void()> onSpawnReentrantAction{nullptr};
    std::function<void()> onRemoveReentrantAction{nullptr};
    std::function<void()> onUpdateReentrantAction{nullptr};

    uint32_t lastActorType{0};
    float lastX{0.0f}, lastY{0.0f}, lastZ{0.0f};
    float lastYaw{0.0f}, lastPitch{0.0f};
    int lastSector{-1};

    bool uafDetectedOnRemove{false};
    bool uafDetectedOnSpawn{false};
    bool uafDetectedOnUpdate{false};

    uintptr_t actorAddressCounter{0x61A00000ULL};

    explicit MockNativeVisualProxySeam(MockKexLevelEnvironment* e) : env(e) {}

    void* spawnVisualActor(uint32_t actorType, float x, float y, float z, float yaw, int sector) override {
        spawnCalls++;
        lastActorType = actorType;
        lastX = x; lastY = y; lastZ = z; lastYaw = yaw; lastSector = sector;

        if (env && env->sectorsDeallocated) {
            uafDetectedOnSpawn = true;
            return nullptr;
        }

        if (reentrantCallbackOnSpawn && onSpawnReentrantAction && spawnCalls == 1) {
            onSpawnReentrantAction();
        }

        if (shouldThrowOnSpawn) {
            throw std::runtime_error("Fault Injection: Native exception in spawnVisualActor");
        }

        actorAddressCounter += 0x1000;
        void* actorPtr = reinterpret_cast<void*>(actorAddressCounter);

        if (env) {
            auto node = std::make_unique<MockKexActorNode>();
            node->payload = actorPtr;
            node->actorType = actorType;
            node->x = x; node->y = y; node->z = z; node->yaw = yaw;
            node->sectorId = sector;
            node->refs = 1;
            node->in_use = true;

            if (sector >= 0 && sector < static_cast<int>(env->sectors.size())) {
                env->sectors[sector].actorList.push_back(actorPtr);
            }
            env->actorPool.push_back(std::move(node));
        }

        return actorPtr;
    }

    bool updateVisualTransform(void* actor, float x, float y, float z, float yaw, float pitch) override {
        updateCalls++;
        lastX = x; lastY = y; lastZ = z; lastYaw = yaw; lastPitch = pitch;

        if (env && env->sectorsDeallocated) {
            uafDetectedOnUpdate = true;
            return false;
        }

        if (reentrantCallbackOnUpdate && onUpdateReentrantAction && updateCalls == 1) {
            onUpdateReentrantAction();
        }

        if (shouldThrowOnUpdate) {
            throw std::runtime_error("Fault Injection: Native exception in updateVisualTransform");
        }

        if (env) {
            for (auto& node : env->actorPool) {
                if (node->payload == actor) {
                    node->x = x; node->y = y; node->z = z;
                    node->yaw = yaw; node->pitch = pitch;
                    return true;
                }
            }
        }
        return true;
    }

    bool transitionSector(void* actor, int newSector) override {
        transitionCalls++;
        if (env && env->sectorsDeallocated) {
            return false;
        }

        if (env) {
            for (auto& node : env->actorPool) {
                if (node->payload == actor) {
                    int oldSec = node->sectorId;
                    if (oldSec >= 0 && oldSec < static_cast<int>(env->sectors.size())) {
                        auto& list = env->sectors[oldSec].actorList;
                        for (auto it = list.begin(); it != list.end(); ++it) {
                            if (*it == actor) {
                                list.erase(it);
                                break;
                            }
                        }
                    }
                    node->sectorId = newSector;
                    if (newSector >= 0 && newSector < static_cast<int>(env->sectors.size())) {
                        env->sectors[newSector].actorList.push_back(actor);
                    }
                    return true;
                }
            }
        }
        return true;
    }

    pse::proxy::hardened::RemoveOutcome removeVisualActorEx(void* actor) override {
        removeCalls++;

        if (env && env->sectorsDeallocated) {
            uafDetectedOnRemove = true;
            return pse::proxy::hardened::RemoveOutcome::Uncertain;
        }

        if (reentrantCallbackOnRemove && onRemoveReentrantAction && removeCalls == 1) {
            onRemoveReentrantAction();
        }

        if (shouldThrowOnRemove) {
            throw std::runtime_error("Fault Injection: Native exception in removeVisualActorEx");
        }

        if (programmedRemoveOutcome == pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess) {
            if (env) {
                for (auto& node : env->actorPool) {
                    if (node->payload == actor) {
                        if (node->sectorId >= 0 && node->sectorId < static_cast<int>(env->sectors.size())) {
                            auto& list = env->sectors[node->sectorId].actorList;
                            for (auto it = list.begin(); it != list.end(); ++it) {
                                if (*it == actor) {
                                    list.erase(it);
                                    break;
                                }
                            }
                        }
                        node->refs = 0;
                        node->isDrained = true;
                        node->isMarkedForRemoval = true;

                        if (!simulateDeferredDestruction) {
                            node->payload = nullptr;
                            node->in_use = false;
                            node->isSwept = true;
                        }
                        break;
                    }
                }
            }
            return pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess;
        } else if (programmedRemoveOutcome == pse::proxy::hardened::RemoveOutcome::DefiniteFailure) {
            return pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        } else {
            return pse::proxy::hardened::RemoveOutcome::Uncertain;
        }
    }
};

// ============================================================================
// Concrete Mock Sector Validator
// ============================================================================

class MockSectorValidator : public pse::proxy::hardened::ISectorValidator {
public:
    MockKexLevelEnvironment* env{nullptr};
    float boundaryEpsilon{0.01f};

    explicit MockSectorValidator(MockKexLevelEnvironment* e) : env(e) {}

    bool validateDestinationSector(int sectorId, float x, float y, float z) const noexcept override {
        if (!env || !env->isLevelLoaded || env->sectorsDeallocated) {
            return false;
        }
        return env->isPointInSector(sectorId, x, y, z, boundaryEpsilon);
    }
};

// ============================================================================
// Test Record Structure
// ============================================================================

struct TestCaseResult {
    std::string id;
    std::string suite;
    std::string name;
    std::string initialState;
    std::string injectedFault;
    std::string expectedState;
    std::string actualState;
    int seamCalls{0};
    std::string verdict; // PASS, FAIL, BLOCKED, NATIVE_VERIFICATION_REQUIRED
    std::string riskLevel;
    std::string notes;
};

static pse::BridgePlayerSnapshot makeSnapshot(uint8_t slot = 0,
                                              uint32_t seq = 1,
                                              uint32_t tick = 100,
                                              uint32_t epoch = 7365,
                                              uint32_t connGen = 1,
                                              uint32_t entId = 0x5500,
                                              uint32_t entGen = 1,
                                              float x = -7424.0f,
                                              float y = -6880.0f,
                                              float z = 1240.0f) {
    pse::BridgePlayerSnapshot s;
    s.slotId = slot;
    s.sequence = seq;
    s.serverTick = tick;
    s.mapEpoch = epoch;
    s.connectionGeneration = connGen;
    s.entityId = entId;
    s.entityGeneration = entGen;
    s.posX = x;
    s.posY = y;
    s.posZ = z;
    s.yaw = 0.0f;
    s.pitch = 0.0f;
    s.health = 200;
    return s;
}

} // namespace pse::doc58d

int main() {
    std::cout << "============================================================================\n";
    std::cout << "PowerSlave Exhumed Together -- DOC-58D Full Regression & Adversarial QA\n";
    std::cout << "C++20 Autonomous Verification Pipeline\n";
    std::cout << "Target: hardened_visual_proxy_state_machine.hpp + multi_slot_snapshot_guard.hpp\n";
    std::cout << "============================================================================\n\n";

    std::vector<pse::doc58d::TestCaseResult> results;
    auto recordTest = [&](const pse::doc58d::TestCaseResult& res) {
        results.push_back(res);
        std::cout << "[" << std::setw(15) << std::left << res.id << "] "
                  << "[" << std::setw(28) << std::left << res.verdict << "] "
                  << res.name << "\n";
        if (res.verdict == "FAIL") {
            std::cout << "    --> FAILURE: Expected " << res.expectedState
                      << ", got " << res.actualState << " | Notes: " << res.notes << "\n";
        }
    };

    // ========================================================================
    // SUITE 1: DOC-58B FULL REGRESSION (18 SCENARIOS)
    // ========================================================================
    std::cout << "============================================================================\n";
    std::cout << "SUITE 1: DOC-58B FULL REGRESSION (18 SCENARIOS)\n";
    std::cout << "============================================================================\n";

    // SCENARIO-01: Failed Remove DefiniteFailure -> QUARANTINED
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        void* expectedActor = sm.activeActor();

        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        auto res = sm.executePendingRemove(&seam);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::NativeDefiniteFailureQuarantined) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED) &&
                    (sm.quarantineReason() == pse::proxy::hardened::QuarantineReason::RemoveDefiniteFailure) &&
                    (sm.activeActor() == expectedActor);

        recordTest({
            "DOC58B-SC-01", "DOC-58B Regression", "Failed Remove (DefiniteFailure -> QUARANTINED)",
            "REMOVE_PENDING", "Native seam returns DefiniteFailure", "QUARANTINED",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "Critical", "Pointer preserved in quarantine."
        });
    }

    // SCENARIO-02: Failed Remove Uncertain -> QUARANTINED
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::Uncertain;
        auto res = sm.executePendingRemove(&seam);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::NativeUncertainQuarantined) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED) &&
                    (sm.quarantineReason() == pse::proxy::hardened::QuarantineReason::RemoveUncertain);

        recordTest({
            "DOC58B-SC-02", "DOC-58B Regression", "Failed Remove (Uncertain -> QUARANTINED)",
            "REMOVE_PENDING", "Native seam returns Uncertain", "QUARANTINED",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "Critical", "Uncertain outcome isolates actor in quarantine."
        });
    }

    // SCENARIO-03: Successful Native Remove -> ABSENT
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess;
        auto res = sm.executePendingRemove(&seam);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                    (sm.activeActor() == nullptr) && (sm.removeCount() == 1);

        recordTest({
            "DOC58B-SC-03", "DOC-58B Regression", "Successful Native Remove -> ABSENT",
            "REMOVE_PENDING", "Native seam returns ConfirmedSuccess", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "High", "Clean retirement confirms pointer nullification."
        });
    }

    // SCENARIO-04: Repeated Remove After Failure -> QuarantinedBlocked
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        sm.executePendingRemove(&seam);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);
        int callsBefore = seam.removeCalls;

        auto res = sm.executePendingRemove(&seam);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::QuarantinedBlocked) &&
                    (seam.removeCalls == callsBefore) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);

        recordTest({
            "DOC58B-SC-04", "DOC-58B Regression", "Repeated Remove After Failure Blocked",
            "QUARANTINED", "executePendingRemove() called again", "QUARANTINED",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "Critical", "Zero native seam calls on repeated remove in quarantine."
        });
    }

    // SCENARIO-05: Shutdown During SPAWN_PENDING -> Clean Abort
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING);

        bool reqOk = sm.requestShutdown();

        bool pass = reqOk && (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                    sm.isShutdownRequested() && (seam.spawnCalls == 0);

        recordTest({
            "DOC58B-SC-05", "DOC-58B Regression", "Shutdown During SPAWN_PENDING",
            "SPAWN_PENDING", "requestShutdown() before spawn executed", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), seam.spawnCalls,
            pass ? "PASS" : "FAIL", "Medium", "Pending spawn cleanly cancelled without native call."
        });
    }

    // SCENARIO-06: Shutdown During REMOVE_PENDING (Level Valid)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.requestShutdown();
        auto res = sm.executeShutdownCleanup(&seam, true /* isLevelValid */);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                    (seam.removeCalls == 1);

        recordTest({
            "DOC58B-SC-06", "DOC-58B Regression", "Shutdown During REMOVE_PENDING (Level Valid)",
            "REMOVE_PENDING", "executeShutdownCleanup(true)", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "High", "Clean remove executed when level structures intact."
        });
    }

    // SCENARIO-07: Shutdown After Level Unloaded (UAF Hazard Avoided)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        env.simulateEngineLevelUnload();
        sm.requestShutdown();
        auto res = sm.executeShutdownCleanup(&seam, false /* isLevelValid */);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::LevelInvalidSkipped) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                    (seam.removeCalls == 0) && !seam.uafDetectedOnRemove;

        recordTest({
            "DOC58B-SC-07", "DOC-58B Regression", "Shutdown After Level Unloaded (UAF Avoided)",
            "ACTIVE", "Level unloaded -> executeShutdownCleanup(false)", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "Critical", "Native remove skipped; zero UAF on freed sectors."
        });
    }

    // SCENARIO-08: Wrong Simulation Thread Affinity
    {
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        bool caughtSpawn = false;
        bool caughtSnapshot = false;

        std::thread worker([&]() {
            auto r1 = sm.executePendingSpawn(nullptr, nullptr, 1007, 259);
            caughtSpawn = (r1 == pse::proxy::hardened::SpawnExecutionResult::AffinityViolation);

            pse::BridgePlayerSnapshot s{};
            auto r2 = sm.handleSnapshot(s, true, true, nullptr);
            caughtSnapshot = (r2 == pse::proxy::hardened::ProxyRejectionReason::AffinityViolation);
        });
        worker.join();

        bool pass = caughtSpawn && caughtSnapshot;

        recordTest({
            "DOC58B-SC-08", "DOC-58B Regression", "Wrong Simulation Thread Affinity Rejection",
            "ABSENT", "Worker thread invokes mutating methods", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), 0,
            pass ? "PASS" : "FAIL", "Critical", "Fail-closed without throwing exceptions."
        });
    }

    // SCENARIO-09: Exception from Mock Native Seam Isolated
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        seam.shouldThrowOnSpawn = true;
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        auto spRes = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        bool step1Ok = (spRes == pse::proxy::hardened::SpawnExecutionResult::NativeExceptionCaught) &&
                        (sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING);

        seam.shouldThrowOnSpawn = false;
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        seam.shouldThrowOnRemove = true;
        sm.onDisconnect();
        auto remRes = sm.executePendingRemove(&seam);
        bool step2Ok = (remRes == pse::proxy::hardened::RemoveExecutionResult::NativeExceptionCaught) &&
                        (sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);

        bool pass = step1Ok && step2Ok;

        recordTest({
            "DOC58B-SC-09", "DOC-58B Regression", "Exception from Mock Seam Isolated",
            "SPAWN_PENDING / REMOVE_PENDING", "Seam throws std::runtime_error", "QUARANTINED",
            pse::proxy::hardened::toString(sm.state()), seam.removeCalls,
            pass ? "PASS" : "FAIL", "Critical", "All exceptions trapped; actor quarantined safely."
        });
    }

    // SCENARIO-10: Recycled Actor Pointer Tracking (Generational Safety)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        uint32_t gen1 = sm.localProxyGeneration();

        sm.onDisconnect();
        sm.executePendingRemove(&seam);
        uint32_t gen2 = sm.localProxyGeneration();

        sm.configureSession(7365, 2);
        auto s2 = pse::doc58d::makeSnapshot(0, 2, 101, 7365, 2, 0x5502, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s2, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        uint32_t gen3 = sm.localProxyGeneration();

        bool pass = (gen2 == gen1 + 1) && (gen3 == gen2 + 1);

        recordTest({
            "DOC58B-SC-10", "DOC-58B Regression", "Recycled Pointer Generational Safety",
            "ACTIVE", "Spawn -> Remove -> Spawn sequence", "ACTIVE",
            pse::proxy::hardened::toString(sm.state()), seam.spawnCalls,
            pass ? "PASS" : "FAIL", "High", "Strict monotonic generation increments on every transition."
        });
    }

    // SCENARIO-11: Stale DTO After Disconnect
    {
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);
        sm.onDisconnect();

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rej = sm.handleSnapshot(s, true, true);

        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::SessionInactive) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({
            "DOC58B-SC-11", "DOC-58B Regression", "Stale DTO After Disconnect Rejection",
            "ABSENT", "Snapshot dispatched after onDisconnect()", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), 0,
            pass ? "PASS" : "FAIL", "Medium", "SessionInactive protects against late network packets."
        });
    }

    // SCENARIO-12: Map Epoch Invalidation
    {
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 9999 /* Mismatched */, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rej = sm.handleSnapshot(s, true, true);

        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::IdentityMismatch) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({
            "DOC58B-SC-12", "DOC-58B Regression", "Map Epoch Invalidation Rejection",
            "ABSENT", "Snapshot arrives with mapEpoch = 9999", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), 0,
            pass ? "PASS" : "FAIL", "High", "IdentityMismatch prevents cross-map transform leakage."
        });
    }

    // SCENARIO-13: Wrong Slot Protection (Client Puppet Slot 1)
    {
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(1 /* Slot 1 */, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rej = sm.handleSnapshot(s, true, true);

        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::WrongSlot) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({
            "DOC58B-SC-13", "DOC-58B Regression", "Wrong Slot Protection (Target Isolation)",
            "ABSENT", "Snapshot with slotId = 1 dispatched to targetSlot = 0", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), 0,
            pass ? "PASS" : "FAIL", "High", "WrongSlot strictly isolates remote proxy targets."
        });
    }

    // SCENARIO-14: Unknown Sector Spawn Rejection
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);

        auto res = sm.executePendingSpawn(&seam, &validator, 1007, -1);

        bool pass = (res == pse::proxy::hardened::SpawnExecutionResult::SectorValidationFailed) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING) &&
                    (seam.spawnCalls == 0);

        recordTest({
            "DOC58B-SC-14", "DOC-58B Regression", "Unknown Sector Spawn Rejection",
            "SPAWN_PENDING", "executePendingSpawn with sectorId = -1", "SPAWN_PENDING",
            pse::proxy::hardened::toString(sm.state()), seam.spawnCalls,
            pass ? "PASS" : "FAIL", "Critical", "Zero factory calls on unverified sector."
        });
    }

    // SCENARIO-15: Mismatched Position/Sector (ADV-11 Fallacy Eliminated)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        // Coords in corridor 176
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7550.0f, -7100.0f, 1250.0f);
        sm.handleSnapshot(s, true, true, &seam);

        // Pass hardcoded sector 259
        auto resMismatch = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        bool step1Ok = (resMismatch == pse::proxy::hardened::SpawnExecutionResult::SectorValidationFailed);

        // Pass verified sector 176
        auto resMatch = sm.executePendingSpawn(&seam, &validator, 1007, 176);
        bool step2Ok = (resMatch == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                       (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        bool pass = step1Ok && step2Ok;

        recordTest({
            "DOC58B-SC-15", "DOC-58B Regression", "Mismatched Position/Sector (ADV-11)",
            "SPAWN_PENDING", "Coords in 176: sector 259 rejected, sector 176 accepted", "ACTIVE",
            pse::proxy::hardened::toString(sm.state()), seam.spawnCalls,
            pass ? "PASS" : "FAIL", "High", "ADV-11 hardcoded sector fallacy fully eliminated."
        });
    }

    // SCENARIO-16: Repeated Spawn After Quarantined Removal Blocked
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        sm.executePendingRemove(&seam);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);
        int spawnCallsBefore = seam.spawnCalls;

        auto rejSnap = sm.handleSnapshot(s, true, true, &seam);
        auto spRes = sm.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (rejSnap == pse::proxy::hardened::ProxyRejectionReason::Quarantined) &&
                    (spRes == pse::proxy::hardened::SpawnExecutionResult::Quarantined) &&
                    (seam.spawnCalls == spawnCallsBefore) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);

        recordTest({
            "DOC58B-SC-16", "DOC-58B Regression", "Repeated Spawn Blocked in Quarantine",
            "QUARANTINED", "handleSnapshot & executePendingSpawn called while quarantined", "QUARANTINED",
            pse::proxy::hardened::toString(sm.state()), seam.spawnCalls,
            pass ? "PASS" : "FAIL", "Critical", "Zero duplicate native actors spawned in KEX."
        });
    }

    // SCENARIO-17: No Native Calls After Terminal Shutdown
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        sm.requestShutdown();
        sm.executeShutdownCleanup(&seam, false);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rejSnap = sm.handleSnapshot(s, true, true, &seam);
        auto resSpawn = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        auto resRem = sm.executePendingRemove(&seam);

        bool pass = (rejSnap == pse::proxy::hardened::ProxyRejectionReason::ShutdownRequested) &&
                    (resSpawn == pse::proxy::hardened::SpawnExecutionResult::ShutdownBlocked) &&
                    (resRem == pse::proxy::hardened::RemoveExecutionResult::NotPending) &&
                    (seam.spawnCalls == 0) && (seam.removeCalls == 0);

        recordTest({
            "DOC58B-SC-17", "DOC-58B Regression", "Silent Terminal Shutdown State",
            "ABSENT (Shut down)", "Calls to snapshot, spawn, remove after shutdown", "ABSENT",
            pse::proxy::hardened::toString(sm.state()), 0,
            pass ? "PASS" : "FAIL", "Medium", "Seam call count strictly zero after terminal shutdown."
        });
    }

    // SCENARIO-18: No C++ Exceptions Across API Boundary (noexcept Audit)
    {
        using SM = pse::proxy::hardened::VisualProxyStateMachine;

        static_assert(noexcept(std::declval<SM>().registerSimulationThread()), "registerSimulationThread must be noexcept");
        static_assert(noexcept(std::declval<SM>().isSimulationThread()), "isSimulationThread must be noexcept");
        static_assert(noexcept(std::declval<SM>().configureSession(0, 0)), "configureSession must be noexcept");
        static_assert(noexcept(std::declval<SM>().resetSession()), "resetSession must be noexcept");
        static_assert(noexcept(std::declval<SM>().handleSnapshot(std::declval<pse::BridgePlayerSnapshot>(), true, true, nullptr)), "handleSnapshot must be noexcept");
        static_assert(noexcept(std::declval<SM>().executePendingSpawn(nullptr, nullptr, 0, 0)), "executePendingSpawn must be noexcept");
        static_assert(noexcept(std::declval<SM>().executePendingRemove(nullptr)), "executePendingRemove must be noexcept");
        static_assert(noexcept(std::declval<SM>().requestShutdown()), "requestShutdown must be noexcept");
        static_assert(noexcept(std::declval<SM>().executeShutdownCleanup(nullptr, true)), "executeShutdownCleanup must be noexcept");
        static_assert(noexcept(std::declval<SM>().onDisconnect()), "onDisconnect must be noexcept");
        static_assert(noexcept(std::declval<SM>().onMapInvalidated(true)), "onMapInvalidated must be noexcept");
        static_assert(noexcept(std::declval<SM>().retireQuarantinedActor(true)), "retireQuarantinedActor must be noexcept");
        static_assert(noexcept(std::declval<SM>().onPreUnload()), "onPreUnload must be noexcept");
        static_assert(noexcept(std::declval<SM>().onPostUnload()), "onPostUnload must be noexcept");

        recordTest({
            "DOC58B-SC-18", "DOC-58B Regression", "Verified noexcept ABI Safety Audit",
            "N/A", "Compile-time static_assert on all public methods", "ALL_NOEXCEPT",
            "ALL_NOEXCEPT", 0, "PASS", "Low", "Zero uncaught exceptions guarantee across C-ABI."
        });
    }

    // ========================================================================
    // SUITE 2: DOC-58C REGRESSION ON HARDENED CANDIDATE (32 SCENARIOS)
    // ========================================================================
    std::cout << "\n============================================================================\n";
    std::cout << "SUITE 2: DOC-58C REGRESSION ON HARDENED CANDIDATE (32 SCENARIOS)\n";
    std::cout << "============================================================================\n";

    // TC-PSE-58C-01 to 05 (Domain A: Lifetime)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.requestShutdown();
        auto cleanRes = sm.executeShutdownCleanup(&seam, true);
        bool pass01 = (cleanRes == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                      (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({"TC-PSE-58C-01", "DOC-58C Regression", "Shutdown before Level::Unload (isLevelValid == true)",
                    "ACTIVE", "requestShutdown() + executeShutdownCleanup(true)", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass01 ? "PASS" : "FAIL", "High", "Clean teardown."});

        // 02: Shutdown after sector free
        pse::doc58d::MockKexLevelEnvironment env2;
        pse::doc58d::MockNativeVisualProxySeam seam2(&env2);
        pse::doc58d::MockSectorValidator validator2(&env2);
        pse::proxy::hardened::VisualProxyStateMachine sm2(0);
        sm2.registerSimulationThread();
        sm2.configureSession(7365, 1);
        sm2.handleSnapshot(s, true, true, &seam2);
        sm2.executePendingSpawn(&seam2, &validator2, 1007, 259);
        env2.simulateEngineLevelUnload();
        sm2.requestShutdown();
        auto cleanRes2 = sm2.executeShutdownCleanup(&seam2, false);
        bool pass02 = (cleanRes2 == pse::proxy::hardened::RemoveExecutionResult::LevelInvalidSkipped) &&
                      (sm2.state() == pse::proxy::hardened::VisualProxyState::ABSENT) && (seam2.removeCalls == 0);

        recordTest({"TC-PSE-58C-02", "DOC-58C Regression", "Shutdown after Sector Deallocation (isLevelValid == false)",
                    "ACTIVE", "executeShutdownCleanup(false)", "ABSENT",
                    pse::proxy::hardened::toString(sm2.state()), seam2.removeCalls, pass02 ? "PASS" : "FAIL", "Critical", "Seam call skipped."});

        // 03: Stale isLevelValid UAF trap
        pse::doc58d::MockKexLevelEnvironment env3;
        pse::doc58d::MockNativeVisualProxySeam seam3(&env3);
        pse::doc58d::MockSectorValidator validator3(&env3);
        pse::proxy::hardened::VisualProxyStateMachine sm3(0);
        sm3.registerSimulationThread();
        sm3.configureSession(7365, 1);
        sm3.handleSnapshot(s, true, true, &seam3);
        sm3.executePendingSpawn(&seam3, &validator3, 1007, 259);
        env3.simulateEngineLevelUnload();
        sm3.requestShutdown();
        sm3.executeShutdownCleanup(&seam3, true /* STALE TRUE */);

        recordTest({"TC-PSE-58C-03", "DOC-58C Regression", "Stale isLevelValid==true during Unload (UAF Trap)",
                    "ACTIVE", "Fault: Stale isLevelValid=true after sector deallocation", "UAF_DETECTED",
                    seam3.uafDetectedOnRemove ? "UAF_DETECTED" : "UAF_MISSED", seam3.removeCalls,
                    "NATIVE_VERIFICATION_REQUIRED", "Critical", "Pre-unload hook barrier required in native KEX."});

        // 04: Repeated unload
        pse::proxy::hardened::VisualProxyStateMachine sm4(0);
        sm4.registerSimulationThread();
        sm4.configureSession(7365, 1);
        sm4.onMapInvalidated(true);
        uint32_t g1 = sm4.localProxyGeneration();
        sm4.onMapInvalidated(true);
        uint32_t g2 = sm4.localProxyGeneration();
        bool pass04 = (sm4.state() == pse::proxy::hardened::VisualProxyState::ABSENT) && (g2 == g1 + 1);

        recordTest({"TC-PSE-58C-04", "DOC-58C Regression", "Repeated Level Unload (Idempotence)",
                    "ABSENT", "onMapInvalidated(true) x2", "ABSENT",
                    pse::proxy::hardened::toString(sm4.state()), 0, pass04 ? "PASS" : "FAIL", "Medium", "Idempotent."});

        // 05: Disconnect -> Map Transition -> Reconnect
        pse::doc58d::MockKexLevelEnvironment env5;
        pse::doc58d::MockNativeVisualProxySeam seam5(&env5);
        pse::doc58d::MockSectorValidator validator5(&env5);
        pse::proxy::hardened::VisualProxyStateMachine sm5(0);
        sm5.registerSimulationThread();
        sm5.configureSession(7365, 1);
        sm5.handleSnapshot(s, true, true, &seam5);
        sm5.executePendingSpawn(&seam5, &validator5, 1007, 259);
        sm5.onDisconnect();
        env5.simulateEngineLevelUnload();
        sm5.onMapInvalidated(true);
        env5.initTombOfRamses();
        sm5.configureSession(7366, 2);
        auto sNew = pse::doc58d::makeSnapshot(0, 1, 10, 7366, 2, 0x5502, 1, -7424.0f, -6880.0f, 1240.0f);
        sm5.handleSnapshot(sNew, true, true, &seam5);
        auto spRes5 = sm5.executePendingSpawn(&seam5, &validator5, 1007, 259);
        bool pass05 = (spRes5 == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                      (sm5.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        recordTest({"TC-PSE-58C-05", "DOC-58C Regression", "Disconnect -> Map Transition -> Reconnect Cycle",
                    "ACTIVE", "Disconnect -> onMapInvalidated -> configureSession -> spawn", "ACTIVE",
                    pse::proxy::hardened::toString(sm5.state()), seam5.spawnCalls, pass05 ? "PASS" : "FAIL", "High", "Clean cycle."});
    }

    // TC-PSE-58C-06 to 09 (Domain B: RemoveOutcome)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        // 06: ConfirmedSuccess
        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess;
        auto r06 = sm.executePendingRemove(&seam);
        bool pass06 = (r06 == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                      (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);
        recordTest({"TC-PSE-58C-06", "DOC-58C Regression", "ConfirmedSuccess Clean Removal Contract",
                    "REMOVE_PENDING", "removeVisualActorEx -> ConfirmedSuccess", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass06 ? "PASS" : "FAIL", "High", "Clean removal."});

        // 07: DefiniteFailure
        pse::doc58d::MockNativeVisualProxySeam seam7(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm7(0);
        sm7.registerSimulationThread();
        sm7.configureSession(7365, 1);
        sm7.handleSnapshot(s, true, true, &seam7);
        sm7.executePendingSpawn(&seam7, &validator, 1007, 259);
        sm7.onDisconnect();
        seam7.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        auto r07 = sm7.executePendingRemove(&seam7);
        bool pass07 = (r07 == pse::proxy::hardened::RemoveExecutionResult::NativeDefiniteFailureQuarantined) &&
                      (sm7.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);
        recordTest({"TC-PSE-58C-07", "DOC-58C Regression", "DefiniteFailure Quarantine Contract",
                    "REMOVE_PENDING", "removeVisualActorEx -> DefiniteFailure", "QUARANTINED",
                    pse::proxy::hardened::toString(sm7.state()), seam7.removeCalls, pass07 ? "PASS" : "FAIL", "Critical", "Quarantined."});

        // 08: Uncertain
        pse::doc58d::MockNativeVisualProxySeam seam8(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm8(0);
        sm8.registerSimulationThread();
        sm8.configureSession(7365, 1);
        sm8.handleSnapshot(s, true, true, &seam8);
        sm8.executePendingSpawn(&seam8, &validator, 1007, 259);
        sm8.onDisconnect();
        seam8.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::Uncertain;
        auto r08 = sm8.executePendingRemove(&seam8);
        bool pass08 = (r08 == pse::proxy::hardened::RemoveExecutionResult::NativeUncertainQuarantined) &&
                      (sm8.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);
        recordTest({"TC-PSE-58C-08", "DOC-58C Regression", "Uncertain Outcome Quarantine Contract",
                    "REMOVE_PENDING", "removeVisualActorEx -> Uncertain", "QUARANTINED",
                    pse::proxy::hardened::toString(sm8.state()), seam8.removeCalls, pass08 ? "PASS" : "FAIL", "Critical", "Quarantined."});

        // 09: Deferred Pool Return Race
        pse::doc58d::MockNativeVisualProxySeam seam9(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm9(0);
        sm9.registerSimulationThread();
        sm9.configureSession(7365, 1);
        sm9.handleSnapshot(s, true, true, &seam9);
        sm9.executePendingSpawn(&seam9, &validator, 1007, 259);
        seam9.simulateDeferredDestruction = true;
        sm9.onDisconnect();
        sm9.executePendingRemove(&seam9);
        sm9.configureSession(7365, 2);
        auto s2 = pse::doc58d::makeSnapshot(0, 2, 101, 7365, 2, 0x5502, 1, -7424.0f, -6880.0f, 1240.0f);
        sm9.handleSnapshot(s2, true, true, &seam9);
        auto spRes9 = sm9.executePendingSpawn(&seam9, &validator, 1007, 259);
        bool pass09 = (spRes9 == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                      (sm9.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                      (sm9.localProxyGeneration() == 3);
        recordTest({"TC-PSE-58C-09", "DOC-58C Regression", "Deferred Destruction / Pending Sweep Race",
                    "REMOVE_PENDING", "Drain completed, pool return deferred -> re-spawn", "ACTIVE",
                    pse::proxy::hardened::toString(sm9.state()), seam9.spawnCalls, pass09 ? "PASS" : "FAIL", "High", "ABA safe."});
    }

    // TC-PSE-58C-10 to 15 (Domain C: Quarantine)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        sm.onDisconnect();
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        sm.executePendingRemove(&seam);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);

        // 10: Blind repeat remove
        auto r10 = sm.executePendingRemove(&seam);
        bool pass10 = (r10 == pse::proxy::hardened::RemoveExecutionResult::QuarantinedBlocked);
        recordTest({"TC-PSE-58C-10", "DOC-58C Regression", "Blind Repeated Remove Blocked",
                    "QUARANTINED", "executePendingRemove() while quarantined", "QUARANTINED",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass10 ? "PASS" : "FAIL", "Critical", "Blocked."});

        // 11: Snapshot & Spawn blocked
        auto r11a = sm.handleSnapshot(s, true, true, &seam);
        auto r11b = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        bool pass11 = (r11a == pse::proxy::hardened::ProxyRejectionReason::Quarantined) &&
                      (r11b == pse::proxy::hardened::SpawnExecutionResult::Quarantined);
        recordTest({"TC-PSE-58C-11", "DOC-58C Regression", "Snapshot & Spawn Blocked during Quarantine",
                    "QUARANTINED", "handleSnapshot & executePendingSpawn", "QUARANTINED",
                    pse::proxy::hardened::toString(sm.state()), 0, pass11 ? "PASS" : "FAIL", "Critical", "Updates blocked."});

        // 12: Disconnect & Reconnect retention
        sm.onDisconnect();
        sm.configureSession(7365, 2);
        auto r12 = sm.handleSnapshot(s, true, true, &seam);
        bool pass12 = (sm.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED) &&
                      (r12 == pse::proxy::hardened::ProxyRejectionReason::Quarantined);
        recordTest({"TC-PSE-58C-12", "DOC-58C Regression", "Quarantine Retention across Disconnect & Reconnect",
                    "QUARANTINED", "onDisconnect -> configureSession", "QUARANTINED",
                    pse::proxy::hardened::toString(sm.state()), 0, pass12 ? "PASS" : "FAIL", "High", "Retained."});

        // 13: Shutdown while quarantined
        sm.requestShutdown();
        auto r13a = sm.executeShutdownCleanup(&seam, true);
        env.simulateEngineLevelUnload();
        auto r13b = sm.executeShutdownCleanup(&seam, false);
        bool pass13 = (r13a == pse::proxy::hardened::RemoveExecutionResult::QuarantinedBlocked) &&
                      (r13b == pse::proxy::hardened::RemoveExecutionResult::LevelInvalidSkipped) &&
                      (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);
        recordTest({"TC-PSE-58C-13", "DOC-58C Regression", "Shutdown while Quarantined (Valid vs Unloaded)",
                    "QUARANTINED", "executeShutdownCleanup(true) -> blocked; (false) -> ABSENT", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass13 ? "PASS" : "FAIL", "High", "Cleared."});

        // 14: Pointer reuse ABA
        pse::doc58d::MockKexLevelEnvironment env14;
        pse::doc58d::MockNativeVisualProxySeam seam14(&env14);
        pse::doc58d::MockSectorValidator validator14(&env14);
        pse::proxy::hardened::VisualProxyStateMachine sm14(0);
        sm14.registerSimulationThread();
        sm14.configureSession(7365, 1);
        sm14.handleSnapshot(s, true, true, &seam14);
        sm14.executePendingSpawn(&seam14, &validator14, 1007, 259);
        void* a1 = sm14.activeActor();
        uint32_t genA = sm14.localProxyGeneration();
        sm14.onMapInvalidated(true);
        seam14.actorAddressCounter = reinterpret_cast<uintptr_t>(a1) - 0x1000;
        sm14.configureSession(7366, 1);
        auto s14 = pse::doc58d::makeSnapshot(0, 1, 10, 7366, 1, 0x5502, 1, -7424.0f, -6880.0f, 1240.0f);
        sm14.handleSnapshot(s14, true, true, &seam14);
        sm14.executePendingSpawn(&seam14, &validator14, 1007, 259);
        void* a2 = sm14.activeActor();
        uint32_t genB = sm14.localProxyGeneration();
        bool pass14 = (a1 == a2) && (genB > genA + 1);
        recordTest({"TC-PSE-58C-14", "DOC-58C Regression", "Pointer Address Reuse (ABA) Disambiguation",
                    "ABSENT", "Memory recycled across map reload", "ACTIVE",
                    pse::proxy::hardened::toString(sm14.state()), seam14.spawnCalls, pass14 ? "PASS" : "FAIL", "High", "Disambiguated."});

        // 15: retireQuarantinedActor witness rule
        pse::doc58d::MockKexLevelEnvironment env15;
        pse::doc58d::MockNativeVisualProxySeam seam15(&env15);
        pse::doc58d::MockSectorValidator validator15(&env15);
        pse::proxy::hardened::VisualProxyStateMachine sm15(0);
        sm15.registerSimulationThread();
        sm15.configureSession(7365, 1);
        sm15.handleSnapshot(s, true, true, &seam15);
        sm15.executePendingSpawn(&seam15, &validator15, 1007, 259);
        sm15.onDisconnect();
        seam15.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        sm15.executePendingRemove(&seam15);
        bool retF = sm15.retireQuarantinedActor(false);
        bool step1Ok = (!retF && sm15.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);
        bool retT = sm15.retireQuarantinedActor(true);
        bool step2Ok = (retT && sm15.state() == pse::proxy::hardened::VisualProxyState::ABSENT);
        bool pass15 = step1Ok && step2Ok;
        recordTest({"TC-PSE-58C-15", "DOC-58C Regression", "retireQuarantinedActor Witness Rule",
                    "QUARANTINED", "retireQuarantinedActor(false) -> false; (true) -> ABSENT", "ABSENT",
                    pse::proxy::hardened::toString(sm15.state()), 0, pass15 ? "PASS" : "FAIL", "Critical", "Witness enforced."});
    }

    // TC-PSE-58C-16 to 21 (Domain D: Sector Validation)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);

        // 16: Out-of-bounds sector
        auto r16a = sm.executePendingSpawn(&seam, &validator, 1007, -1);
        auto r16b = sm.executePendingSpawn(&seam, &validator, 1007, 9999);
        bool pass16 = (r16a == pse::proxy::hardened::SpawnExecutionResult::SectorValidationFailed) &&
                      (r16b == pse::proxy::hardened::SpawnExecutionResult::SectorValidationFailed);
        recordTest({"TC-PSE-58C-16", "DOC-58C Regression", "Out-of-Bounds Sector ID Rejection",
                    "SPAWN_PENDING", "executePendingSpawn with -1 and 9999", "SPAWN_PENDING",
                    pse::proxy::hardened::toString(sm.state()), 0, pass16 ? "PASS" : "FAIL", "Critical", "Prevented assert7b3c0."});

        // 17: Coordinate mismatch
        pse::proxy::hardened::VisualProxyStateMachine sm17(0);
        sm17.registerSimulationThread();
        sm17.configureSession(7365, 1);
        auto s17 = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7550.0f, -7100.0f, 1250.0f);
        sm17.handleSnapshot(s17, true, true, &seam);
        auto r17 = sm17.executePendingSpawn(&seam, &validator, 1007, 259);
        bool pass17 = (r17 == pse::proxy::hardened::SpawnExecutionResult::SectorValidationFailed);
        recordTest({"TC-PSE-58C-17", "DOC-58C Regression", "Coordinate / Sector Spatial Mismatch",
                    "SPAWN_PENDING", "Coords in 176, destination 259 passed", "SPAWN_PENDING",
                    pse::proxy::hardened::toString(sm17.state()), 0, pass17 ? "PASS" : "FAIL", "High", "Rejected."});

        // 18: Missing validator
        auto r18 = sm.executePendingSpawn(&seam, nullptr, 1007, 259);
        bool pass18 = (r18 == pse::proxy::hardened::SpawnExecutionResult::SectorValidatorMissing);
        recordTest({"TC-PSE-58C-18", "DOC-58C Regression", "Missing Sector Validator",
                    "SPAWN_PENDING", "validator == nullptr", "SPAWN_PENDING",
                    pse::proxy::hardened::toString(sm.state()), 0, pass18 ? "PASS" : "FAIL", "Medium", "Validator required."});

        // 19: Portal crossing
        pse::proxy::hardened::VisualProxyStateMachine sm19(0);
        sm19.registerSimulationThread();
        sm19.configureSession(7365, 1);
        auto s19 = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -7008.0f, 1240.0f);
        sm19.handleSnapshot(s19, true, true, &seam);
        auto r19a = sm19.executePendingSpawn(&seam, &validator, 1007, 259);
        auto r19b = sm19.executePendingSpawn(&seam, &validator, 1007, 123);
        bool pass19 = (r19a == pse::proxy::hardened::SpawnExecutionResult::SectorValidationFailed) &&
                      (r19b == pse::proxy::hardened::SpawnExecutionResult::Success);
        recordTest({"TC-PSE-58C-19", "DOC-58C Regression", "Portal Crossing Verification (259 -> 123)",
                    "SPAWN_PENDING", "Coords in 123: 259 rejected, 123 accepted", "ACTIVE",
                    pse::proxy::hardened::toString(sm19.state()), seam.spawnCalls, pass19 ? "PASS" : "FAIL", "High", "Portal cross ok."});

        // 20: Aperture boundary tolerance
        pse::proxy::hardened::VisualProxyStateMachine sm20(0);
        sm20.registerSimulationThread();
        sm20.configureSession(7365, 1);
        auto s20 = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6912.0f, 1240.0f);
        sm20.handleSnapshot(s20, true, true, &seam);
        auto r20 = sm20.executePendingSpawn(&seam, &validator, 1007, 123);
        bool pass20 = (r20 == pse::proxy::hardened::SpawnExecutionResult::Success);
        recordTest({"TC-PSE-58C-20", "DOC-58C Regression", "Aperture Boundary Tolerance (Y = -6912.0)",
                    "SPAWN_PENDING", "Coords on portal plane", "ACTIVE",
                    pse::proxy::hardened::toString(sm20.state()), seam.spawnCalls, pass20 ? "PASS" : "FAIL", "Medium", "Epsilon ok."});

        // 21: Map epoch change between snapshot & spawn
        pse::proxy::hardened::VisualProxyStateMachine sm21(0);
        sm21.registerSimulationThread();
        sm21.configureSession(7365, 1);
        sm21.handleSnapshot(s, true, true, &seam);
        sm21.onMapInvalidated(true);
        auto r21 = sm21.executePendingSpawn(&seam, &validator, 1007, 259);
        bool pass21 = (r21 == pse::proxy::hardened::SpawnExecutionResult::NotPending ||
                       r21 == pse::proxy::hardened::SpawnExecutionResult::ShutdownBlocked) &&
                      (sm21.state() == pse::proxy::hardened::VisualProxyState::ABSENT);
        recordTest({"TC-PSE-58C-21", "DOC-58C Regression", "Map Epoch Change between Snapshot & Spawn",
                    "SPAWN_PENDING", "onMapInvalidated(true) while pending", "ABSENT",
                    pse::proxy::hardened::toString(sm21.state()), 0, pass21 ? "PASS" : "FAIL", "High", "Cancelled."});
    }

    // TC-PSE-58C-22 to 26 (Domain E: Affinity, Exceptions, Reentrancy)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);

        // 22: Worker thread rejection
        pse::proxy::hardened::VisualProxyStateMachine sm22(0);
        sm22.registerSimulationThread();
        sm22.configureSession(7365, 1);
        bool affOk = false;
        std::thread w22([&]() {
            auto r = sm22.executePendingSpawn(&seam, &validator, 1007, 259);
            affOk = (r == pse::proxy::hardened::SpawnExecutionResult::AffinityViolation);
        });
        w22.join();
        recordTest({"TC-PSE-58C-22", "DOC-58C Regression", "Worker Thread Cross-Call Rejection",
                    "ABSENT", "Worker thread invokes executePendingSpawn", "ABSENT",
                    pse::proxy::hardened::toString(sm22.state()), 0, affOk ? "PASS" : "FAIL", "Critical", "Protected."});

        // 23: Seam exception isolation
        pse::proxy::hardened::VisualProxyStateMachine sm23(0);
        sm23.registerSimulationThread();
        sm23.configureSession(7365, 1);
        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm23.handleSnapshot(s, true, true, &seam);
        seam.shouldThrowOnSpawn = true;
        auto r23a = sm23.executePendingSpawn(&seam, &validator, 1007, 259);
        seam.shouldThrowOnSpawn = false;
        sm23.executePendingSpawn(&seam, &validator, 1007, 259);
        seam.shouldThrowOnRemove = true;
        sm23.onDisconnect();
        auto r23b = sm23.executePendingRemove(&seam);
        seam.shouldThrowOnRemove = false;
        bool pass23 = (r23a == pse::proxy::hardened::SpawnExecutionResult::NativeExceptionCaught) &&
                      (r23b == pse::proxy::hardened::RemoveExecutionResult::NativeExceptionCaught) &&
                      (sm23.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);
        recordTest({"TC-PSE-58C-23", "DOC-58C Regression", "Mock Native Seam Exception Isolation",
                    "SPAWN/REMOVE", "Seam throws during spawn & remove", "QUARANTINED",
                    pse::proxy::hardened::toString(sm23.state()), seam.removeCalls, pass23 ? "PASS" : "FAIL", "Critical", "Exceptions caught."});

        // 24: noexcept specification
        recordTest({"TC-PSE-58C-24", "DOC-58C Regression", "noexcept Specification Verification",
                    "N/A", "Compile-time static_assert", "ALL_NOEXCEPT",
                    "ALL_NOEXCEPT", 0, "PASS", "Low", "noexcept proven."});

        // 25: Lifecycle order resilience
        pse::proxy::hardened::VisualProxyStateMachine sm25(0);
        sm25.registerSimulationThread();
        sm25.configureSession(7365, 1);
        auto r25a = sm25.executePendingSpawn(&seam, &validator, 1007, 259);
        auto r25b = sm25.executePendingRemove(&seam);
        bool pass25 = (r25a == pse::proxy::hardened::SpawnExecutionResult::NotPending) &&
                      (r25b == pse::proxy::hardened::RemoveExecutionResult::NotPending);
        recordTest({"TC-PSE-58C-25", "DOC-58C Regression", "Out-of-Order Lifecycle Sequence Resilience",
                    "ABSENT", "Spawn & Remove called while ABSENT", "ABSENT",
                    pse::proxy::hardened::toString(sm25.state()), 0, pass25 ? "PASS" : "FAIL", "Medium", "Safe no-op."});

        // 26: REENTRANCY HAZARD REMEDIATION VERIFICATION (WAS FAIL IN DOC-58C, NOW PASS!)
        pse::doc58d::MockNativeVisualProxySeam seam26(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm26(0);
        sm26.registerSimulationThread();
        sm26.configureSession(7365, 1);
        sm26.handleSnapshot(s, true, true, &seam26);

        pse::proxy::hardened::SpawnExecutionResult innerSpawnRes{pse::proxy::hardened::SpawnExecutionResult::Success};
        seam26.reentrantCallbackOnSpawn = true;
        seam26.onSpawnReentrantAction = [&]() {
            innerSpawnRes = sm26.executePendingSpawn(&seam26, &validator, 1007, 259);
        };

        auto outerSpawnRes = sm26.executePendingSpawn(&seam26, &validator, 1007, 259);

        bool pass26 = (innerSpawnRes == pse::proxy::hardened::SpawnExecutionResult::ReentrancyBlocked) &&
                      (outerSpawnRes == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                      (seam26.spawnCalls == 1) && // Zero extra native spawns!
                      (sm26.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        recordTest({"TC-PSE-58C-26", "DOC-58C Regression", "Reentrant Native Seam Callback Guard (Remediated)",
                    "SPAWN_PENDING", "Native hook triggers reentrant spawnVisualActor", "ACTIVE",
                    pse::proxy::hardened::toString(sm26.state()), seam26.spawnCalls,
                    pass26 ? "PASS" : "FAIL", "Critical",
                    "REMEDIATED: Reentrant call safely rejected with ReentrancyBlocked; zero actor leaks."});
    }

    // TC-PSE-58C-27 to 32 (Domain F: Multi-Client)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);

        // 27: Multi-slot filtering
        pse::proxy::hardened::VisualProxyStateMachine proxy0(0);
        pse::proxy::hardened::VisualProxyStateMachine proxy1(1);
        proxy0.registerSimulationThread();
        proxy1.registerSimulationThread();
        proxy0.configureSession(7365, 1);
        proxy1.configureSession(7365, 1);
        auto s0 = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5500, 1, -7424.0f, -6880.0f, 1240.0f);
        auto s1 = pse::doc58d::makeSnapshot(1, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rejCross = proxy0.handleSnapshot(s1, true, true, &seam);
        auto acc0 = proxy0.handleSnapshot(s0, true, true, &seam);
        auto acc1 = proxy1.handleSnapshot(s1, true, true, &seam);
        bool pass27 = (rejCross == pse::proxy::hardened::ProxyRejectionReason::WrongSlot) &&
                      (acc0 == pse::proxy::hardened::ProxyRejectionReason::None) &&
                      (acc1 == pse::proxy::hardened::ProxyRejectionReason::None);
        recordTest({"TC-PSE-58C-27", "DOC-58C Regression", "Multi-Slot Filtering & Target Isolation",
                    "ABSENT", "Cross-slot snapshot dispatch", "SPAWN_PENDING",
                    pse::proxy::hardened::toString(proxy0.state()), 0, pass27 ? "PASS" : "FAIL", "High", "Isolated."});

        // 28: 3-8 PLAYER SLOT DEFECT REMEDIATION VERIFICATION (WAS FAIL IN DOC-58C, NOW PASS!)
        pse::proxy::hardened::VisualProxyStateMachine proxy3(static_cast<uint8_t>(3));
        proxy3.registerSimulationThread();
        proxy3.configureSession(7365, 1);
        auto s3 = pse::doc58d::makeSnapshot(3, 1, 100, 7365, 1, 0x5503, 1, -7424.0f, -6880.0f, 1240.0f);
        auto acc3 = proxy3.handleSnapshot(s3, true, true, &seam);
        bool pass28 = (acc3 == pse::proxy::hardened::ProxyRejectionReason::None) &&
                      (proxy3.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING) &&
                      (proxy3.activeEntityId() == 0x5503);
        recordTest({"TC-PSE-58C-28", "DOC-58C Regression", "3-8 Player Slot MultiSlotGuard8 Support (Remediated)",
                    "ABSENT", "Snapshot for Slot 3 dispatched to VisualProxyStateMachine(3)", "SPAWN_PENDING",
                    pse::proxy::hardened::toString(proxy3.state()), 0,
                    pass28 ? "PASS" : "FAIL", "Critical",
                    "REMEDIATED: MultiSlotGuard8 accepts Slot 3 snapshot cleanly into SPAWN_PENDING."});

        // 29: Independent generations
        proxy0.executePendingSpawn(&seam, &validator, 1007, 259);
        proxy1.executePendingSpawn(&seam, &validator, 1007, 259);
        uint32_t g0 = proxy0.localProxyGeneration();
        proxy1.onDisconnect();
        proxy1.executePendingRemove(&seam);
        bool pass29 = (proxy0.localProxyGeneration() == g0) && (proxy1.localProxyGeneration() == 2);
        recordTest({"TC-PSE-58C-29", "DOC-58C Regression", "Independent Proxy Generations Across Slots",
                    "ACTIVE", "Proxy 1 removed, Proxy 0 gen verified unchanged", "GEN0_UNCHANGED",
                    "GEN0_UNCHANGED", seam.removeCalls, pass29 ? "PASS" : "FAIL", "High", "Independent."});

        // 30: Single client disconnect
        auto s0_update = pse::doc58d::makeSnapshot(0, 2, 101, 7365, 1, 0x5500, 1, -7420.0f, -6875.0f, 1240.0f);
        auto rej0 = proxy0.handleSnapshot(s0_update, true, true, &seam);
        bool pass30 = (proxy1.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                      (proxy0.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                      (rej0 == pse::proxy::hardened::ProxyRejectionReason::None);
        recordTest({"TC-PSE-58C-30", "DOC-58C Regression", "Single Client Disconnect with Others Active",
                    "ACTIVE", "Proxy 1 absent; Proxy 0 active streaming", "PROXY1_ABSENT_PROXY0_ACTIVE",
                    "PROXY1_ABSENT_PROXY0_ACTIVE", seam.removeCalls, pass30 ? "PASS" : "FAIL", "High", "Unaffected."});

        // 31: Quarantine isolation
        pse::doc58d::MockNativeVisualProxySeam seam31(&env);
        pse::proxy::hardened::VisualProxyStateMachine p0(0);
        pse::proxy::hardened::VisualProxyStateMachine p1(1);
        p0.registerSimulationThread(); p1.registerSimulationThread();
        p0.configureSession(7365, 1); p1.configureSession(7365, 1);
        p0.handleSnapshot(s0, true, true, &seam31);
        p1.handleSnapshot(s1, true, true, &seam31);
        p0.executePendingSpawn(&seam31, &validator, 1007, 259);
        p1.executePendingSpawn(&seam31, &validator, 1007, 259);
        seam31.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        p1.onDisconnect();
        p1.executePendingRemove(&seam31);
        seam31.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess;
        auto rejP0 = p0.handleSnapshot(s0_update, true, true, &seam31);
        bool pass31 = (p1.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED) &&
                      (p0.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                      (rejP0 == pse::proxy::hardened::ProxyRejectionReason::None);
        recordTest({"TC-PSE-58C-31", "DOC-58C Regression", "Quarantine Blast Radius Isolation",
                    "ACTIVE", "Proxy 1 quarantined; Proxy 0 streams normally", "PROXY1_QUAR_PROXY0_ACT",
                    "PROXY1_QUAR_PROXY0_ACT", seam31.updateCalls, pass31 ? "PASS" : "FAIL", "Critical", "Contained."});

        // 32: Reverse shutdown order
        pse::doc58d::MockKexLevelEnvironment env32;
        pse::doc58d::MockNativeVisualProxySeam seam32(&env32);
        std::array<std::unique_ptr<pse::proxy::hardened::VisualProxyStateMachine>, pse::MAX_PLAYERS> allProxies;
        for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
            allProxies[slot] = std::make_unique<pse::proxy::hardened::VisualProxyStateMachine>(slot);
            allProxies[slot]->registerSimulationThread();
            allProxies[slot]->configureSession(7365, 1);
        }
        env32.simulateEngineLevelUnload();
        int shutCount = 0;
        for (int slot = pse::MAX_PLAYERS - 1; slot >= 0; --slot) {
            allProxies[slot]->requestShutdown();
            auto res = allProxies[slot]->executeShutdownCleanup(&seam32, false);
            if ((res == pse::proxy::hardened::RemoveExecutionResult::LevelInvalidSkipped ||
                 res == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                allProxies[slot]->state() == pse::proxy::hardened::VisualProxyState::ABSENT) {
                shutCount++;
            }
        }
        bool pass32 = (shutCount == pse::MAX_PLAYERS) && (seam32.removeCalls == 0);
        recordTest({"TC-PSE-58C-32", "DOC-58C Regression", "Coordinated Reverse Shutdown Order (8 Slots)",
                    "8_PROXIES", "Reverse shutdown after level unload", "ALL_ABSENT",
                    "ALL_ABSENT", seam32.removeCalls, pass32 ? "PASS" : "FAIL", "High", "Clean order."});
    }

    // ========================================================================
    // SUITE 3: NEW DOC-58D ADVANCED ADVERSARIAL TESTS (20 SCENARIOS)
    // ========================================================================
    std::cout << "\n============================================================================\n";
    std::cout << "SUITE 3: NEW DOC-58D ADVANCED ADVERSARIAL TESTS (20 SCENARIOS)\n";
    std::cout << "============================================================================\n";

    // TC-PSE-58D-01: Reentrant executePendingSpawn during active spawn -> ReentrancyBlocked
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);

        pse::proxy::hardened::SpawnExecutionResult innerResult{pse::proxy::hardened::SpawnExecutionResult::Success};
        seam.reentrantCallbackOnSpawn = true;
        seam.onSpawnReentrantAction = [&]() {
            innerResult = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        };

        auto outerResult = sm.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (innerResult == pse::proxy::hardened::SpawnExecutionResult::ReentrancyBlocked) &&
                    (outerResult == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                    (seam.spawnCalls == 1) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        recordTest({"TC-PSE-58D-01", "DOC-58D Adversarial", "Reentrant Spawn Blocked (m_inSeamCall)",
                    "SPAWN_PENDING", "Inner executePendingSpawn during seam spawn", "ACTIVE",
                    pse::proxy::hardened::toString(sm.state()), seam.spawnCalls, pass ? "PASS" : "FAIL", "Critical",
                    "Inner spawn rejected with ReentrancyBlocked; zero leaked actors."});
    }

    // TC-PSE-58D-02: Reentrant executePendingRemove during active remove -> ReentrancyBlocked
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        sm.onDisconnect();

        pse::proxy::hardened::RemoveExecutionResult innerResult{pse::proxy::hardened::RemoveExecutionResult::Success};
        seam.reentrantCallbackOnRemove = true;
        seam.onRemoveReentrantAction = [&]() {
            innerResult = sm.executePendingRemove(&seam);
        };

        auto outerResult = sm.executePendingRemove(&seam);

        bool pass = (innerResult == pse::proxy::hardened::RemoveExecutionResult::ReentrancyBlocked) &&
                    (outerResult == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                    (seam.removeCalls == 1) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({"TC-PSE-58D-02", "DOC-58D Adversarial", "Reentrant Remove Blocked (Double-Free Avoided)",
                    "REMOVE_PENDING", "Inner executePendingRemove during seam remove", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass ? "PASS" : "FAIL", "Critical",
                    "Inner remove rejected with ReentrancyBlocked; double-free avoided."});
    }

    // TC-PSE-58D-03: Reentrant executeShutdownCleanup during active remove -> ReentrancyBlocked
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        sm.onDisconnect();

        pse::proxy::hardened::RemoveExecutionResult innerCleanupRes{pse::proxy::hardened::RemoveExecutionResult::Success};
        seam.reentrantCallbackOnRemove = true;
        seam.onRemoveReentrantAction = [&]() {
            innerCleanupRes = sm.executeShutdownCleanup(&seam, true);
        };

        auto outerResult = sm.executePendingRemove(&seam);

        bool pass = (innerCleanupRes == pse::proxy::hardened::RemoveExecutionResult::ReentrancyBlocked) &&
                    (outerResult == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                    (seam.removeCalls == 1) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({"TC-PSE-58D-03", "DOC-58D Adversarial", "Reentrant Shutdown Cleanup Blocked",
                    "REMOVE_PENDING", "Inner executeShutdownCleanup during active remove", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass ? "PASS" : "FAIL", "High",
                    "Shutdown cleanup during active seam call cleanly rejected."});
    }

    // TC-PSE-58D-04: Reentrant handleSnapshot during updateVisualTransform -> ReentrancyBlocked
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s1 = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s1, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        pse::proxy::hardened::ProxyRejectionReason innerRej{pse::proxy::hardened::ProxyRejectionReason::None};
        auto s2 = pse::doc58d::makeSnapshot(0, 2, 101, 7365, 1, 0x5501, 1, -7420.0f, -6875.0f, 1240.0f);
        auto s3 = pse::doc58d::makeSnapshot(0, 3, 102, 7365, 1, 0x5501, 1, -7415.0f, -6870.0f, 1240.0f);

        seam.reentrantCallbackOnUpdate = true;
        seam.onUpdateReentrantAction = [&]() {
            innerRej = sm.handleSnapshot(s3, true, true, &seam);
        };

        auto outerRej = sm.handleSnapshot(s2, true, true, &seam);

        bool pass = (innerRej == pse::proxy::hardened::ProxyRejectionReason::ReentrancyBlocked) &&
                    (outerRej == pse::proxy::hardened::ProxyRejectionReason::None) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                    (sm.currentTransform().x == -7420.0f); // s2 applied cleanly, s3 blocked

        recordTest({"TC-PSE-58D-04", "DOC-58D Adversarial", "Reentrant Snapshot during Transform Update",
                    "ACTIVE", "Inner handleSnapshot during updateVisualTransform", "ACTIVE",
                    pse::proxy::hardened::toString(sm.state()), seam.updateCalls, pass ? "PASS" : "FAIL", "Medium",
                    "State transform consistency preserved; reentrant snapshot blocked."});
    }

    // TC-PSE-58D-05: Reentrant requestShutdown during seam call -> returns false
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);

        bool innerShutdownResult = true;
        seam.reentrantCallbackOnSpawn = true;
        seam.onSpawnReentrantAction = [&]() {
            innerShutdownResult = sm.requestShutdown();
        };

        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (!innerShutdownResult) && (!sm.isShutdownRequested()) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        recordTest({"TC-PSE-58D-05", "DOC-58D Adversarial", "Reentrant requestShutdown Blocked",
                    "SPAWN_PENDING", "requestShutdown invoked inside spawnVisualActor", "ACTIVE",
                    pse::proxy::hardened::toString(sm.state()), seam.spawnCalls, pass ? "PASS" : "FAIL", "High",
                    "requestShutdown returns false while seam call in flight."});
    }

    // TC-PSE-58D-06: Scope guard unwinding on seam exception resets m_inSeamCall
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);

        // First attempt throws exception in seam
        seam.shouldThrowOnSpawn = true;
        auto resThrow = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        bool guardResetOk = (!sm.isInSeamCall());

        // Second attempt succeeds immediately because m_inSeamCall was cleanly reset
        seam.shouldThrowOnSpawn = false;
        auto resRetry = sm.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (resThrow == pse::proxy::hardened::SpawnExecutionResult::NativeExceptionCaught) &&
                    guardResetOk &&
                    (resRetry == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        recordTest({"TC-PSE-58D-06", "DOC-58D Adversarial", "Scope Guard Unwinding on Seam Exception",
                    "SPAWN_PENDING", "Seam throws exception; verify m_inSeamCall reset", "ACTIVE",
                    pse::proxy::hardened::toString(sm.state()), seam.spawnCalls, pass ? "PASS" : "FAIL", "High",
                    "SeamCallScopeGuard unwinds cleanly; subsequent calls succeed."});
    }

    // TC-PSE-58D-07: Sequential reentrancy bursts (all blocked without state corruption)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);

        int burstRejectionCount = 0;
        seam.reentrantCallbackOnSpawn = true;
        seam.onSpawnReentrantAction = [&]() {
            for (int i = 0; i < 5; ++i) {
                auto r = sm.executePendingSpawn(&seam, &validator, 1007, 259);
                if (r == pse::proxy::hardened::SpawnExecutionResult::ReentrancyBlocked) {
                    burstRejectionCount++;
                }
            }
        };

        auto res = sm.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (burstRejectionCount == 5) &&
                    (res == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                    (seam.spawnCalls == 1) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        recordTest({"TC-PSE-58D-07", "DOC-58D Adversarial", "Sequential Reentrancy Burst Resilience",
                    "SPAWN_PENDING", "5 consecutive reentrant spawn calls inside seam", "ACTIVE",
                    pse::proxy::hardened::toString(sm.state()), seam.spawnCalls, pass ? "PASS" : "FAIL", "Critical",
                    "All 5 reentrant calls blocked; exactly 1 native actor created."});
    }

    // TC-PSE-58D-08: Full 8-Slot Sweep (Slots 0..7 all accepted cleanly)
    {
        std::array<std::unique_ptr<pse::proxy::hardened::VisualProxyStateMachine>, pse::MAX_PLAYERS> proxies;
        int acceptedCount = 0;

        for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
            proxies[slot] = std::make_unique<pse::proxy::hardened::VisualProxyStateMachine>(slot);
            proxies[slot]->registerSimulationThread();
            proxies[slot]->configureSession(7365, 1);

            auto s = pse::doc58d::makeSnapshot(slot, 1, 100, 7365, 1, 0x5500 + slot, 1, -7424.0f, -6880.0f, 1240.0f);
            auto rej = proxies[slot]->handleSnapshot(s, true, true);
            if (rej == pse::proxy::hardened::ProxyRejectionReason::None &&
                proxies[slot]->state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING) {
                acceptedCount++;
            }
        }

        bool pass = (acceptedCount == pse::MAX_PLAYERS);

        recordTest({"TC-PSE-58D-08", "DOC-58D Adversarial", "Full 8-Slot Sweep (Slots 0..7 All Accepted)",
                    "ALL_ABSENT", "Valid snapshot dispatched to each of slots 0..7", "ALL_SPAWN_PENDING",
                    (acceptedCount == 8) ? "ALL_SPAWN_PENDING" : "PARTIAL_REJECTION", 0, pass ? "PASS" : "FAIL", "Critical",
                    "MultiSlotGuard8 enables full 8-player co-op across all slots."});
    }

    // TC-PSE-58D-09: Invalid Slot 8 boundary rejection
    {
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s8 = pse::doc58d::makeSnapshot(8 /* Out of bounds */, 1, 100, 7365, 1, 0x5508, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rej = sm.handleSnapshot(s8, true, true);

        // Target mismatch: slot 8 != targetSlot 0 -> WrongSlot
        bool step1Ok = (rej == pse::proxy::hardened::ProxyRejectionReason::WrongSlot);

        // Target is 8: guard must reject slotId >= slots.size() -> IdentityMismatch
        pse::proxy::hardened::VisualProxyStateMachine sm8(8);
        sm8.registerSimulationThread();
        sm8.configureSession(7365, 1);
        auto rejGuard = sm8.handleSnapshot(s8, true, true);
        bool step2Ok = (rejGuard == pse::proxy::hardened::ProxyRejectionReason::IdentityMismatch);

        bool pass = step1Ok && step2Ok;

        recordTest({"TC-PSE-58D-09", "DOC-58D Adversarial", "Invalid Slot 8 Boundary Rejection",
                    "ABSENT", "Snapshot with slotId = 8 dispatched", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Slot 8 strictly rejected by guard capacity check."});
    }

    // TC-PSE-58D-10: Invalid Slot 255 boundary rejection
    {
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s255 = pse::doc58d::makeSnapshot(255, 1, 100, 7365, 1, 0x55FF, 1, -7424.0f, -6880.0f, 1240.0f);
        auto rej = sm.handleSnapshot(s255, true, true);

        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::WrongSlot) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({"TC-PSE-58D-10", "DOC-58D Adversarial", "Invalid Slot 255 Boundary Rejection",
                    "ABSENT", "Snapshot with slotId = 255 dispatched", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "Medium",
                    "Maximum uint8_t slot boundary safely rejected."});
    }

    // TC-PSE-58D-11: Cross-slot replay isolation
    {
        pse::proxy::hardened::VisualProxyStateMachine proxy2(2);
        pse::proxy::hardened::VisualProxyStateMachine proxy3(3);
        proxy2.registerSimulationThread(); proxy3.registerSimulationThread();
        proxy2.configureSession(7365, 1); proxy3.configureSession(7365, 1);

        auto s2 = pse::doc58d::makeSnapshot(2, 1, 100, 7365, 1, 0x5502, 1, -7424.0f, -6880.0f, 1240.0f);
        proxy2.handleSnapshot(s2, true, true);

        // Attempt to replay Slot 2 snapshot to Slot 3
        auto rej = proxy3.handleSnapshot(s2, true, true);

        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::WrongSlot) &&
                    (proxy3.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({"TC-PSE-58D-11", "DOC-58D Adversarial", "Cross-Slot Replay Isolation",
                    "ABSENT", "Slot 2 snapshot replayed to Proxy 3", "ABSENT",
                    pse::proxy::hardened::toString(proxy3.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Cross-slot replay rejected without polluting slot 3 state."});
    }

    // TC-PSE-58D-12: Simultaneous 7 Remote Proxies Streaming Transforms
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);

        std::array<std::unique_ptr<pse::proxy::hardened::VisualProxyStateMachine>, pse::MAX_PLAYERS> proxies;
        for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
            proxies[slot] = std::make_unique<pse::proxy::hardened::VisualProxyStateMachine>(slot);
            proxies[slot]->registerSimulationThread();
            proxies[slot]->configureSession(7365, 1);

            if (slot != 1) { // 7 remote proxies (0, 2, 3, 4, 5, 6, 7)
                auto s = pse::doc58d::makeSnapshot(slot, 1, 100, 7365, 1, 0x5500 + slot, 1, -7424.0f, -6880.0f, 1240.0f);
                proxies[slot]->handleSnapshot(s, true, true, &seam);
                proxies[slot]->executePendingSpawn(&seam, &validator, 1007, 259);
            }
        }

        // Stream 10 transform updates to all 7 remote proxies
        int totalUpdatesApplied = 0;
        for (uint32_t step = 2; step <= 11; ++step) {
            for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
                if (slot != 1) {
                    auto s = pse::doc58d::makeSnapshot(slot, step, 100 + step, 7365, 1, 0x5500 + slot, 1,
                                                        -7424.0f + static_cast<float>(step), -6880.0f, 1240.0f);
                    auto rej = proxies[slot]->handleSnapshot(s, true, true, &seam);
                    if (rej == pse::proxy::hardened::ProxyRejectionReason::None) {
                        totalUpdatesApplied++;
                    }
                }
            }
        }

        bool pass = (totalUpdatesApplied == 70) && (seam.updateCalls == 70);

        recordTest({"TC-PSE-58D-12", "DOC-58D Adversarial", "Simultaneous 7 Remote Proxies Transform Streaming",
                    "7_PROXIES_ACTIVE", "10 updates streamed to all 7 remote proxies", "ALL_ACTIVE",
                    "ALL_ACTIVE", seam.updateCalls, pass ? "PASS" : "FAIL", "Critical",
                    "Zero cross-talk; all 70 updates applied with independent coordinates."});
    }

    // TC-PSE-58D-13: Single Client Disconnect & Rapid Reconnect in 8-Player Session
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);

        pse::proxy::hardened::VisualProxyStateMachine proxy4(4);
        proxy4.registerSimulationThread();
        proxy4.configureSession(7365, 1);

        auto s1 = pse::doc58d::makeSnapshot(4, 1, 100, 7365, 1, 0x5504, 1, -7424.0f, -6880.0f, 1240.0f);
        proxy4.handleSnapshot(s1, true, true, &seam);
        proxy4.executePendingSpawn(&seam, &validator, 1007, 259);
        assert(proxy4.state() == pse::proxy::hardened::VisualProxyState::ACTIVE);

        // Player 4 drops
        proxy4.onDisconnect();
        proxy4.executePendingRemove(&seam);
        assert(proxy4.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        // Player 4 reconnects with connectionGen = 2
        proxy4.configureSession(7365, 2);
        auto s2 = pse::doc58d::makeSnapshot(4, 1, 10, 7365, 2, 0x5504, 2, -7424.0f, -6880.0f, 1240.0f);
        proxy4.handleSnapshot(s2, true, true, &seam);
        auto spRes = proxy4.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (spRes == pse::proxy::hardened::SpawnExecutionResult::Success) &&
                    (proxy4.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                    (proxy4.localProxyGeneration() == 3);

        recordTest({"TC-PSE-58D-13", "DOC-58D Adversarial", "Client Disconnect & Reconnect in Slot 4",
                    "ACTIVE", "Slot 4 drops and reconnects with connectionGen = 2", "ACTIVE",
                    pse::proxy::hardened::toString(proxy4.state()), seam.spawnCalls, pass ? "PASS" : "FAIL", "High",
                    "Slot 4 teardown and re-spawn cleanly isolated."});
    }

    // TC-PSE-58D-14: Quarantine Blast Radius Isolation in 8-Player Session
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);

        pse::proxy::hardened::VisualProxyStateMachine proxy6(6);
        pse::proxy::hardened::VisualProxyStateMachine proxy7(7);
        proxy6.registerSimulationThread(); proxy7.registerSimulationThread();
        proxy6.configureSession(7365, 1); proxy7.configureSession(7365, 1);

        auto s6 = pse::doc58d::makeSnapshot(6, 1, 100, 7365, 1, 0x5506, 1, -7424.0f, -6880.0f, 1240.0f);
        auto s7 = pse::doc58d::makeSnapshot(7, 1, 100, 7365, 1, 0x5507, 1, -7424.0f, -6880.0f, 1240.0f);
        proxy6.handleSnapshot(s6, true, true, &seam);
        proxy7.handleSnapshot(s7, true, true, &seam);
        proxy6.executePendingSpawn(&seam, &validator, 1007, 259);
        proxy7.executePendingSpawn(&seam, &validator, 1007, 259);

        // Proxy 6 fails remove -> enters quarantine
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::DefiniteFailure;
        proxy6.onDisconnect();
        proxy6.executePendingRemove(&seam);
        assert(proxy6.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED);

        // Proxy 7 continues streaming updates cleanly
        seam.programmedRemoveOutcome = pse::proxy::hardened::RemoveOutcome::ConfirmedSuccess;
        auto s7_up = pse::doc58d::makeSnapshot(7, 2, 101, 7365, 1, 0x5507, 1, -7420.0f, -6875.0f, 1240.0f);
        auto rej7 = proxy7.handleSnapshot(s7_up, true, true, &seam);

        bool pass = (proxy6.state() == pse::proxy::hardened::VisualProxyState::QUARANTINED) &&
                    (proxy7.state() == pse::proxy::hardened::VisualProxyState::ACTIVE) &&
                    (rej7 == pse::proxy::hardened::ProxyRejectionReason::None);

        recordTest({"TC-PSE-58D-14", "DOC-58D Adversarial", "Quarantine Blast Radius Isolation (Slot 6 vs 7)",
                    "ACTIVE", "Proxy 6 quarantined; Proxy 7 updates normally", "P6_QUAR_P7_ACT",
                    "P6_QUAR_P7_ACT", seam.updateCalls, pass ? "PASS" : "FAIL", "Critical",
                    "Quarantine blast radius strictly isolated between remote client slots."});
    }

    // TC-PSE-58D-15: Reverse-Order Shutdown of all 8 Slots after Level Unload
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);

        std::array<std::unique_ptr<pse::proxy::hardened::VisualProxyStateMachine>, pse::MAX_PLAYERS> proxies;
        for (uint8_t slot = 0; slot < pse::MAX_PLAYERS; ++slot) {
            proxies[slot] = std::make_unique<pse::proxy::hardened::VisualProxyStateMachine>(slot);
            proxies[slot]->registerSimulationThread();
            proxies[slot]->configureSession(7365, 1);
        }

        env.simulateEngineLevelUnload();

        int decommissionedCount = 0;
        for (int slot = pse::MAX_PLAYERS - 1; slot >= 0; --slot) {
            proxies[slot]->requestShutdown();
            auto res = proxies[slot]->executeShutdownCleanup(&seam, false);
            if ((res == pse::proxy::hardened::RemoveExecutionResult::LevelInvalidSkipped ||
                 res == pse::proxy::hardened::RemoveExecutionResult::Success) &&
                proxies[slot]->state() == pse::proxy::hardened::VisualProxyState::ABSENT) {
                decommissionedCount++;
            }
        }

        bool pass = (decommissionedCount == pse::MAX_PLAYERS) && (seam.removeCalls == 0);

        recordTest({"TC-PSE-58D-15", "DOC-58D Adversarial", "Reverse-Order Shutdown (8 Slots)",
                    "8_PROXIES", "Decommission slots 7 down to 0 after level unload", "ALL_ABSENT",
                    "ALL_ABSENT", 0, pass ? "PASS" : "FAIL", "High",
                    "Zero seam calls, zero crashes on reverse-order multi-slot shutdown."});
    }

    // TC-PSE-58D-16: Formal Pre-Unload Barrier Activation (onPreUnload)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING);

        // Pre-Unload Barrier activated at entry of Level::Unload before sectors freed
        sm.onPreUnload();

        bool pass = sm.isTeardownBarrierActive() && (!sm.isSessionActive()) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        recordTest({"TC-PSE-58D-16", "DOC-58D Adversarial", "Pre-Unload Barrier Activation (onPreUnload)",
                    "SPAWN_PENDING", "onPreUnload invoked at entry to Level::Unload", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Teardown barrier set, session deactivated, pending spawn safely cancelled."});
    }

    // TC-PSE-58D-17: Pre-Unload Barrier Blocks New Snapshot Updates
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.onPreUnload(); // Sets teardown barrier, transitions ACTIVE -> REMOVE_PENDING

        auto s_late = pse::doc58d::makeSnapshot(0, 2, 101, 7365, 1, 0x5501, 1, -7420.0f, -6875.0f, 1240.0f);
        auto rej = sm.handleSnapshot(s_late, true, true, &seam);

        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::ShutdownRequested) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::REMOVE_PENDING);

        recordTest({"TC-PSE-58D-17", "DOC-58D Adversarial", "Pre-Unload Barrier Blocks Snapshots",
                    "REMOVE_PENDING", "handleSnapshot during active teardown barrier", "REMOVE_PENDING",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Late network snapshot rejected with ShutdownRequested."});
    }

    // TC-PSE-58D-18: Pre-Unload Barrier Blocks Spawn
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        sm.onPreUnload();
        auto res = sm.executePendingSpawn(&seam, &validator, 1007, 259);

        bool pass = (res == pse::proxy::hardened::SpawnExecutionResult::ShutdownBlocked) &&
                    (seam.spawnCalls == 0);

        recordTest({"TC-PSE-58D-18", "DOC-58D Adversarial", "Pre-Unload Barrier Blocks Spawn",
                    "ABSENT", "executePendingSpawn during active teardown barrier", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Spawn blocked; factory call count strictly zero."});
    }

    // TC-PSE-58D-19: Pre-Unload Barrier Skips Native Remove (Preventing Drain UAF)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        // Pre-unload barrier activated
        sm.onPreUnload();
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::REMOVE_PENDING);

        // Call executePendingRemove during teardown barrier -> skips native call!
        auto res = sm.executePendingRemove(&seam);

        bool pass = (res == pse::proxy::hardened::RemoveExecutionResult::LevelInvalidSkipped) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                    (sm.activeActor() == nullptr) &&
                    (seam.removeCalls == 0); // Native seam call strictly skipped!

        recordTest({"TC-PSE-58D-19", "DOC-58D Adversarial", "Pre-Unload Barrier Skips Native Remove (Drain UAF Avoided)",
                    "REMOVE_PENDING", "executePendingRemove during active teardown barrier", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.removeCalls, pass ? "PASS" : "FAIL", "Critical",
                    "Native seam call skipped; actor pointer nullified safely without Drain UAF."});
    }

    // TC-PSE-58D-20: Post-Unload Completion (onPostUnload)
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);
        uint32_t genBefore = sm.localProxyGeneration();

        sm.onPreUnload();
        sm.onPostUnload();

        bool barrierRetained = sm.isTeardownBarrierActive();
        void* lastUnloaded = sm.lastUnloadedActor();
        sm.onMapReady();
        bool barrierCleared = !sm.isTeardownBarrierActive();

        bool pass = barrierRetained && barrierCleared &&
                    (lastUnloaded != nullptr) &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT) &&
                    (sm.activeActor() == nullptr) &&
                    (sm.localProxyGeneration() == genBefore + 1);

        recordTest({"TC-PSE-58D-20", "DOC-58D Adversarial", "Post-Unload Clean State Reset (onPostUnload)",
                    "REMOVE_PENDING", "onPostUnload invoked after Level::Unload finishes", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Teardown barrier retained until onMapReady, pointer nullified, generation monotonically advanced."});
    }

    // ========================================================================
    // SUITE 4: NEW DOC-58D.1 RECONCILED CONTRACT AUDIT TESTS (4 SCENARIOS)
    // ========================================================================
    std::cout << "\n============================================================================\n";
    std::cout << "SUITE 4: NEW DOC-58D.1 RECONCILED CONTRACT AUDIT TESTS (4 SCENARIOS)\n";
    std::cout << "============================================================================\n";

    // TC-PSE-58D1-01: Map Epoch Change via configureSession cancels pending spawn to ABSENT
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        assert(sm.state() == pse::proxy::hardened::VisualProxyState::SPAWN_PENDING);

        // Host advances map epoch to 7366; new session configured
        sm.configureSession(7366, 1);

        // Stale spawn from epoch 7365 must be cancelled to ABSENT
        bool pass = (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        // Subsequent executePendingSpawn must be rejected as NotPending
        auto spRes = sm.executePendingSpawn(&seam, &validator, 1007, 259);
        pass = pass && (spRes == pse::proxy::hardened::SpawnExecutionResult::NotPending) && (seam.spawnCalls == 0);

        recordTest({"TC-PSE-58D1-01", "DOC-58D.1 Audit", "Map Epoch configureSession Cancels Pending Spawn",
                    "SPAWN_PENDING", "configureSession(7366, 1) invoked while spawn is pending", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), seam.spawnCalls, pass ? "PASS" : "FAIL", "Critical",
                    "Stale pending spawn from prior epoch safely dropped to ABSENT on epoch advance."});
    }

    // TC-PSE-58D1-02: Post-Unload Teardown Barrier blocks snapshots until onMapReady
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        sm.onPreUnload();
        sm.onPostUnload();

        // Level is unloaded. Barrier must block snapshots even if someone passes mapReady=true
        auto rej = sm.handleSnapshot(s, true, true, &seam);
        bool pass = (rej == pse::proxy::hardened::ProxyRejectionReason::ShutdownRequested) &&
                    sm.isTeardownBarrierActive() &&
                    (sm.state() == pse::proxy::hardened::VisualProxyState::ABSENT);

        // Lower barrier when new map is ready
        sm.onMapReady();
        bool barrierLowered = !sm.isTeardownBarrierActive();
        pass = pass && barrierLowered;

        recordTest({"TC-PSE-58D1-02", "DOC-58D.1 Audit", "Post-Unload Barrier Blocks Snapshots until onMapReady",
                    "ABSENT", "handleSnapshot during active post-unload teardown barrier", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "Critical",
                    "Native operations prohibited between level unload and onMapReady confirmation."});
    }

    // TC-PSE-58D1-03: Last Unloaded Actor Pointer tracking preserved across onPostUnload
    {
        pse::doc58d::MockKexLevelEnvironment env;
        pse::doc58d::MockNativeVisualProxySeam seam(&env);
        pse::doc58d::MockSectorValidator validator(&env);
        pse::proxy::hardened::VisualProxyStateMachine sm(0);
        sm.registerSimulationThread();
        sm.configureSession(7365, 1);

        auto s = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5501, 1, -7424.0f, -6880.0f, 1240.0f);
        sm.handleSnapshot(s, true, true, &seam);
        sm.executePendingSpawn(&seam, &validator, 1007, 259);

        void* liveActorPtr = sm.activeActor();
        assert(liveActorPtr != nullptr);

        sm.onPreUnload();
        sm.onPostUnload();

        bool pass = (sm.activeActor() == nullptr) &&
                    (sm.lastUnloadedActor() == liveActorPtr);

        recordTest({"TC-PSE-58D1-03", "DOC-58D.1 Audit", "Last Unloaded Actor Pointer Tracking",
                    "ACTIVE", "Tracked native pointer preserved in lastUnloadedActor() for audit", "ABSENT",
                    pse::proxy::hardened::toString(sm.state()), 0, pass ? "PASS" : "FAIL", "High",
                    "Pointer not silently discarded; preserved for GATE-PSE-58C-01 verification."});
    }

    // TC-PSE-58D1-04: MultiSlotGuard8 Session Identity & Slot Isolation
    {
        pse::snapshot::MultiSlotGuard8 guard;
        guard.reset(1, 7365); // connectionGen = 1, epoch = 7365

        auto s0 = pse::doc58d::makeSnapshot(0, 1, 100, 7365, 1, 0x5500, 1, -7424.0f, -6880.0f, 1240.0f);
        auto sWrongEpoch = pse::doc58d::makeSnapshot(0, 2, 101, 7366, 1, 0x5500, 1, -7424.0f, -6880.0f, 1240.0f);
        auto sWrongGen = pse::doc58d::makeSnapshot(0, 2, 101, 7365, 2, 0x5500, 1, -7424.0f, -6880.0f, 1240.0f);
        auto sSlot7 = pse::doc58d::makeSnapshot(7, 1, 100, 7365, 1, 0x5507, 1, -7424.0f, -6880.0f, 1240.0f);

        auto v0 = guard.accept(s0, true);
        auto vEpoch = guard.accept(sWrongEpoch, true);
        auto vGen = guard.accept(sWrongGen, true);
        auto v7 = guard.accept(sSlot7, true);

        bool pass = (v0 == snapshot55::Verdict::Accept) &&
                    (vEpoch == snapshot55::Verdict::Identity) &&
                    (vGen == snapshot55::Verdict::Identity) &&
                    (v7 == snapshot55::Verdict::Accept);

        recordTest({"TC-PSE-58D1-04", "DOC-58D.1 Audit", "MultiSlotGuard8 Session Identity Invariants",
                    "GUARD_CONFIGURED", "Verify epoch mismatch, gen mismatch, and slot 7 accept", "ALL_VERIFIED",
                    "ALL_VERIFIED", 0, pass ? "PASS" : "FAIL", "Critical",
                    "Session identity (epoch & connectionGen) strictly enforced per slot 0..7."});
    }

    // ========================================================================
    // SUMMARY STATISTICS & JSON EXPORT
    // ========================================================================
    int passCount = 0;
    int failCount = 0;
    int blockedCount = 0;
    int nativeReqCount = 0;

    int doc58b_pass = 0, doc58b_fail = 0;
    int doc58c_pass = 0, doc58c_fail = 0, doc58c_nat = 0;
    int doc58d_pass = 0, doc58d_fail = 0;
    int doc58d1_pass = 0, doc58d1_fail = 0;

    for (const auto& r : results) {
        if (r.verdict == "PASS") passCount++;
        else if (r.verdict == "FAIL") failCount++;
        else if (r.verdict == "BLOCKED") blockedCount++;
        else if (r.verdict == "NATIVE_VERIFICATION_REQUIRED") nativeReqCount++;

        if (r.suite == "DOC-58B Regression") {
            if (r.verdict == "PASS") doc58b_pass++; else doc58b_fail++;
        } else if (r.suite == "DOC-58C Regression") {
            if (r.verdict == "PASS") doc58c_pass++;
            else if (r.verdict == "FAIL") doc58c_fail++;
            else if (r.verdict == "NATIVE_VERIFICATION_REQUIRED") doc58c_nat++;
        } else if (r.suite == "DOC-58D Adversarial") {
            if (r.verdict == "PASS") doc58d_pass++; else doc58d_fail++;
        } else if (r.suite == "DOC-58D.1 Audit") {
            if (r.verdict == "PASS") doc58d1_pass++; else doc58d1_fail++;
        }
    }

    std::cout << "\n============================================================================\n";
    std::cout << "DOC-58D.1 REGRESSION & CONTRACT RECONCILIATION QA SUMMARY:\n";
    std::cout << "----------------------------------------------------------------------------\n";
    std::cout << "Suite 1: DOC-58B Regression:   " << doc58b_pass << "/18 PASS (Failures: " << doc58b_fail << ")\n";
    std::cout << "Suite 2: DOC-58C Regression:   " << doc58c_pass << "/32 PASS (" << doc58c_nat << " NATIVE_REQ, Failures: " << doc58c_fail << ")\n";
    std::cout << "Suite 3: DOC-58D Adversarial:  " << doc58d_pass << "/20 PASS (Failures: " << doc58d_fail << ")\n";
    std::cout << "Suite 4: DOC-58D.1 Audit:      " << doc58d1_pass << "/4 PASS (Failures: " << doc58d1_fail << ")\n";
    std::cout << "----------------------------------------------------------------------------\n";
    std::cout << "OVERALL TOTALS: " << results.size() << " Scenarios Executed\n";
    std::cout << "  PASS:                        " << passCount << " / " << results.size() << "\n";
    std::cout << "  FAIL:                        " << failCount << "\n";
    std::cout << "  NATIVE_VERIFICATION_REQUIRED:" << nativeReqCount << "\n";
    std::cout << "  BLOCKED:                     " << blockedCount << "\n";
    std::cout << "============================================================================\n";

    // Write machine-readable TEST_RESULTS.json
    std::ofstream jsonFile("TEST_RESULTS.json");
    if (jsonFile.is_open()) {
        jsonFile << "{\n";
        jsonFile << "  \"metadata\": {\n";
        jsonFile << "    \"suite\": \"DOC-58D.1 Regression Integrity & Contract Reconciliation\",\n";
        jsonFile << "    \"target\": \"doc58d1_hardened_visual_proxy_state_machine.hpp\",\n";
        jsonFile << "    \"guard\": \"multi_slot_snapshot_guard.hpp\",\n";
        jsonFile << "    \"timestamp\": \"2026-11-03T20:00:00Z\",\n";
        jsonFile << "    \"totalTests\": " << results.size() << ",\n";
        jsonFile << "    \"passed\": " << passCount << ",\n";
        jsonFile << "    \"failed\": " << failCount << ",\n";
        jsonFile << "    \"nativeVerificationRequired\": " << nativeReqCount << ",\n";
        jsonFile << "    \"blocked\": " << blockedCount << ",\n";
        jsonFile << "    \"suites\": {\n";
        jsonFile << "      \"doc58b_regression\": { \"passed\": " << doc58b_pass << ", \"failed\": " << doc58b_fail << " },\n";
        jsonFile << "      \"doc58c_regression\": { \"passed\": " << doc58c_pass << ", \"failed\": " << doc58c_fail << ", \"nativeVerificationRequired\": " << doc58c_nat << " },\n";
        jsonFile << "      \"doc58d_adversarial\": { \"passed\": " << doc58d_pass << ", \"failed\": " << doc58d_fail << " },\n";
        jsonFile << "      \"doc58d1_audit\": { \"passed\": " << doc58d1_pass << ", \"failed\": " << doc58d1_fail << " }\n";
        jsonFile << "    }\n";
        jsonFile << "  },\n";
        jsonFile << "  \"results\": [\n";

        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            jsonFile << "    {\n";
            jsonFile << "      \"id\": \"" << r.id << "\",\n";
            jsonFile << "      \"suite\": \"" << r.suite << "\",\n";
            jsonFile << "      \"name\": \"" << r.name << "\",\n";
            jsonFile << "      \"initialState\": \"" << r.initialState << "\",\n";
            jsonFile << "      \"injectedFault\": \"" << r.injectedFault << "\",\n";
            jsonFile << "      \"expectedState\": \"" << r.expectedState << "\",\n";
            jsonFile << "      \"actualState\": \"" << r.actualState << "\",\n";
            jsonFile << "      \"seamCalls\": " << r.seamCalls << ",\n";
            jsonFile << "      \"verdict\": \"" << r.verdict << "\",\n";
            jsonFile << "      \"riskLevel\": \"" << r.riskLevel << "\",\n";
            jsonFile << "      \"notes\": \"" << r.notes << "\"\n";
            jsonFile << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }

        jsonFile << "  ]\n";
        jsonFile << "}\n";
        jsonFile.close();
        std::cout << "Successfully saved machine-readable results to TEST_RESULTS.json\n";
    }

    return (failCount > 0) ? 1 : 0;
}
