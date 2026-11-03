#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <fstream>
#include <cstdint>

namespace pse::m12a {

enum class LogLevel {
    DEBUG_LVL,
    INFO_LVL,
    WARN_LVL,
    ERROR_LVL
};

enum class LogCategory {
    NET,
    HANDSHAKE,
    OUTBOUND,
    INBOUND,
    STATE,
    VERIFY
};

struct LogEntry {
    std::string timestamp;
    double elapsedMs = 0.0;
    LogLevel level = LogLevel::INFO_LVL;
    LogCategory category = LogCategory::NET;
    std::string message;
};

class M12aLogger {
public:
    static M12aLogger& instance();

    bool init(const std::string& logFilePath, bool verbose = true);
    void close();

    void log(LogLevel level, LogCategory category, const std::string& msg);

    // High-convenience logging helpers
    void logNet(const std::string& msg);
    void logHandshake(const std::string& msg);
    void logState(const std::string& fromState, const std::string& toState, const std::string& context = "");
    void logOutboundCommand(uint32_t seq, uint32_t tick, uint8_t slot, uint32_t gen,
                            float fwd, float rgt, float pitch, float yaw,
                            uint32_t buttons, uint8_t weapon);
    void logInboundMessage(uint8_t msgType, uint32_t seq, const std::string& details);
    void logVerify(const std::string& checkId, bool pass, const std::string& detail);

    std::vector<LogEntry> getEntries() const;
    void clear();

    void setVerbose(bool verbose) { m_verbose = verbose; }
    bool isVerbose() const { return m_verbose; }

private:
    M12aLogger() = default;
    ~M12aLogger() { close(); }

    std::string getTimestampString() const;
    const char* categoryToString(LogCategory cat) const;
    const char* levelToString(LogLevel lvl) const;

private:
    mutable std::mutex m_mutex;
    std::ofstream m_file;
    bool m_verbose = true;
    std::chrono::steady_clock::time_point m_startTime;
    std::vector<LogEntry> m_entries;
};

} // namespace pse::m12a
