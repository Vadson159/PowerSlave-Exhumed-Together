// CADENCE QA HOST & INPUT TIMEOUT ANALYZER
// Purpose: Independent QA test host for measuring command cadence, network jitter,
// and input timeout sensitivity (DOC-45 P2-2).

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <string>

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

struct PacketEvent {
    size_t packetIndex = 0;
    uint64_t timestampUs = 0;
    int64_t deltaUs = 0;
    uint32_t wireSeq = 0;
    uint32_t clientTick = 0;
    uint8_t slotId = 0;
    uint32_t gen = 0;
    float forward = 0.0f;
    uint64_t arrivalHostTick = 0;
};

struct DropoutDetail {
    uint64_t startTick = 0;
    uint64_t endTick = 0;
    uint64_t durationTicks = 0;
    double durationMs = 0.0;
    uint64_t ticksSinceLast = 0;
    uint64_t wallMsSinceLast = 0;
    std::string triggerReason; // "TICK_ONLY", "WALL_CLOCK_ONLY", "BOTH"
    float forwardBefore = 0.0f;
    float forwardAfter = 0.0f;
};

struct ThresholdCandidate {
    uint32_t thresholdTicks = 0;
    uint32_t thresholdMs = 0;
    uint32_t dropoutCount = 0;
    uint64_t totalDropoutTicks = 0;
    uint32_t triggeredTickOnly = 0;
    uint32_t triggeredWallOnly = 0;
    uint32_t triggeredBoth = 0;
    bool active = false;
    uint64_t activeStartTick = 0;
    std::vector<DropoutDetail> events;
};

class CadenceQaHost {
public:
    CadenceQaHost(uint16_t port, double durationSec, const std::string& summaryPath, const std::string& rawCsvPath, uint32_t simHz = 60, uint32_t timeoutTicks = 6, uint32_t timeoutMs = 150)
        : m_port(port)
        , m_durationSec(durationSec)
        , m_summaryPath(summaryPath)
        , m_rawCsvPath(rawCsvPath)
        , m_simHz(simHz)
        , m_timeoutTicks(timeoutTicks)
        , m_timeoutMs(timeoutMs)
    {}

    bool run() {
        if (enet_initialize() != 0) {
            std::cerr << "[QA Host] Failed to initialize ENet" << std::endl;
            return false;
        }

        timeBeginPeriod(1); // ENet standard

        ENetAddress address;
        address.host = ENET_HOST_ANY;
        address.port = m_port;

        m_enetHost = enet_host_create(&address, 8, pse::CHANNEL_COUNT, 0, 0);
        if (!m_enetHost) {
            std::cerr << "[QA Host] Failed to bind ENet host on port " << m_port << std::endl;
            timeEndPeriod(1);
            enet_deinitialize();
            return false;
        }

        std::cout << "[QA Host] Listening on port " << m_port << " (SimTick: " << m_simHz << "Hz, Timeout: " << m_timeoutTicks << " ticks / " << m_timeoutMs << " ms)" << std::endl;

        m_running.store(true);
        m_sessionStartTime = steady_clock::now();

        // Start 60 Hz simulation thread
        std::thread simThread(&CadenceQaHost::simulationLoop, this);

        auto testDeadline = m_sessionStartTime + milliseconds(static_cast<int64_t>(m_durationSec * 1000.0));

        ENetEvent event;
        while (m_running.load() && steady_clock::now() < testDeadline) {
            int serviceRes = enet_host_service(m_enetHost, &event, 1);
            // Non-blocking event drain loop
            while (serviceRes > 0) {
                m_totalEnetEventsReceived++;
                switch (event.type) {
                    case ENET_EVENT_TYPE_CONNECT:
                        std::cout << "[QA Host] Client connected from peer " << event.peer->address.host << std::endl;
                        m_clientConnected.store(true);
                        m_connectionSeen.store(true);
                        break;
                    case ENET_EVENT_TYPE_RECEIVE:
                        handlePacket(event.peer, event.packet, event.channelID);
                        enet_packet_destroy(event.packet);
                        break;
                    case ENET_EVENT_TYPE_DISCONNECT:
                        std::cout << "[QA Host] Client disconnected" << std::endl;
                        m_clientConnected.store(false);
                        break;
                    default:
                        break;
                }
                serviceRes = enet_host_check_events(m_enetHost, &event);
            }
        }

        std::cout << "[QA Host] Test duration reached. Shutting down..." << std::endl;
        m_running.store(false);

        if (simThread.joinable()) {
            simThread.join();
        }

        if (m_enetHost) {
            m_totalUdpPacketsReceived = m_enetHost->totalReceivedPackets;
            enet_host_flush(m_enetHost);
            enet_host_destroy(m_enetHost);
            m_enetHost = nullptr;
        }

        timeEndPeriod(1);
        enet_deinitialize();

        processAndExportResults();
        return true;
    }

private:
    void handlePacket(ENetPeer* peer, ENetPacket* packet, uint8_t /*channel*/) {
        if (!packet || packet->dataLength < pse::MsgHeader::SERIALIZED_SIZE) return;

        auto recvTime = steady_clock::now();
        uint64_t timestampUs = duration_cast<microseconds>(recvTime - m_sessionStartTime).count();

        pse::MsgHeader header;
        pse::ByteReader reader(packet->data, packet->dataLength);
        if (!header.deserialize(reader)) return;
        if (header.magic != pse::NET_MAGIC) return;

        if (header.msgType == pse::MSG_HELLO) {
            m_controlPacketsReceived++;
            pse::MsgHello hello;
            if (hello.deserialize(reader)) {
                std::cout << "[QA Host] Received MSG_HELLO: participant=" << hello.participantId << std::endl;
                // Send MSG_WELCOME
                pse::MsgWelcome welcome;
                welcome.protocolVersion = pse::NET_PROTOCOL_VERSION;
                welcome.sessionId = 1;
                welcome.assignedSlot = 1; // P2
                welcome.connectionGeneration = 1;
                welcome.mapEpoch = 1;
                welcome.serverTickRate = static_cast<uint16_t>(m_simHz);
                welcome.serverSnapshotRate = 30;
                welcome.mapName = "M12A_INPUT_ONLY";
                welcome.reconnectKey = 0x123456789ABCDEF0ULL;

                auto encWelcome = pse::encodePacket(pse::MSG_WELCOME, ++m_hostSeq, welcome);
                sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encWelcome, true);

                // Send MSG_MAP_CHANGE
                pse::MsgMapChange mapChange;
                mapChange.newMapEpoch = 1;
                mapChange.mapName = "M12A_INPUT_ONLY";
                auto encMap = pse::encodePacket(pse::MSG_MAP_CHANGE, ++m_hostSeq, mapChange);
                sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encMap, true);
                enet_host_flush(m_enetHost);
                std::cout << "[QA Host] Sent MSG_WELCOME and MSG_MAP_CHANGE(M12A_INPUT_ONLY)" << std::endl;
            }
        } else if (header.msgType == pse::MSG_MAP_READY) {
            m_controlPacketsReceived++;
            pse::MsgMapReady ready;
            if (ready.deserialize(reader)) {
                std::cout << "[QA Host] Received MSG_MAP_READY from slot " << static_cast<int>(ready.slotId) << std::endl;
                // Send MSG_WORLD_BASELINE
                pse::MsgWorldBaseline baseline;
                baseline.mapEpoch = 1;
                baseline.mapName = "M12A_INPUT_ONLY";
                baseline.serverTick = static_cast<uint32_t>(m_currentTick.load());
                baseline.totalBatches = 1;
                baseline.batchIndex = 0;
                auto encBaseline = pse::encodePacket(pse::MSG_WORLD_BASELINE, ++m_hostSeq, baseline);
                sendPacket(peer, pse::CHANNEL_RELIABLE_CONTROL, encBaseline, true);
                enet_host_flush(m_enetHost);
                std::cout << "[QA Host] Sent MSG_WORLD_BASELINE. Client ready for input commands." << std::endl;
            }
        } else if (header.msgType == pse::MSG_INPUT_COMMAND) {
            pse::MsgInputCommand cmd;
            if (cmd.deserialize(reader)) {
                m_inputCommandsAccepted++;
                int64_t deltaUs = 0;
                if (m_lastRecvUs > 0) {
                    deltaUs = timestampUs - m_lastRecvUs;
                }
                m_lastRecvUs = timestampUs;

                PacketEvent ev;
                ev.packetIndex = ++m_packetCounter;
                ev.timestampUs = timestampUs;
                ev.deltaUs = deltaUs;
                ev.wireSeq = header.sequence;
                ev.clientTick = cmd.clientTick;
                ev.slotId = cmd.slotId;
                ev.gen = cmd.connectionGeneration;
                ev.forward = cmd.moveForward;
                ev.arrivalHostTick = m_currentTick.load();

                {
                    std::lock_guard<std::mutex> lock(m_telemetryMutex);
                    m_packetEvents.push_back(ev);
                    m_inboundQueue.push_back(cmd);
                }
            }
        }
    }

    void sendPacket(ENetPeer* peer, uint8_t channel, const std::vector<uint8_t>& data, bool reliable) {
        if (!peer) return;
        enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : 0;
        ENetPacket* packet = enet_packet_create(data.data(), data.size(), flags);
        if (packet) {
            enet_peer_send(peer, channel, packet);
        }
    }

    void simulationLoop() {
        const auto tickInterval = microseconds(1000000 / m_simHz);
        auto nextTickTime = steady_clock::now() + tickInterval;

        uint64_t lastInputTick = 0;
        ULONGLONG lastInputTickCount = 0;
        uint32_t buttons = 0;
        float currentForward = 0.0f;
        uint32_t lastSeq = 0;

        // Specific candidate thresholds requested in audit:
        // 1. 6 ticks / 150 ms (Production Baseline)
        // 2. 8 ticks / 150 ms
        // 3. 10 ticks / 150 ms
        // 4. 10 ticks / 170 ms
        // 5. 12 ticks / 200 ms
        std::vector<ThresholdCandidate> candidates = {
            {6, 150},
            {8, 150},
            {10, 150},
            {10, 170},
            {12, 200}
        };

        while (m_running.load()) {
            m_currentTick++;
            uint64_t nowTick = m_currentTick.load();
            ULONGLONG nowMs = GetTickCount64();

            std::vector<pse::MsgInputCommand> batch;
            {
                std::lock_guard<std::mutex> lock(m_telemetryMutex);
                while (!m_inboundQueue.empty()) {
                    batch.push_back(m_inboundQueue.front());
                    m_inboundQueue.pop_front();
                }
            }

            bool hadInputThisTick = !batch.empty();
            if (hadInputThisTick) {
                const auto& lastCmd = batch.back();
                lastInputTick = nowTick;
                lastInputTickCount = nowMs;
                lastSeq = lastCmd.commandSequence;
                currentForward = lastCmd.moveForward;
                buttons = (currentForward > 0.5f) ? 4u : 0u;

                // Close any active dropouts on candidates
                for (auto& cand : candidates) {
                    if (cand.active) {
                        cand.active = false;
                        if (!cand.events.empty()) {
                            cand.events.back().endTick = nowTick;
                            cand.events.back().durationTicks = nowTick - cand.events.back().startTick;
                            cand.events.back().durationMs = cand.events.back().durationTicks * (1000.0 / m_simHz);
                            cand.events.back().forwardAfter = currentForward;
                        }
                    }
                }
            } else if (m_clientConnected.load() && lastInputTick > 0 && buttons != 0) {
                // Client is actively connected and holding input: evaluate timeout OR-semantics
                uint64_t ticksSince = nowTick - lastInputTick;
                uint64_t msSince = nowMs - lastInputTickCount;

                for (auto& cand : candidates) {
                    bool tickCondition = (ticksSince >= cand.thresholdTicks);
                    bool wallCondition = (msSince >= cand.thresholdMs);
                    bool timedOut = (tickCondition || wallCondition);

                    if (timedOut && !cand.active) {
                        cand.active = true;
                        cand.activeStartTick = nowTick;
                        cand.dropoutCount++;

                        std::string reason;
                        if (tickCondition && wallCondition) {
                            cand.triggeredBoth++;
                            reason = "BOTH";
                        } else if (tickCondition) {
                            cand.triggeredTickOnly++;
                            reason = "TICK_ONLY";
                        } else {
                            cand.triggeredWallOnly++;
                            reason = "WALL_CLOCK_ONLY";
                        }

                        DropoutDetail d;
                        d.startTick = nowTick;
                        d.endTick = nowTick;
                        d.ticksSinceLast = ticksSince;
                        d.wallMsSinceLast = msSince;
                        d.triggerReason = reason;
                        d.forwardBefore = currentForward;
                        d.forwardAfter = 0.0f;
                        cand.events.push_back(d);
                    }

                    if (cand.active) {
                        cand.totalDropoutTicks++;
                    }
                }

                // Baseline neutralization effect
                if (candidates[0].active) {
                    buttons = 0; // Neutralized!
                }
            } else if (!m_clientConnected.load()) {
                // Disconnected: reset buttons without incrementing false dropout counters
                buttons = 0;
                for (auto& cand : candidates) {
                    cand.active = false;
                }
            }

            // High-precision pacing
            auto now = steady_clock::now();
            if (nextTickTime > now) {
                auto rem = duration_cast<milliseconds>(nextTickTime - now);
                if (rem.count() > 2) {
                    Sleep(1);
                }
                while (steady_clock::now() < nextTickTime) {
                    YieldProcessor();
                }
            }
            nextTickTime += tickInterval;
        }

        m_candidatesSummary = candidates;
    }

    void processAndExportResults() {
        std::lock_guard<std::mutex> lock(m_telemetryMutex);

        std::cout << "\n================================================================================" << std::endl;
        std::cout << "  CADENCE QA HOST: AUDIT EXECUTION SUMMARY" << std::endl;
        std::cout << "================================================================================" << std::endl;

        if (m_packetEvents.empty()) {
            std::cout << "[QA Host] WARNING: No input packets received during test!" << std::endl;
        }

        double totalDurationSec = 0.0;
        if (m_packetEvents.size() >= 2) {
            totalDurationSec = (m_packetEvents.back().timestampUs - m_packetEvents.front().timestampUs) / 1000000.0;
        }
        if (totalDurationSec <= 0.001) totalDurationSec = 0.001;

        double effectiveHz = static_cast<double>(m_packetEvents.size()) / totalDurationSec;

        std::vector<double> deltasMs;
        deltasMs.reserve(m_packetEvents.size());
        for (size_t i = 1; i < m_packetEvents.size(); ++i) {
            deltasMs.push_back(m_packetEvents[i].deltaUs / 1000.0);
        }

        std::vector<double> sortedDeltas = deltasMs;
        std::sort(sortedDeltas.begin(), sortedDeltas.end());

        double minMs = sortedDeltas.empty() ? 0.0 : sortedDeltas.front();
        double maxMs = sortedDeltas.empty() ? 0.0 : sortedDeltas.back();
        double meanMs = sortedDeltas.empty() ? 0.0 : std::accumulate(sortedDeltas.begin(), sortedDeltas.end(), 0.0) / (sortedDeltas.empty() ? 1 : sortedDeltas.size());

        double sqSum = 0.0;
        for (double v : sortedDeltas) {
            double d = v - meanMs;
            sqSum += d * d;
        }
        double stddevMs = sortedDeltas.empty() ? 0.0 : std::sqrt(sqSum / (sortedDeltas.empty() ? 1 : sortedDeltas.size()));

        double p50Ms = sortedDeltas.empty() ? 0.0 : sortedDeltas[sortedDeltas.size() * 50 / 100];
        double p95Ms = sortedDeltas.empty() ? 0.0 : sortedDeltas[sortedDeltas.size() * 95 / 100];
        double p99Ms = sortedDeltas.empty() ? 0.0 : sortedDeltas[sortedDeltas.size() * 99 / 100];

        std::map<int, int> tickGaps;
        for (size_t i = 1; i < m_packetEvents.size(); ++i) {
            int gap = static_cast<int>(m_packetEvents[i].arrivalHostTick - m_packetEvents[i - 1].arrivalHostTick);
            tickGaps[gap]++;
        }

        std::cout << "[Accounting] Total UDP Packets: " << m_totalUdpPacketsReceived << std::endl;
        std::cout << "[Accounting] Total ENet Events:  " << m_totalEnetEventsReceived << std::endl;
        std::cout << "[Accounting] Control Packets:    " << m_controlPacketsReceived << std::endl;
        std::cout << "[Accounting] INPUT Accepted:     " << m_inputCommandsAccepted << std::endl;
        std::cout << "[Cadence] Effective Rate:        " << std::fixed << std::setprecision(1) << effectiveHz << " Hz" << std::endl;
        std::cout << "[Arrival Deltas] min=" << minMs << "ms, max=" << maxMs << "ms, mean=" << meanMs << "ms, stddev=" << stddevMs << "ms" << std::endl;
        std::cout << "[Arrival Percentiles] p50=" << p50Ms << "ms, p95=" << p95Ms << "ms, p99=" << p99Ms << "ms" << std::endl;

        std::cout << "\n[Input Timeout Multi-Threshold Evaluation (OR-semantics)]:" << std::endl;
        std::cout << "  Threshold       | Dropouts | Total Ticks | Total ms | Trigger (Tick / Wall / Both)" << std::endl;
        std::cout << "  ----------------+----------+-------------+----------+----------------------------" << std::endl;
        for (const auto& c : m_candidatesSummary) {
            double totalMs = c.totalDropoutTicks * (1000.0 / m_simHz);
            std::cout << "  " << std::setw(3) << c.thresholdTicks << "t / "
                      << std::setw(3) << c.thresholdMs << "ms   | "
                      << std::setw(8) << c.dropoutCount << " | "
                      << std::setw(11) << c.totalDropoutTicks << " | "
                      << std::setw(8) << std::fixed << std::setprecision(1) << totalMs << " | "
                      << "Tick: " << c.triggeredTickOnly << ", Wall: " << c.triggeredWallOnly << ", Both: " << c.triggeredBoth << std::endl;
        }

        // Export Raw CSV
        if (!m_rawCsvPath.empty()) {
            std::ofstream csv(m_rawCsvPath);
            if (csv.is_open()) {
                csv << "packet_idx,timestamp_us,delta_us,delta_ms,wire_seq,client_tick,slot,gen,forward,host_tick\n";
                for (const auto& ev : m_packetEvents) {
                    csv << ev.packetIndex << ","
                        << ev.timestampUs << ","
                        << ev.deltaUs << ","
                        << std::fixed << std::setprecision(3) << (ev.deltaUs / 1000.0) << ","
                        << ev.wireSeq << ","
                        << ev.clientTick << ","
                        << static_cast<int>(ev.slotId) << ","
                        << ev.gen << ","
                        << ev.forward << ","
                        << ev.arrivalHostTick << "\n";
                }
                std::cout << "[CSV Export] Saved raw arrival telemetry to: " << m_rawCsvPath << std::endl;
            }
        }

        // Export JSON Summary
        if (!m_summaryPath.empty()) {
            std::ofstream json(m_summaryPath);
            if (json.is_open()) {
                json << "{\n";
                json << "  \"accounting\": {\n";
                json << "    \"total_udp_received\": " << m_totalUdpPacketsReceived << ",\n";
                json << "    \"total_enet_events\": " << m_totalEnetEventsReceived << ",\n";
                json << "    \"control_packets_received\": " << m_controlPacketsReceived << ",\n";
                json << "    \"input_commands_accepted\": " << m_inputCommandsAccepted << "\n";
                json << "  },\n";
                json << "  \"total_packets\": " << m_packetEvents.size() << ",\n";
                json << "  \"duration_sec\": " << totalDurationSec << ",\n";
                json << "  \"effective_hz\": " << effectiveHz << ",\n";
                json << "  \"min_interval_ms\": " << minMs << ",\n";
                json << "  \"max_interval_ms\": " << maxMs << ",\n";
                json << "  \"mean_interval_ms\": " << meanMs << ",\n";
                json << "  \"stddev_interval_ms\": " << stddevMs << ",\n";
                json << "  \"p50_interval_ms\": " << p50Ms << ",\n";
                json << "  \"p95_interval_ms\": " << p95Ms << ",\n";
                json << "  \"p99_interval_ms\": " << p99Ms << ",\n";
                json << "  \"baseline_dropouts\": " << (m_candidatesSummary.empty() ? 0 : m_candidatesSummary[0].dropoutCount) << ",\n";

                json << "  \"tick_gap_histogram\": {\n";
                size_t gapIdx = 0;
                for (const auto& [gap, cnt] : tickGaps) {
                    json << "    \"" << gap << "\": " << cnt << (gapIdx + 1 < tickGaps.size() ? "," : "") << "\n";
                    gapIdx++;
                }
                json << "  },\n";

                json << "  \"timeout_candidates\": [\n";
                for (size_t i = 0; i < m_candidatesSummary.size(); ++i) {
                    const auto& c = m_candidatesSummary[i];
                    double totalMs = c.totalDropoutTicks * (1000.0 / m_simHz);
                    json << "    {\n";
                    json << "      \"threshold_ticks\": " << c.thresholdTicks << ",\n";
                    json << "      \"threshold_ms\": " << c.thresholdMs << ",\n";
                    json << "      \"dropout_count\": " << c.dropoutCount << ",\n";
                    json << "      \"total_dropout_ticks\": " << c.totalDropoutTicks << ",\n";
                    json << "      \"total_dropout_ms\": " << totalMs << ",\n";
                    json << "      \"triggered_tick_only\": " << c.triggeredTickOnly << ",\n";
                    json << "      \"triggered_wall_only\": " << c.triggeredWallOnly << ",\n";
                    json << "      \"triggered_both\": " << c.triggeredBoth << ",\n";
                    json << "      \"stopping_safety_overshoot_ms\": " << (c.thresholdTicks * (1000.0 / m_simHz)) << ",\n";
                    json << "      \"events\": [\n";
                    for (size_t j = 0; j < c.events.size(); ++j) {
                        const auto& ev = c.events[j];
                        json << "        {\n";
                        json << "          \"start_tick\": " << ev.startTick << ",\n";
                        json << "          \"end_tick\": " << ev.endTick << ",\n";
                        json << "          \"duration_ticks\": " << ev.durationTicks << ",\n";
                        json << "          \"duration_ms\": " << ev.durationMs << ",\n";
                        json << "          \"ticks_since_last\": " << ev.ticksSinceLast << ",\n";
                        json << "          \"wall_ms_since_last\": " << ev.wallMsSinceLast << ",\n";
                        json << "          \"trigger_reason\": \"" << ev.triggerReason << "\",\n";
                        json << "          \"forward_before\": " << ev.forwardBefore << ",\n";
                        json << "          \"forward_after\": " << ev.forwardAfter << "\n";
                        json << "        }" << (j + 1 < c.events.size() ? "," : "") << "\n";
                    }
                    json << "      ]\n";
                    json << "    }" << (i + 1 < m_candidatesSummary.size() ? "," : "") << "\n";
                }
                json << "  ]\n";
                json << "}\n";
                std::cout << "[JSON Export] Saved summary to: " << m_summaryPath << std::endl;
            }
        }
    }

private:
    uint16_t m_port = 28745;
    double m_durationSec = 10.0;
    std::string m_summaryPath;
    std::string m_rawCsvPath;
    uint32_t m_simHz = 60;
    uint32_t m_timeoutTicks = 6;
    uint32_t m_timeoutMs = 150;

    ENetHost* m_enetHost = nullptr;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_clientConnected{false};
    std::atomic<bool> m_connectionSeen{false};
    std::atomic<uint64_t> m_currentTick{0};
    steady_clock::time_point m_sessionStartTime;

    uint32_t m_hostSeq = 0;
    size_t m_packetCounter = 0;
    uint64_t m_lastRecvUs = 0;

    size_t m_totalUdpPacketsReceived = 0;
    size_t m_totalEnetEventsReceived = 0;
    size_t m_controlPacketsReceived = 0;
    size_t m_inputCommandsAccepted = 0;

    std::mutex m_telemetryMutex;
    std::vector<PacketEvent> m_packetEvents;
    std::deque<pse::MsgInputCommand> m_inboundQueue;
    std::vector<ThresholdCandidate> m_candidatesSummary;
};

int main(int argc, char** argv) {
    uint16_t port = 28745;
    double duration = 10.0;
    std::string summaryPath = "cadence_qa_summary.json";
    std::string rawCsvPath = "cadence_qa_raw.csv";
    uint32_t simHz = 60;
    uint32_t timeoutTicks = 6;
    uint32_t timeoutMs = 150;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::stoul(argv[++i]));
        else if (arg == "--duration" && i + 1 < argc) duration = std::stod(argv[++i]);
        else if (arg == "--summary" && i + 1 < argc) summaryPath = argv[++i];
        else if (arg == "--raw" && i + 1 < argc) rawCsvPath = argv[++i];
        else if (arg == "--sim-hz" && i + 1 < argc) simHz = std::stoul(argv[++i]);
        else if (arg == "--timeout-ticks" && i + 1 < argc) timeoutTicks = std::stoul(argv[++i]);
        else if (arg == "--timeout-ms" && i + 1 < argc) timeoutMs = std::stoul(argv[++i]);
    }

    CadenceQaHost host(port, duration, summaryPath, rawCsvPath, simHz, timeoutTicks, timeoutMs);
    if (!host.run()) {
        return 1;
    }
    return 0;
}
