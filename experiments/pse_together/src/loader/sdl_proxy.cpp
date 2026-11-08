#include "loader_common.hpp"

namespace {
HMODULE self{};
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
using Init = int (__cdecl*)(unsigned);
Init originalInit{};
[[noreturn]] void fail(const std::wstring& text, DWORD code) {
    MessageBoxW(nullptr, text.c_str(), L"PowerSlave Exhumed Together — загрузчик / loader", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    ExitProcess(code ? code : ERROR_DLL_INIT_FAILED);
}
BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) {
    using namespace pse::loader;
    std::wstring message;
    const auto exe = modulePath(nullptr);
    const DWORD check = checkExe(exe.c_str(), message);
    if (check) fail(message, check); // No pe-together/runtime loaded on an unknown EXE.
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
    const auto brokerPath = dir + L"\\pe-together.dll";
    const HMODULE broker = LoadLibraryExW(brokerPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!broker || _wcsicmp(modulePath(broker).c_str(), brokerPath.c_str()) != 0)
        fail(L"Не удалось загрузить pe-together.dll рядом с SDL2.dll. / Cannot load adjacent pe-together.dll.", ERROR_MOD_NOT_FOUND);
    using Start = DWORD (WINAPI*)();
    const auto start = reinterpret_cast<Start>(GetProcAddress(broker, "PSELoaderStart"));
    if (!originalInit || !start) fail(L"Неверный интерфейс загрузчика. / Missing loader exports.", ERROR_PROC_NOT_FOUND);
    const DWORD result = start();
    if (result) fail(L"pe-together.dll отклонила запуск. Проверьте pe-together.ini, пути и хеш игровой DLL. / Loader rejected startup. Check config, paths and runtime DLL hash.\nWin32: " + std::to_wstring(result), result);
    return TRUE;
}
}
// SDL's cdecl uint32 -> int ABI; every other original export is a PE forwarder.
// Called by the application after Windows completed static DLL initialization.
extern "C" int __cdecl PSE_SDL_Init(unsigned flags) {
    if (!InitOnceExecuteOnce(&once, initialize, nullptr, nullptr)) fail(L"Loader initialization failed.", GetLastError());
    return originalInit(flags);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) self = instance;
    return TRUE; // No I/O, LoadLibrary, hooks, worker, wait or cleanup under loader lock.
}
