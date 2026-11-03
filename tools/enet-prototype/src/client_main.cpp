#include "enet_client.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>

using namespace pse;

void printUsage() {
    std::cout << "PowerSlave Exhumed Together - Standalone ENet Client\n"
              << "Usage: pse_client [options]\n"
              << "Options:\n"
              << "  --connect <ip:port>   Host address and port (default: 127.0.0.1:27015)\n"
              << "  --id <string>         Persistent Participant ID (default: auto-generated)\n"
              << "  --name <string>       Player display name (default: Client_Player)\n"
              << "  --token <string>      Session password / join token\n"
              << "  --auto-input          Generate synthetic continuous input stream\n"
              << "  --sim-loss <percent>  Simulated packet drop percentage (0..100)\n"
              << "  --sim-delay <ms>      Simulated latency in ms\n"
              << "  --help                Show this help message\n"
              << "\nInteractive Console Commands:\n"
              << "  move <fwd> <strafe>   Send movement input vector\n"
              << "  look <yaw> <pitch>    Send orientation angles\n"
              << "  shoot                 Fire equipped weapon\n"
              << "  respawn / e           Trigger manual respawn on 'E'\n"
              << "  status                Display local slot and world state\n"
              << "  stat                  Print latency (RTT p50/p95/p99) and packet stats\n"
              << "  reconnect             Disconnect and reconnect with same participantId\n"
              << "  quit / q              Disconnect and exit\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    ClientConfig config;
    bool bAutoInput = false;

    // Generate pseudo-random default participant ID
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(10000, 99999);
    config.participantId = "client_" + std::to_string(dist(gen));
    config.displayName = "Player_" + std::to_string(dist(gen) % 100);

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "--connect" && i + 1 < argc) {
            std::string fullAddr = argv[++i];
            size_t colon = fullAddr.find(':');
            if (colon != std::string::npos) {
                config.hostAddress = fullAddr.substr(0, colon);
                config.hostPort = static_cast<uint16_t>(std::stoi(fullAddr.substr(colon + 1)));
            } else {
                config.hostAddress = fullAddr;
            }
        } else if (arg == "--id" && i + 1 < argc) {
            config.participantId = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            config.displayName = argv[++i];
        } else if (arg == "--token" && i + 1 < argc) {
            config.joinToken = argv[++i];
        } else if (arg == "--auto-input") {
            bAutoInput = true;
        } else if (arg == "--sim-loss" && i + 1 < argc) {
            config.simDropPercent = std::stof(argv[++i]);
        } else if (arg == "--sim-delay" && i + 1 < argc) {
            config.simLatencyMs = static_cast<uint32_t>(std::stoi(argv[++i]));
        }
    }

    std::cout << "=========================================================\n"
              << "  PowerSlave Exhumed Together - Standalone ENet Client\n"
              << "=========================================================\n";

    ENetClientNode client;
    if (!client.connect(config)) {
        std::cerr << "[Client] Connection initialization failed." << std::endl;
        return 1;
    }

    std::atomic<bool> bRun{ true };
    std::atomic<bool> bSimulateInput{ bAutoInput };

    // Network polling thread
    std::thread netThread([&]() {
        float angle = 0.0f;
        while (bRun.load()) {
            client.poll(2);

            if (bSimulateInput.load() && client.isAuthenticated()) {
                const auto* local = client.getLocalPlayerState();
                if (local && local->lifeState == LIFE_ALIVE) {
                    angle += 1.0f;
                    if (angle >= 360.0f) angle = 0.0f;
                    client.sendInputCommand(1.0f, 0.0f, 0.0f, angle, 0, WEAPON_PISTOL);
                } else if (local && local->lifeState == LIFE_DEAD) {
                    // Auto respawn test: request manual respawn on 'E'
                    client.triggerManualRespawn();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 Hz input rate
        }
    });

    std::cout << "[Client] Type 'status', 'move <fwd> <strafe>', 'shoot', 'respawn', 'stat', or 'quit'." << std::endl;

    std::string line;
    while (bRun.load() && std::getline(std::cin, line)) {
        if (line.empty()) continue;
        if (line == "quit" || line == "q" || line == "exit") {
            break;
        } else if (line == "status") {
            std::cout << "--- Client Status ---\n"
                      << "Host: " << config.hostAddress << ":" << config.hostPort << "\n"
                      << "Connected: " << (client.isConnected() ? "YES" : "NO")
                      << " | Authenticated: " << (client.isAuthenticated() ? "YES" : "NO") << "\n"
                      << "Assigned Slot: P" << (int)(client.getAssignedSlot() + 1)
                      << " | Generation: " << client.getConnectionGeneration()
                      << " | Map: " << client.getMapName()
                      << " (Epoch: " << client.getMapEpoch() << ")\n";

            const auto* local = client.getLocalPlayerState();
            if (local) {
                std::cout << "Local State: "
                          << (local->lifeState == LIFE_ALIVE ? "ALIVE" : (local->lifeState == LIFE_DEAD ? "DEAD" : "RESPAWNING"))
                          << " | HP: " << local->health
                          << " | Armor: " << local->armor
                          << " | Weapon: " << (int)local->activeWeapon
                          << " | Ammo: " << local->ammo
                          << " | Pos: (" << local->posX << ", " << local->posY << ", " << local->posZ << ")\n";
            } else {
                std::cout << "Local State: [Waiting for initial snapshot]\n";
            }

            const auto& allPlayers = client.getLastSnapshotPlayers();
            std::cout << "Active World Entities (" << allPlayers.size() << " players visible):\n";
            for (const auto& p : allPlayers) {
                std::cout << "  Slot P" << (int)(p.slotId + 1)
                          << (p.slotId == client.getAssignedSlot() ? " [YOU]" : "")
                          << " HP=" << p.health
                          << " Pos=(" << p.posX << ", " << p.posY << ", " << p.posZ << ")\n";
            }
        } else if (line == "respawn" || line == "e" || line == "E") {
            if (client.triggerManualRespawn()) {
                std::cout << "[Client] Dispatched manual respawn request on 'E'." << std::endl;
            } else {
                std::cout << "[Client] Cannot respawn: player is not dead or request already pending." << std::endl;
            }
        } else if (line == "shoot") {
            client.sendInputCommand(0.0f, 0.0f, 0.0f, 0.0f, BUTTON_ATTACK, WEAPON_PISTOL);
            std::cout << "[Client] Sent attack command." << std::endl;
        } else if (line.rfind("move ", 0) == 0) {
            try {
                size_t space = line.find(' ', 5);
                float fwd = std::stof(line.substr(5, space - 5));
                float right = (space != std::string::npos) ? std::stof(line.substr(space + 1)) : 0.0f;
                client.sendInputCommand(fwd, right, 0.0f, 0.0f, 0, WEAPON_PISTOL);
                std::cout << "[Client] Sent movement (" << fwd << ", " << right << ")" << std::endl;
            } catch (...) {
                std::cout << "Usage: move <fwd> <strafe>" << std::endl;
            }
        } else if (line == "stat") {
            const auto& m = client.getMetrics();
            auto rtt = m.calculateRttStats();
            SystemUsage usage = MetricsCollector::querySystemUsage();
            std::cout << "--- Client Metrics ---\n"
                      << "RTT: avg=" << rtt.avg << "ms, p50=" << rtt.p50
                      << "ms, p95=" << rtt.p95 << "ms, p99=" << rtt.p99
                      << "ms (samples: " << rtt.sampleCount << ")\n"
                      << "Packets Sent: " << m.getTotalPacketsSent()
                      << " | Received: " << m.getTotalPacketsReceived() << "\n"
                      << "Bytes Sent: " << m.getTotalBytesSent()
                      << " | Received: " << m.getTotalBytesReceived() << "\n"
                      << "CPU: " << usage.cpuPercent << " %, RAM: " << (usage.ramWorkingSetBytes / (1024 * 1024)) << " MB\n";
        } else if (line == "reconnect") {
            std::cout << "[Client] Performing reconnect..." << std::endl;
            client.disconnect(LEAVE_USER_QUIT);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            client.connect(config);
        } else {
            std::cout << "Unknown command: " << line << std::endl;
        }
    }

    bRun.store(false);
    if (netThread.joinable()) {
        netThread.join();
    }
    client.disconnect(LEAVE_USER_QUIT);

    return 0;
}
