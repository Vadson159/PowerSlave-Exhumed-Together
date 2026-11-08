#include "loader_common.hpp"
#include "../native/build_profile.hpp"

namespace {
HMODULE self{};
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
DWORD result = ERROR_DLL_INIT_FAILED;
using Prepare = DWORD (WINAPI*)(const wchar_t*, DWORD);
BOOL CALLBACK start(PINIT_ONCE, PVOID, PVOID*) {
    using namespace pse::loader;
    std::wstring message;
    const auto exe = modulePath(nullptr);
    result = checkExe(exe.c_str(), message);
    if (result) return TRUE;
    // P24 is deliberately bounded to the two accepted P1 test copies. The frozen
    // payload independently enforces its exact path. P25 must address distribution.
    const std::wstring root = profile::Workspace;
    if (!within(exe, root) || !within(modulePath(self), root)) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const auto dir = parent(modulePath(self));
    if (_wcsicmp(dir.c_str(), parent(exe).c_str()) != 0) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const auto ini = dir + L"\\pe-together.ini";
    std::array<wchar_t, 32768> logPath{};
    wchar_t role[16]{};
    GetPrivateProfileStringW(L"loader", L"role", L"", role, 16, ini.c_str());
    const bool host = wcscmp(role, L"host") == 0;
    const bool client = wcscmp(role, L"client") == 0;
    const DWORD n = GetPrivateProfileStringW(L"loader", L"native_log", L"", logPath.data(), static_cast<DWORD>(logPath.size()), ini.c_str());
    if ((!host && !client) || !n || n >= logPath.size() - 1) { result = ERROR_INVALID_DATA; return TRUE; }
    const auto expectedExe = root + (host ? L"\\experiments\\m0\\game\\anubis_Shipping_Steam_x64.exe" : L"\\experiments\\m1\\native_second_instance\\game\\anubis_Shipping_Steam_x64.exe");
    if (_wcsicmp(exe.c_str(), expectedExe.c_str()) != 0) { result = ERROR_BAD_PATHNAME; return TRUE; }
    // Canonical existing log directory, CREATE_NEW in M11Prepare, no overwrite.
    File directory;
    const auto logDir = parent(logPath.data());
    directory.value = CreateFileW(logDir.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    const auto resolvedDir = finalPath(directory.value);
    if (!within(resolvedDir, root) || _wcsicmp(logDir.c_str(), resolvedDir.c_str()) != 0 ||
        std::wstring(logPath.data()).find(L':', 2) != std::wstring::npos ||
        GetFileAttributesW(logPath.data()) != INVALID_FILE_ATTRIBUTES) { result = ERROR_BAD_PATHNAME; return TRUE; }
    File runtimeFile;
    std::string sha;
    const auto runtimePath = dir + L"\\pse-runtime.dll";
    result = hashFile(runtimePath.c_str(), runtimeFile, sha);
    if (result) return TRUE;
    if (sha != (host ? HostSha : ClientSha)) { result = ERROR_CRC; return TRUE; }
    const auto resolvedRuntime = finalPath(runtimeFile.value);
    if (_wcsicmp(resolvedRuntime.c_str(), runtimePath.c_str()) != 0) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const HMODULE runtime = LoadLibraryExW(runtimePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtime) { result = GetLastError(); return TRUE; }
    // Pin both layers; the payload pins itself. No unload of live hooks/trampolines.
    HMODULE pinned{};
    if (_wcsicmp(modulePath(runtime).c_str(), resolvedRuntime.c_str()) != 0 ||
        !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&start), &pinned)) { result = ERROR_BAD_PATHNAME; return TRUE; }
    const auto prepare = reinterpret_cast<Prepare>(GetProcAddress(runtime, "M11Prepare"));
    if (!prepare) { result = ERROR_PROC_NOT_FOUND; return TRUE; }
    result = prepare(logPath.data(), GetCurrentThreadId());
    if (result) return TRUE;
    // DOC-41 same-process APC, dispatched on the calling game thread. No remote
    // thread/injection, no Frida, and no gameplay permit is issued by the loader.
    SleepEx(0, TRUE);
    return TRUE;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI PSELoaderStart() {
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
