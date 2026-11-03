// HIGH-PRECISION 60 HZ CONTROL GENERATOR CLIENT
// Purpose: Independent control reference generator targeting exact 60 Hz cadence
// with steady_clock and interval accumulation (DOC-45 P2-2).

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include <emmintrin.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmsystem.h>

#include "net_protocol.h"
#include "replication_protocol.h"
#include <enet/enet.h>
#include "timer_resolution.hpp"

using namespace std::chrono;

struct SentPacketRecord {
    uint32_t seq = 0;
    uint64_t timestampUs = 0;
    int64_t deltaUs = 0;
    float forward = 0.0f;
};

class PrecisionClient {
public:
    PrecisionClient(const std::string& hostIp, uint16_t hostPort, double rateHz, double durationSec, const std::string& rawOutCsv, const std::string& statsOutJson)
        : m_hostIp(hostIp)
        , m_hostPort(hostPort)
        , m_rateHz(rateHz)
        , m_durationSec(durationSec)
        , m_rawOutCsv(rawOutCsv)
        , m_statsOutJson(statsOutJson)
    {}

    bool run() {
        if (enet_initialize() != 0) {
            std::cerr << "[Precision Client] Failed to initialize ENet" << std::endl;
            return false;
        }

        timeBeginPeriod(1);

        m_enetClient = enet_host_create(nullptr, 1, pse::CHANNEL_COUNT, 0, 0);
        if (!m_enetClient) {
            std::cerr << "[Precision Client] Failed to create ENet client host" << std::endl;
            timeEndPeriod(1);
            enet_deinitialize();
            return false;
        }

        ENetAddress address;
        enet_address_set_host(&address, m_hostIp.c_str());
        address.port = m_hostPort;

        m_peer = enet_host_connect(m_enetClient, &address, pse::CHANNEL_COUNT, 0);
        if (!m_peer) {
            std::cerr << "[Precision Client] Failed to connect to " << m_hostIp << ":" << m_hostPort << std::endl;
            enet_host_destroy(m_enetClient);
            timeEndPeriod(1);
            enet_deinitialize();
            return false;
        }

        std::cout << "[Precision Client] Connecting to " << m_hostIp << ":" << m_hostPort << " (Target Cadence: " << m_rateHz << " Hz)..." << std::endl;

        // Step 1: Wait for connection
        bool connected = false;
        ENetEvent event;
        auto connectDeadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < connectDeadline) {
            if (enet_host_service(m_enetClient, &event, 10) > 0) {
                if (event.type == ENET_EVENT_TYPE_CONNECT) {
                    connected = true;
                    std::cout << "[Precision Client] ENet socket connected. Sending MSG_HELLO..." << std::endl;
                    break;
                }
            }
        }

        if (!connected) {
            std::cerr << "[Precision Client] Connection timed out" << std::endl;
            cleanup();
            return false;
        }

        // Send MSG_HELLO
        pse::MsgHello hello;
        hello.clientProtocolVersion = pse::NET_PROTOCOL_VERSION;
        hello.joinToken = "M12A_Loopback";
        hello.participantId = "precision_client";
        hello.reconnectKey = 0;
        auto encHello = pse::encodePacket(pse::MSG_HELLO, ++m_sequence, hello);
        sendPacket(pse::CHANNEL_RELIABLE_CONTROL, encHello, true);
        enet_host_flush(m_enetClient);

        // Step 2: Handshake loop (Wait for WELCOME, MAP_CHANGE, MAP_READY, BASELINE)
        bool authenticated = false;
        bool baselineReceived = false;
        uint8_t assignedSlot = 1;
        uint32_t connectionGeneration = 1;

        auto handshakeDeadline = steady_clock::now() + seconds(5);
        while (steady_clock::now() < handshakeDeadline && !baselineReceived) {
            if (enet_host_service(m_enetClient, &event, 10) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    if (event.packet->dataLength >= pse::MsgHeader::SERIALIZED_SIZE) {
                        pse::MsgHeader header;
                        pse::ByteReader reader(event.packet->data, event.packet->dataLength);
                        if (header.deserialize(reader) && header.magic == pse::NET_MAGIC) {
                            if (header.msgType == pse::MSG_WELCOME) {
                                pse::MsgWelcome welcome;
                                if (welcome.deserialize(reader)) {
                                    assignedSlot = welcome.assignedSlot;
                                    connectionGeneration = welcome.connectionGeneration;
                                    authenticated = true;
                                    std::cout << "[Precision Client] Authenticated! slot=" << (int)assignedSlot << ", gen=" << connectionGeneration << std::endl;
                                }
                            } else if (header.msgType == pse::MSG_MAP_CHANGE) {
                                pse::MsgMapChange mapChange;
                                if (mapChange.deserialize(reader)) {
                                    std::cout << "[Precision Client] Received MSG_MAP_CHANGE(" << mapChange.mapName << "). Sending MSG_MAP_READY..." << std::endl;
                                    pse::MsgMapReady ready;
                                    ready.slotId = assignedSlot;
                                    ready.mapEpoch = mapChange.newMapEpoch;
                                    auto encReady = pse::encodePacket(pse::MSG_MAP_READY, ++m_sequence, ready);
                                    sendPacket(pse::CHANNEL_RELIABLE_CONTROL, encReady, true);
                                    enet_host_flush(m_enetClient);
                                }
                            } else if (header.msgType == pse::MSG_WORLD_BASELINE) {
                                pse::MsgWorldBaseline baseline;
                                if (baseline.deserialize(reader)) {
                                    baselineReceived = true;
                                    std::cout << "[Precision Client] Received MSG_WORLD_BASELINE. Ready for high-precision streaming!" << std::endl;
                                }
                            }
                        }
                    }
                    enet_packet_destroy(event.packet);
                }
            }
        }

        if (!baselineReceived) {
            std::cerr << "[Precision Client] Failed to complete handshake within deadline" << std::endl;
            cleanup();
            return false;
        }

        uint32_t initialUdpSent = m_enetClient->totalSentPackets;
        uint32_t initialThrottle = m_peer ? m_peer->packetThrottle : 32;

        // Step 3: High-precision 60 Hz transmission loop
        const auto interval = nanoseconds(static_cast<int64_t>(1000000000.0 / m_rateHz));
        auto sessionStart = steady_clock::now();
        auto nextSendTime = sessionStart + interval;
        auto prevSendTime = sessionStart;
        auto testDeadline = sessionStart + milliseconds(static_cast<int64_t>(m_durationSec * 1000.0));

        std::vector<SentPacketRecord> sentRecords;
        sentRecords.reserve(static_cast<size_t>(m_rateHz * m_durationSec * 1.2));

        uint32_t inputSeq = 0;
        size_t commandsGenerated = 0;
        size_t commandsEnqueuedSuccess = 0;
        size_t commandsEnqueuedFailed = 0;

        std::cout << "[Precision Client] Streaming input commands at " << m_rateHz << " Hz for " << m_durationSec << " seconds..." << std::endl;

        while (steady_clock::now() < testDeadline) {
            // Drain incoming ENet events non-blocking
            while (enet_host_service(m_enetClient, &event, 0) > 0) {
                if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                    enet_packet_destroy(event.packet);
                }
            }

            auto now = steady_clock::now();
            if (nextSendTime > now) {
                auto rem = duration_cast<milliseconds>(nextSendTime - now);
                if (rem.count() > 2) {
                    Sleep(1);
                }
                while (steady_clock::now() < nextSendTime) {
                    _mm_pause();
                }
            }

            auto sendTime = steady_clock::now();
            uint64_t timestampUs = duration_cast<microseconds>(sendTime - sessionStart).count();
            int64_t deltaUs = duration_cast<microseconds>(sendTime - prevSendTime).count();
            prevSendTime = sendTime;

            commandsGenerated++;

            pse::MsgInputCommand cmd;
            cmd.slotId = assignedSlot;
            cmd.connectionGeneration = connectionGeneration;
            cmd.commandSequence = ++inputSeq;
            cmd.clientTick = inputSeq;
            cmd.moveForward = 1.0f; // Hold W
            cmd.moveRight = 0.0f;
            cmd.pitch = 0.0f;
            cmd.yaw = 0.0f;
            cmd.buttons = 0;
            cmd.selectedWeapon = 0;

            auto encCmd = pse::encodePacket(pse::MSG_INPUT_COMMAND, inputSeq, cmd);
            bool sendOk = sendPacket(pse::CHANNEL_UNRELIABLE_SEQUENCED, encCmd, false);
            if (sendOk) {
                commandsEnqueuedSuccess++;
            } else {
                commandsEnqueuedFailed++;
            }

            // Flush immediately to minimize arbitrary multi-command bundling
            enet_host_flush(m_enetClient);

            SentPacketRecord rec;
            rec.seq = inputSeq;
            rec.timestampUs = timestampUs;
            rec.deltaUs = deltaUs;
            rec.forward = 1.0f;
            sentRecords.push_back(rec);

            // Fixed interval accumulation
            nextSendTime += interval;
        }

        uint32_t finalUdpSent = m_enetClient->totalSentPackets;
        uint32_t finalThrottle = m_peer ? m_peer->packetThrottle : 32;
        uint32_t streamingUdpSent = finalUdpSent - initialUdpSent;

        std::cout << "[Precision Client] Completed streaming " << sentRecords.size() << " commands." << std::endl;
        std::cout << "[Precision Client] ENet UDP datagrams sent during stream: " << streamingUdpSent << std::endl;
        std::cout << "[Precision Client] ENet Throttle: " << initialThrottle << " -> " << finalThrottle << " / 32" << std::endl;

        // Disconnect
        enet_peer_disconnect(m_peer, 0);
        enet_host_flush(m_enetClient);
        std::this_thread::sleep_for(milliseconds(100));

        cleanup();

        // Export Sent CSV
        if (!m_rawOutCsv.empty() && !sentRecords.empty()) {
            std::ofstream csv(m_rawOutCsv);
            if (csv.is_open()) {
                csv << "seq,timestamp_us,delta_us,delta_ms,forward\n";
                for (const auto& r : sentRecords) {
                    csv << r.seq << ","
                        << r.timestampUs << ","
                        << r.deltaUs << ","
                        << std::fixed << std::setprecision(3) << (r.deltaUs / 1000.0) << ","
                        << r.forward << "\n";
                }
                std::cout << "[Precision Client] Saved sent records to: " << m_rawOutCsv << std::endl;
            }
        }

        // Export Client Stats JSON
        if (!m_statsOutJson.empty()) {
            std::ofstream stats(m_statsOutJson);
            if (stats.is_open()) {
                stats << "{\n"
                      << "  \"commands_generated\": " << commandsGenerated << ",\n"
                      << "  \"commands_enqueued_success\": " << commandsEnqueuedSuccess << ",\n"
                      << "  \"commands_enqueued_failed\": " << commandsEnqueuedFailed << ",\n"
                      << "  \"enet_udp_datagrams_sent\": " << streamingUdpSent << ",\n"
                      << "  \"initial_packet_throttle\": " << initialThrottle << ",\n"
                      << "  \"final_packet_throttle\": " << finalThrottle << ",\n"
                      << "  \"target_rate_hz\": " << m_rateHz << ",\n"
                      << "  \"duration_sec\": " << m_durationSec << "\n"
                      << "}\n";
                std::cout << "[Precision Client] Saved client stats to: " << m_statsOutJson << std::endl;
            }
        }

        return true;
    }

private:
    bool sendPacket(uint8_t channel, const std::vector<uint8_t>& data, bool reliable) {
        if (!m_peer) return false;
        enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
        ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
        if (packet) {
            int res = enet_peer_send(m_peer, channel, packet);
            return (res == 0);
        }
        return false;
    }

    void cleanup() {
        if (m_enetClient) {
            enet_host_destroy(m_enetClient);
            m_enetClient = nullptr;
            m_peer = nullptr;
        }
        timeEndPeriod(1);
        enet_deinitialize();
    }

    std::string m_hostIp = "127.0.0.1";
    uint16_t m_hostPort = 28745;
    double m_rateHz = 60.0;
    double m_durationSec = 10.0;
    std::string m_rawOutCsv;
    std::string m_statsOutJson;

    ENetHost* m_enetClient = nullptr;
    ENetPeer* m_peer = nullptr;
    uint32_t m_sequence = 0;
};

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    uint16_t port = 28745;
    double rateHz = 60.0;
    double durationSec = 10.0;
    std::string rawOutCsv = "precision_client_sent.csv";
    std::string statsOutJson;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) host = argv[++i];
        else if (arg == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::stoul(argv[++i]));
        else if (arg == "--rate" && i + 1 < argc) rateHz = std::stod(argv[++i]);
        else if (arg == "--duration" && i + 1 < argc) durationSec = std::stod(argv[++i]);
        else if (arg == "--raw-out" && i + 1 < argc) rawOutCsv = argv[++i];
        else if (arg == "--stats-out" && i + 1 < argc) statsOutJson = argv[++i];
    }

    PrecisionClient client(host, port, rateHz, durationSec, rawOutCsv, statsOutJson);
    if (!client.run()) {
        return 1;
    }
    return 0;
}
