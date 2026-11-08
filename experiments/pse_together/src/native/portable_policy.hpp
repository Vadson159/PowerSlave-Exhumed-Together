#pragma once
// P27/S9.5 (minimal): install-root policy shared by the portable P23 payloads.
// pe-play.exe exports PSE_GUARD_ROOT (the install root that also confines writes through the native guard).
// With it, the process must be <root>\game\anubis_Shipping_Steam_x64.exe and the payload/log must live inside <root>.
// Without it the unchanged lab policy (profile::Executable / ClientExe / profile::Workspace) applies.
#include <windows.h>
#include <cwchar>

namespace pse::portable {
inline constexpr wchar_t GameExe[] = L"\\game\\anubis_Shipping_Steam_x64.exe";
// True and fills `root` (no trailing separator) when launched by pe-play.
inline bool root(wchar_t* out, DWORD capacity) {
    const DWORD n = GetEnvironmentVariableW(L"PSE_GUARD_ROOT", out, capacity);
    if (!n || n >= capacity) return false;
    if (out[n - 1] == L'\\' && n > 3) out[n - 1] = 0;
    return true;
}
inline bool exeMatches(const wchar_t* exe, const wchar_t* root) {
    wchar_t expected[32768];
    if (swprintf_s(expected, L"%ls%ls", root, GameExe) < 0) return false;
    return _wcsicmp(exe, expected) == 0;
}
}  // namespace pse::portable
