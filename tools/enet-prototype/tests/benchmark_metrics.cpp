#include "enet_host.h"
#include "enet_client.h"
#include "world_replication.h"
#include "world_scale_config.h"
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <iomanip>
#include <fstream>
#include <algorithm>

using namespace pse;

struct BenchmarkRunResult {
    int clientCount = 0;
    int tickRate = 0;
    int snapshotRate = 0;
    double durationSec = 0.0;
    uint64_t totalBytesSentHost = 0;
    uint64_t totalBytesReceivedHost = 0;
    uint64_t totalPacketsSentHost = 0;
    double hostEgressKBps = 0.0;
    double bandwidthPerClientKBps = 0.0;
    double rttAvgMs = 0.0;
    double rttP50Ms = 0.0;
    double rttP95Ms = 0.0;
    double rttP99Ms = 0.0;
    double cpuPercent = 0.0;
    uint64_t ramWorkingSetMB = 0;
    double reconnectDurationMs = 0.0;
};

BenchmarkRunResult runBenchmark(int numClients, int tickRate, int snapshotRate, double durationSeconds) {
    BenchmarkRunResult res;
    res.clientCount = numClients;
    res.tickRate = tickRate;
    res.snapshotRate = snapshotRate;
    res.durationSec = durationSeconds;

    uint16_t port = static_cast<uint16_t>(28000 + numClients * 10 + tickRate / 10);

    HostConfig hostCfg;
    hostCfg.bindAddress = "127.0.0.1";
    hostCfg.port = port;
    hostCfg.maxClients = static_cast<uint8_t>(numClients);
    hostCfg.tickRate = static_cast<uint16_t>(tickRate);
    hostCfg.snapshotRate = static_cast<uint16_t>(snapshotRate);
    hostCfg.joinToken = "bench";
    hostCfg.hostName = "Host_Bench";
    hostCfg.participantId = "host_bench_p1";

    ENetHostServer host;
    if (!host.start(hostCfg)) {
        std::cerr << "Failed to start bench host on " << port << std::endl;
        return res;
    }

    std::vector<std::unique_ptr<ENetClientNode>> clients;
    for (int i = 0; i < numClients; ++i) {
        auto c = std::make_unique<ENetClientNode>();
        ClientConfig cCfg;
        cCfg.hostAddress = "127.0.0.1";
        cCfg.hostPort = port;
        cCfg.participantId = "bench_client_" + std::to_string(i + 2);
        cCfg.displayName = "Client_" + std::to_string(i + 2);
        cCfg.joinToken = "bench";
        c->connect(cCfg);
        clients.push_back(std::move(c));
    }

    // Connect stabilization
    for (int i = 0; i < 50; ++i) {
        host.poll(1);
        for (auto& c : clients) c->poll(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Reset metrics before measurement period
    host.getMetrics().reset();
    for (auto& c : clients) c->getMetrics().reset();

    auto startTime = std::chrono::steady_clock::now();
    uint64_t frame = 0;

    while (true) {
        frame++;
        host.poll(1);
        for (size_t i = 0; i < clients.size(); ++i) {
            clients[i]->poll(1);
            if (clients[i]->isAuthenticated()) {
                float fwd = (frame % 2 == 0) ? 1.0f : -0.5f;
                float yaw = static_cast<float>((frame * 3) % 360);
                clients[i]->sendInputCommand(fwd, 0.0f, 0.0f, yaw, 0, WEAPON_PISTOL);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsedMs >= static_cast<int64_t>(durationSeconds * 1000.0)) {
            break;
        }
    }

    // Measure reconnect duration on Client 0
    if (!clients.empty()) {
        auto recStart = std::chrono::steady_clock::now();
        uint64_t client0Key = clients[0]->getReconnectKey();
        clients[0]->disconnect(LEAVE_USER_QUIT);
        for (int i = 0; i < 10; ++i) {
            host.poll(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        auto recClient = std::make_unique<ENetClientNode>();
        ClientConfig cCfg;
        cCfg.hostAddress = "127.0.0.1";
        cCfg.hostPort = port;
        cCfg.participantId = "bench_client_2"; // Same
        cCfg.displayName = "Client_2_Reconnected";
        cCfg.joinToken = "bench";
        cCfg.reconnectKey = client0Key;
        recClient->connect(cCfg);
        clients[0] = std::move(recClient);

        for (int i = 0; i < 60; ++i) {
            host.poll(1);
            clients[0]->poll(1);
            if (clients[0]->isAuthenticated()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        auto recMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - recStart).count();
        res.reconnectDurationMs = recMs;
    }

    const auto& hm = host.getMetrics();
    res.totalBytesSentHost = hm.getTotalBytesSent();
    res.totalBytesReceivedHost = hm.getTotalBytesReceived();
    res.totalPacketsSentHost = hm.getTotalPacketsSent();

    res.hostEgressKBps = (static_cast<double>(res.totalBytesSentHost) / 1024.0) / durationSeconds;
    res.bandwidthPerClientKBps = (numClients > 0) ? (res.hostEgressKBps / numClients) : 0.0;

    // RTT stats from clients
    std::vector<double> allRtts;
    for (const auto& c : clients) {
        auto rttStat = c->getMetrics().calculateRttStats();
        if (rttStat.sampleCount > 0) {
            allRtts.push_back(rttStat.avg);
        }
    }

    if (!allRtts.empty()) {
        std::sort(allRtts.begin(), allRtts.end());
        double sum = 0;
        for (double v : allRtts) sum += v;
        res.rttAvgMs = sum / allRtts.size();
        res.rttP50Ms = allRtts[allRtts.size() / 2];
        res.rttP95Ms = allRtts[static_cast<size_t>(allRtts.size() * 0.95)];
        res.rttP99Ms = allRtts.back();
    }

    SystemUsage usage = MetricsCollector::querySystemUsage();
    res.cpuPercent = usage.cpuPercent;
    res.ramWorkingSetMB = usage.ramWorkingSetBytes / (1024 * 1024);

    for (auto& c : clients) c->disconnect(LEAVE_USER_QUIT);
    host.stop();
    return res;
}

// ----------------------------------------------------------------------------
// World State Replication Scalability Benchmark (128 Entities, 8 Players)
// ----------------------------------------------------------------------------
struct ReplicationBenchmarkResult {
    WorldScaleConfig config;
    std::string scenarioName;
    int totalEntities = 0;
    int clientCount = 7;
    double durationSec = 0.0;
    uint64_t totalBytesSentHost = 0;
    double hostEgressKBps = 0.0;
    double perClientKBps = 0.0;
    double avgSnapshotBytes = 0.0;
    int batchesPerSnapshot = 0;
    double rttAvgMs = 0.0;
    double cpuPercent = 0.0;
    uint64_t ramWorkingSetMB = 0;
    bool impaired = false;

    // Direct empirical measurements required by Acceptance Gate
    uint64_t hostSnapshotsBroadcast = 0;
    uint64_t hostBatchesBroadcast = 0;
    uint64_t hostEntitiesSerialized = 0;
    double actualBroadcastRateHz = 0.0;
    double avgEntitiesPerSnapshotHost = 0.0;
    double entitiesReceivedPerClientSec = 0.0;
    double snapshotsReceivedPerClientSec = 0.0;
    uint64_t totalEntitiesReceivedAllClients = 0;
};

ReplicationBenchmarkResult runReplicationScaleBenchmark(const WorldScaleConfig& cfg, double durationSeconds, bool withImpairments, bool sustainedLoad = false, const std::string& scenarioName = "") {
    ReplicationBenchmarkResult res;
    res.config = cfg;
    res.scenarioName = scenarioName.empty() ? (withImpairments ? "Impaired (40ms+15ms, 10% drop)" : "Clean Loopback") : scenarioName;
    res.totalEntities = cfg.totalEntities();
    res.clientCount = 7;
    res.durationSec = durationSeconds;
    res.impaired = withImpairments;

    uint16_t port = withImpairments ? 28910 : (sustainedLoad ? 28905 : 28900);

    HostConfig hostCfg;
    hostCfg.bindAddress = "127.0.0.1";
    hostCfg.port = port;
    hostCfg.maxClients = 7;
    hostCfg.tickRate = 60;
    hostCfg.snapshotRate = 30;
    hostCfg.joinToken = "bench_rep";
    hostCfg.hostName = "Rep_Bench_Host";
    hostCfg.participantId = "rep_bench_host_p1";

    ENetHostServer host;
    if (!host.start(hostCfg)) {
        std::cerr << "Failed to start replication benchmark host on port " << port << std::endl;
        return res;
    }

    if (withImpairments) {
        // 40ms base latency, 15ms jitter, 10% packet drop
        host.getNetSimulator().setLatencyMs(40, 15);
        host.getNetSimulator().setDropRatePercent(10.0f);
        host.getNetSimulator().setReorderEnabled(true);
    }

    WorldReplicationHost repHost(host);

    std::vector<std::unique_ptr<ENetClientNode>> clients;
    std::vector<std::unique_ptr<WorldReplicationClient>> repClients;

    for (int i = 0; i < 7; ++i) {
        auto c = std::make_unique<ENetClientNode>();
        ClientConfig cCfg;
        cCfg.hostAddress = "127.0.0.1";
        cCfg.hostPort = port;
        cCfg.participantId = "rep_bench_c" + std::to_string(i + 2);
        cCfg.displayName = "Rep_Client_" + std::to_string(i + 2);
        cCfg.joinToken = "bench_rep";
        c->connect(cCfg);
        auto rc = std::make_unique<WorldReplicationClient>(*c);
        clients.push_back(std::move(c));
        repClients.push_back(std::move(rc));
    }

    // Connect stabilization
    for (int i = 0; i < 60; ++i) {
        host.poll(1);
        for (auto& c : clients) c->poll(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Populate synthetic world (128 entities)
    populateSyntheticWorld(repHost, cfg);

    // Warm-up and snapshot distribution
    for (int i = 0; i < 20; ++i) {
        host.poll(1);
        for (auto& c : clients) c->poll(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Reset metrics before measurement period
    host.getMetrics().reset();
    for (auto& c : clients) c->getMetrics().reset();
    repHost.resetReplicationMetrics();
    for (auto& rc : repClients) rc->resetReplicationMetrics();

    auto startTime = std::chrono::steady_clock::now();
    uint64_t frame = 0;

    while (true) {
        frame++;
        host.poll(1);

        // In sustained load mode, maintain exact entity population
        if (sustainedLoad) {
            while (repHost.getActiveEntityCount() < static_cast<size_t>(cfg.totalEntities())) {
                float angle = static_cast<float>(frame * 0.1f);
                Vec3 origin(std::cos(angle) * 1500.0f, std::sin(angle) * 1500.0f, 200.0f);
                Vec3 vel(0.0f, 0.0f, 0.0f);
                repHost.spawnProjectile(ProjectileSubType::FIREBALL, 1001, 0, origin, vel, 25);
            }
        }

        for (size_t i = 0; i < clients.size(); ++i) {
            clients[i]->poll(1);
            if (clients[i]->isAuthenticated()) {
                float fwd = (frame % 2 == 0) ? 1.0f : -0.5f;
                float yaw = static_cast<float>((frame * 3) % 360);
                clients[i]->sendInputCommand(fwd, 0.0f, 0.0f, yaw, 0, WEAPON_PISTOL);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));

        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsedMs >= static_cast<int64_t>(durationSeconds * 1000.0)) {
            break;
        }
    }

    const auto& hm = host.getMetrics();
    res.totalBytesSentHost = hm.getTotalBytesSent();
    res.hostEgressKBps = (static_cast<double>(res.totalBytesSentHost) / 1024.0) / durationSeconds;
    res.perClientKBps = res.hostEgressKBps / 7.0;

    res.hostSnapshotsBroadcast = repHost.getTotalWorldSnapshotsBroadcast();
    res.hostBatchesBroadcast = repHost.getTotalWorldBatchesBroadcast();
    res.hostEntitiesSerialized = repHost.getTotalWorldEntitiesSerialized();
    res.actualBroadcastRateHz = durationSeconds > 0.0 ? (static_cast<double>(res.hostSnapshotsBroadcast) / durationSeconds) : 0.0;
    res.avgEntitiesPerSnapshotHost = res.hostSnapshotsBroadcast > 0 ? (static_cast<double>(res.hostEntitiesSerialized) / res.hostSnapshotsBroadcast) : 0.0;

    uint64_t sumEntitiesRcvd = 0;
    uint64_t sumSnapsRcvd = 0;
    for (const auto& rc : repClients) {
        sumEntitiesRcvd += rc->getTotalEntitiesReceived();
        sumSnapsRcvd += rc->getTotalSnapshotsReceived();
    }
    res.totalEntitiesReceivedAllClients = sumEntitiesRcvd;
    res.entitiesReceivedPerClientSec = (durationSeconds > 0.0 && !repClients.empty()) ? (static_cast<double>(sumEntitiesRcvd) / repClients.size()) / durationSeconds : 0.0;
    res.snapshotsReceivedPerClientSec = (durationSeconds > 0.0 && !repClients.empty()) ? (static_cast<double>(sumSnapsRcvd) / repClients.size()) / durationSeconds : 0.0;

    res.batchesPerSnapshot = res.hostSnapshotsBroadcast > 0 ? static_cast<int>((res.hostBatchesBroadcast + res.hostSnapshotsBroadcast - 1) / res.hostSnapshotsBroadcast)
                                                            : static_cast<int>(WorldScaleConfig::calculateBatchesForEntityCount(res.totalEntities, 1200));
    res.avgSnapshotBytes = res.hostSnapshotsBroadcast > 0 ? (static_cast<double>(res.totalBytesSentHost) / (res.hostSnapshotsBroadcast * 7.0)) : 0.0;

    std::vector<double> allRtts;
    for (const auto& c : clients) {
        auto rttStat = c->getMetrics().calculateRttStats();
        if (rttStat.sampleCount > 0) {
            allRtts.push_back(rttStat.avg);
        }
    }
    if (!allRtts.empty()) {
        double sum = 0;
        for (double v : allRtts) sum += v;
        res.rttAvgMs = sum / allRtts.size();
    }

    SystemUsage usage = MetricsCollector::querySystemUsage();
    res.cpuPercent = usage.cpuPercent;
    res.ramWorkingSetMB = usage.ramWorkingSetBytes / (1024 * 1024);

    for (auto& c : clients) c->disconnect(LEAVE_USER_QUIT);
    host.poll(10);
    host.stop();

    return res;
}

int main() {
    std::cout << "=========================================================\n"
              << "  PowerSlave Exhumed Together - Scalability Benchmarks\n"
              << "=========================================================\n";

    std::vector<BenchmarkRunResult> results;

    // Test matrix: Host egress at 1, 2, 4, 7 clients at standard 60Hz tick / 30Hz snapshot
    std::vector<int> clientCounts = { 1, 2, 4, 7 };
    for (int nc : clientCounts) {
        std::cout << "[Bench] Running test with " << nc << " clients (60Hz sim / 30Hz snap)..." << std::endl;
        auto r = runBenchmark(nc, 60, 30, 2.5);
        results.push_back(r);
        std::cout << "  -> Host Egress: " << std::fixed << std::setprecision(2) << r.hostEgressKBps
                  << " KB/s | Per Client: " << r.bandwidthPerClientKBps
                  << " KB/s | Reconnect: " << r.reconnectDurationMs << " ms" << std::endl;
    }

    // Test matrix: Frequencies comparison at 7 clients (30Hz, 60Hz, 120Hz)
    struct FreqPair { int tick; int snap; };
    std::vector<FreqPair> freqs = { { 30, 20 }, { 60, 30 }, { 60, 60 }, { 120, 60 } };
    std::vector<BenchmarkRunResult> freqResults;

    for (auto fp : freqs) {
        std::cout << "[Bench] Frequency test: Tick " << fp.tick << " Hz / Snap " << fp.snap << " Hz (7 clients)..." << std::endl;
        auto r = runBenchmark(7, fp.tick, fp.snap, 2.5);
        freqResults.push_back(r);
        std::cout << "  -> Host Egress: " << std::fixed << std::setprecision(2) << r.hostEgressKBps
                  << " KB/s | CPU: " << r.cpuPercent << "% | RAM: " << r.ramWorkingSetMB << " MB" << std::endl;
    }

    // World Replication Scalability Benchmark (128 Entities: 8P, 20E, 50Proj, 30Pick, 20Inter)
    std::cout << "\n=========================================================\n"
              << "  Phase 3/4: World Replication Scalability & Delivery Rates\n"
              << "=========================================================\n";
    WorldScaleConfig scaleCfg; // Default: 8 players, 20 enemies, 50 projectiles, 30 pickups, 20 interactives

    std::cout << "[Bench] Running Combat Decay Simulation (Clean Loopback)..." << std::endl;
    auto repCombat = runReplicationScaleBenchmark(scaleCfg, 3.0, false, false, "Combat Decay (Loopback)");
    std::cout << "  -> Avg Entities: " << std::fixed << std::setprecision(1) << repCombat.avgEntitiesPerSnapshotHost
              << " | Host Egress: " << std::setprecision(2) << repCombat.hostEgressKBps << " KB/s"
              << " | Per Client: " << repCombat.perClientKBps << " KB/s"
              << " | Client Ingest: " << std::setprecision(1) << repCombat.entitiesReceivedPerClientSec << " ent/s"
              << " | Batches/Snap: " << repCombat.batchesPerSnapshot
              << " | Avg RTT: " << repCombat.rttAvgMs << " ms"
              << " | CPU: " << repCombat.cpuPercent << "% | RAM: " << repCombat.ramWorkingSetMB << " MB" << std::endl;

    std::cout << "[Bench] Running Sustained 128-Entity Full Load (Clean Loopback)..." << std::endl;
    auto repSustained = runReplicationScaleBenchmark(scaleCfg, 3.0, false, true, "Sustained 128 Ent (Loopback)");
    std::cout << "  -> Avg Entities: " << std::fixed << std::setprecision(1) << repSustained.avgEntitiesPerSnapshotHost
              << " | Host Egress: " << std::setprecision(2) << repSustained.hostEgressKBps << " KB/s"
              << " | Per Client: " << repSustained.perClientKBps << " KB/s"
              << " | Client Ingest: " << std::setprecision(1) << repSustained.entitiesReceivedPerClientSec << " ent/s"
              << " | Batches/Snap: " << repSustained.batchesPerSnapshot
              << " | Avg RTT: " << repSustained.rttAvgMs << " ms"
              << " | CPU: " << repSustained.cpuPercent << "% | RAM: " << repSustained.ramWorkingSetMB << " MB" << std::endl;

    std::cout << "[Bench] Running Sustained 128-Entity with Impairments (40ms Latency, 15ms Jitter, 10% Drop)..." << std::endl;
    auto repImpaired = runReplicationScaleBenchmark(scaleCfg, 3.0, true, true, "Sustained 128 Ent (Impaired)");
    std::cout << "  -> Avg Entities: " << std::fixed << std::setprecision(1) << repImpaired.avgEntitiesPerSnapshotHost
              << " | Host Egress: " << std::setprecision(2) << repImpaired.hostEgressKBps << " KB/s"
              << " | Per Client: " << repImpaired.perClientKBps << " KB/s"
              << " | Client Ingest: " << std::setprecision(1) << repImpaired.entitiesReceivedPerClientSec << " ent/s"
              << " | Batches/Snap: " << repImpaired.batchesPerSnapshot
              << " | Avg RTT: " << repImpaired.rttAvgMs << " ms"
              << " | CPU: " << repImpaired.cpuPercent << "% | RAM: " << repImpaired.ramWorkingSetMB << " MB" << std::endl;

    // Append to docs/TEST_RESULTS.md
    std::ofstream report("docs/TEST_RESULTS.md", std::ios::app);
    if (!report.is_open()) {
        report.open("TEST_RESULTS.md", std::ios::app);
    }

    if (report.is_open()) {
        report << "\n## Client Scaling Benchmark (60Hz Tick, 30Hz Snapshots)\n\n";
        report << "| Clients (Peers) | Host Total Egress (KB/s) | Per-Client Bandwidth (KB/s) | Packets/sec Host | Reconnect Time (ms) |\n";
        report << "|---|---|---|---|---|\n";
        for (const auto& r : results) {
            report << "| " << r.clientCount << " client(s) | "
                   << std::fixed << std::setprecision(2) << r.hostEgressKBps << " KB/s | "
                   << r.bandwidthPerClientKBps << " KB/s | "
                   << (r.totalPacketsSentHost / r.durationSec) << " p/s | "
                   << std::setprecision(1) << r.reconnectDurationMs << " ms |\n";
        }

        report << "\n## Tick & Snapshot Frequency Comparison (7 Clients)\n\n";
        report << "| Tick Rate | Snapshot Rate | Host Egress (KB/s) | CPU Usage (%) | Working Set RAM (MB) |\n";
        report << "|---|---|---|---|---|\n";
        for (const auto& r : freqResults) {
            report << "| " << r.tickRate << " Hz | " << r.snapshotRate << " Hz | "
                   << std::fixed << std::setprecision(2) << r.hostEgressKBps << " KB/s | "
                   << r.cpuPercent << " % | "
                   << r.ramWorkingSetMB << " MB |\n";
        }

        report << "\n## World State Replication Scalability & Empirical Delivery (128 Entities Target)\n\n";
        report << "| Scenario | Serialized Entities/Frame | Broadcast Rate | Host Egress (KB/s) | Per-Client Bandwidth | Client Ingest (Entities/sec) | Batches/Frame | Avg RTT | CPU (%) | RAM |\n";
        report << "|---|---|---|---|---|---|---|---|---|---|\n";
        report << "| Combat Decay Simulation | "
               << std::fixed << std::setprecision(1) << repCombat.avgEntitiesPerSnapshotHost << " ent | "
               << std::setprecision(1) << repCombat.actualBroadcastRateHz << " Hz | "
               << std::setprecision(2) << repCombat.hostEgressKBps << " KB/s | "
               << repCombat.perClientKBps << " KB/s | "
               << std::setprecision(1) << repCombat.entitiesReceivedPerClientSec << " ent/s | "
               << repCombat.batchesPerSnapshot << " | "
               << std::setprecision(1) << repCombat.rttAvgMs << " ms | "
               << std::setprecision(2) << repCombat.cpuPercent << " % | "
               << repCombat.ramWorkingSetMB << " MB |\n";
        report << "| Sustained 128 Entities (Clean) | "
               << std::fixed << std::setprecision(1) << repSustained.avgEntitiesPerSnapshotHost << " ent | "
               << std::setprecision(1) << repSustained.actualBroadcastRateHz << " Hz | "
               << std::setprecision(2) << repSustained.hostEgressKBps << " KB/s | "
               << repSustained.perClientKBps << " KB/s | "
               << std::setprecision(1) << repSustained.entitiesReceivedPerClientSec << " ent/s | "
               << repSustained.batchesPerSnapshot << " | "
               << std::setprecision(1) << repSustained.rttAvgMs << " ms | "
               << std::setprecision(2) << repSustained.cpuPercent << " % | "
               << repSustained.ramWorkingSetMB << " MB |\n";
        report << "| Sustained 128 Entities (Impaired) | "
               << std::fixed << std::setprecision(1) << repImpaired.avgEntitiesPerSnapshotHost << " ent | "
               << std::setprecision(1) << repImpaired.actualBroadcastRateHz << " Hz | "
               << std::setprecision(2) << repImpaired.hostEgressKBps << " KB/s | "
               << repImpaired.perClientKBps << " KB/s | "
               << std::setprecision(1) << repImpaired.entitiesReceivedPerClientSec << " ent/s | "
               << repImpaired.batchesPerSnapshot << " | "
               << std::setprecision(1) << repImpaired.rttAvgMs << " ms | "
               << std::setprecision(2) << repImpaired.cpuPercent << " % | "
               << repImpaired.ramWorkingSetMB << " MB |\n";

        report.close();
        std::cout << "[Bench] Scaling, Frequency & Replication metrics appended to TEST_RESULTS.md" << std::endl;
    }

    return 0;
}
