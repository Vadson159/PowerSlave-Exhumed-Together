// pse_together.dll: one binary for both roles. The role comes from the PSE_ROLE environment variable
// ("host" or "client"; pe-play sets it). host.cpp and client.cpp stay separate translation units with their
// own private state; this file only exports the loader entry points and forwards them to the active role.
#include <windows.h>

extern "C" {
DWORD WINAPI host_M11Prepare(const wchar_t* logPath, DWORD primaryTid);
DWORD WINAPI host_M11PermitTest();
DWORD WINAPI host_PSE_P16_PermitRestore(unsigned long long probe, unsigned long long review);
void host_attach(HINSTANCE module);
DWORD WINAPI client_M11Prepare(const wchar_t* logPath, DWORD primaryTid);
DWORD WINAPI client_M11PermitTest();
DWORD WINAPI client_PSE_P1_PermitProxySpawn();
DWORD WINAPI client_PSE_P4_EnableBackground(DWORD enabled);
DWORD WINAPI client_PSE_P16_PermitRestore(unsigned long long probe, unsigned long long review);
void client_attach(HINSTANCE module);
}

namespace {
enum class Role { Unknown, Host, Client };
Role g_role = Role::Unknown;

Role detectRole() {
    wchar_t value[16]{};
    const DWORD n = GetEnvironmentVariableW(L"PSE_ROLE", value, 16);
    if (n == 0 || n >= 16) return Role::Unknown;
    if (_wcsicmp(value, L"host") == 0) return Role::Host;
    if (_wcsicmp(value, L"client") == 0) return Role::Client;
    return Role::Unknown;
}
}  // namespace

extern "C" {
__declspec(dllexport) DWORD WINAPI M11Prepare(const wchar_t* logPath, DWORD primaryTid) {
    g_role = detectRole();
    switch (g_role) {
    case Role::Host: return host_M11Prepare(logPath, primaryTid);
    case Role::Client: return client_M11Prepare(logPath, primaryTid);
    default: return ERROR_INVALID_PARAMETER;  // PSE_ROLE must be host or client
    }
}
__declspec(dllexport) DWORD WINAPI M11PermitTest() {
    switch (g_role) {
    case Role::Host: return host_M11PermitTest();
    case Role::Client: return client_M11PermitTest();
    default: return ERROR_NOT_READY;
    }
}
__declspec(dllexport) DWORD WINAPI PSE_P1_PermitProxySpawn() {
    return g_role == Role::Client ? client_PSE_P1_PermitProxySpawn() : ERROR_NOT_SUPPORTED;
}
__declspec(dllexport) DWORD WINAPI PSE_P4_EnableBackground(DWORD enabled) {
    return g_role == Role::Client ? client_PSE_P4_EnableBackground(enabled) : ERROR_NOT_SUPPORTED;
}
__declspec(dllexport) DWORD WINAPI PSE_P16_PermitRestore(unsigned long long probe, unsigned long long review) {
    switch (g_role) {
    case Role::Host: return host_PSE_P16_PermitRestore(probe, review);
    case Role::Client: return client_PSE_P16_PermitRestore(probe, review);
    default: return ERROR_NOT_READY;
    }
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    // No hooks, threads, KEX calls or file I/O under loader lock: only remember the module handle.
    if (reason == DLL_PROCESS_ATTACH) { host_attach(module); client_attach(module); }
    return TRUE;
}
