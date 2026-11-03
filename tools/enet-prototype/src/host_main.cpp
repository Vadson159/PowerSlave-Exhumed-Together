#include "enet_host.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace pse;

void printUsage() {
    std::cout << "PowerSlave Exhumed Together - Standalone ENet Host\n"
              << "Usage: pse_host [options]\n"
              << "Options:\n"
              << "  --bind <ip>           IPv4 interface address (default: 127.0.0.1)\n"
              << "  --port <port>         UDP port to listen on (default: 27015)\n"
              << "  --max-clients <1..7>  Maximum client count (default: 7)\n"
              << "  --tick-rate <hz>      Simulation tick rate (default: 60)\n"
              << "  --snapshot-rate <hz>  Snapshot broadcast rate (default: 30)\n"
              << "  --token <string>      Session password / join token\n"
              << "  --sim-loss <percent>  Simulated packet drop (0..100)\n"
              << "  --sim-delay <ms>      Simulated latency in ms\n"
              << "  --help                Show this help message\n"
              << "\nInteractive Console Commands:\n"
              << "  status                Display connected participants\n"
              << "  map <name>            Initiate level transition to new map\n"
              << "  kill <slot 1..8>      Inflict fatal damage on player slot\n"
              << "  stat                  Print metrics, throughput, CPU and RAM\n"
              << "  quit / q              Shut down server gracefully\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    HostConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "--bind" && i + 1 < argc) {
            config.bindAddress = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--max-clients" && i + 1 < argc) {
            config.maxClients = static_cast<uint8_t>(std::clamp(std::stoi(argv[++i]), 1, 7));
        } else if (arg == "--tick-rate" && i + 1 < argc) {
            config.tickRate = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--snapshot-rate" && i + 1 < argc) {
            config.snapshotRate = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--token" && i + 1 < argc) {
            config.joinToken = argv[++i];
        } else if (arg == "--sim-loss" && i + 1 < argc) {
            config.simDropPercent = std::stof(argv[++i]);
        } else if (arg == "--sim-delay" && i + 1 < argc) {
            config.simLatencyMs = static_cast<uint32_t>(std::stoi(argv[++i]));
        }
    }

    std::cout << "=========================================================\n"
              << "  PowerSlave Exhumed Together - Standalone ENet Host\n"
              << "=========================================================\n";

    ENetHostServer host;
    if (!host.start(config)) {
        std::cerr << "[Host] Initialization failed." << std::endl;
        return 1;
    }

    // Worker thread for network pumping
    std::atomic<bool> bRun{ true };
    std::thread netThread([&]() {
        while (bRun.load()) {
            host.poll(2);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    std::cout << "[Host] Type 'status', 'map <name>', 'kill <slot>', 'stat', or 'quit'." << std::endl;

    std::string line;
    while (bRun.load() && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "quit" || line == "q" || line == "exit") {
            break;
        } else if (line == "status") {
            std::cout << "--- Session Status [SessionID: " << host.getSessionId() << "] ---\n"
                      << "Current Map: " << host.getSimulation().getMapName()
                      << " | Epoch: " << host.getSimulation().getMapEpoch()
                      << " | Tick: " << host.getSimulation().getCurrentTick() << "\n"
                      << "Connected Clients: " << host.getConnectedClientCount() << " / " << (int)config.maxClients << "\n";
            for (uint8_t i = 0; i < MAX_PLAYERS; ++i) {
                const auto& slot = host.getSimulation().getSlot(i);
                if (slot.connected) {
                    std::cout << "  Slot P" << (int)(i + 1)
                              << ": " << slot.displayName
                              << " [ID: " << slot.participantId << "]"
                              << " Gen=" << slot.connectionGeneration
                              << " State=" << (slot.lifeState == LIFE_ALIVE ? "ALIVE" : (slot.lifeState == LIFE_DEAD ? "DEAD" : "OTHER"))
                              << " HP=" << slot.health
                              << " Pos=(" << slot.posX << ", " << slot.posY << ", " << slot.posZ << ")\n";
                }
            }
        } else if (line.rfind("map ", 0) == 0) {
            std::string mapName = line.substr(4);
            host.changeMap(mapName);
        } else if (line.rfind("kill ", 0) == 0) {
            try {
                int slotNumber = std::stoi(line.substr(5));
                if (slotNumber >= 1 && slotNumber <= 8) {
                    uint8_t slotId = static_cast<uint8_t>(slotNumber - 1);
                    host.triggerDamageOnSlot(slotId, 999);
                    std::cout << "[Host] Inflicted fatal damage on slot P" << slotNumber << std::endl;
                } else {
                    std::cout << "Invalid slot. Must be between 1 and 8." << std::endl;
                }
            } catch (...) {
                std::cout << "Usage: kill <slot 1..8>" << std::endl;
            }
        } else if (line == "stat") {
            const auto& m = host.getMetrics();
            SystemUsage usage = MetricsCollector::querySystemUsage();
            std::cout << "--- Host Metrics ---\n"
                      << "Packets Sent: " << m.getTotalPacketsSent()
                      << " | Received: " << m.getTotalPacketsReceived() << "\n"
                      << "Bytes Sent: " << m.getTotalBytesSent()
                      << " | Received: " << m.getTotalBytesReceived() << "\n"
                      << "Peak Connected Peers: " << m.getPeakQueueSize()
                      << " | Avg: " << m.getAvgQueueSize() << "\n"
                      << "CPU Usage: " << usage.cpuPercent << " %\n"
                      << "RAM Working Set: " << (usage.ramWorkingSetBytes / (1024 * 1024)) << " MB\n";
        } else {
            std::cout << "Unknown command: " << line << ". Type 'status', 'map <name>', 'kill <slot>', 'stat', or 'quit'." << std::endl;
        }
    }

    bRun.store(false);
    if (netThread.joinable()) {
        netThread.join();
    }
    host.stop();

    return 0;
}
