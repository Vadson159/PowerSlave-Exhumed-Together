// DETERMINISTIC UDP NETWORK IMPAIRMENT PROXY
// Purpose: Injects controlled packet loss and jitter with fixed seed for reproducible QA (DOC-45 P2-2).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <deque>
#include <chrono>
#include <random>
#include <algorithm>
#include <string>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")

using namespace std::chrono;

struct DelayedPacket {
    std::vector<uint8_t> data;
    sockaddr_in destAddr;
    steady_clock::time_point releaseTime;
};

int main(int argc, char** argv) {
    uint16_t listenPort = 28745;
    uint16_t targetPort = 28746;
    float dropPercent = 0.0f;
    int32_t latencyMs = 0;
    int32_t jitterMs = 0;
    uint32_t seed = 1337;
    double durationSec = 15.0;
    std::string statsOutPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen" && i + 1 < argc) listenPort = static_cast<uint16_t>(std::stoul(argv[++i]));
        else if (arg == "--target" && i + 1 < argc) targetPort = static_cast<uint16_t>(std::stoul(argv[++i]));
        else if (arg == "--drop" && i + 1 < argc) dropPercent = std::stof(argv[++i]);
        else if (arg == "--latency" && i + 1 < argc) latencyMs = std::stoi(argv[++i]);
        else if (arg == "--jitter" && i + 1 < argc) jitterMs = std::stoi(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::stoul(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc) durationSec = std::stod(argv[++i]);
        else if (arg == "--stats-out" && i + 1 < argc) statsOutPath = argv[++i];
    }

    std::cout << "================================================================================" << std::endl;
    std::cout << "  DETERMINISTIC UDP NETWORK IMPAIRMENT PROXY" << std::endl;
    std::cout << "  Listen: " << listenPort << " -> Target: " << targetPort << std::endl;
    std::cout << "  Loss: " << dropPercent << "%, Latency: " << latencyMs << "ms, Jitter: " << jitterMs << "ms (Seed: " << seed << ")" << std::endl;
    std::cout << "================================================================================" << std::endl;

    timeBeginPeriod(1);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[Proxy] WSAStartup failed" << std::endl;
        timeEndPeriod(1);
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "[Proxy] Failed to create UDP socket" << std::endl;
        WSACleanup();
        timeEndPeriod(1);
        return 1;
    }

    // Set non-blocking
    u_long nonBlocking = 1;
    ioctlsocket(sock, FIONBIO, &nonBlocking);

    sockaddr_in bindAddr{};
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_port = htons(listenPort);
    bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, reinterpret_cast<sockaddr*>(&bindAddr), sizeof(bindAddr)) == SOCKET_ERROR) {
        std::cerr << "[Proxy] Failed to bind socket on port " << listenPort << std::endl;
        closesocket(sock);
        WSACleanup();
        timeEndPeriod(1);
        return 1;
    }

    sockaddr_in targetAddr{};
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(targetPort);
    inet_pton(AF_INET, "127.0.0.1", &targetAddr.sin_addr);

    sockaddr_in clientAddr{};
    bool hasClient = false;

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dropDist(0.0f, 100.0f);

    std::deque<DelayedPacket> delayQueue;

    size_t totalClientUdpPackets = 0;
    size_t clientControlUdpPackets = 0;
    size_t clientInputUdpPackets = 0;
    size_t clientInputDropped = 0;
    size_t clientInputForwarded = 0;
    size_t clientInputDelayed = 0;
    size_t totalHostUdpPackets = 0;

    auto startTime = steady_clock::now();
    auto deadline = startTime + milliseconds(static_cast<int64_t>(durationSec * 1000.0));

    std::vector<uint8_t> buffer(65536);

    while (steady_clock::now() < deadline) {
        // 1. Drain incoming UDP packets completely (non-blocking loop)
        while (true) {
            sockaddr_in fromAddr{};
            int fromLen = sizeof(fromAddr);
            int bytesRecv = recvfrom(sock, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&fromAddr), &fromLen);

            if (bytesRecv <= 0) {
                break; // Socket drained
            }

            bool isFromHost = (ntohs(fromAddr.sin_port) == targetPort);

            if (isFromHost) {
                // Packet from Host -> Client
                totalHostUdpPackets++;
                if (hasClient) {
                    sendto(sock, reinterpret_cast<const char*>(buffer.data()), bytesRecv, 0, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr));
                }
            } else {
                // Packet from Client -> Host
                totalClientUdpPackets++;
                if (!hasClient || clientAddr.sin_port != fromAddr.sin_port) {
                    clientAddr = fromAddr;
                    hasClient = true;
                    std::cout << "[Proxy] Registered client from port " << ntohs(clientAddr.sin_port) << std::endl;
                }

                // Parse ENet protocol header correctly
                bool isChannel0OrControl = false;
                if (bytesRecv >= 4) {
                    uint16_t netPeerId = *reinterpret_cast<const uint16_t*>(buffer.data());
                    uint16_t hostPeerId = ntohs(netPeerId);
                    size_t headerSize = (hostPeerId & 0x8000) ? 4 : 2; // ENET_PROTOCOL_HEADER_FLAG_SENT_TIME = (1 << 15)

                    if (bytesRecv >= headerSize + 2) {
                        uint8_t command = buffer[headerSize] & 0x0F;
                        uint8_t channelId = buffer[headerSize + 1];

                        // Acknowledges (1), Connect (2), Verify (3), Disconnect (4), Ping (5) are always control
                        if (command <= 5 || channelId == 0) {
                            isChannel0OrControl = true;
                        }
                    }
                }

                if (isChannel0OrControl) {
                    // Pass reliable control packets immediately without loss or jitter
                    clientControlUdpPackets++;
                    sendto(sock, reinterpret_cast<const char*>(buffer.data()), bytesRecv, 0, reinterpret_cast<sockaddr*>(&targetAddr), sizeof(targetAddr));
                } else {
                    // Channel 1: Unreliable input packets
                    clientInputUdpPackets++;

                    // Check packet loss
                    if (dropPercent > 0.0f && dropDist(rng) < dropPercent) {
                        clientInputDropped++;
                        // Discard packet intentionally!
                    } else if (latencyMs > 0 || jitterMs > 0) {
                        clientInputDelayed++;
                        int32_t j = 0;
                        if (jitterMs > 0) {
                            std::uniform_int_distribution<int32_t> jDist(-jitterMs, jitterMs);
                            j = jDist(rng);
                        }
                        int32_t totalDelay = (std::max)(0, latencyMs + j);
                        DelayedPacket dp;
                        dp.data.assign(buffer.begin(), buffer.begin() + bytesRecv);
                        dp.destAddr = targetAddr;
                        dp.releaseTime = steady_clock::now() + milliseconds(totalDelay);
                        delayQueue.push_back(std::move(dp));
                    } else {
                        // Direct forward
                        clientInputForwarded++;
                        sendto(sock, reinterpret_cast<const char*>(buffer.data()), bytesRecv, 0, reinterpret_cast<sockaddr*>(&targetAddr), sizeof(targetAddr));
                    }
                }
            }
        }

        // 2. Dispatch delayed packets
        auto now = steady_clock::now();
        size_t idx = 0;
        while (idx < delayQueue.size()) {
            if (now >= delayQueue[idx].releaseTime) {
                auto& dp = delayQueue[idx];
                clientInputForwarded++;
                sendto(sock, reinterpret_cast<const char*>(dp.data.data()), static_cast<int>(dp.data.size()), 0, reinterpret_cast<sockaddr*>(&dp.destAddr), sizeof(dp.destAddr));
                delayQueue.erase(delayQueue.begin() + idx);
            } else {
                ++idx;
            }
        }

        Sleep(1);
    }

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  PROXY METRICS:" << std::endl;
    std::cout << "  Client UDP Total:    " << totalClientUdpPackets << std::endl;
    std::cout << "    - Control Packets: " << clientControlUdpPackets << std::endl;
    std::cout << "    - Input Packets:   " << clientInputUdpPackets << std::endl;
    std::cout << "    - Input Dropped:   " << clientInputDropped << " (" << (clientInputUdpPackets ? (clientInputDropped * 100.0 / clientInputUdpPackets) : 0.0) << "%)" << std::endl;
    std::cout << "    - Input Forwarded: " << clientInputForwarded << std::endl;
    std::cout << "    - Input Delayed:   " << clientInputDelayed << std::endl;
    std::cout << "  Host UDP Total:      " << totalHostUdpPackets << std::endl;
    std::cout << "================================================================================" << std::endl;

    if (!statsOutPath.empty()) {
        std::ofstream out(statsOutPath);
        if (out.is_open()) {
            out << "{\n"
                << "  \"client_udp_total\": " << totalClientUdpPackets << ",\n"
                << "  \"client_control_udp\": " << clientControlUdpPackets << ",\n"
                << "  \"client_input_udp\": " << clientInputUdpPackets << ",\n"
                << "  \"client_input_dropped\": " << clientInputDropped << ",\n"
                << "  \"client_input_forwarded\": " << clientInputForwarded << ",\n"
                << "  \"client_input_delayed\": " << clientInputDelayed << ",\n"
                << "  \"host_udp_total\": " << totalHostUdpPackets << ",\n"
                << "  \"drop_percent_configured\": " << dropPercent << ",\n"
                << "  \"actual_drop_percent\": " << (clientInputUdpPackets ? (clientInputDropped * 100.0 / clientInputUdpPackets) : 0.0) << "\n"
                << "}\n";
            std::cout << "[Proxy] Exported stats to: " << statsOutPath << std::endl;
        }
    }

    closesocket(sock);
    WSACleanup();
    timeEndPeriod(1);
    return 0;
}
