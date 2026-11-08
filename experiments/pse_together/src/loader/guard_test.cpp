// P27 offline fixtures (never shipped). guard_probe.dll mimics the SDL2 shim: it is statically imported by
// guard_fixture.exe, so its DllMain installs the guard before the fixture's main() runs.
#include "pe_guard.hpp"
#include <cstdio>
#include <shlobj.h>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

#ifdef PSE_PROBE_DLL
namespace {
DWORD installResult = ERROR_NOT_READY;
bool configured = false;
std::wstring env(const wchar_t* name) {
    wchar_t buffer[32768];
    const DWORD n = GetEnvironmentVariableW(name, buffer, 32768);
    return n && n < 32768 ? std::wstring(buffer, n) : std::wstring{};
}
}
extern "C" __declspec(dllexport) DWORD PSEProbeInstallResult() { return configured ? installResult : ERROR_NOT_READY; }
extern "C" __declspec(dllexport) DWORD PSEProbeInstallAgain() { return pse::guard::install({env(L"PSE_GUARD_ROOT"), env(L"PSE_GUARD_PROFILE"), L""}); }
extern "C" __declspec(dllexport) void PSEProbeStats(unsigned long long* out) {
    const auto s = pse::guard::stats();
    const unsigned long long v[] = {s.write_opens, s.allowed_disk, s.redirected, s.denied, s.device_exemptions, s.errors, s.unique_overflow, s.unique_records};
    for (int i = 0; i < 8; ++i) out[i] = v[i];
}
extern "C" __declspec(dllexport) int PSEProbeMap(const wchar_t* canonical, const wchar_t* profile, wchar_t* out, unsigned cap) {
    const auto r = pse::guard::mapExternal(canonical, profile);
    if (r.size() + 1 > cap) return -1;
    wcscpy_s(out, cap, r.c_str());
    return static_cast<int>(r.size());
}
extern "C" __declspec(dllexport) int PSEProbeCanonical(const wchar_t* in, wchar_t* out, unsigned cap) { return pse::guard::canonicalize(in, out, cap); }
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        const auto root = env(L"PSE_GUARD_ROOT");
        if (!root.empty()) { configured = true; installResult = pse::guard::install({root, env(L"PSE_GUARD_PROFILE"), env(L"PSE_GUARD_LOG")}); }
    }
    return TRUE;
}
#else
extern "C" __declspec(dllimport) DWORD PSEProbeInstallResult();
extern "C" __declspec(dllimport) DWORD PSEProbeInstallAgain();
extern "C" __declspec(dllimport) void PSEProbeStats(unsigned long long*);
extern "C" __declspec(dllimport) int PSEProbeMap(const wchar_t*, const wchar_t*, wchar_t*, unsigned);
extern "C" __declspec(dllimport) int PSEProbeCanonical(const wchar_t*, wchar_t*, unsigned);

namespace {
void escape(const std::wstring& value) {
    for (const wchar_t c : value) { if (c == L'\\' || c == L'"') std::putchar('\\'); std::putchar(static_cast<int>(c)); }
}
void result(const wchar_t* op, const std::wstring& path, bool ok, DWORD error) {
    std::printf("{\"op\":\"%ls\",\"path\":\"", op);
    escape(path);
    std::printf("\",\"ok\":%s,\"error\":%lu}\n", ok ? "true" : "false", error);
}
}

int wmain(int argc, wchar_t** argv) {
    std::printf("{\"op\":\"install\",\"code\":%lu}\n", PSEProbeInstallResult());
    for (int i = 1; i < argc; ++i) {
        const std::wstring op = argv[i];
        const std::wstring arg = i + 1 < argc ? argv[i + 1] : L"";
        if (op == L"write" || op == L"append") {
            const HANDLE f = CreateFileW(arg.c_str(), op == L"write" ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, op == L"write" ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            const DWORD error = GetLastError();
            if (f != INVALID_HANDLE_VALUE) { DWORD n; WriteFile(f, "x", 1, &n, nullptr); CloseHandle(f); }
            result(op.c_str(), arg, f != INVALID_HANDLE_VALUE, f != INVALID_HANDLE_VALUE ? 0 : error); ++i;
        } else if (op == L"read") {
            const HANDLE f = CreateFileW(arg.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            const DWORD error = GetLastError();
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
            result(op.c_str(), arg, f != INVALID_HANDLE_VALUE, f != INVALID_HANDLE_VALUE ? 0 : error); ++i;
        } else if (op == L"mkdir") {
            const BOOL ok = CreateDirectoryW(arg.c_str(), nullptr);
            result(op.c_str(), arg, ok != 0, ok ? 0 : GetLastError()); ++i;
        } else if (op == L"delete") {
            const BOOL ok = DeleteFileW(arg.c_str());
            result(op.c_str(), arg, ok != 0, ok ? 0 : GetLastError()); ++i;
        } else if (op == L"deleteonclose") {
            const HANDLE f = CreateFileW(arg.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
            const DWORD error = GetLastError();
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
            result(op.c_str(), arg, f != INVALID_HANDLE_VALUE, f != INVALID_HANDLE_VALUE ? 0 : error); ++i;
        } else if (op == L"list") {
            const HANDLE f = CreateFileW(arg.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            const DWORD error = GetLastError();
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
            result(op.c_str(), arg, f != INVALID_HANDLE_VALUE, f != INVALID_HANDLE_VALUE ? 0 : error); ++i;
        } else if (op == L"savedgames") {
            // Real documented call with KF_FLAG_CREATE: the stand's C06 trap. The folder exists, so a correct guard
            // lets the idempotent directory open through and creates nothing.
            PWSTR path = nullptr;
            const HRESULT hr = SHGetKnownFolderPath(FOLDERID_SavedGames, KF_FLAG_CREATE, nullptr, &path);
            result(op.c_str(), path ? path : L"", SUCCEEDED(hr), static_cast<DWORD>(hr));
            if (path) CoTaskMemFree(path);
        } else if (op == L"map") {
            wchar_t out[4096];
            const std::wstring profile = i + 2 < argc ? argv[i + 2] : L"";
            const int n = PSEProbeMap(arg.c_str(), profile.c_str(), out, 4096);
            result(op.c_str(), n > 0 ? std::wstring(out) : std::wstring(), n > 0, 0); i += 2;
        } else if (op == L"canonical") {
            wchar_t out[4096];
            const int n = PSEProbeCanonical(arg.c_str(), out, 4096);
            std::printf("{\"op\":\"canonical\",\"status\":%d,\"path\":\"", n);
            if (n == 1) escape(out);
            std::printf("\"}\n"); ++i;
        } else if (op == L"stress") {
            // 8 threads x 400 iterations: allowed inside write, denied outside write, redirected cache write, inside read.
            const std::wstring outsideDir = i + 2 < argc ? argv[i + 2] : L"";
            std::atomic<int> insideOk{0}, outsideDenied{0}, outsideLeaked{0}, redirectOk{0}, readOk{0};
            std::vector<std::thread> pool;
            for (int t = 0; t < 8; ++t) pool.emplace_back([&, t] {
                for (int n = 0; n < 400; ++n) {
                    const std::wstring tag = std::to_wstring(t) + L"_" + std::to_wstring(n);
                    HANDLE f = CreateFileW((arg + L"\\s" + tag + L".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f != INVALID_HANDLE_VALUE) { ++insideOk; CloseHandle(f); }
                    f = CreateFileW((outsideDir + L"\\leak" + tag + L".txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED) ++outsideDenied;
                    else if (f != INVALID_HANDLE_VALUE) { ++outsideLeaked; CloseHandle(f); }
                    f = CreateFileW((outsideDir + L"\\steamapps\\shadercache\\1678430\\r" + tag + L".bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f != INVALID_HANDLE_VALUE) { ++redirectOk; CloseHandle(f); }
                    f = CreateFileW((arg + L"\\s" + tag + L".txt").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f != INVALID_HANDLE_VALUE) { ++readOk; CloseHandle(f); }
                }
            });
            for (auto& th : pool) th.join();
            std::printf("{\"op\":\"stress\",\"inside_ok\":%d,\"outside_denied\":%d,\"outside_leaked\":%d,\"redirect_ok\":%d,\"read_ok\":%d}\n",
                insideOk.load(), outsideDenied.load(), outsideLeaked.load(), redirectOk.load(), readOk.load());
            i += 2;
        } else if (op == L"again") {
            std::printf("{\"op\":\"again\",\"code\":%lu}\n", PSEProbeInstallAgain());
        } else if (op == L"stats") {
            unsigned long long v[8];
            PSEProbeStats(v);
            std::printf("{\"op\":\"stats\",\"write_opens\":%llu,\"allowed_disk\":%llu,\"redirected\":%llu,\"denied\":%llu,\"device\":%llu,\"errors\":%llu,\"overflow\":%llu,\"unique\":%llu}\n",
                v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        }
    }
    return 0;
}
#endif
