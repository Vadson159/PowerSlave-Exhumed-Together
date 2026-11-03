#pragma once

// Shared fixtures for Thread Bridge tests: one host (bridge + simulation + ENet
// worker) and N real ENet clients, each with its own bridge, worker and client
// simulation. Every SPSC queue has exactly one producer and one consumer thread.

#include "thread_bridge.h"
#include "mock_simulation.h"
#include "enet_worker.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <map>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace pse::test {

inline bool waitUntil(const std::function<bool()>& pred, int timeoutMs, int stepMs = 5) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(stepMs));
    }
    return pred();
}

inline void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

inline size_t workingSetMB() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / (1024 * 1024);
    }
#endif
    return 0;
}

struct ClientNode {
    // Declaration order = destruction in reverse: worker, sim, then bridge.
    ThreadBridge bridge;
    MockKexSimulation sim{ bridge, MockSimRole::CLIENT };
    ENetWorker worker{ bridge, ENetWorkerMode::CLIENT };
    ClientConfig config;
};

struct Cluster {
    uint16_t port;
    HostConfig hostConfig;
    ThreadBridge hostBridge;
    MockKexSimulation hostSim{ hostBridge, MockSimRole::HOST };
    ENetWorker hostWorker{ hostBridge, ENetWorkerMode::HOST };
    std::vector<std::unique_ptr<ClientNode>> clients;

    Cluster(uint16_t port_, int numClients, uint32_t mapLoadTicks = 10) : port(port_) {
        hostConfig.bindAddress = "127.0.0.1";
        hostConfig.port = port;
        hostConfig.hostName = "BridgeHost";
        hostConfig.joinToken = "BridgeToken";
        for (int i = 0; i < numClients; ++i) {
            auto c = std::make_unique<ClientNode>();
            c->config.hostAddress = "127.0.0.1";
            c->config.hostPort = port;
            c->config.participantId = "bridge_client_" + std::to_string(port) + "_" + std::to_string(i);
            c->config.displayName = "Client" + std::to_string(i + 2);
            c->config.joinToken = "BridgeToken";
            c->sim.setMapLoadTicks(mapLoadTicks);
            clients.push_back(std::move(c));
        }
    }

    ~Cluster() { stop(); }

    bool start() {
        hostSim.start(60.0);
        if (!hostWorker.startHost(hostConfig)) return false;
        for (auto& c : clients) {
            c->sim.start(60.0);
            if (!c->worker.startClient(c->config)) return false;
        }
        return true;
    }

    void stop() {
        for (auto& c : clients) c->worker.stop();
        hostWorker.stop();
        for (auto& c : clients) c->sim.stop();
        hostSim.stop();
    }

    bool allSynced() const {
        for (const auto& c : clients) {
            if (c->sim.getClientSyncState() != ClientSyncState::SYNCED) return false;
        }
        return true;
    }

    // Every client is synced on the host's level and holds exactly the host's
    // entities (id + generation).
    bool converged() const {
        const uint32_t epoch = hostSim.getMapEpoch();
        const std::string map = hostSim.getMapName();
        const auto hostEntities = hostSim.getEntityGenerations();
        for (const auto& c : clients) {
            if (c->sim.getClientSyncState() != ClientSyncState::SYNCED) return false;
            if (c->sim.getMapEpoch() != epoch || c->sim.getMapName() != map) return false;
            if (c->sim.getEntityGenerations() != hostEntities) return false;
        }
        return true;
    }

    bool waitConverged(int timeoutMs) { return waitUntil([this] { return converged(); }, timeoutMs, 10); }
    bool waitSynced(int timeoutMs) { return waitUntil([this] { return allSynced(); }, timeoutMs, 10); }

    uint64_t affinityViolations() const {
        uint64_t v = hostBridge.getAffinityViolationCount();
        for (const auto& c : clients) v += c->bridge.getAffinityViolationCount();
        return v;
    }

    std::string describe() const {
        std::string s = "host epoch=" + std::to_string(hostSim.getMapEpoch()) +
                        " entities=" + std::to_string(hostSim.getMockEntityCount());
        for (size_t i = 0; i < clients.size(); ++i) {
            const auto& c = clients[i];
            s += "; c" + std::to_string(i) + " state=" + std::to_string(static_cast<int>(c->sim.getClientSyncState())) +
                 " epoch=" + std::to_string(c->sim.getMapEpoch()) +
                 " entities=" + std::to_string(c->sim.getMockEntityCount());
        }
        return s;
    }
};

inline void spawnEnemy(MockKexSimulation& sim, uint32_t id, uint32_t gen = 1) {
    sim.triggerSpawn(id, gen, EntityType::ENEMY, 1,
                     Transform3D(static_cast<float>(id % 1000), 10.0f, 0.0f),
                     Velocity3D(0.0f, 0.0f, 0.0f), ActorStateInfo(100, 100));
}

} // namespace pse::test
