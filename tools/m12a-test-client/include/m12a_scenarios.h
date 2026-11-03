#pragma once

#include "m12a_client.h"
#include "m12a_report.h"
#include <string>
#include <cstdint>

namespace pse::m12a {

struct ScenarioConfig {
    std::string host = "127.0.0.1";
    uint16_t port = 27015;
    std::string participantId = "m12a_test_p2";
    std::string displayName = "M12a_Test_P2";
    std::string joinToken = "";
    uint32_t holdTicks = 30;     // ~500ms at 60Hz
    uint32_t neutralTicks = 15;  // ~250ms at 60Hz
    uint16_t tickRate = 60;
    uint32_t timeoutMs = 5000;
};

// Scenario 1: Connect -> Hold W -> Release W -> Neutral -> Disconnect
bool runHoldWScenario(M12aClient& client, const ScenarioConfig& cfg, TestReport& report);

// Scenario 2: Reconnection with generational advancement and anti-stale-state checks
bool runReconnectScenario(M12aClient& client, const ScenarioConfig& cfg, TestReport& report);

// Scenario 3: Negative anti-hijack verification (forged reconnectKey)
bool runAntiHijackScenario(M12aClient& client, const ScenarioConfig& cfg, TestReport& report);

// Comprehensive Suite: Scenario 1 + Scenario 2 + Scenario 3 + Deferred KEX annotations
bool runFullSuite(M12aClient& client, const ScenarioConfig& cfg, TestReport& report);

} // namespace pse::m12a
