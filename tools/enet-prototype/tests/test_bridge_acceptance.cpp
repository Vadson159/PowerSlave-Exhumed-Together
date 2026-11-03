// Thread Bridge P0 end-to-end acceptance suite (DOC-40).
// Real ENet transport over loopback, host + up to 7 clients, every bridge with
// thread-affinity enforcement active (default: abort on violation, Release too).

#include "bridge_test_harness.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <sstream>
#include <atomic>
#include <cmath>
#include <stdexcept>

using namespace pse;
using namespace pse::test;

struct AcceptanceResult {
    int id;
    std::string name;
    bool passed;
    std::string details;
    double ms;
};

static std::vector<AcceptanceResult> g_results;

static void report(int id, const std::string& name, bool passed, const std::string& details, double ms) {
    g_results.push_back({ id, name, passed, details, ms });
    std::cout << "[ACCEPT " << std::setw(2) << id << "] " << (passed ? "[PASS] " : "[FAIL] ") << name
              << " (" << std::fixed << std::setprecision(1) << ms << " ms)\n      " << details << std::endl;
}

static double sinceMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    std::cout << "=====================================================================\n"
              << "  PowerSlave Exhumed Together - Thread Bridge P0 Acceptance Suite\n"
              << "=====================================================================\n";

    // ------------------------------------------------------------------------
    // A1: Overflow of BOTH queues (host outbound + client inbound) without loss
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28401, 3);
        bool ok = c.start() && c.waitSynced(8000);

        c.hostWorker.setOutboundDrainPaused(true);            // Transport stalls: outbound fills
        for (uint32_t i = 0; i < 600; ++i) spawnEnemy(c.hostSim, 10000 + i);
        for (uint32_t i = 0; i < 200; ++i) c.hostSim.triggerDespawn(10000 + i * 3, 1, DespawnReason::KILLED);
        ok = ok && c.hostSim.sync();
        sleepMs(100);
        const uint64_t hostOutboxPeak = c.hostSim.getStats().outboxPeak;
        const size_t hostQueue = c.hostBridge.getOutboundOccupancy();

        for (auto& cl : c.clients) cl->sim.injectTickStall(900); // Client sims stall: inbound fills
        sleepMs(20);
        c.hostWorker.setOutboundDrainPaused(false);

        bool conv = c.waitConverged(15000);
        uint64_t deferred = 0, escalations = 0, clientResync = 0;
        for (auto& cl : c.clients) {
            auto ws = cl->worker.getStats();
            deferred += ws.criticalDeferred;
            escalations += ws.pendingEscalations;
            clientResync |= cl->bridge.getMetrics().resyncReasons;
        }
        auto hs = c.hostSim.getStats();
        const size_t hostCount = c.hostSim.getMockEntityCount();
        std::string desc = c.describe();
        c.stop();

        bool pass = ok && conv && hostCount == 400 && hostOutboxPeak > 0 && hostQueue == THREAD_BRIDGE_OUTBOUND_CAPACITY &&
                    deferred > 0 && escalations == 0 && hs.outboxEscalations == 0 && clientResync == 0 &&
                    c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "host outbound full=" << hostQueue << "/256, host outbox peak=" << hostOutboxPeak
           << ", client inbound parked=" << deferred << ", escalations=" << escalations
           << ", converged=" << conv << " (" << desc << ")";
        report(1, "Overflow of both queues: no Spawn/Despawn lost", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A2: Spawn/Despawn churn under random transport pauses and sim stalls
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28402, 2);
        bool ok = c.start() && c.waitSynced(8000);

        std::mt19937 rng(12345);
        std::vector<std::pair<uint32_t, uint32_t>> live; // id, gen
        uint32_t nextId = 20000;
        uint64_t spawns = 0, despawns = 0;
        for (int step = 0; step < 3000; ++step) {
            if (live.empty() || (rng() % 100) < 55) {
                uint32_t gen = 1 + (rng() % 3);
                spawnEnemy(c.hostSim, nextId, gen);
                live.push_back({ nextId++, gen });
                ++spawns;
            } else {
                size_t idx = rng() % live.size();
                c.hostSim.triggerDespawn(live[idx].first, live[idx].second, DespawnReason::KILLED);
                live[idx] = live.back();
                live.pop_back();
                ++despawns;
            }
            if (step % 300 == 0) c.hostWorker.setOutboundDrainPaused(true);
            if (step % 300 == 150) c.hostWorker.setOutboundDrainPaused(false);
            if (step % 500 == 250) c.clients[rng() % c.clients.size()]->sim.injectTickStall(150 + rng() % 200);
            if (step % 25 == 0) sleepMs(1);
        }
        c.hostWorker.setOutboundDrainPaused(false);
        ok = ok && c.hostSim.sync();
        bool conv = c.waitConverged(15000);
        const size_t hostCount = c.hostSim.getMockEntityCount();
        std::string desc = c.describe();
        c.stop();

        bool pass = ok && conv && hostCount == live.size() && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << spawns << " spawns / " << despawns << " despawns, live=" << live.size()
           << ", converged=" << conv << " (" << desc << ")";
        report(2, "Spawn/Despawn churn with transport pauses and client stalls", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A3: Respawn correctness (death -> request -> confirm), retries idempotent
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28403, 2);
        bool ok = c.start() && c.waitSynced(8000);
        auto& client = *c.clients[0];
        const uint8_t slot = client.sim.getLocalSlot();

        bool rounds = ok;
        uint32_t expectedGen = 1;
        for (int round = 1; round <= 2 && rounds; ++round) {
            c.hostSim.triggerPlayerDeath(slot);
            rounds = waitUntil([&] { return client.sim.getPlayerState(slot).lifeState == LIFE_DEAD; }, 3000);

            // Transport stalls while the request is honoured: the confirm stays owned
            // by the host outbox; the client retries the request (idempotent).
            c.hostWorker.setOutboundDrainPaused(true);
            const auto duplicatesBefore = c.hostSim.getStats().respawnDuplicates;
            const auto clientTicksBefore = client.sim.getStats().totalTicks;
            client.sim.requestRespawn();
            rounds = rounds && waitUntil([&] { return c.hostSim.getStats().respawnsConfirmed == static_cast<uint64_t>(round); }, 3000);
            // Retry is tick based. Wall-clock 1300 ms does not guarantee that the
            // client has run RESPAWN_RETRY_TICKS at standard Windows resolution.
            rounds = rounds && waitUntil([&] {
                return c.hostSim.getStats().respawnDuplicates > duplicatesBefore;
            }, 5000);
            std::cout << "[A3 retry] client ticks=" << client.sim.getStats().totalTicks - clientTicksBefore
                      << ", new host duplicates=" << c.hostSim.getStats().respawnDuplicates - duplicatesBefore << std::endl;
            c.hostWorker.setOutboundDrainPaused(false);

            ++expectedGen;
            rounds = rounds && waitUntil([&] {
                auto p0 = client.sim.getPlayerState(slot);
                auto p1 = c.clients[1]->sim.getPlayerState(slot);
                return p0.lifeState == LIFE_ALIVE && p0.entityGeneration == expectedGen &&
                       p0.deathSequence == static_cast<uint32_t>(round) &&
                       p1.lifeState == LIFE_ALIVE && p1.entityGeneration == expectedGen;
            }, 4000);
        }
        auto hs = c.hostSim.getStats();
        auto hp = c.hostSim.getPlayerState(slot);
        auto cs = client.sim.getStats();
        c.stop();

        bool pass = rounds && hs.respawnsConfirmed == 2 && hs.respawnDuplicates >= 1 &&
                    hp.entityGeneration == 3 && hp.lifeState == LIFE_ALIVE && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "confirmed=" << hs.respawnsConfirmed << ", duplicate retries ignored=" << hs.respawnDuplicates
           << ", rejected=" << hs.respawnRejected << ", client requests sent=" << cs.respawnRequestsSent
           << ", host gen=" << hp.entityGeneration << ", both clients alive=" << rounds;
        report(3, "Respawn: request/confirm under transport stall, retries idempotent", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A4: MapChange without a missed level load (pauses + stalls during load)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28404, 3, 30);
        bool ok = c.start() && c.waitSynced(8000);
        for (uint32_t i = 0; i < 50; ++i) spawnEnemy(c.hostSim, 30000 + i);
        ok = ok && c.waitConverged(8000);

        // Watcher: a client must never be SYNCED on a level it did not load.
        std::atomic<bool> watch{ true };
        std::atomic<uint64_t> invariantBreaks{ 0 };
        std::thread watcher([&] {
            while (watch.load()) {
                for (auto& cl : c.clients) {
                    if (cl->sim.getClientSyncState() == ClientSyncState::SYNCED) {
                        const uint32_t e = cl->sim.getMapEpoch();
                        const std::string m = cl->sim.getMapName();
                        const bool known = (e == 1 && m == "KARNAK_SANCTUARY") || (e == 2 && m == "TEMPLE_OF_RAMSES") ||
                                           (e == 3 && m == "VALLEY_OF_THE_KINGS") || (e == 4 && m == "KILMAATIKAHN");
                        if (!known) invariantBreaks.fetch_add(1);
                    }
                }
                sleepMs(2);
            }
        });

        // Round 1: plain transition, new level populated immediately
        c.hostSim.triggerMapChange("TEMPLE_OF_RAMSES", 2);
        for (uint32_t i = 0; i < 40; ++i) spawnEnemy(c.hostSim, 31000 + i);
        bool r1 = c.hostSim.sync() && c.waitConverged(10000);

        // Round 2: transport paused across the MapChange, one client stalled
        c.hostWorker.setOutboundDrainPaused(true);
        c.hostSim.triggerMapChange("VALLEY_OF_THE_KINGS", 3);
        for (uint32_t i = 0; i < 300; ++i) spawnEnemy(c.hostSim, 32000 + i);
        c.hostSim.sync();
        c.clients[1]->sim.injectTickStall(500);
        sleepMs(200);
        c.hostWorker.setOutboundDrainPaused(false);
        bool r2 = c.waitConverged(12000);

        // Round 3: a client stalls while it is loading the level
        c.hostSim.triggerMapChange("KILMAATIKAHN", 4);
        sleepMs(120);
        c.clients[2]->sim.injectTickStall(1200);
        for (uint32_t i = 0; i < 25; ++i) spawnEnemy(c.hostSim, 33000 + i);
        bool r3 = c.hostSim.sync() && c.waitConverged(12000);

        watch.store(false);
        watcher.join();
        auto hs = c.hostSim.getStats();
        uint64_t anomalies = 0, loads = 0;
        for (auto& cl : c.clients) {
            auto cs = cl->sim.getStats();
            anomalies += cs.epochAnomalies;
            loads += cs.mapLoads;
        }
        std::string desc = c.describe();
        c.stop();

        // Every client loads each of the 4 levels exactly once and confirms each with MAP_READY.
        bool pass = ok && r1 && r2 && r3 && invariantBreaks.load() == 0 && anomalies == 0 &&
                    hs.mapTransitionsCompleted == 3 && loads == 12 && hs.mapReadyReceived == 12 &&
                    hs.kicksIssued == 0 && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "rounds=" << r1 << r2 << r3 << ", transitions completed=" << hs.mapTransitionsCompleted
           << " (map changes=" << hs.mapChangesProcessed << ", MAP_READY=" << hs.mapReadyReceived
           << ", stale MAP_READY=" << hs.mapReadyStale << ")"
           << ", client level loads=" << loads << ", anomalies=" << anomalies
           << ", synced-on-unloaded-level=" << invariantBreaks.load() << ", kicks=" << hs.kicksIssued << " (" << desc << ")";
        report(4, "MapChange two-phase: no missed level load, MAP_READY after load", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A5: 8 players (host + 7 clients) input under simulation-thread jitter
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28405, 7);
        bool ok = c.start() && c.waitSynced(10000);
        std::vector<uint8_t> slots;
        for (auto& cl : c.clients) slots.push_back(cl->sim.getLocalSlot());

        std::vector<float> y0;
        for (uint8_t s : slots) y0.push_back(c.hostSim.getPlayerTransform(s).y);
        const uint32_t tickStart = c.hostSim.getCurrentTick();

        for (auto& cl : c.clients) cl->sim.submitLocalInput(1.0f, 0.0f, 90.0f, 0.0f, 0);

        std::atomic<bool> jitter{ true };
        std::thread jitterThread([&] {
            std::mt19937 rng(777);
            while (jitter.load()) {
                c.hostSim.injectTickStall(20 + rng() % 280); // 20..300 ms stalls (inbound reaches watermark)
                sleepMs(80 + static_cast<int>(rng() % 120));
            }
        });

        constexpr int TAPS = 10;
        for (int t = 0; t < TAPS; ++t) {
            for (auto& cl : c.clients) cl->sim.submitButtonTap(BUTTON_ATTACK);
            sleepMs(110);
        }
        for (auto& cl : c.clients) cl->sim.submitLocalInput(0.0f, 0.0f, 90.0f, 0.0f, 0); // Release W
        sleepMs(300);
        jitter.store(false);
        jitterThread.join();
        sleepMs(400);

        const uint32_t ticksElapsed = c.hostSim.getCurrentTick() - tickStart;
        std::vector<float> y1, y2;
        for (uint8_t s : slots) y1.push_back(c.hostSim.getPlayerTransform(s).y);
        sleepMs(300);
        for (uint8_t s : slots) y2.push_back(c.hostSim.getPlayerTransform(s).y);

        bool shotsExact = true, noPhantom = true, noAcceleration = true;
        std::ostringstream shots;
        for (size_t i = 0; i < slots.size(); ++i) {
            auto p = c.hostSim.getPlayerState(slots[i]);
            shots << p.shotsFired << (i + 1 < slots.size() ? "," : "");
            if (p.shotsFired != TAPS) shotsExact = false;
            if (std::fabs(y2[i] - y1[i]) > 1e-4f) noPhantom = false;
            if ((y1[i] - y0[i]) > 0.1f * static_cast<float>(ticksElapsed) + 1e-3f) noAcceleration = false;
        }
        auto hs = c.hostSim.getStats();
        auto ws = c.hostWorker.getStats();
        c.stop();

        bool pass = ok && hs.maxMovementStepsPerSlotTick == 1 && shotsExact && noPhantom && noAcceleration &&
                    hs.stallCount >= 5 && hs.inputsRejectedGeneration == 0 && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "host stalls=" << hs.stallCount << ", max movement steps/slot/tick=" << hs.maxMovementStepsPerSlotTick
           << ", shots per slot=[" << shots.str() << "] (taps=" << TAPS << ")"
           << ", wire cmds coalesced=" << ws.inputsCoalesced << ", stale rejected=" << ws.inputsRejectedStale
           << ", stopped after release=" << noPhantom << ", no burst=" << noAcceleration;
        report(5, "8 players input under sim-thread jitter: 1 step/tick, no lost taps", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A6: Reconnect and connectionGeneration change mid-session
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28406, 2);
        bool ok = c.start() && c.waitSynced(8000);
        for (uint32_t i = 0; i < 30; ++i) spawnEnemy(c.hostSim, 40000 + i);
        ok = ok && c.waitConverged(8000);

        auto& client = *c.clients[0];
        const uint8_t slot = client.sim.getLocalSlot();
        const uint32_t gen1 = client.sim.getLocalConnectionGeneration();
        client.sim.submitLocalInput(1.0f, 0.0f, 0.0f, 0.0f, 0);
        bool moving = waitUntil([&] { return c.hostSim.getPlayerTransform(slot).y > 1.0f; }, 3000);

        client.sim.requestReconnect();
        for (uint32_t i = 0; i < 20; ++i) spawnEnemy(c.hostSim, 41000 + i); // World changes meanwhile
        bool regen = waitUntil([&] {
            return client.sim.getLocalConnectionGeneration() == gen1 + 1 &&
                   c.hostSim.getPlayerState(slot).connectionGeneration == gen1 + 1 &&
                   c.hostSim.getPlayerState(slot).active;
        }, 6000);
        bool conv = c.waitConverged(8000);
        const float yAfter = c.hostSim.getPlayerTransform(slot).y;
        bool resumed = waitUntil([&] { return c.hostSim.getPlayerTransform(slot).y > yAfter + 1.0f; }, 3000);
        auto ws = client.worker.getStats();
        c.stop();

        bool pass = ok && moving && regen && conv && resumed && ws.reconnects == 1 && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "gen " << gen1 << " -> " << (gen1 + 1) << " on host and client=" << regen
           << ", resynced=" << conv << ", input resumed under new generation=" << resumed
           << ", worker reconnects=" << ws.reconnects;
        report(6, "Reconnect: connectionGeneration change, resync, input continuity", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A7: Host worker stop + restart on the SAME bridge while the sim keeps running
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28407, 2);
        for (auto& cl : c.clients) cl->sim.setAutoReconnect(true, 30);
        bool ok = c.start() && c.waitSynced(8000);
        for (uint32_t i = 0; i < 30; ++i) spawnEnemy(c.hostSim, 50000 + i);
        ok = ok && c.waitConverged(8000);

        bool cycles = true;
        for (int cycle = 0; cycle < 3 && cycles; ++cycle) {
            c.hostWorker.stop();
            // World keeps changing while the transport is down (owned by sim outbox/queue)
            for (uint32_t i = 0; i < 20; ++i) spawnEnemy(c.hostSim, 51000 + cycle * 100 + i);
            for (uint32_t i = 0; i < 5; ++i) c.hostSim.triggerDespawn(50000 + cycle * 5 + i, 1, DespawnReason::KILLED);
            if (cycle == 1) c.hostSim.triggerMapChange("TEMPLE_OF_RAMSES", 2);
            sleepMs(150);
            cycles = c.hostWorker.startHost(c.hostConfig) && c.waitConverged(15000);
        }
        auto hs = c.hostSim.getStats();
        auto hm = c.hostBridge.getMetrics();
        std::string desc = c.describe();
        c.stop();

        bool pass = ok && cycles && hs.kicksIssued == 0 && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "3 stop/restart cycles on one bridge, map change while down, clients auto-reconnected and converged="
           << cycles << ", host resync reasons=" << hm.resyncReasons << " (" << desc << ")";
        report(7, "Host worker stop/restart on the same bridge", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A8: Client inbound pending bound exceeded -> reconnect + baseline (no silent loss)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28408, 1);
        c.clients[0]->worker.setInboundPendingCap(64);
        bool ok = c.start() && c.waitSynced(8000);

        c.clients[0]->sim.injectTickStall(1500);
        sleepMs(50);
        for (uint32_t i = 0; i < 500; ++i) spawnEnemy(c.hostSim, 60000 + i);
        ok = ok && c.hostSim.sync();
        bool conv = c.waitConverged(15000);
        auto ws = c.clients[0]->worker.getStats();
        auto cm = c.clients[0]->bridge.getMetrics();
        std::string desc = c.describe();
        c.stop();

        bool pass = ok && conv && ws.pendingEscalations >= 1 && ws.reconnects >= 1 &&
                    (cm.resyncReasons & RESYNC_INBOUND_LOSS) && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "pending peak=" << ws.pendingPeak << " > cap 64 -> escalations=" << ws.pendingEscalations
           << ", reconnects=" << ws.reconnects << ", resync reasons=" << cm.resyncReasons
           << ", converged after rebuild=" << conv << " (" << desc << ")";
        report(8, "Client inbound pending overflow: explicit reconnect, state rebuilt", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A9: Host outbox bound exceeded -> explicit resync by baseline replacement
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28409, 2);
        c.hostSim.setOutboxCap(300);
        bool ok = c.start() && c.waitSynced(8000);

        c.hostWorker.setOutboundDrainPaused(true);
        for (uint32_t i = 0; i < 1000; ++i) spawnEnemy(c.hostSim, 70000 + i);
        for (uint32_t i = 0; i < 100; ++i) c.hostSim.triggerDespawn(70000 + i, 1, DespawnReason::KILLED);
        ok = ok && c.hostSim.sync();
        sleepMs(50);
        c.hostWorker.setOutboundDrainPaused(false);
        bool conv = c.waitConverged(15000);
        auto hs = c.hostSim.getStats();
        auto hm = c.hostBridge.getMetrics();
        std::string desc = c.describe();
        c.stop();

        bool pass = ok && conv && hs.outboxEscalations >= 1 && (hm.resyncReasons & RESYNC_OUTBOUND_LOSS) &&
                    c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "outbox escalations=" << hs.outboxEscalations << ", baselines sent=" << hs.baselinesSent
           << ", resync reasons=" << hm.resyncReasons << ", converged=" << conv << " (" << desc << ")";
        report(9, "Host outbox overflow: declared resync, baseline replaces client state", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A10: Input stream stops while connected -> neutral after timeout (no phantom)
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        Cluster c(28410, 1);
        bool ok = c.start() && c.waitSynced(8000);
        auto& client = *c.clients[0];
        const uint8_t slot = client.sim.getLocalSlot();
        client.sim.submitLocalInput(1.0f, 0.0f, 0.0f, 0.0f, BUTTON_ATTACK);
        bool moving = waitUntil([&] { return c.hostSim.getPlayerTransform(slot).y > 2.0f; }, 3000);
        const auto timeoutsBefore = c.hostSim.getStats().inputNeutralTimeouts;
        client.sim.stopLocalInput(); // Held W and fire, then the command stream stops
        // stopLocalInput posts to the client simulation; wall-clock sleep is not
        // an acknowledgement, nor does it guarantee six host simulation ticks.
        const bool stopped = client.sim.sync();
        const bool neutralized = waitUntil([&] {
            return c.hostSim.getStats().inputNeutralTimeouts > timeoutsBefore;
        }, 2000);
        const float ya = c.hostSim.getPlayerTransform(slot).y;
        sleepMs(300);
        const float yb = c.hostSim.getPlayerTransform(slot).y;
        auto hs = c.hostSim.getStats();
        c.stop();

        bool pass = ok && moving && stopped && neutralized && std::fabs(yb - ya) < 1e-4f && hs.inputNeutralTimeouts >= 1 && c.affinityViolations() == 0;
        std::ostringstream ss;
        ss << "movement after observed neutral over 300 ms=" << (yb - ya) << ", neutral timeouts=" << hs.inputNeutralTimeouts
           << ", stop acknowledged=" << stopped << ", new timeout observed=" << neutralized;
        report(10, "Input stream loss: held state neutralized after 6 ticks", pass, ss.str(), sinceMs(t0));
    }

    // ------------------------------------------------------------------------
    // A11: resetForNewSession only while no producer/consumer thread runs
    // ------------------------------------------------------------------------
    {
        auto t0 = std::chrono::steady_clock::now();
        bool blockedWhileRunning = false;
        bool clearedWhenStopped = false;
        {
            ThreadBridge bridge;
            MockKexSimulation sim(bridge);
            // Handler must be installed before any other thread owns a role.
            bridge.setAffinityViolationHandler([](const char* msg) { throw std::runtime_error(msg); });
            for (uint32_t i = 0; i < 300; ++i) spawnEnemy(sim, 80000 + i); // Fills outbound + outbox on first ticks
            sim.start(60.0);
            sim.sync();
            try {
                bridge.resetForNewSession(); // Simulation thread still owns its role
            } catch (const std::exception&) {
                blockedWhileRunning = bridge.getOutboundOccupancy() == THREAD_BRIDGE_OUTBOUND_CAPACITY;
            }
            sim.stop(); // Leftover outbox -> declared OUTBOUND loss (nothing silent)
            const bool lossDeclared = (bridge.getMetrics().resyncReasons & RESYNC_OUTBOUND_LOSS) != 0;
            bridge.resetForNewSession();
            clearedWhenStopped = lossDeclared && bridge.getOutboundOccupancy() == 0 &&
                                 bridge.getInboundOccupancy() == 0 && !bridge.isResyncRequired();
        }
        bool pass = blockedWhileRunning && clearedWhenStopped;
        report(11, "resetForNewSession guarded by thread ownership", pass,
               std::string("refused while simulation runs=") + (blockedWhileRunning ? "yes" : "NO") +
               ", after stop: unsent outbox declared as loss, queues and resync cleared=" + (clearedWhenStopped ? "yes" : "NO"),
               sinceMs(t0));
    }

    std::cout << "=====================================================================\n";
    int passed = 0;
    for (const auto& r : g_results) if (r.passed) ++passed;
    std::cout << "Acceptance Passed: " << passed << " / " << g_results.size()
              << "  (thread-affinity enforcement active in this build; any violation aborts the process)\n"
              << "Working Set RAM: " << workingSetMB() << " MB\n"
              << "=====================================================================\n";
    return passed == static_cast<int>(g_results.size()) ? 0 : 1;
}
