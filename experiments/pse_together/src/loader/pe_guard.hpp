#pragma once
// P27/S9.6: native write-open guard + path redirection for the owned game process.
// Port of the accepted Frida isolation (experiments/m0/isolation_guard.js write guard +
// observer.js path remaps) to a Frida-free module. NOT an OS sandbox: direct syscalls,
// inherited writable handles, rename APIs and other processes are not covered.
#include <windows.h>
#include <string>

namespace pse::guard {
struct Config {
    std::wstring root;     // absolute DOS path; disk write-opens are allowed only inside it
    std::wstring profile;  // absolute DOS path inside root; target of the external-path redirects
    std::wstring log;      // optional JSONL file (inside root, must not exist); unique denied/redirected paths
};
struct Stats {
    unsigned long long write_opens, allowed_disk, redirected, denied, device_exemptions, errors, unique_overflow, unique_records;
};
// 0 on success. One-shot: a second call returns ERROR_ALREADY_EXISTS. Fail-closed: on any
// error no hook stays installed and the caller must not start the game.
DWORD install(const Config& config);
bool installed();
Stats stats();
// Pure helpers, exposed for the offline contract only.
// 1 = canonical DOS path written (case preserved, no trailing separator), 0 = not a drive path, -1 = malformed, -2 = overflow.
int canonicalize(const wchar_t* input, wchar_t* output, unsigned capacity);
// Returns the redirect target for an external write path, or empty when no rule matches.
std::wstring mapExternal(const std::wstring& canonical, const std::wstring& profile);
}
