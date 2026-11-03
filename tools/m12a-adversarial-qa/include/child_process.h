#pragma once

#include <string>
#include <vector>

namespace pse::qa {

struct ProcessResult {
    int exitCode = -1;
    bool timedOut = false;
    std::string stdOut;
    std::string stdErr;
};

ProcessResult runProcess(const std::wstring& executablePath,
                         const std::wstring& arguments,
                         uint32_t timeoutMs = 15000);

} // namespace pse::qa
