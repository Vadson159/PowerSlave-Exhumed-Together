#include "m12a_logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace pse::m12a {

M12aLogger& M12aLogger::instance() {
    static M12aLogger s_logger;
    return s_logger;
}

bool M12aLogger::init(const std::string& logFilePath, bool verbose) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_verbose = verbose;
    m_startTime = std::chrono::steady_clock::now();
    m_entries.clear();

    if (m_file.is_open()) {
        m_file.close();
    }

    if (!logFilePath.empty()) {
        m_file.open(logFilePath, std::ios::out | std::ios::trunc);
        if (!m_file.is_open()) {
            std::cerr << "[M12A-LOGGER] Warning: Failed to open log file: " << logFilePath << std::endl;
            return false;
        }
        m_file << "================================================================================" << std::endl;
        m_file << "  PowerSlave Exhumed Together — M1.2a Test Client Log" << std::endl;
        m_file << "  Started: " << getTimestampString() << std::endl;
        m_file << "================================================================================" << std::endl;
    }
    return true;
}

void M12aLogger::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file << "================================================================================" << std::endl;
        m_file << "  End of Log (Total entries: " << m_entries.size() << ")" << std::endl;
        m_file << "================================================================================" << std::endl;
        m_file.close();
    }
}

std::string M12aLogger::getTimestampString() const {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

const char* M12aLogger::categoryToString(LogCategory cat) const {
    switch (cat) {
        case LogCategory::NET:       return "NET";
        case LogCategory::HANDSHAKE: return "HANDSHAKE";
        case LogCategory::OUTBOUND:  return "OUTBOUND";
        case LogCategory::INBOUND:   return "INBOUND";
        case LogCategory::STATE:     return "STATE";
        case LogCategory::VERIFY:    return "VERIFY";
        default:                     return "GENERAL";
    }
}

const char* M12aLogger::levelToString(LogLevel lvl) const {
    switch (lvl) {
        case LogLevel::DEBUG_LVL: return "DEBUG";
        case LogLevel::INFO_LVL:  return "INFO";
        case LogLevel::WARN_LVL:  return "WARN";
        case LogLevel::ERROR_LVL: return "ERROR";
        default:                  return "INFO";
    }
}

void M12aLogger::log(LogLevel level, LogCategory category, const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double, std::milli>(now - m_startTime).count();
    std::string ts = getTimestampString();

    LogEntry entry;
    entry.timestamp = ts;
    entry.elapsedMs = elapsed;
    entry.level = level;
    entry.category = category;
    entry.message = msg;
    m_entries.push_back(entry);

    std::ostringstream line;
    line << "[" << ts << " +" << std::fixed << std::setprecision(1) << std::setw(7) << elapsed << "ms] "
         << "[" << std::setw(9) << categoryToString(category) << "] "
         << msg;

    if (m_verbose || level == LogLevel::WARN_LVL || level == LogLevel::ERROR_LVL || category == LogCategory::VERIFY) {
        std::cout << line.str() << std::endl;
    }

    if (m_file.is_open()) {
        m_file << line.str() << "\n";
        m_file.flush();
    }
}

void M12aLogger::logNet(const std::string& msg) {
    log(LogLevel::INFO_LVL, LogCategory::NET, msg);
}

void M12aLogger::logHandshake(const std::string& msg) {
    log(LogLevel::INFO_LVL, LogCategory::HANDSHAKE, msg);
}

void M12aLogger::logState(const std::string& fromState, const std::string& toState, const std::string& context) {
    std::ostringstream ss;
    ss << fromState << " -> " << toState;
    if (!context.empty()) {
        ss << " (" << context << ")";
    }
    log(LogLevel::INFO_LVL, LogCategory::STATE, ss.str());
}

void M12aLogger::logOutboundCommand(uint32_t seq, uint32_t tick, uint8_t slot, uint32_t gen,
                                    float fwd, float rgt, float pitch, float yaw,
                                    uint32_t buttons, uint8_t weapon) {
    std::ostringstream ss;
    ss << "MSG_INPUT_COMMAND seq=" << seq
       << " tick=" << tick
       << " slot=" << (int)slot << " (P" << (int)(slot + 1) << ")"
       << " gen=" << gen
       << " fwd=" << std::fixed << std::setprecision(2) << fwd
       << " rgt=" << std::fixed << std::setprecision(2) << rgt
       << " pitch=" << std::fixed << std::setprecision(1) << pitch
       << " yaw=" << std::fixed << std::setprecision(1) << yaw
       << " btn=0x" << std::hex << buttons << std::dec
       << " wpn=" << (int)weapon;
    log(LogLevel::INFO_LVL, LogCategory::OUTBOUND, ss.str());
}

void M12aLogger::logInboundMessage(uint8_t msgType, uint32_t seq, const std::string& details) {
    std::ostringstream ss;
    ss << "MSG_TYPE=0x" << std::hex << std::setw(2) << std::setfill('0') << (int)msgType << std::dec
       << " seq=" << seq
       << " " << details;
    log(LogLevel::INFO_LVL, LogCategory::INBOUND, ss.str());
}

void M12aLogger::logVerify(const std::string& checkId, bool pass, const std::string& detail) {
    std::ostringstream ss;
    ss << "[" << (pass ? "PASS" : "FAIL") << "] " << checkId << ": " << detail;
    log(pass ? LogLevel::INFO_LVL : LogLevel::ERROR_LVL, LogCategory::VERIFY, ss.str());
}

std::vector<LogEntry> M12aLogger::getEntries() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

void M12aLogger::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_startTime = std::chrono::steady_clock::now();
}

} // namespace pse::m12a
