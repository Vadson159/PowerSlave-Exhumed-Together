#include "m12a_report.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>

namespace pse::m12a {

const char* checkStatusToString(CheckStatus st) {
    switch (st) {
        case CheckStatus::STATUS_PASS:     return "PASS";
        case CheckStatus::STATUS_FAIL:     return "FAIL";
        case CheckStatus::STATUS_DEFERRED: return "DEFERRED";
        case CheckStatus::STATUS_SKIPPED:  return "SKIPPED";
        default:                           return "UNKNOWN";
    }
}

TestReport::TestReport() = default;

void TestReport::setTarget(const std::string& host, uint16_t port, const std::string& participantId) {
    m_targetHost = host;
    m_targetPort = port;
    m_participantId = participantId;
}

void TestReport::addPass(const std::string& id, const std::string& name, const std::string& detail, double ms) {
    m_results.push_back({ id, name, CheckStatus::STATUS_PASS, detail, ms });
}

void TestReport::addFail(const std::string& id, const std::string& name, const std::string& detail, double ms) {
    m_results.push_back({ id, name, CheckStatus::STATUS_FAIL, detail, ms });
}

void TestReport::addDeferred(const std::string& id, const std::string& name, const std::string& reason) {
    m_results.push_back({ id, name, CheckStatus::STATUS_DEFERRED, reason, 0.0 });
}

void TestReport::addSkipped(const std::string& id, const std::string& name, const std::string& reason) {
    m_results.push_back({ id, name, CheckStatus::STATUS_SKIPPED, reason, 0.0 });
}

bool TestReport::allPassed() const {
    return getFailCount() == 0 && getPassCount() > 0;
}

size_t TestReport::getPassCount() const {
    size_t c = 0;
    for (const auto& r : m_results) if (r.status == CheckStatus::STATUS_PASS) ++c;
    return c;
}

size_t TestReport::getFailCount() const {
    size_t c = 0;
    for (const auto& r : m_results) if (r.status == CheckStatus::STATUS_FAIL) ++c;
    return c;
}

size_t TestReport::getDeferredCount() const {
    size_t c = 0;
    for (const auto& r : m_results) if (r.status == CheckStatus::STATUS_DEFERRED) ++c;
    return c;
}

size_t TestReport::getSkippedCount() const {
    size_t c = 0;
    for (const auto& r : m_results) if (r.status == CheckStatus::STATUS_SKIPPED) ++c;
    return c;
}

void TestReport::printConsoleSummary() const {
    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  PowerSlave Exhumed Together — M1.2a Test Client Results" << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "Target: " << m_targetHost << ":" << m_targetPort
              << " | Participant: " << m_participantId
              << " | Total Duration: " << std::fixed << std::setprecision(1) << m_totalDurationMs << " ms" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;

    std::cout << "\n--- SECTION 1: VERIFIABLE NETWORK PROTOCOL CONTRACTS ---" << std::endl;
    for (const auto& r : m_results) {
        if (r.status == CheckStatus::STATUS_DEFERRED) continue;
        std::string tag = (r.status == CheckStatus::STATUS_PASS) ? "[PASS]" :
                          (r.status == CheckStatus::STATUS_FAIL) ? "[FAIL]" : "[SKIP]";
        std::cout << std::setw(7) << std::left << tag << " "
                  << std::setw(8) << r.id << " : "
                  << r.name;
        if (r.elapsedMs > 0.0) {
            std::cout << " (" << std::fixed << std::setprecision(1) << r.elapsedMs << " ms)";
        }
        std::cout << "\n        Detail: " << r.detail << std::endl;
    }

    std::cout << "\n--- SECTION 2: CONDITIONS UNVERIFIABLE WITHOUT NATIVE KEX DLL (DEFERRED) ---" << std::endl;
    for (const auto& r : m_results) {
        if (r.status != CheckStatus::STATUS_DEFERRED) continue;
        std::cout << "[DEFERRED] " << std::setw(8) << r.id << " : " << r.name << std::endl;
        std::cout << "        Reason: " << r.detail << std::endl;
    }

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "SUMMARY:" << std::endl;
    std::cout << "  Verifiable Network Passed : " << getPassCount() << std::endl;
    std::cout << "  Verifiable Network Failed : " << getFailCount() << std::endl;
    std::cout << "  Deferred Native KEX Checks: " << getDeferredCount() << " (Pending Sol Max M1.2a native adapter)" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    if (allPassed()) {
        std::cout << "OVERALL STATUS: [PASS] All verifiable network protocol contracts satisfied." << std::endl;
        std::cout << "                Native player movement is explicitly DEFERRED to M1.2a KEX validation." << std::endl;
    } else {
        std::cout << "OVERALL STATUS: [FAIL] One or more verifiable network conditions failed." << std::endl;
    }
    std::cout << "================================================================================\n" << std::endl;
}

bool TestReport::writeMarkdownReport(const std::string& path) const {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;

    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char timeStr[64];
    std::strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S UTC", &tm_buf);

    f << "# PowerSlave Exhumed Together — M1.2a Test Client Report\n\n";
    f << "**Target:** `" << m_targetHost << ":" << m_targetPort << "`  \n";
    f << "**Participant ID:** `" << m_participantId << "`  \n";
    f << "**Wire Protocol:** v1.10 (`0x0110`, Magic: `0x50534558`)  \n";
    f << "**Execution Timestamp:** " << timeStr << "  \n";
    f << "**Total Elapsed Time:** " << std::fixed << std::setprecision(1) << m_totalDurationMs << " ms  \n";
    f << "**Overall Verifiable Result:** **" << (allPassed() ? "PASS" : "FAIL") << "** ("
      << getPassCount() << "/" << (getPassCount() + getFailCount()) << " passed, "
      << getDeferredCount() << " deferred to native KEX host)  \n\n";

    f << "## 1. Verifiable Network Protocol Contracts\n\n";
    f << "| ID | Check | Status | Elapsed | Observations |\n";
    f << "|---|---|---|---|---|\n";
    for (const auto& r : m_results) {
        if (r.status == CheckStatus::STATUS_DEFERRED) continue;
        f << "| **" << r.id << "** | " << r.name << " | **" << checkStatusToString(r.status) << "** | "
          << std::fixed << std::setprecision(1) << r.elapsedMs << " ms | " << r.detail << " |\n";
    }

    f << "\n## 2. Conditions Unverifiable Without Native KEX DLL\n\n";
    f << "> [!NOTE]\n";
    f << "> Packet transmission and protocol conformance do not prove that P2 Puppet moved inside KEX.\n";
    f << "> The conditions below can only be verified after GPT-6.1 Sol Max connects the native DLL adapter to ENet.\n\n";
    f << "| ID | Condition | Status | Reason & Future Verification Gate |\n";
    f << "|---|---|---|---|\n";
    for (const auto& r : m_results) {
        if (r.status != CheckStatus::STATUS_DEFERRED) continue;
        f << "| **" << r.id << "** | " << r.name << " | **DEFERRED** | " << r.detail << " |\n";
    }

    f << "\n## 3. Telemetry and Methodology\n\n";
    f << "> [!IMPORTANT]\n";
    f << "> **Local Transmission Dispatch vs Guaranteed Delivery & KEX Movement:** Checks NET-06, NET-07, and NET-08 verify that input packets were successfully formatted and transmitted into the local ENet socket queue (`enet_peer_send`). Over lossy UDP (`CHANNEL_UNRELIABLE_SEQUENCED`), network packet drop means outbound transmission count does not guarantee host reception count. Furthermore, host reception does not constitute proof of native KEX physics displacement or actor rendering, which remain deferred under KEX-01..KEX-04.\n\n";
    f << "- Test client uses `pse::ENetClientNode` and Wire Protocol v1.10 directly (`net_protocol.h`).\n";
    f << "- Handshake exchanges `MsgHello` -> `MsgWelcome`, authenticating the slot, sessionId, and cryptographic reconnectKey.\n";
    f << "- Automated scenario: 60Hz input stream with `moveForward = 1.0f`, followed by an explicit `moveForward = 0.0f` release packet and neutral input.\n";
    f << "- Reconnect scenario: verifies that disconnect clears session state, and reconnection increments `connectionGeneration` without reusing stale state.\n";

    return true;
}

bool TestReport::writeJsonReport(const std::string& path) const {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"targetHost\": \"" << m_targetHost << "\",\n";
    f << "  \"targetPort\": " << m_targetPort << ",\n";
    f << "  \"participantId\": \"" << m_participantId << "\",\n";
    f << "  \"totalDurationMs\": " << m_totalDurationMs << ",\n";
    f << "  \"verifiablePassed\": " << (allPassed() ? "true" : "false") << ",\n";
    f << "  \"passCount\": " << getPassCount() << ",\n";
    f << "  \"failCount\": " << getFailCount() << ",\n";
    f << "  \"deferredCount\": " << getDeferredCount() << ",\n";
    f << "  \"results\": [\n";

    for (size_t i = 0; i < m_results.size(); ++i) {
        const auto& r = m_results[i];
        f << "    {\n";
        f << "      \"id\": \"" << r.id << "\",\n";
        f << "      \"name\": \"" << r.name << "\",\n";
        f << "      \"status\": \"" << checkStatusToString(r.status) << "\",\n";
        f << "      \"elapsedMs\": " << r.elapsedMs << ",\n";
        f << "      \"detail\": \"" << r.detail << "\"\n";
        f << "    }" << (i + 1 < m_results.size() ? "," : "") << "\n";
    }

    f << "  ]\n";
    f << "}\n";
    return true;
}

} // namespace pse::m12a
