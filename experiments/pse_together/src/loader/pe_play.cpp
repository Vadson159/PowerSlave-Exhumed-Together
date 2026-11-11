// P27/S9.6-S9.7: pe-play.exe - starts the game copy with an isolated profile and the native write guard.
// Layout (install root = folder of this exe unless --root is given):  <root>\game\  <root>\profile\  (logs in profile)
// Mirrors experiments/m0/launch.py: fresh sub-directories, redirected APPDATA/LOCALAPPDATA/TEMP/USERPROFILE,
// steam_appid.txt in the CWD, Valve Vulkan-layer disable keys, -basepath/-userpath. No Steam/original install writes.
#include "loader_common.hpp"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>
#include <vector>

namespace {
// Standard Powerslave EX keyboard/mouse/pad binds (identical to the accepted lab host config and to a stock install).
// A profile config that only has the renderer line would leave the game without movement keys.
const char DefaultBinds[] =
    "bind tab \"automap\"\n"
    "bind space \"jump\"\n"
    "bind 1 \"weapon 0\"\n"
    "bind 2 \"weapon 1\"\n"
    "bind 3 \"weapon 2\"\n"
    "bind 4 \"weapon 3\"\n"
    "bind 5 \"weapon 4\"\n"
    "bind 6 \"weapon 5\"\n"
    "bind 7 \"weapon 6\"\n"
    "bind 8 \"weapon 7\"\n"
    "bind a \"strafeleft\"\n"
    "bind d \"straferight\"\n"
    "bind e \"+use\"\n"
    "bind q \"*weaponwheel\"\n"
    "bind s \"backward\"\n"
    "bind w \"forward\"\n"
    "bind f5 \"screenshot\"\n"
    "bind lctrl \"mapzoomout\"\n"
    "bind lshift \"inventoryscreen\"\n"
    "bind lalt \"mapzoomin\"\n"
    "bind mouse_left \"attack\"\n"
    "bind mouse_wheel_up \"+weapnext\"\n"
    "bind mouse_wheel_down \"+weapprev\"\n"
    "bind joy_0 \"+use\"\n"
    "bind joy_2 \"*weaponwheel\"\n"
    "bind joy_3 \"automap\"\n"
    "bind joy_4 \"inventoryscreen\"\n"
    "bind joy_9 \"+weapprev\"\n"
    "bind joy_10 \"+weapnext\"\n"
    "bind joy_11 \"mapzoomin\"\n"
    "bind joy_12 \"mapzoomout\"\n"
    "bind joy_13 \"turnleft\"\n"
    "bind joy_14 \"turnright\"\n"
    "bind joy_15 \"jump\"\n"
    "bind joy_16 \"attack\"\n";
bool makeDir(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
bool makeTree(const std::wstring& path) {
    const auto at = path.find_last_of(L'\\');
    if (at != std::wstring::npos && at > 2 && GetFileAttributesW(path.substr(0, at).c_str()) == INVALID_FILE_ATTRIBUTES && !makeTree(path.substr(0, at))) return false;
    return makeDir(path);
}
std::wstring fullPath(const std::wstring& value) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD n = GetFullPathNameW(value.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    return n && n < buffer.size() ? std::wstring(buffer.data(), n) : std::wstring{};
}
std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (const wchar_t c : value) {
        if (c == L'\\') { ++slashes; out += c; continue; }
        if (c == L'"') { out.append(slashes, L'\\'); out += L"\\\""; slashes = 0; continue; }
        slashes = 0; out += c;
    }
    out.append(slashes, L'\\');
    return out + L"\"";
}
}

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    std::wstring root = pse::loader::parent(pse::loader::modulePath(nullptr));
    std::vector<std::wstring> extra;
    bool dryRun = false;
    std::wstring role;  // host | client: selects the payload (broker v2), profile\<role>, and the D3D11 profile config
    std::wstring instance;  // --instance N (1..9): another client of the same install uses its own profile\client<N> (several players on one PC)
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--root" && i + 1 < argc) root = argv[++i];
        else if (a == L"--role" && i + 1 < argc) role = argv[++i];
        else if (a == L"--instance" && i + 1 < argc) instance = argv[++i];
        else if (a == L"--dry-run") dryRun = true;  // prepare profile + print the plan, do not start the game
        else extra.push_back(a);
    }
    root = fullPath(root);
    if (root.empty()) { std::wprintf(L"Invalid --root.\n"); return ERROR_BAD_PATHNAME; }
    if (!role.empty() && role != L"host" && role != L"client") { std::wprintf(L"--role must be host or client.\n"); return ERROR_INVALID_PARAMETER; }
    if (!instance.empty() && (role.empty() || instance.size() != 1 || instance[0] < L'1' || instance[0] > L'9')) { std::wprintf(L"--instance must be one digit 1..9 and needs --role.\n"); return ERROR_INVALID_PARAMETER; }
    const std::wstring game = root + L"\\game", profile = root + L"\\profile" + (role.empty() ? L"" : L"\\" + role + instance);
    const std::wstring exe = game + L"\\anubis_Shipping_Steam_x64.exe";
    std::wstring message;
    const DWORD check = pse::loader::checkExe(exe.c_str(), message);
    if (check) { std::wprintf(L"%ls\n", message.c_str()); return static_cast<int>(check); }
    // The mod's SDL2.dll forwards to the game's original one, kept next to it as SDL2_pse_original.dll (install.bat makes it). Without it Windows only says
    // "SDL2_pse_original.DLL was not found" (exit code -1073741515): say what to do instead.
    if (GetFileAttributesW((game + L"\\SDL2_pse_original.dll").c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wprintf(L"SDL2_pse_original.dll is missing in %ls\nRun install.bat again (it copies it from your Steam game folder).\n", game.c_str());
        return ERROR_FILE_NOT_FOUND;
    }
    // early_native_logs\saves: the guard serves Saved Games\...\Powerslave EX\ from early_native_logs and the game does not create the saves directory
    // itself, so without it every save (autosave at level start, savegame) failed with "Unable to open ... for writing". Each machine keeps its own saves.
    for (const wchar_t* sub : {L"", L"\\roaming", L"\\local", L"\\temp", L"\\user", L"\\early_saved_games", L"\\early_native_logs", L"\\early_native_logs\\saves", L"\\gpu_cache",
                               L"\\gpu_cache\\steam", L"\\gpu_cache\\steam\\AMDv1", L"\\gpu_cache\\steam\\fozpipelinesv6", L"\\steam_overlay", L"\\logs"})
        if (!makeTree(profile + sub)) { std::wprintf(L"Cannot create %ls%ls (Win32 %lu)\n", profile.c_str(), sub, GetLastError()); return ERROR_CANNOT_MAKE; }
    // The P23 HUD/lobby renders through D3D11; the stand wrote this into the profile kexengine.cfg (the guard serves the
    // profile copy of Saved Games\...\kexengine.cfg, so the user's real config is neither read nor written).
    if (!role.empty()) {
        const std::wstring cfg = profile + L"\\early_native_logs\\kexengine.cfg";
        std::string text;
        const HANDLE rf = CreateFileW(cfg.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rf != INVALID_HANDLE_VALUE) {
            std::vector<char> buf(65536);
            DWORD n{};
            while (ReadFile(rf, buf.data(), static_cast<DWORD>(buf.size()), &n, nullptr) && n) text.append(buf.data(), n);
            CloseHandle(rf);
        }
        // Idempotent: only what is missing is appended (the engine rewrites this file at exit without the binds of a fresh profile).
        std::string add;
        if (text.find("r_rhirenderfamily") == std::string::npos) add += "seta r_rhirenderfamily \"d3d11\"\n";
        if (text.rfind("bind ", 0) != 0 && text.find("\nbind ") == std::string::npos) add += DefaultBinds;
        if (!add.empty()) {
            const HANDLE f = CreateFileW(cfg.c_str(), FILE_APPEND_DATA, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE) { std::wprintf(L"Cannot write profile kexengine.cfg\n"); return ERROR_WRITE_FAULT; }
            DWORD written{};
            WriteFile(f, add.data(), static_cast<DWORD>(add.size()), &written, nullptr);
            CloseHandle(f);
        }
    }
    // Steam's documented development AppID file must be in the process CWD (same as the lab launcher).
    const std::wstring appid = profile + L"\\steam_appid.txt";
    if (GetFileAttributesW(appid.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const HANDLE f = CreateFileW(appid.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) { std::wprintf(L"Cannot write steam_appid.txt\n"); return ERROR_WRITE_FAULT; }
        DWORD written{};
        WriteFile(f, "1678430\n", 8, &written, nullptr);
        CloseHandle(f);
    }
    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t stamp[40];
    swprintf_s(stamp, L"%04u%02u%02u_%02u%02u%02u_%lu", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    const std::wstring guardLog = profile + L"\\logs\\guard_" + stamp + L".jsonl";
    SetEnvironmentVariableW(L"APPDATA", (profile + L"\\roaming").c_str());
    SetEnvironmentVariableW(L"LOCALAPPDATA", (profile + L"\\local").c_str());
    SetEnvironmentVariableW(L"TEMP", (profile + L"\\temp").c_str());
    SetEnvironmentVariableW(L"TMP", (profile + L"\\temp").c_str());
    SetEnvironmentVariableW(L"USERPROFILE", (profile + L"\\user").c_str());
    SetEnvironmentVariableW(L"DISABLE_VK_LAYER_VALVE_steam_fossilize_1", L"1");
    SetEnvironmentVariableW(L"DISABLE_VK_LAYER_VALVE_steam_overlay_1", L"1");
    SetEnvironmentVariableW(L"PSE_GUARD_ROOT", root.c_str());
    SetEnvironmentVariableW(L"PSE_GUARD_PROFILE", profile.c_str());
    SetEnvironmentVariableW(L"PSE_GUARD_LOG", guardLog.c_str());
    const std::wstring nativeLog = profile + L"\\logs\\native_" + (role.empty() ? L"none" : role) + L"_" + stamp + L".jsonl";
    if (!role.empty()) {
        SetEnvironmentVariableW(L"PSE_ROLE", role.c_str());
        SetEnvironmentVariableW(L"PSE_NATIVE_LOG", nativeLog.c_str());
    }
    std::wstring cmd = quote(exe) + L" -basepath " + quote(game) + L" -userpath " + quote(profile);
    if (extra.empty()) cmd += L" -window -width 960 -height 540 -skipmovies -stdout";
    else for (const auto& a : extra) cmd += L" " + quote(a);
    std::wprintf(L"root=%ls\nprofile=%ls\nrole=%ls\nguard_log=%ls\nnative_log=%ls\ncommand=%ls\n", root.c_str(), profile.c_str(), role.empty() ? L"(none, guard only)" : role.c_str(),
                 guardLog.c_str(), role.empty() ? L"(none)" : nativeLog.c_str(), cmd.c_str());
    if (dryRun) return 0;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    if (!CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, profile.c_str(), &si, &pi)) {
        const DWORD error = GetLastError();
        std::wprintf(L"CreateProcess failed: Win32 %lu\n", error);
        return static_cast<int>(error);
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code{};
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    std::wprintf(L"game exit code %lu\n", code);
    return static_cast<int>(code);
}
