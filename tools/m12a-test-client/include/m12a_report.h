#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace pse::m12a {

enum class CheckStatus {
    STATUS_PASS,
    STATUS_FAIL,
    STATUS_DEFERRED,
    STATUS_SKIPPED
};

const char* checkStatusToString(CheckStatus st);

struct CheckResult {
    std::string id;
    std::string name;
    CheckStatus status = CheckStatus::STATUS_PASS;
    std::string detail;
    double elapsedMs = 0.0;
};

class TestReport {
public:
    TestReport();

    void setTarget(const std::string& host, uint16_t port, const std::string& participantId);
    void setDuration(double ms) { m_totalDurationMs = ms; }

    void addPass(const std::string& id, const std::string& name, const std::string& detail, double ms = 0.0);
    void addFail(const std::string& id, const std::string& name, const std::string& detail, double ms = 0.0);
    void addDeferred(const std::string& id, const std::string& name, const std::string& reason);
    void addSkipped(const std::string& id, const std::string& name, const std::string& reason);

    bool allPassed() const;
    size_t getPassCount() const;
    size_t getFailCount() const;
    size_t getDeferredCount() const;
    size_t getSkippedCount() const;

    const std::vector<CheckResult>& getResults() const { return m_results; }

    void printConsoleSummary() const;
    bool writeMarkdownReport(const std::string& path) const;
    bool writeJsonReport(const std::string& path) const;

private:
    std::string m_targetHost = "127.0.0.1";
    uint16_t m_targetPort = 27015;
    std::string m_participantId = "m12a_test_p2";
    double m_totalDurationMs = 0.0;
    std::vector<CheckResult> m_results;
};

} // namespace pse::m12a
