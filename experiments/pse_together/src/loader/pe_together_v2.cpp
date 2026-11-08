// P27/S9.7 broker v2: portable payload loader + native self-issued permits (no REA, no Frida).
// Differences from the accepted P24 broker (pe_together.cpp, unchanged):
//  - install root, role, profile and native log come from the pe-play environment (PSE_GUARD_ROOT, PSE_ROLE,
//    PSE_GUARD_PROFILE, PSE_NATIVE_LOG) instead of two compiled-in workspace paths;
//  - payload SHA-256 comes from pe-together.ini ([payload] host_sha256 / client_sha256) written by the installer;
//  - after Prepare + the bootstrap APC the broker itself issues the mailbox permits the REA stand used to send.
// Every gate of P24 stays: exact EXE SHA, exact <root>\game\EXE path, broker next to the EXE, canonical fresh log
// directory inside root (no reparse, no ADS, no overwrite), payload read-handle held across LoadLibrary, loaded path ==
// hashed path, payload pinned. The shim v2 passes guardInstalled=1 only after the native write guard is live.
#include "loader_common.hpp"
#include <cwctype>

namespace {
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
DWORD result = ERROR_DLL_INIT_FAILED;
HMODULE self{};
using Prepare = DWORD (WINAPI*)(const wchar_t*, DWORD);
using Mailbox = DWORD (WINAPI*)();
DWORD guardFlag = 0;

std::wstring env(const wchar_t* name) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD n = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    return n && n < buffer.size() ? std::wstring(buffer.data(), n) : std::wstring{};
}
bool hex64(const std::string& value) {
    if (value.size() != 64) return false;
    for (const char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
std::string narrow(const std::wstring& value) {
    std::string out;
    for (const wchar_t c : value) out += c < 128 ? static_cast<char>(std::towlower(c)) : '?';
    return out;
}
// Optional mailbox export: absent is fine for roles that do not have it, but a present export must succeed.
bool issue(HMODULE module, const char* name, bool required, DWORD& error) {
    const auto fn = reinterpret_cast<Mailbox>(GetProcAddress(module, name));
    if (!fn) { if (required) error = ERROR_PROC_NOT_FOUND; return !required; }
    const DWORD r = fn();
    if (r) { error = r; return false; }
    return true;
}

BOOL CALLBACK start(PINIT_ONCE, PVOID, PVOID*) {
    using namespace pse::loader;
    if (!guardFlag) { result = ERROR_ACCESS_DENIED; return TRUE; }  // shim v2 did not confirm the native write guard
    std::wstring message;
    const auto exe = modulePath(nullptr);
    result = checkExe(exe.c_str(), message);
    if (result) return TRUE;
    const auto root = env(L"PSE_GUARD_ROOT"), role = env(L"PSE_ROLE"), profile = env(L"PSE_GUARD_PROFILE"), logPath = env(L"PSE_NATIVE_LOG");
    const bool host = role == L"host", client = role == L"client";
    if (root.empty() || profile.empty() || logPath.empty() || (!host && !client)) { result = ERROR_INVALID_DATA; return TRUE; }
    if (_wcsicmp(exe.c_str(), (root + L"\\game\\anubis_Shipping_Steam_x64.exe").c_str()) != 0 || !within(exe, root)) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const auto dir = parent(modulePath(self));
    if (_wcsicmp(dir.c_str(), parent(exe).c_str()) != 0) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const auto ini = dir + L"\\pe-together.ini";
    std::array<wchar_t, 128> shaText{};
    GetPrivateProfileStringW(L"payload", host ? L"host_sha256" : L"client_sha256", L"", shaText.data(), static_cast<DWORD>(shaText.size()), ini.c_str());
    const std::string expected = narrow(shaText.data());
    if (!hex64(expected)) { result = ERROR_INVALID_DATA; return TRUE; }
    // Canonical existing log directory inside root, CREATE_NEW in M11Prepare, no overwrite.
    File directory;
    const auto logDir = parent(logPath);
    directory.value = CreateFileW(logDir.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const auto resolvedDir = finalPath(directory.value);
    if (!within(resolvedDir, root) || _wcsicmp(logDir.c_str(), resolvedDir.c_str()) != 0 || logPath.find(L':', 2) != std::wstring::npos ||
        GetFileAttributesW(logPath.c_str()) != INVALID_FILE_ATTRIBUTES) { result = ERROR_BAD_PATHNAME; return TRUE; }
    File runtimeFile;
    std::string sha;
    const auto runtimePath = dir + (host ? L"\\pse-host.dll" : L"\\pse-client.dll");
    result = hashFile(runtimePath.c_str(), runtimeFile, sha);
    if (result) return TRUE;
    if (sha != expected) { result = ERROR_CRC; return TRUE; }
    const auto resolvedRuntime = finalPath(runtimeFile.value);
    if (_wcsicmp(resolvedRuntime.c_str(), runtimePath.c_str()) != 0) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const HMODULE runtime = LoadLibraryExW(runtimePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtime) { result = GetLastError(); return TRUE; }
    HMODULE pinned{};
    if (_wcsicmp(modulePath(runtime).c_str(), resolvedRuntime.c_str()) != 0 ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&start), &pinned)) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const auto prepare = reinterpret_cast<Prepare>(GetProcAddress(runtime, "M11Prepare"));
    if (!prepare) { result = ERROR_PROC_NOT_FOUND; return TRUE; }
    result = prepare(logPath.c_str(), GetCurrentThreadId());
    if (result) return TRUE;
    // DOC-41 same-process APC on the calling game thread; the payload installs its hooks there.
    SleepEx(0, TRUE);
    // Native permits. All are atomic mailbox flags inside the payload (verified in source: no game call, no
    // allocation); they replace the Frida permit scripts of the REA stand. Gameplay itself stays gated inside the
    // payload (tick/gameplay state, host spawn-on-ready, UI request).
    DWORD error = ERROR_SUCCESS;
    if (!issue(runtime, "M11PermitTest", true, error) || !issue(runtime, "PSE_P23_PermitInterface", false, error) ||
        (client && !issue(runtime, "PSE_P1_PermitProxySpawn", false, error))) result = error ? error : ERROR_GEN_FAILURE;
    return TRUE;
}
}  // namespace

extern "C" __declspec(dllexport) DWORD WINAPI PSELoaderStartV2(DWORD guardInstalled) {
    if (guardInstalled == 1) guardFlag = 1;
    if (!InitOnceExecuteOnce(&once, start, nullptr, nullptr)) return GetLastError();
    return result;
}
extern "C" __declspec(dllexport) DWORD WINAPI PSELoaderCheckFile(const wchar_t* path, wchar_t* text, DWORD capacity) {
    if (!path || !text || !capacity) return ERROR_INVALID_PARAMETER;
    std::wstring message;
    const auto code = pse::loader::checkExe(path, message);
    if (message.size() >= capacity) return ERROR_INSUFFICIENT_BUFFER;
    wcscpy_s(text, capacity, message.c_str());
    return code;
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self = instance;
    return TRUE;
}
