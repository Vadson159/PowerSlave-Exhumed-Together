#include "m12a_client.h"
#include "m12a_logger.h"
#include "m12a_scenarios.h"
#include "m12a_report.h"
#include "enet_host.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>

namespace pse::m12a {

void printHelp() {
    std::cout << "================================================================================\n"
              << "  PowerSlave Exhumed Together — M1.2a Test Client CLI\n"
              << "================================================================================\n"
              << "Usage: m12a_test_client [options]\n\n"
              << "Target Options:\n"
              << "  --host <ip>             Host IPv4 address (default: 127.0.0.1)\n"
              << "  --port <port>           Host UDP port (default: 27015)\n"
              << "  --participant <id>      Client participant ID (default: m12a_test_p2)\n"
              << "  --name <string>         Client display name (default: M12a_Test_P2)\n"
              << "  --token <string>        Join token / password (default: empty)\n\n"
              << "Scenario Options:\n"
              << "  --scenario <name>       Test scenario to execute:\n"
              << "                            'hold-w'    : connect -> hold W -> release W -> neutral -> disconnect\n"
              << "                            'reconnect' : connect -> hold W -> disconnect -> reconnect -> hold W -> neutral -> disconnect\n"
              << "                            'anti-hijack': verify rejection of forged cryptographic reconnect key\n"
              << "                            'full'      : execute all automated test scenarios (default)\n"
              << "  --self-test             Spawn in-process ENet mock-host and execute test against it\n"
              << "  --self-test-port <port> UDP port for in-process mock host (default: 28715)\n"
              << "  --hold-ticks <n>        Number of ticks to hold W (default: 30)\n"
              << "  --neutral-ticks <n>     Number of ticks of neutral input (default: 15)\n"
              << "  --tick-rate <hz>        Simulation tick rate in Hz (default: 60)\n"
              << "  --timeout-ms <ms>       Network response timeout in milliseconds (default: 5000)\n\n"
              << "Output & Logging Options:\n"
              << "  --log-file <path>       Path to output detailed execution log (default: m12a_client.log)\n"
              << "  --report-file <path>    Path to output markdown report (default: TEST_REPORT.md)\n"
              << "  --json-file <path>      Path to output JSON test summary (optional)\n"
              << "  --quiet, -q             Suppress verbose packet logs on console\n"
              << "  --help, -h              Display this help message\n\n"
              << "Exit Codes:\n"
              << "  0 : All verifiable network protocol contracts PASSED\n"
              << "  1 : One or more verifiable network checks FAILED\n"
              << "  2 : Configuration or initialization error\n"
              << "================================================================================" << std::endl;
}

} // namespace pse::m12a

int main(int argc, char* argv[]) {
    pse::m12a::ScenarioConfig cfg;
    std::string scenario = "full";
    bool selfTest = false;
    uint16_t selfTestPort = 28715;
    std::string logFilePath = "m12a_client.log";
    std::string reportFilePath = "TEST_REPORT.md";
    std::string jsonFilePath = "";
    bool verbose = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            pse::m12a::printHelp();
            return 0;
        } else if (arg == "--host" && i + 1 < argc) {
            cfg.host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--participant" && i + 1 < argc) {
            cfg.participantId = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            cfg.displayName = argv[++i];
        } else if (arg == "--token" && i + 1 < argc) {
            cfg.joinToken = argv[++i];
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario = argv[++i];
        } else if (arg == "--self-test") {
            selfTest = true;
        } else if (arg == "--self-test-port" && i + 1 < argc) {
            selfTestPort = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--hold-ticks" && i + 1 < argc) {
            cfg.holdTicks = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--neutral-ticks" && i + 1 < argc) {
            cfg.neutralTicks = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--tick-rate" && i + 1 < argc) {
            cfg.tickRate = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            cfg.timeoutMs = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (arg == "--log-file" && i + 1 < argc) {
            logFilePath = argv[++i];
        } else if (arg == "--report-file" && i + 1 < argc) {
            reportFilePath = argv[++i];
        } else if (arg == "--json-file" && i + 1 < argc) {
            jsonFilePath = argv[++i];
        } else if (arg == "--quiet" || arg == "-q") {
            verbose = false;
        } else {
            std::cerr << "Unknown option: " << arg << " (use --help for usage)" << std::endl;
            return 2;
        }
    }

    // Initialize logger
    pse::m12a::M12aLogger::instance().init(logFilePath, verbose);

    std::cout << "================================================================================\n"
              << "  PowerSlave Exhumed Together — M1.2a Test Client\n"
              << "  Wire Protocol: v1.10 (0x0110) | Target: "
              << (selfTest ? ("In-process Mock Host (port " + std::to_string(selfTestPort) + ")") : (cfg.host + ":" + std::to_string(cfg.port)))
              << "\n================================================================================" << std::endl;

    // In-process mock host management if requested
    std::unique_ptr<pse::ENetHostServer> mockHost;
    std::atomic<bool> mockHostRunning{ false };
    std::thread mockHostThread;

    if (selfTest) {
        pse::HostConfig hostCfg;
        hostCfg.bindAddress = "127.0.0.1";
        hostCfg.port = selfTestPort;
        hostCfg.hostName = "M12a_Mock_Host";
        hostCfg.participantId = "mock_host_p1";
        hostCfg.maxClients = 7;
        hostCfg.tickRate = cfg.tickRate;
        hostCfg.snapshotRate = 30;
        hostCfg.joinToken = cfg.joinToken;
        hostCfg.bridgeMode = false;

        mockHost = std::make_unique<pse::ENetHostServer>();
        if (!mockHost->start(hostCfg)) {
            std::cerr << "[FATAL] Failed to start in-process mock host on port " << selfTestPort << std::endl;
            return 2;
        }

        mockHostRunning.store(true);
        mockHostThread = std::thread([&]() {
            while (mockHostRunning.load()) {
                mockHost->poll(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        cfg.host = "127.0.0.1";
        cfg.port = selfTestPort;
        std::cout << "[MOCK-HOST] In-process host started on 127.0.0.1:" << selfTestPort << std::endl;
    }

    pse::m12a::TestReport report;
    report.setTarget(cfg.host, cfg.port, cfg.participantId);

    pse::m12a::M12aClient client;
    bool success = false;

    if (scenario == "hold-w") {
        success = pse::m12a::runHoldWScenario(client, cfg, report);
    } else if (scenario == "reconnect") {
        success = pse::m12a::runReconnectScenario(client, cfg, report);
    } else if (scenario == "anti-hijack") {
        success = pse::m12a::runAntiHijackScenario(client, cfg, report);
    } else if (scenario == "full") {
        success = pse::m12a::runFullSuite(client, cfg, report);
    } else {
        std::cerr << "Unknown scenario: " << scenario << std::endl;
        pse::m12a::printHelp();
        success = false;
    }

    // Shut down mock host if started
    if (selfTest) {
        mockHostRunning.store(false);
        if (mockHostThread.joinable()) {
            mockHostThread.join();
        }
        mockHost->stop();
        std::cout << "[MOCK-HOST] In-process host stopped cleanly." << std::endl;
    }

    // Display summary
    report.printConsoleSummary();

    // Export report files
    if (!reportFilePath.empty()) {
        if (report.writeMarkdownReport(reportFilePath)) {
            std::cout << "[REPORT] Written markdown report to: " << reportFilePath << std::endl;
        } else {
            std::cerr << "[REPORT] Failed to write markdown report to: " << reportFilePath << std::endl;
        }
    }

    if (!jsonFilePath.empty()) {
        if (report.writeJsonReport(jsonFilePath)) {
            std::cout << "[REPORT] Written JSON report to: " << jsonFilePath << std::endl;
        } else {
            std::cerr << "[REPORT] Failed to write JSON report to: " << jsonFilePath << std::endl;
        }
    }

    pse::m12a::M12aLogger::instance().close();

    return report.allPassed() ? 0 : 1;
}
