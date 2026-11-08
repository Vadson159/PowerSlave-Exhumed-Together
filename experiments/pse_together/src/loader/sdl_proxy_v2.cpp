// P27/S9.6 SDL2 shim v2 = the accepted P24 shim + the native write guard installed at process load.
// DllMain installs ONLY the ntdll NtCreateFile/NtOpenFile guard (MinHook; no LoadLibrary, no worker,
// no wait) so that the engine's earliest writes (before SDL_Init and before -userpath) are already
// confined. The guard is configured by the launcher (pe-play.exe) through the environment; a game
// started without it is refused at SDL_Init (fail-closed). Detours-style attach is the documented
// pattern for early API interception; the failure latch below keeps loader-lock work minimal.
#include "loader_common.hpp"
#include "pe_guard.hpp"

namespace {
HMODULE self{};
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
DWORD guardResult = ERROR_NOT_READY;
bool guardConfigured = false;
using Init = int (__cdecl*)(unsigned);
Init originalInit{};
[[noreturn]] void fail(const std::wstring& text, DWORD code) {
    MessageBoxW(nullptr, text.c_str(), L"PowerSlave Exhumed Together — загрузчик / loader", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(code ? code : ERROR_DLL_INIT_FAILED);
}
std::wstring env(const wchar_t* name) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD n = GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    return n && n < buffer.size() ? std::wstring(buffer.data(), n) : std::wstring{};
}
BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    using namespace pse::loader;
    if (!guardConfigured)
        fail(L"Запуск без изоляции профиля отклонён. Запускайте игру через pe-play.exe. / Launch without profile isolation refused. Start the game through pe-play.exe.", ERROR_ACCESS_DENIED);
    if (guardResult)
        fail(L"Изоляция записи не установилась; игра не запущена, файлы не тронуты. / Write guard failed to install; game not started, no files touched.\nWin32: " + std::to_wstring(guardResult), guardResult);
    std::wstring message;
    const auto exe = modulePath(nullptr);
    const DWORD check = checkExe(exe.c_str(), message);
    if (check) fail(message, check);
    const auto dir = parent(modulePath(self));
    const auto sdlPath = dir + L"\\SDL2_pse_original.dll";
    File file;
    std::string sha;
    if (hashFile(sdlPath.c_str(), file, sha) || sha != SdlSha)
        fail(L"Штатная SDL2 отсутствует или изменена. / Original SDL2 missing or changed.", ERROR_CRC);
    const HMODULE sdl = LoadLibraryExW(sdlPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!sdl || _wcsicmp(finalPath(file.value).c_str(), modulePath(sdl).c_str()) != 0)
        fail(L"Не удалось загрузить штатную SDL2 по проверенному пути. / Original SDL2 path mismatch.", ERROR_MOD_NOT_FOUND);
    originalInit = reinterpret_cast<Init>(GetProcAddress(sdl, "SDL_Init"));
    if (!originalInit) fail(L"Неверный интерфейс SDL2. / SDL2 export missing.", ERROR_PROC_NOT_FOUND);
    // The P24 broker (frozen payload loader) is optional in this slice: present only in the lab copies.
    const auto brokerPath = dir + L"\\pe-together.dll";
    if (GetFileAttributesW(brokerPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        const HMODULE broker = LoadLibraryExW(brokerPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!broker || _wcsicmp(modulePath(broker).c_str(), brokerPath.c_str()) != 0)
            fail(L"Не удалось загрузить pe-together.dll рядом с SDL2.dll. / Cannot load adjacent pe-together.dll.", ERROR_MOD_NOT_FOUND);
        using Start = DWORD (WINAPI*)();
        using StartV2 = DWORD (WINAPI*)(DWORD);
        // v2 broker (portable, native permits) requires the guard confirmation; the P24 broker keeps its own lab gates.
        const auto startV2 = reinterpret_cast<StartV2>(GetProcAddress(broker, "PSELoaderStartV2"));
        const auto start = reinterpret_cast<Start>(GetProcAddress(broker, "PSELoaderStart"));
        if (!startV2 && !start) fail(L"Неверный интерфейс загрузчика. / Missing loader exports.", ERROR_PROC_NOT_FOUND);
        const DWORD result = startV2 ? startV2(1) : start();
        if (result) fail(L"pe-together.dll отклонила запуск. / Loader rejected startup.\nWin32: " + std::to_wstring(result), result);
    }
    return TRUE;
}
}
extern "C" int __cdecl PSE_SDL_Init(unsigned flags) {
    if (!InitOnceExecuteOnce(&once, initialize, nullptr, nullptr)) fail(L"Loader initialization failed.", GetLastError());
    return originalInit(flags);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance;
        const auto root = env(L"PSE_GUARD_ROOT");
        if (!root.empty()) {
            guardConfigured = true;
            guardResult = pse::guard::install({root, env(L"PSE_GUARD_PROFILE"), env(L"PSE_GUARD_LOG")});
        }
    }
    return TRUE;
}
