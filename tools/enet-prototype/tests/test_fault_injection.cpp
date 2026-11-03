// Thread Bridge adversarial fault-injection suite.
// Scenarios 1..16 from DOC-39, with the expectations corrected by DOC-38:
// a bridge push rejection is not a loss (the owner keeps the event), the
// guarantees are checked at the owner (simulation outbox, worker inbound
// pending, input mailbox) and the test bench never runs two producers on one
// SPSC queue (thread-affinity enforcement is active and aborts on violation).

#include "bridge_test_harness.h"
#include "net_protocol.h"
#include "replication_protocol.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace pse;
using namespace pse::test;

enum class TestVerdict {
    PASS,
    FAIL,
    NOT_TESTED
};

struct FaultTestResult {
    int id = 0;
    std::string category;
    std::string name;
    TestVerdict verdict = TestVerdict::NOT_TESTED;
    std::string expectedBehavior;
    std::string actualBehavior;
    std::string details;
    double durationMs = 0.0;
};

static std::vector<FaultTestResult> g_results;

void logFaultResult(int id,
                    const std::string& category,
                    const std::string& name,
                    TestVerdict verdict,
                    const std::string& expected,
                    const std::string& actual,
                    const std::string& details,
                    double durationMs) {
    g_results.push_back({ id, category, name, verdict, expected, actual, details, durationMs });

    const char* tag = "\x1b[33m[NOT TESTED]\x1b[0m";
    if (verdict == TestVerdict::PASS) tag = "\x1b[32m[PASS]\x1b[0m";
    else if (verdict == TestVerdict::FAIL) tag = "\x1b[31m[FAIL]\x1b[0m";

    std::cout << "[FAULT " << std::setw(2) << id << "] "
              << tag << " [" << category << "] " << name
              << " (" << std::fixed << std::setprecision(1) << durationMs << " ms)\n"
              << "    Expected: " << expected << "\n"
              << "    Actual:   " << actual << "\n"
              << "    Details:  " << details << "\n"
              << std::endl;
}

static double msSince(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static TestVerdict verdict(bool pass) { return pass ? TestVerdict::PASS : TestVerdict::FAIL; }

// Fills an outbound queue (caller owns the SIMULATION role) with 256 world events.
static void fillOutbound(ThreadBridge& bridge) {
    for (uint32_t i = 0; i < THREAD_BRIDGE_OUTBOUND_CAPACITY; ++i) {
        BridgeWorldEvent evt;
        evt.eventSequence = i;
        evt.eventType = EVENT_SWITCH_TRIGGERED;
        bridge.pushOutbound(evt);
    }
}

// Child process for the Release death test (FAULT 16b): default handler must abort.
static int runAffinityDeathChild() {
#if defined(_WIN32)
    // Suppress the crash dialog of the child only; the abort itself is what is tested.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    ThreadBridge bridge;
    bridge.registerSimulationThread();
    std::thread rogue([&] {
        BridgeEntitySpawn spawn;
        spawn.entityId = 1;
        bridge.pushOutbound(spawn); // Must never return
        std::cout << "CHILD: illegal call returned" << std::endl;
    });
    rogue.join();
    return 0; // Reaching this means the violation was not fatal
}

static int runSelfAsChild(const char* arg) {
#if defined(_WIN32)
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" ";
    for (const char* p = arg; *p; ++p) cmd += static_cast<wchar_t>(*p);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    (void)arg;
    return -1;
#endif
}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--affinity-death-child") {
        return runAffinityDeathChild();
    }

    std::cout << "====================================================================\n"
              << "  PowerSlave Exhumed Together — Thread Bridge Adversarial Fault Suite\n"
              << "  (criteria corrected per DOC-38; P0 remediation build)\n"
              << "====================================================================\n\n";

    // ========================================================================
    // SECTION 1: CRITICAL EVENT OVERFLOW
    // ========================================================================

    // ------------------------------------------------------------------------
    // Test 1: Saturated outbound: rejection hands every critical DTO back intact
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bridge.setMaxBackpressureRetries(5);
        std::atomic<uint32_t> callbackCount{ 0 };
        bridge.setResyncCallback([&](const std::string&) { callbackCount.fetch_add(1); });
        fillOutbound(bridge);

        std::vector<BridgeMessage> owned;
        { BridgeEntitySpawn m; m.entityId = 9001; owned.emplace_back(m); }
        { BridgeEntityDespawn m; m.entityId = 9001; owned.emplace_back(m); }
        { BridgeRespawnConfirm m; m.slotId = 2; m.newEntityId = 1002; owned.emplace_back(m); }
        { BridgeMapChange m; m.newMapEpoch = 2; m.mapName = "TEMPLE_OF_RAMSES"; owned.emplace_back(m); }
        { BridgeWorldEvent m; m.eventSequence = 4242; m.eventType = EVENT_DOOR_STATE_CHANGE; owned.emplace_back(m); }
        { BridgeSessionEvent m; m.type = BridgeSessionEvent::Type::CLIENT_LEFT; m.slotId = 3; owned.emplace_back(m); }

        bool allRejected = true;
        for (auto& m : owned) {
            if (bridge.pushOutbound(std::move(m)) != QueuePushResult::CRITICAL_REJECTED_FULL) allRejected = false;
        }
        bool intact =
            std::get<BridgeEntitySpawn>(owned[0]).entityId == 9001 &&
            std::get<BridgeEntityDespawn>(owned[1]).entityId == 9001 &&
            std::get<BridgeRespawnConfirm>(owned[2]).newEntityId == 1002 &&
            std::get<BridgeMapChange>(owned[3]).mapName == "TEMPLE_OF_RAMSES" &&
            std::get<BridgeWorldEvent>(owned[4]).eventSequence == 4242 &&
            std::get<BridgeSessionEvent>(owned[5]).slotId == 3;
        bool noLossDeclared = !bridge.isResyncRequired() && callbackCount.load() == 0;

        // The owner retries once the consumer frees space: all six are delivered, in order.
        std::vector<BridgeMessage> head;
        bridge.drainOutbound(head, 6);
        bool retried = true;
        for (auto& m : owned) retried = retried && isSuccess(bridge.pushOutbound(std::move(m)));
        std::vector<BridgeMessage> rest;
        bridge.drainOutbound(rest);
        bool tailOrder = rest.size() == 256 &&
            std::holds_alternative<BridgeEntitySpawn>(rest[250]) && std::holds_alternative<BridgeSessionEvent>(rest[255]);

        bool pass = allRejected && intact && noLossDeclared && retried && tailOrder;
        logFaultResult(1, "Critical Overflow", "Rejection Returns Ownership (All 6 Critical DTOs)", verdict(pass),
            "Saturated push returns CRITICAL_REJECTED_FULL, leaves each DTO intact with its owner, declares no loss; retry delivers in order",
            std::string("rejected=") + (allRejected ? "6/6" : "NO") + ", intact=" + (intact ? "yes" : "NO") +
            ", resync/callback=" + (noLossDeclared ? "none" : "FIRED") + ", retried+ordered=" + (retried && tailOrder ? "yes" : "NO"),
            "DOC-38 correction: a rejection is not a loss; loss is declared only by an owner via requestResync()", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 2: Owner retention: the simulation outbox replays rejected events
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        MockKexSimulation sim(bridge);
        sim.start(60.0);
        for (uint32_t i = 0; i < 256; ++i) sim.triggerWorldEvent(EVENT_SWITCH_TRIGGERED, 1, 2, 1, static_cast<int32_t>(i), 0.0f);
        for (uint32_t i = 0; i < 44; ++i) spawnEnemy(sim, 5000 + i);
        sim.sync();
        sleepMs(100);
        const uint64_t retained = sim.getStats().outboxDepth;

        // This thread is the only consumer (NETWORK role); the sim thread the only producer.
        std::vector<uint32_t> events, spawns;
        waitUntil([&] {
            std::vector<BridgeMessage> batch;
            bridge.drainOutbound(batch);
            for (auto& m : batch) {
                if (auto* e = std::get_if<BridgeWorldEvent>(&m)) events.push_back(e->eventSequence);
                if (auto* s = std::get_if<BridgeEntitySpawn>(&m)) spawns.push_back(s->entityId);
            }
            return events.size() + spawns.size() >= 300;
        }, 3000);
        sim.stop();

        bool ordered = events.size() == 256 && spawns.size() == 44;
        for (size_t i = 0; ordered && i < events.size(); ++i) ordered = (events[i] == i + 1);
        for (size_t i = 0; ordered && i < spawns.size(); ++i) ordered = (spawns[i] == 5000 + i);
        bool pass = retained >= 44 && ordered && sim.getStats().outboxDepth == 0;
        logFaultResult(2, "Critical Overflow", "Rejected Events Retained by Owner and Replayed", verdict(pass),
            "Events the bridge cannot accept stay in the simulation outbox and are delivered once, in order, when space frees",
            "outbox retained " + std::to_string(retained) + " events; delivered " + std::to_string(events.size()) +
            " events + " + std::to_string(spawns.size()) + " spawns in order=" + (ordered ? "yes" : "NO"),
            "DOC-38 correction: retention belongs to the event owner (P0-3), not inside SpscQueue", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 3: MapChange during transport saturation (host stays up: DOC-38 TB-2)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28201, 1, 20);
        bool ok = c.start() && c.waitSynced(8000);

        c.hostWorker.setOutboundDrainPaused(true); // Saturate without destroying the ENet host
        for (uint32_t i = 0; i < 300; ++i) spawnEnemy(c.hostSim, 6000 + i);
        c.hostSim.triggerMapChange("TEMPLE_OF_RAMSES", 2);
        for (uint32_t i = 0; i < 30; ++i) spawnEnemy(c.hostSim, 6500 + i);
        ok = ok && c.hostSim.sync();
        sleepMs(150);
        const uint64_t retained = c.hostSim.getStats().outboxDepth;
        c.hostWorker.setOutboundDrainPaused(false);

        bool conv = c.waitConverged(10000);
        auto cs = c.clients[0]->sim.getStats();
        std::string clientMap = c.clients[0]->sim.getMapName();
        uint32_t clientEpoch = c.clients[0]->sim.getMapEpoch();
        size_t clientEntities = c.clients[0]->sim.getMockEntityCount();
        c.stop();

        bool pass = ok && retained > 0 && conv && clientEpoch == 2 && clientMap == "TEMPLE_OF_RAMSES" &&
                    clientEntities == 30 && cs.mapLoads == 2 && cs.epochAnomalies == 0;
        logFaultResult(3, "Critical Overflow", "MapChange Under Saturation Reaches the Client", verdict(pass),
            "MapChange retained while the transport is saturated; the client loads the new level, confirms it and holds exactly the new level's entities",
            "outbox retained " + std::to_string(retained) + "; client map=" + clientMap + " epoch=" + std::to_string(clientEpoch) +
            " entities=" + std::to_string(clientEntities) + " level loads=" + std::to_string(cs.mapLoads),
            "Bench corrected: transport paused via setOutboundDrainPaused instead of destroying the ENet host", msSince(t0));
    }

    // ========================================================================
    // SECTION 2: STALE INPUT UNDER SATURATION
    // ========================================================================

    // ------------------------------------------------------------------------
    // Test 4: Release of W while the host inbound queue sits at the watermark
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28202, 1);
        c.hostBridge.setInboundWatermark(8); // Fault injection: reach the watermark with one client
        bool ok = c.start() && c.waitSynced(8000);
        auto& cl = *c.clients[0];
        const uint8_t slot = cl.sim.getLocalSlot();
        cl.sim.submitLocalInput(1.0f, 0.0f, 0.0f, 0.0f, 0);
        ok = ok && waitUntil([&] { return c.hostSim.getPlayerTransform(slot).y > 1.0f; }, 3000);

        const uint64_t shotsBefore = c.hostSim.getPlayerState(slot).shotsFired;
        c.hostSim.injectTickStall(700);
        sleepMs(300);
        cl.sim.submitLocalInput(0.0f, 0.0f, 0.0f, 0.0f, 0); // Release while the host is stalled
        sleepMs(100);
        cl.sim.submitButtonTap(BUTTON_ATTACK);               // One-shot press+release inside the stall
        sleepMs(600);                                        // Stall over
        const float ya = c.hostSim.getPlayerTransform(slot).y;
        sleepMs(300);
        const float yb = c.hostSim.getPlayerTransform(slot).y;
        const uint64_t shots = c.hostSim.getPlayerState(slot).shotsFired - shotsBefore;
        auto ws = c.hostWorker.getStats();
        auto bm = c.hostBridge.getMetrics();
        c.stop();

        bool released = std::fabs(yb - ya) < 1e-4f;
        bool pass = ok && released && shots == 1 && bm.inbound.bestEffortDroppedCount > 0 && ws.inputsCoalesced > 0;
        logFaultResult(4, "Stale Input", "Release and Tap Survive Watermark (Latest-State Mailbox)", verdict(pass),
            "Queue-level drops at the watermark happen, but the worker mailbox keeps the newest state and accumulated presses: "
            "the release is applied right after the stall and the tap fires exactly once",
            "queue drops=" + std::to_string(bm.inbound.bestEffortDroppedCount) + ", wire commands coalesced=" +
            std::to_string(ws.inputsCoalesced) + ", movement 300 ms after stall=" + std::to_string(yb - ya) +
            ", shots from tap=" + std::to_string(shots),
            "P0-5: newest input state is never the one discarded; one-shot presses are counted, not overwritten", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 5: No movement burst after a stall (metric corrected per DOC-38 §4.2)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28203, 1);
        bool ok = c.start() && c.waitSynced(8000);
        auto& cl = *c.clients[0];
        const uint8_t slot = cl.sim.getLocalSlot();
        cl.sim.submitLocalInput(1.0f, 0.0f, 0.0f, 0.0f, 0);
        ok = ok && waitUntil([&] { return c.hostSim.getPlayerTransform(slot).y > 1.0f; }, 3000);

        c.hostSim.injectTickStall(200);
        sleepMs(20);
        const uint32_t tickA = c.hostSim.getCurrentTick();
        const float yA = c.hostSim.getPlayerTransform(slot).y;
        sleepMs(330); // ~12 commands accumulate during the stall, then ~8 ticks run
        const uint32_t tickB = c.hostSim.getCurrentTick();
        const float yB = c.hostSim.getPlayerTransform(slot).y;
        auto hs = c.hostSim.getStats();
        c.stop();

        const float allowed = 0.1f * static_cast<float>(tickB - tickA) + 0.1f + 1e-3f;
        bool pass = ok && hs.stallCount >= 1 && hs.maxMovementStepsPerSlotTick == 1 && (yB - yA) <= allowed;
        std::ostringstream ss;
        ss << "max movement steps per player per tick=" << hs.maxMovementStepsPerSlotTick
           << "; displacement over " << (tickB - tickA) << " ticks spanning the stall=" << (yB - yA)
           << " (bound " << allowed << ")";
        logFaultResult(5, "Stale Input", "No Accumulated-Command Burst After Stall", verdict(pass),
            "At most one movement application per player per simulation tick; displacement bounded by elapsed ticks",
            ss.str(),
            "DOC-38: old criterion (commandsProcessed > 1, y > 1.0) failed any correct implementation; '96 across 3 ticks' was total run ticks", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 6: Stale backlog does not keep moving the player (input timeout)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28204, 1);
        bool ok = c.start() && c.waitSynced(8000);
        auto& cl = *c.clients[0];
        const uint8_t slot = cl.sim.getLocalSlot();
        cl.sim.submitLocalInput(1.0f, 0.0f, 0.0f, 0.0f, 0);
        ok = ok && waitUntil([&] { return c.hostSim.getPlayerTransform(slot).y > 1.0f; }, 3000);

        c.hostSim.injectTickStall(1000);   // ~60 commands of 'W held' arrive meanwhile
        sleepMs(20);
        const float yA = c.hostSim.getPlayerTransform(slot).y;
        sleepMs(400);
        cl.sim.stopLocalInput();           // The input stream stops (no release packet)
        sleepMs(1100);                     // Stall over, host keeps ticking
        const float yB = c.hostSim.getPlayerTransform(slot).y;
        auto hs = c.hostSim.getStats();
        c.stop();

        const float bound = 0.1f * static_cast<float>(MockKexSimulation::INPUT_TIMEOUT_TICKS) + 0.1f + 1e-3f;
        bool pass = ok && (yB - yA) <= bound && hs.inputNeutralTimeouts >= 1;
        logFaultResult(6, "Stale Input", "No Delay-Line Replay of Buffered Movement", verdict(pass),
            "After a stall, a backlog of held commands yields at most INPUT_TIMEOUT_TICKS steps once the stream has stopped",
            "displacement after stall=" + std::to_string(yB - yA) + " (bound " + std::to_string(bound) +
            ", old FIFO replay ~+2.4..6.0), neutral timeouts=" + std::to_string(hs.inputNeutralTimeouts),
            "DOC-38: same root cause as FAULT 05 (no per-tick cap); checked here for the stream-stops case", msSince(t0));
    }

    // ========================================================================
    // SECTION 3: PUSH FAILURE PROPAGATION
    // ========================================================================

    // ------------------------------------------------------------------------
    // Test 7: Spawn while the outbound queue is saturated
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        MockKexSimulation sim(bridge);
        sim.start(60.0);
        for (uint32_t i = 0; i < 256; ++i) sim.triggerWorldEvent(EVENT_SWITCH_TRIGGERED, 1, 2, 1, 0, 0.0f);
        spawnEnemy(sim, 5001);
        sim.sync();
        bool hasLocal = sim.hasEntity(5001);
        sleepMs(300);

        int found = 0;
        waitUntil([&] {
            std::vector<BridgeMessage> batch;
            bridge.drainOutbound(batch);
            for (auto& m : batch) {
                if (auto* s = std::get_if<BridgeEntitySpawn>(&m); s && s->entityId == 5001) ++found;
            }
            return sim.getStats().outboxDepth == 0 && found > 0;
        }, 2000);
        sleepMs(50);
        { std::vector<BridgeMessage> batch; bridge.drainOutbound(batch);
          for (auto& m : batch) if (auto* s = std::get_if<BridgeEntitySpawn>(&m); s && s->entityId == 5001) ++found; }
        sim.stop();

        bool pass = hasLocal && found == 1;
        logFaultResult(7, "Push Failure Propagation", "Spawn Under Saturation Is Published Exactly Once", verdict(pass),
            "Local spawn and its publication are one outbox operation; the spawn reaches the transport exactly once",
            "local entity=" + std::string(hasLocal ? "yes" : "no") + ", spawn delivered " + std::to_string(found) + " time(s)",
            "Bench corrected: hooks run on the simulation thread (mailbox); this thread is the only consumer", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 8: Despawn under saturation + baseline as full replacement (ghost purge)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        bool hostPart = false;
        {
            ThreadBridge bridge;
            MockKexSimulation sim(bridge);
            sim.start(60.0);
            spawnEnemy(sim, 5002);
            sim.sync();
            for (uint32_t i = 0; i < 256; ++i) sim.triggerWorldEvent(EVENT_SWITCH_TRIGGERED, 1, 2, 1, 0, 0.0f);
            sim.triggerDespawn(5002, 1, DespawnReason::KILLED);
            sim.sync();
            int despawns = 0;
            waitUntil([&] {
                std::vector<BridgeMessage> batch;
                bridge.drainOutbound(batch);
                for (auto& m : batch) if (auto* d = std::get_if<BridgeEntityDespawn>(&m); d && d->entityId == 5002) ++despawns;
                return sim.getStats().outboxDepth == 0 && despawns > 0;
            }, 2000);
            hostPart = !sim.hasEntity(5002) && despawns == 1;
            sim.stop();
        }

        // Client side: this thread plays the client network worker (sole inbound
        // producer / outbound consumer of the client bridge).
        bool clientPart = false;
        size_t afterFirst = 0, afterSpawn = 0;
        std::map<uint32_t, uint32_t> finalSet;
        {
            ThreadBridge bridge;
            MockKexSimulation client(bridge, MockSimRole::CLIENT);
            client.setMapLoadTicks(3);
            client.start(60.0);
            auto pushIn = [&](BridgeMessage m) { return isSuccess(bridge.pushInbound(std::move(m))); };
            auto spawnDto = [](uint32_t id) { BridgeEntitySpawn s; s.mapEpoch = 1; s.entityId = id; s.type = 2; return s; };
            auto baseline = [&](uint32_t tick, std::vector<uint32_t> ids) {
                BridgeWorldBaseline b; b.mapEpoch = 1; b.serverTick = tick; b.mapName = "KARNAK_SANCTUARY";
                for (uint32_t id : ids) b.entities.push_back(spawnDto(id));
                return b;
            };

            BridgeSessionEvent joined; joined.type = BridgeSessionEvent::Type::CLIENT_JOINED; joined.slotId = 1;
            pushIn(joined);
            BridgeMapChange assign; assign.newMapEpoch = 1; assign.mapName = "KARNAK_SANCTUARY";
            pushIn(assign);
            bool ready = waitUntil([&] {
                BridgeMessage m;
                while (bridge.pollOutbound(m)) if (std::holds_alternative<BridgeMapReady>(m)) return true;
                return false;
            }, 3000);
            pushIn(baseline(100, { 1, 2, 3 }));
            waitUntil([&] { return client.getMockEntityCount() == 3; }, 2000);
            afterFirst = client.getMockEntityCount();
            pushIn(spawnDto(4));
            waitUntil([&] { return client.getMockEntityCount() == 4; }, 2000);
            afterSpawn = client.getMockEntityCount();
            // Despawn of 2 is lost; the authoritative replacement must purge it.
            pushIn(baseline(200, { 1, 3, 4 }));
            waitUntil([&] { return !client.hasEntity(2); }, 2000);
            finalSet = client.getEntityGenerations();
            client.stop();
            clientPart = ready && afterFirst == 3 && afterSpawn == 4 && finalSet.size() == 3 &&
                         finalSet.count(1) && finalSet.count(3) && finalSet.count(4) && !finalSet.count(2);
        }

        bool pass = hostPart && clientPart;
        logFaultResult(8, "Push Failure Propagation", "Despawn Under Saturation + Baseline Purges Ghosts", verdict(pass),
            "Despawn published exactly once from the outbox; a completed baseline replaces the client world (entities absent from it are removed)",
            std::string("host despawn delivered once=") + (hostPart ? "yes" : "NO") + "; client {1,2,3}+4 then baseline {1,3,4} -> " +
            std::to_string(finalSet.size()) + " entities, ghost 2 purged=" + (finalSet.count(2) ? "NO" : "yes"),
            "DOC-38 correction: old baseline only purged on epoch change; replacement semantics now apply to every completed baseline", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 9: Respawn confirm while the outbound path is saturated
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28209, 1);
        bool ok = c.start() && c.waitSynced(8000);
        auto& cl = *c.clients[0];
        const uint8_t slot = cl.sim.getLocalSlot();
        c.hostSim.triggerPlayerDeath(slot);
        ok = ok && waitUntil([&] { return cl.sim.getPlayerState(slot).lifeState == LIFE_DEAD; }, 3000);

        c.hostWorker.setOutboundDrainPaused(true);
        for (uint32_t i = 0; i < 300; ++i) spawnEnemy(c.hostSim, 7000 + i); // queue full -> confirm lands in outbox
        c.hostSim.sync();
        cl.sim.requestRespawn();
        bool confirmed = waitUntil([&] { return c.hostSim.getStats().respawnsConfirmed == 1; }, 3000);
        const uint64_t retained = c.hostSim.getStats().outboxDepth;
        sleepMs(1200); // client retries (idempotent on the host)
        c.hostWorker.setOutboundDrainPaused(false);

        bool alive = waitUntil([&] {
            auto p = cl.sim.getPlayerState(slot);
            return p.lifeState == LIFE_ALIVE && p.entityGeneration == 2;
        }, 5000);
        bool conv = c.waitConverged(8000);
        auto hs = c.hostSim.getStats();
        c.stop();

        bool pass = ok && confirmed && retained > 0 && alive && conv && hs.respawnsConfirmed == 1 && hs.respawnDuplicates >= 1;
        logFaultResult(9, "Push Failure Propagation", "Respawn Confirm Under Saturation Reaches the Client", verdict(pass),
            "Confirm retained by the host outbox; client retries are idempotent; the client ends alive with the new generation",
            "outbox retained " + std::to_string(retained) + ", confirms=" + std::to_string(hs.respawnsConfirmed) +
            ", duplicate retries=" + std::to_string(hs.respawnDuplicates) + ", client alive gen2=" + (alive ? "yes" : "NO"),
            "Owner-level retry (host outbox) + state reconciliation via PlayerSnapshot", msSince(t0));
    }

    // ========================================================================
    // SECTION 4: SPSC CONCURRENCY & MEMORY INTEGRITY
    // ========================================================================

    // ------------------------------------------------------------------------
    // Test 10: 1,000,000 Operations SPSC Concurrent Throughput & Strict FIFO
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;

        constexpr uint64_t TOTAL_OPS = 1'000'000;
        std::atomic<bool> producerDone{ false };
        std::atomic<uint64_t> consumerCount{ 0 };
        std::atomic<bool> strictFifo{ true };

        std::thread producer([&]() {
            for (uint64_t i = 0; i < TOTAL_OPS; ++i) {
                BridgeWorldEvent evt;
                evt.eventSequence = static_cast<uint32_t>(i);
                evt.eventType = EVENT_DAMAGE_CONFIRMED;
                evt.eventValue = static_cast<int32_t>(i & 0xFF);
                while (!isSuccess(bridge.pushOutbound(evt))) {
                    std::this_thread::yield();
                }
            }
            producerDone.store(true, std::memory_order_release);
            bridge.unregisterSimulationThread();
        });

        std::thread consumer([&]() {
            uint64_t expectedSeq = 0;
            BridgeMessage msg;
            while (!producerDone.load(std::memory_order_acquire) || expectedSeq < TOTAL_OPS) {
                if (bridge.pollOutbound(msg)) {
                    auto* evt = std::get_if<BridgeWorldEvent>(&msg);
                    if (evt) {
                        if (evt->eventSequence != expectedSeq) {
                            strictFifo.store(false, std::memory_order_relaxed);
                        }
                        ++expectedSeq;
                        consumerCount.fetch_add(1, std::memory_order_relaxed);
                    }
                } else {
                    std::this_thread::yield();
                }
            }
            bridge.unregisterNetworkThread();
        });

        producer.join();
        consumer.join();

        double dur = msSince(t0);
        bool pass = (consumerCount.load() == TOTAL_OPS) && strictFifo.load() && bridge.getAffinityViolationCount() == 0;
        double opsPerSec = (TOTAL_OPS / (dur / 1000.0));
        std::stringstream ss;
        ss << "Processed " << TOTAL_OPS << " items in " << dur << " ms ("
           << std::fixed << std::setprecision(1) << (opsPerSec / 1'000'000.0) << " M ops/sec) with affinity checks on; "
           << (strictFifo.load() ? 0 : 1) << " sequence errors";
        logFaultResult(10, "SPSC Concurrency", "1,000,000 Concurrent Operations & Strict Monotonic FIFO Order", verdict(pass),
            "1,000,000 operations, one producer + one consumer thread, 100% FIFO", ss.str(),
            "Valid only for correct SPSC usage; ownership is enforced at runtime", dur);
    }

    // ------------------------------------------------------------------------
    // Test 11: Dynamic Saturation Churn & Drain Recovery Cycle
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bool allCyclesClean = true;
        for (int cycle = 0; cycle < 50; ++cycle) {
            for (uint32_t i = 0; i < 300; ++i) {
                BridgeInputCommand cmd;
                cmd.sequence = i;
                bridge.pushInbound(cmd);
            }
            std::vector<BridgeMessage> batch;
            bridge.drainInbound(batch, 256);
            if (bridge.getInboundOccupancy() != 0) {
                allCyclesClean = false;
                break;
            }
        }
        logFaultResult(11, "SPSC Concurrency", "Dynamic Saturation Churn & Drain Recovery Cycle (50 Cycles)", verdict(allCyclesClean),
            "Queue occupancy reliably returns to 0 after drain; no slot leaks or deadlocks across 50 cycles",
            allCyclesClean ? "Completed 50 saturate-drain cycles; final occupancy = 0" : "Occupancy did not return to 0",
            "Single thread owns both roles (legal: one producer, one consumer)", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 12: Consumer Drain Interruption During Active Backpressure Yield Loop
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        bridge.setMaxBackpressureRetries(50000);
        fillOutbound(bridge);
        bridge.unregisterSimulationThread(); // Explicit producer handoff to the thread below

        std::atomic<bool> producerStarted{ false };
        std::atomic<QueuePushResult> pushRes{ QueuePushResult::SUCCESS };
        std::thread producer([&]() {
            bridge.registerSimulationThread();
            producerStarted.store(true, std::memory_order_release);
            BridgeEntitySpawn spawn;
            spawn.entityId = 777;
            pushRes.store(bridge.pushOutbound(spawn), std::memory_order_release);
            bridge.unregisterSimulationThread();
        });
        while (!producerStarted.load(std::memory_order_acquire)) std::this_thread::yield();
        sleepMs(1); // Let the producer enter the yield loop
        std::vector<BridgeMessage> batch;
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (bridge.drainOutbound(batch, 5) > 0) break;
            std::this_thread::yield();
        }
        producer.join();

        QueuePushResult finalRes = pushRes.load();
        bool pass = isSuccess(finalRes) && bridge.getAffinityViolationCount() == 0;
        logFaultResult(12, "SPSC Concurrency", "Consumer Drain Interruption During Active Backpressure Yield Loop", verdict(pass),
            "Producer completes (BACKPRESSURE_SUCCESS) when the consumer frees space mid-loop; explicit role handoff between producer threads",
            std::string("Result: ") + (finalRes == QueuePushResult::BACKPRESSURE_SUCCESS ? "BACKPRESSURE_SUCCESS" :
                                       finalRes == QueuePushResult::SUCCESS ? "SUCCESS" : "REJECTED"),
            "Bench corrected: the main thread hands the SIMULATION role over before another thread produces", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 13: Repeated host worker start/stop on ONE bridge with the sim running
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28213, 1);
        c.clients[0]->sim.setAutoReconnect(true, 20);
        bool allPass = c.start() && c.waitSynced(8000);
        for (int cycle = 1; cycle <= 5 && allPass; ++cycle) {
            for (int i = 0; i < 40; ++i) {
                c.hostSim.triggerWorldEvent(EVENT_DAMAGE_CONFIRMED, 1001, 2001, 1, 5, 0.0f);
            }
            spawnEnemy(c.hostSim, 8000 + cycle);
            c.hostWorker.stop();
            allPass = c.hostWorker.startHost(c.hostConfig);
        }
        bool conv = allPass && c.hostSim.sync() && c.waitConverged(15000);
        const uint32_t ticks = c.hostSim.getCurrentTick();
        c.stop();

        bool pass = allPass && conv && c.affinityViolations() == 0 && ticks > 0;
        logFaultResult(13, "SPSC Concurrency", "Repeated Start/Stop Under Active Load (5 Cycles, same bridge)", verdict(pass),
            "5 abrupt stop/restart cycles of the transport on one bridge while the simulation keeps running; the client reconnects and converges",
            std::string("restarts ok=") + (allPass ? "5/5" : "NO") + ", client converged=" + (conv ? "yes" : "NO") +
            ", affinity violations=" + std::to_string(c.affinityViolations()),
            "Bench corrected: no concurrent producers; restart on the SAME bridge (DOC-38 §3 Q4)", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 14: Stale connectionGeneration is rejected by the simulation
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        MockKexSimulation sim(bridge);
        sim.start(60.0);
        auto input = [](uint32_t gen, uint32_t seq) {
            BridgeInputCommand cmd; cmd.slotId = 1; cmd.connectionGeneration = gen; cmd.sequence = seq; cmd.moveForward = 1.0f;
            return cmd;
        };
        auto session = [](BridgeSessionEvent::Type t, uint32_t gen) {
            BridgeSessionEvent e; e.type = t; e.slotId = 1; e.connectionGeneration = gen; return e;
        };
        // This thread is the sole inbound producer (plays the network worker).
        bridge.pushInbound(session(BridgeSessionEvent::Type::CLIENT_JOINED, 1));
        bridge.pushInbound(input(1, 10));                                          // accepted
        bridge.pushInbound(session(BridgeSessionEvent::Type::RECONNECTED, 2));
        bridge.pushInbound(input(1, 11));                                          // stale generation
        bridge.pushInbound(input(2, 1));                                           // accepted (new gen, seq restarts)
        bridge.pushInbound(input(2, 1));                                           // duplicate sequence
        bridge.pushInbound(session(BridgeSessionEvent::Type::CLIENT_LEFT, 1));    // stale leave, must not end gen 2

        bool settled = waitUntil([&] {
            auto s = sim.getStats();
            return s.commandsProcessed == 2 && s.inputsRejectedGeneration == 1 && s.inputsRejectedStale == 1;
        }, 2000);
        auto p = sim.getPlayerState(1);
        auto s = sim.getStats();
        sim.stop();

        bool pass = settled && p.active && p.connectionGeneration == 2;
        logFaultResult(14, "SPSC Concurrency", "Stale Generation Rejection at the Simulation", verdict(pass),
            "Gen-1 input after RECONNECTED(gen 2) is rejected, duplicate sequence rejected, stale CLIENT_LEFT(gen 1) ignored",
            "accepted=" + std::to_string(s.commandsProcessed) + ", rejected generation=" + std::to_string(s.inputsRejectedGeneration) +
            ", rejected stale seq=" + std::to_string(s.inputsRejectedStale) + ", slot active gen=" + std::to_string(p.connectionGeneration),
            "DOC-38 correction: the old test only counted batch.size()==4; rejection is now asserted", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 15: Non-Trivial DTO Destruction & Consumer Slot Cleanup Safety
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        ThreadBridge bridge;
        constexpr size_t DTO_CYCLES = 20'000;
        size_t memBefore = workingSetMB();
        bool allPopped = true;
        for (size_t i = 0; i < DTO_CYCLES; ++i) {
            BridgeWorldSnapshot snap;
            snap.snapshotSequence = static_cast<uint32_t>(i);
            snap.entities.resize(32);
            for (size_t e = 0; e < 32; ++e) {
                snap.entities[e].entityId = static_cast<uint32_t>(e);
                snap.entities[e].health = 100;
            }
            bridge.pushOutbound(std::move(snap));

            BridgeMapChange mc;
            mc.mapName = "VERY_LONG_MAP_NAME_STRING_REQUIRING_HEAP_ALLOCATION_IF_NOT_SSO";
            mc.newMapEpoch = static_cast<uint32_t>(i);
            bridge.pushOutbound(std::move(mc));

            BridgeMessage poppedMsg;
            allPopped = bridge.pollOutbound(poppedMsg) && bridge.pollOutbound(poppedMsg) && allPopped;
        }
        size_t memAfter = workingSetMB();
        bool pass = allPopped && (memAfter <= memBefore + 4);
        logFaultResult(15, "SPSC Concurrency", "Non-Trivial DTO Destruction & In-Place Slot Reset Safety", verdict(pass),
            "Popped DTOs reset immediately via m_buffer[tail & MASK] = T{}; no heap growth across 40k dynamic DTOs",
            "Working set RAM delta: " + std::to_string(memAfter - memBefore) + " MB across 40,000 dynamic vector/string DTOs",
            "Unchanged scenario", msSince(t0));
    }

    // ------------------------------------------------------------------------
    // Test 16: Thread-affinity enforcement in the Release build
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        // (a) In-process: a throwing handler proves the guarded call does not proceed.
        ThreadBridge bridge;
        bridge.registerSimulationThread();
        bridge.setAffinityViolationHandler([](const char* msg) { throw std::runtime_error(msg); });
        const bool checksOnByDefault = bridge.areThreadChecksEnabled();
        std::atomic<bool> threw{ false };
        std::string violation;
        std::thread rogue([&] {
            try {
                BridgeEntitySpawn spawn;
                spawn.entityId = 1;
                bridge.pushOutbound(spawn);
            } catch (const std::exception& e) {
                violation = e.what();
                threw.store(true);
            }
        });
        rogue.join();
        const bool blocked = threw.load() && bridge.getOutboundOccupancy() == 0 && bridge.getAffinityViolationCount() == 1;

        // (b) Death test: with the default handler the Release process aborts.
        const int childExit = runSelfAsChild("--affinity-death-child");
#if defined(NDEBUG)
        const char* build = "Release (NDEBUG)";
#else
        const char* build = "Debug";
#endif
        bool pass = checksOnByDefault && blocked && childExit == 3;
        logFaultResult(16, "SPSC Concurrency", "Thread-Affinity Checks Enforcement in Release Build", verdict(pass),
            "Checks are on by default; a cross-thread call never reaches the queue and, with the default handler, aborts the process in Release",
            std::string("build=") + build + ", checks on by default=" + (checksOnByDefault ? "yes" : "NO") +
            ", rogue call blocked=" + (blocked ? "yes" : "NO") + ", default-handler child exit code=" + std::to_string(childExit) + " (3 = abort)",
            violation.empty() ? "no violation captured" : violation, msSince(t0));
    }

    // ========================================================================
    // SUMMARY
    // ========================================================================
    std::cout << "\n====================================================================\n"
              << "  Thread Bridge Adversarial Fault Injection Summary\n"
              << "====================================================================\n";

    int passCount = 0;
    int failCount = 0;
    int notTestedCount = 0;
    for (const auto& r : g_results) {
        if (r.verdict == TestVerdict::PASS) ++passCount;
        else if (r.verdict == TestVerdict::FAIL) ++failCount;
        else ++notTestedCount;
    }

    std::cout << "Total Test Scenarios: " << g_results.size() << "\n"
              << "  \x1b[32mPASS:\x1b[0m       " << passCount << " (" << std::fixed << std::setprecision(1) << (100.0 * passCount / g_results.size()) << "%)\n"
              << "  \x1b[31mFAIL:\x1b[0m       " << failCount << " (" << std::fixed << std::setprecision(1) << (100.0 * failCount / g_results.size()) << "%)\n"
              << "  \x1b[33mNOT TESTED:\x1b[0m " << notTestedCount << "\n"
              << "Working Set RAM:  " << workingSetMB() << " MB\n"
              << "====================================================================\n";

    return (failCount > 0) ? 1 : 0;
}
