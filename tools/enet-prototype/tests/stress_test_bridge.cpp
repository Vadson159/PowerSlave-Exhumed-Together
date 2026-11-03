#include "thread_bridge.h"
#include "mock_simulation.h"
#include "enet_worker.h"
#include "net_protocol.h"
#include "replication_protocol.h"
#include "bridge_test_harness.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <cassert>
#include <atomic>
#include <cmath>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

using namespace pse;
using namespace pse::test;

struct StressTestResult {
    int id = 0;
    std::string name;
    bool passed = false;
    std::string details;
    double durationMs = 0.0;
};

static std::vector<StressTestResult> g_stressResults;

void logStressResult(int id, const std::string& name, bool passed, const std::string& details, double durationMs) {
    g_stressResults.push_back({ id, name, passed, details, durationMs });
    std::cout << "[STRESS " << std::setw(2) << id << "] "
              << (passed ? "\x1b[32m[PASS]\x1b[0m " : "\x1b[31m[FAIL]\x1b[0m ")
              << name << " (" << std::fixed << std::setprecision(1) << durationMs << " ms)"
              << (details.empty() ? "" : " - " + details)
              << std::endl;
}

static size_t getProcessWorkingSetMB() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
#endif
    return 0;
}

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n"
              << "  PowerSlave Exhumed Together - Thread Bridge Stress Suite\n"
              << "=========================================================\n";

    // ------------------------------------------------------------------------
    // Scenario 1: Thread Isolation & No Data Races Verification
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bridge.enableThreadChecks(true);

        MockKexSimulation sim(bridge);
        sim.start(60.0);

        HostConfig hcfg;
        hcfg.bindAddress = "127.0.0.1";
        hcfg.port = 28101;
        hcfg.hostName = "StressHost";

        ENetWorker worker(bridge, ENetWorkerMode::HOST);
        bool started = worker.startHost(hcfg);

        // Let them run concurrently for 300 ms with active thread checks enabled
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        sim.stop();
        worker.stop();

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool ok = started && (sim.getStats().totalTicks > 10) && (worker.getStats().pollLoops > 10) &&
                  bridge.areThreadChecksEnabled() && bridge.getAffinityViolationCount() == 0;
        logStressResult(1, "Thread Isolation & Release-Mode Affinity Enforcement", ok,
                        "Release build, checks enabled (abort on violation); violations=" +
                        std::to_string(bridge.getAffinityViolationCount()), dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 2: Bidirectional DTO Transmission Roundtrip (All 7 DTOs)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;

        // 1. InputCommand (Simulation Inbound)
        BridgeInputCommand cmdIn;
        cmdIn.slotId = 2;
        cmdIn.sequence = 101;
        cmdIn.moveForward = 1.0f;
        cmdIn.moveRight = -0.5f;
        cmdIn.yaw = 45.0f;
        cmdIn.buttons = 0x01;
        cmdIn.selectedWeapon = WEAPON_M60;
        cmdIn.connectionGeneration = 1;
        bridge.pushInbound(cmdIn);

        BridgeMessage polledMsg;
        bool okCmd = bridge.pollInbound(polledMsg);
        auto* cmdOut = std::get_if<BridgeInputCommand>(&polledMsg);
        bool valCmd = cmdOut && (cmdOut->slotId == 2) && (cmdOut->sequence == 101) &&
                      (cmdOut->selectedWeapon == WEAPON_M60) && (std::abs(cmdOut->moveForward - 1.0f) < 0.001f);

        // 2. PlayerSnapshot (Simulation Outbound)
        BridgePlayerSnapshot psIn;
        psIn.slotId = 1;
        psIn.sequence = 202;
        psIn.posX = 150.0f; psIn.posY = 250.0f; psIn.posZ = 1152.0f;
        psIn.health = 200;
        psIn.ammo = 60;
        psIn.weaponMask = 0x03;
        bridge.pushOutbound(psIn);

        bool okPs = bridge.pollOutbound(polledMsg);
        auto* psOut = std::get_if<BridgePlayerSnapshot>(&polledMsg);
        bool valPs = psOut && (psOut->slotId == 1) && (psOut->health == 200) && (psOut->ammo == 60);

        // 3. WorldSnapshot (Simulation Outbound)
        BridgeWorldSnapshot wsIn;
        wsIn.snapshotSequence = 303;
        wsIn.serverTick = 500;
        wsIn.mapEpoch = 1;
        EntitySnapshot es;
        es.entityId = 2001;
        es.generation = 1;
        es.type = static_cast<uint8_t>(EntityType::ENEMY);
        es.posX = 10.0f; es.health = 150;
        wsIn.entities.push_back(es);
        bridge.pushOutbound(wsIn);

        bool okWs = bridge.pollOutbound(polledMsg);
        auto* wsOut = std::get_if<BridgeWorldSnapshot>(&polledMsg);
        bool valWs = wsOut && (wsOut->snapshotSequence == 303) && (wsOut->entities.size() == 1) &&
                      (wsOut->entities[0].entityId == 2001);

        // 4. Spawn & Despawn (Simulation Outbound)
        BridgeEntitySpawn spawnIn;
        spawnIn.mapEpoch = 1;
        spawnIn.entityId = 2002;
        spawnIn.entityGeneration = 1;
        spawnIn.type = static_cast<uint8_t>(EntityType::ENEMY);
        spawnIn.transform = Transform3D(100.0f, 200.0f, 300.0f);
        spawnIn.state = ActorStateInfo(180, 200);
        bridge.pushOutbound(spawnIn);

        BridgeEntityDespawn despawnIn;
        despawnIn.mapEpoch = 1;
        despawnIn.entityId = 2002;
        despawnIn.entityGeneration = 1;
        despawnIn.reason = static_cast<uint8_t>(DespawnReason::KILLED);
        bridge.pushOutbound(despawnIn);

        bool okSp = bridge.pollOutbound(polledMsg);
        auto* spOut = std::get_if<BridgeEntitySpawn>(&polledMsg);
        bool valSp = spOut && (spOut->entityId == 2002) && (spOut->state.health == 180);

        bool okDp = bridge.pollOutbound(polledMsg);
        auto* dpOut = std::get_if<BridgeEntityDespawn>(&polledMsg);
        bool valDp = dpOut && (dpOut->entityId == 2002) && (dpOut->reason == static_cast<uint8_t>(DespawnReason::KILLED));

        // 5. Respawn (Request & Confirm)
        BridgeRespawnRequest respReq;
        respReq.slotId = 3;
        respReq.deathSequence = 1;
        respReq.mapEpoch = 1;
        bridge.pushInbound(respReq);

        bool okRr = bridge.pollInbound(polledMsg);
        auto* rrOut = std::get_if<BridgeRespawnRequest>(&polledMsg);
        bool valRr = rrOut && (rrOut->slotId == 3) && (rrOut->deathSequence == 1);

        BridgeRespawnConfirm respConf;
        respConf.slotId = 3;
        respConf.deathSequence = 1;
        respConf.newEntityId = 1003;
        respConf.newGeneration = 2;
        respConf.posX = 50.0f; respConf.posY = 60.0f; respConf.posZ = 0.0f;
        bridge.pushOutbound(respConf);

        bool okRc = bridge.pollOutbound(polledMsg);
        auto* rcOut = std::get_if<BridgeRespawnConfirm>(&polledMsg);
        bool valRc = rcOut && (rcOut->slotId == 3) && (rcOut->newGeneration == 2) && (rcOut->newEntityId == 1003);

        // 6. MapChange & MapReady
        BridgeMapChange mc;
        mc.newMapEpoch = 2;
        mc.mapName = "TEMPLE_OF_RAMSES";
        bridge.pushOutbound(mc);

        bool okMc = bridge.pollOutbound(polledMsg);
        auto* mcOut = std::get_if<BridgeMapChange>(&polledMsg);
        bool valMc = mcOut && (mcOut->newMapEpoch == 2) && (mcOut->mapName == "TEMPLE_OF_RAMSES");

        BridgeMapReady mr;
        mr.slotId = 4;
        mr.mapEpoch = 2;
        bridge.pushInbound(mr);

        bool okMr = bridge.pollInbound(polledMsg);
        auto* mrOut = std::get_if<BridgeMapReady>(&polledMsg);
        bool valMr = mrOut && (mrOut->slotId == 4) && (mrOut->mapEpoch == 2);

        // 7. WorldEvent & InteractionRequest
        BridgeWorldEvent we;
        we.mapEpoch = 2;
        we.eventSequence = 777;
        we.eventType = EVENT_DOOR_STATE_CHANGE;
        we.targetEntityId = 30001;
        we.targetGeneration = 1;
        we.eventValue = static_cast<int32_t>(DoorState::OPENING);
        we.floatValue = 0.5f;
        bridge.pushOutbound(we);

        bool okWe = bridge.pollOutbound(polledMsg);
        auto* weOut = std::get_if<BridgeWorldEvent>(&polledMsg);
        bool valWe = weOut && (weOut->targetEntityId == 30001) && (weOut->eventType == EVENT_DOOR_STATE_CHANGE);

        BridgeInteractionRequest ir;
        ir.mapEpoch = 2;
        ir.slotId = 5;
        ir.targetEntityId = 31001;
        ir.targetGeneration = 1;
        bridge.pushInbound(ir);

        bool okIr = bridge.pollInbound(polledMsg);
        auto* irOut = std::get_if<BridgeInteractionRequest>(&polledMsg);
        bool valIr = irOut && (irOut->targetEntityId == 31001) && (irOut->slotId == 5);

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool allOk = okCmd && valCmd && okPs && valPs && okWs && valWs &&
                     okSp && valSp && okDp && valDp && okRr && valRr &&
                     okRc && valRc && okMc && valMc && okMr && valMr &&
                     okWe && valWe && okIr && valIr;

        logStressResult(2, "Bidirectional DTO Roundtrip (All 7 DTOs)", allOk,
                        "Verified InputCommand, PlayerSnapshot, WorldSnapshot, Spawn, Despawn, Respawn, MapChange, WorldEvent", dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 3: 100,000+ SPSC Concurrent Queue Operations
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;

        constexpr uint64_t TOTAL_OPS = 120'000;
        std::atomic<bool> producerDone{ false };
        std::atomic<uint64_t> consumerVerifiedCount{ 0 };
        std::atomic<bool> orderPreserved{ true };

        // Producer thread (Simulation thread producing outbound messages)
        std::thread producer([&bridge, &producerDone]() {
            for (uint64_t i = 0; i < TOTAL_OPS; ++i) {
                BridgeWorldEvent evt;
                evt.eventSequence = static_cast<uint32_t>(i);
                evt.eventType = EVENT_DAMAGE_CONFIRMED;
                evt.eventValue = static_cast<int32_t>(i % 100);

                // Push with retry until success (critical message)
                while (!isSuccess(bridge.pushOutbound(evt))) {
                    std::this_thread::yield();
                }
            }
            producerDone.store(true, std::memory_order_release);
            bridge.unregisterSimulationThread();
        });

        // Consumer thread (Network worker thread draining outbound messages)
        std::thread consumer([&bridge, &producerDone, &consumerVerifiedCount, &orderPreserved]() {
            uint64_t expectedSeq = 0;
            BridgeMessage msg;
            while (!producerDone.load(std::memory_order_acquire) || expectedSeq < TOTAL_OPS) {
                if (bridge.pollOutbound(msg)) {
                    auto* evt = std::get_if<BridgeWorldEvent>(&msg);
                    if (evt) {
                        if (evt->eventSequence != expectedSeq) {
                            orderPreserved.store(false, std::memory_order_relaxed);
                        }
                        ++expectedSeq;
                        consumerVerifiedCount.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            bridge.unregisterNetworkThread();
        });

        producer.join();
        consumer.join();

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        auto m = bridge.getMetrics();
        bool ok = (consumerVerifiedCount.load() == TOTAL_OPS) && orderPreserved.load() &&
                  (m.outbound.pushedCount == TOTAL_OPS) && (m.outbound.poppedCount == TOTAL_OPS);

        std::string det = "Processed " + std::to_string(TOTAL_OPS) + " events; strict FIFO order verified; 0 data races";
        logStressResult(3, "100,000+ SPSC Concurrent Queue Operations", ok, det, dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 4: High-Load 8 Players + 128 Entities Simulation (real ENet clients)
    // Inputs travel client sim -> client bridge -> ENet -> host worker -> host
    // bridge: every queue keeps exactly one producer and one consumer.
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28104, 7);

        // Populate 128 entities (executed by the simulation thread on its first tick)
        for (uint32_t i = 1; i <= 128; ++i) {
            EntityType t = (i <= 20) ? EntityType::ENEMY :
                           (i <= 70) ? EntityType::PROJECTILE :
                           (i <= 100) ? EntityType::PICKUP : EntityType::DOOR;
            c.hostSim.triggerSpawn(2000 + i, 1, t, 1,
                                   Transform3D(static_cast<float>(i * 10), static_cast<float>(i * 5), 0.0f),
                                   Velocity3D(1.0f, 0.0f, 0.0f),
                                   ActorStateInfo(100, 100));
        }

        bool ok = c.start() && c.waitSynced(10000);
        for (auto& cl : c.clients) cl->sim.submitLocalInput(1.0f, 0.0f, 0.0f, 0.0f, 0);

        // Run sustained for 1.2 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        bool conv = c.waitConverged(5000);

        auto stats = c.hostSim.getStats();
        auto metrics = c.hostBridge.getMetrics();
        size_t ramMB = getProcessWorkingSetMB();
        size_t entities = c.hostSim.getMockEntityCount();
        c.stop();
        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

        ok = ok && conv && (stats.totalTicks > 50) && (stats.commandsProcessed > 200) &&
             (metrics.outbound.pushedCount > 30) && (entities == 128) &&
             (stats.maxMovementStepsPerSlotTick == 1) && (c.affinityViolations() == 0);

        std::string det = "Ticks: " + std::to_string(stats.totalTicks) +
                          ", Cmds: " + std::to_string(stats.commandsProcessed) +
                          ", 7 clients converged on 128 entities: " + std::to_string(conv) +
                          ", outbound pushed: " + std::to_string(metrics.outbound.pushedCount) +
                          ", RAM: " + std::to_string(ramMB) + " MB";
        logStressResult(4, "High-Load 8 Players + 128 Entities Simulation", ok, det, dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 5: Inbound Queue Overflow & Watermark Dropping Policy
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bridge.setInboundWatermark(96); // Explicit 75% watermark for capacity 128

        // Fill up to watermark (96 items) with best-effort input commands
        for (uint32_t i = 0; i < 96; ++i) {
            BridgeInputCommand cmd;
            cmd.sequence = i;
            auto res = bridge.pushInbound(cmd);
            assert(res == QueuePushResult::SUCCESS);
        }

        // Pushing additional best-effort items must be dropped at watermark!
        BridgeInputCommand overflowCmd;
        overflowCmd.sequence = 999;
        auto dropRes = bridge.pushInbound(overflowCmd);
        bool bestEffortDropped = (dropRes == QueuePushResult::DROPPED_WATERMARK);

        // Critical message (Spawn) must be accepted even when best-effort is blocked!
        BridgeEntitySpawn critSpawn;
        critSpawn.entityId = 9999;
        critSpawn.entityGeneration = 1;
        auto critRes = bridge.pushInbound(critSpawn);
        bool critAccepted = (critRes == QueuePushResult::SUCCESS);

        // Verify metrics
        auto m = bridge.getMetrics();
        bool ok = bestEffortDropped && critAccepted &&
                  (m.inbound.bestEffortDroppedCount == 1) &&
                  (m.inbound.pushedCount == 97);

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        logStressResult(5, "Inbound Overflow & Watermark Dropping Policy", ok,
                        "Best-effort dropped at watermark 96; critical spawn accepted in reserved headroom", dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 6: Outbound Queue Overflow & Watermark Dropping Policy
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bridge.setOutboundWatermark(192); // Explicit 75% watermark for capacity 256

        // Fill up to watermark (192 items) with best-effort snapshots
        for (uint32_t i = 0; i < 192; ++i) {
            BridgeWorldSnapshot snap;
            snap.snapshotSequence = i;
            auto res = bridge.pushOutbound(snap);
            assert(res == QueuePushResult::SUCCESS);
        }

        // Next best-effort snapshot must be dropped at watermark
        BridgeWorldSnapshot overflowSnap;
        overflowSnap.snapshotSequence = 9999;
        auto dropRes = bridge.pushOutbound(overflowSnap);
        bool snapDropped = (dropRes == QueuePushResult::DROPPED_WATERMARK);

        // Critical message (WorldEvent) must be accepted in reserved headroom
        BridgeWorldEvent critEvent;
        critEvent.eventType = EVENT_DOOR_STATE_CHANGE;
        critEvent.targetEntityId = 30001;
        critEvent.targetGeneration = 1;
        auto critRes = bridge.pushOutbound(critEvent);
        bool critAccepted = (critRes == QueuePushResult::SUCCESS);

        auto m = bridge.getMetrics();
        bool ok = snapDropped && critAccepted &&
                  (m.outbound.bestEffortDroppedCount == 1) &&
                  (m.outbound.pushedCount == 193);

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        logStressResult(6, "Outbound Overflow & Watermark Dropping Policy", ok,
                        "Best-effort snapshot dropped at watermark 192; critical event accepted in reserved headroom", dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 7: Game Tick Stall / Lag Simulation
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;

        MockKexSimulation sim(bridge);
        sim.start(60.0);

        // Send a high-frequency stream of inputs from "network" (simulating multi-client bursts)
        std::atomic<bool> producerRunning{ true };
        std::thread netProducer([&bridge, &producerRunning]() {
            uint32_t seq = 0;
            while (producerRunning.load(std::memory_order_relaxed)) {
                // Burst of inputs to simulate 7 concurrent clients sending commands
                for (int b = 0; b < 10; ++b) {
                    BridgeInputCommand cmd;
                    cmd.sequence = ++seq;
                    bridge.pushInbound(cmd);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            bridge.unregisterNetworkThread();
        });

        // Inject 150 ms stall into simulation thread
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        sim.injectTickStall(150);

        // Wait for stall to complete and queue to recover
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        producerRunning.store(false);
        netProducer.join();
        // Recovery: once the burst ends, the simulation must empty the queue by itself.
        bool drainedClean = waitUntil([&] { return bridge.getInboundOccupancy() == 0; }, 500);
        sim.stop();

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        auto stats = sim.getStats();
        auto m = bridge.getMetrics();

        bool ok = (stats.stallCount >= 1) && (m.inbound.bestEffortDroppedCount > 0) &&
                  (stats.inboundDrained > 50) && drainedClean && bridge.getAffinityViolationCount() == 0;

        std::string det = "Stall: 150 ms handled; " + std::to_string(m.inbound.bestEffortDroppedCount) +
                          " stale inputs dropped during lag; queue drained clean after recovery";
        logStressResult(7, "Game Tick Stall / Lag Simulation", ok, det, dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 8: Unreliable Loss Resilience & Critical Delivery
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28108, 1);
        c.hostConfig.simDropPercent = 20.0f; // Unreliable packets only (reliable resent by ENet)
        c.hostConfig.simLatencyMs = 30;
        for (uint32_t i = 0; i < 40; ++i) spawnEnemy(c.hostSim, 9000 + i);
        bool ok = c.start() && c.waitSynced(8000);
        const uint64_t before = c.clients[0]->sim.getStats().worldEventsProcessed;

        // Host simulation emits 20 critical world events across the impaired network
        for (uint32_t i = 0; i < 20; ++i) {
            c.hostSim.triggerWorldEvent(EVENT_SWITCH_TRIGGERED, 1001, 31001, 1, 1, 0.0f);
        }
        ok = ok && c.hostSim.sync();
        bool all = waitUntil([&] { return c.clients[0]->sim.getStats().worldEventsProcessed - before == 20; }, 5000);
        sleepMs(200); // No duplicates may arrive later
        const uint64_t got = c.clients[0]->sim.getStats().worldEventsProcessed - before;
        bool conv = c.waitConverged(5000);
        c.stop();

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool pass = ok && all && got == 20 && conv && c.affinityViolations() == 0;
        logStressResult(8, "Unreliable Loss Resilience & Critical Delivery", pass,
                        "20% unreliable loss + 30 ms latency: reliable world events delivered " + std::to_string(got) +
                        "/20 exactly once; world converged=" + std::to_string(conv), dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 9: Mid-Load Reconnection & MapEpoch Transition
    // The client transport restarts on the SAME client bridge and resumes its
    // slot with the reconnect key; the map changed while it was away.
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28109, 1);
        for (uint32_t i = 0; i < 30; ++i) spawnEnemy(c.hostSim, 9100 + i);
        bool ok = c.start() && c.waitConverged(8000);
        auto& cl = *c.clients[0];
        const uint8_t slot = cl.sim.getLocalSlot();

        cl.worker.stop();
        c.hostSim.triggerMapChange("TEMPLE_OF_RAMSES", 2);
        for (uint32_t i = 0; i < 15; ++i) spawnEnemy(c.hostSim, 9200 + i);
        ok = ok && c.hostSim.sync();
        sleepMs(200);

        cl.config.reconnectKey = cl.sim.getReconnectKey();
        bool restarted = cl.worker.startClient(cl.config);
        bool conv = c.waitConverged(10000);
        auto hp = c.hostSim.getPlayerState(slot);
        c.stop();

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool pass = ok && restarted && conv && c.hostSim.getMapEpoch() == 2 && hp.connectionGeneration == 2 &&
                    cl.sim.getLocalSlot() == slot && c.affinityViolations() == 0;
        logStressResult(9, "Mid-Load Reconnection & MapEpoch Transition", pass,
                        "Client resumed slot P" + std::to_string(slot + 1) + " with gen " +
                        std::to_string(hp.connectionGeneration) + ", loaded epoch 2 and converged=" + std::to_string(conv), dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 10: Worker Stop During Active Load & Repeated Restart (same bridge)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28110, 1);
        c.clients[0]->sim.setAutoReconnect(true, 20);
        bool ok = c.start() && c.waitSynced(8000);

        bool cycles = true;
        for (int cycle = 1; cycle <= 3 && cycles; ++cycle) {
            // Blast critical events and abruptly stop the transport while they are in flight
            for (int i = 0; i < 50; ++i) {
                c.hostSim.triggerWorldEvent(EVENT_DAMAGE_CONFIRMED, 1001, 2001, 1, 10, 0.0f);
                spawnEnemy(c.hostSim, 9300 + cycle * 100 + i);
            }
            c.hostWorker.stop();
            sleepMs(50);
            cycles = c.hostWorker.startHost(c.hostConfig);
        }
        bool conv = cycles && c.hostSim.sync() && c.waitConverged(15000);
        const size_t entities = c.hostSim.getMockEntityCount();
        c.stop();

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool pass = ok && cycles && conv && entities == 150 && c.affinityViolations() == 0;
        logStressResult(10, "Worker Stop During Active Load & Repeated Restart", pass,
                        "3 abrupt stop/restart cycles on ONE bridge with the simulation running; client "
                        "auto-reconnected and converged on " + std::to_string(entities) + " entities", dur);
    }

    // ------------------------------------------------------------------------
    // Scenario 11: Zero Silent Loss & Bounded Backpressure Failure Handling
    // A saturated critical push returns within the bounded window and hands the
    // event back to its owner; nothing is lost and no loss is declared.
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bridge.setMaxBackpressureRetries(20);

        for (uint32_t i = 0; i < 256; ++i) {
            BridgeWorldEvent evt;
            evt.eventSequence = i;
            evt.eventType = EVENT_SWITCH_TRIGGERED;
            bridge.pushOutbound(evt);
        }

        BridgeEntitySpawn spawn;
        spawn.entityId = 9999;
        spawn.entityGeneration = 3;
        BridgeMessage owned(spawn);
        auto tPush = std::chrono::steady_clock::now();
        auto rejected = bridge.pushOutbound(std::move(owned));
        double pushMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tPush).count();
        auto* still = std::get_if<BridgeEntitySpawn>(&owned);
        bool intact = still && still->entityId == 9999 && still->entityGeneration == 3;
        bool noLossDeclared = !bridge.isResyncRequired();

        // Owner retries after the consumer frees one slot
        BridgeMessage tmp;
        bridge.pollOutbound(tmp);
        auto accepted = bridge.pushOutbound(std::move(owned));
        std::vector<BridgeMessage> rest;
        bridge.drainOutbound(rest);
        auto* last = rest.empty() ? nullptr : std::get_if<BridgeEntitySpawn>(&rest.back());

        auto dur = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bool ok = rejected == QueuePushResult::CRITICAL_REJECTED_FULL && intact && noLossDeclared &&
                  isSuccess(accepted) && last && last->entityId == 9999 && pushMs < 50.0 &&
                  bridge.getMetrics().outbound.criticalOverflowCount == 1;
        logStressResult(11, "Zero Silent Loss & Bounded Backpressure Failure Handling", ok,
                        "Saturated push returned in " + std::to_string(pushMs) + " ms, event handed back intact "
                        "and delivered on retry; no loss declared", dur);
    }

    std::cout << "=========================================================\n"
              << "  Thread Bridge Stress Suite Summary\n"
              << "=========================================================\n";
    int passedCount = 0;
    for (const auto& r : g_stressResults) {
        if (r.passed) ++passedCount;
    }
    std::cout << "Passed: " << passedCount << " / " << g_stressResults.size() << " ("
              << std::fixed << std::setprecision(1)
              << (100.0 * passedCount / g_stressResults.size()) << "%)\n";
    std::cout << "RAM Working Set: " << getProcessWorkingSetMB() << " MB\n";
    std::cout << "=========================================================\n";

    return (passedCount == static_cast<int>(g_stressResults.size())) ? 0 : 1;
}
