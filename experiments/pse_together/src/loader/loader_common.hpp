#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <string>
#include <string_view>

namespace pse::loader {
inline constexpr char ExeSha[] = "b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d";
inline constexpr char SdlSha[] = "f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0";
// Frozen, accepted P1 artifacts, DOC-64; never select an unreviewed b60 build.
inline constexpr char HostSha[] = "73b76e1f0698d314905f5991a478237557f50b5c7db4daeac2536a66a9303376";
inline constexpr char ClientSha[] = "2366326a99ee05c4a9e4ec388431af901cbc15d99d9d427041a2e38c81758e97";
struct File {
    HANDLE value{INVALID_HANDLE_VALUE};
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
inline std::wstring modulePath(HMODULE module) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD n = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    return n && n < buffer.size() ? std::wstring(buffer.data(), n) : std::wstring{};
}
inline std::wstring parent(const std::wstring& path) {
    const auto at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? std::wstring{} : path.substr(0, at);
}
inline bool within(const std::wstring& path, const std::wstring& root) {
    return !root.empty() && path.size() > root.size() &&
        _wcsnicmp(path.c_str(), root.c_str(), root.size()) == 0 && path[root.size()] == L'\\';
}
// Canonical handle path, including reparse resolution. File handles stay open across LoadLibraryEx.
inline std::wstring finalPath(HANDLE handle) {
    std::array<wchar_t, 32768> buffer{};
    const DWORD n = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
    if (!n || n >= buffer.size()) return {};
    std::wstring result(buffer.data(), n);
    if (result.starts_with(L"\\\\?\\")) result.erase(0, 4);
    return result;
}
inline DWORD hashFile(const wchar_t* path, File& file, std::string& hex) {
    hex.clear();
    file.value = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return GetLastError();
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    std::array<unsigned char, 32> digest{};
    std::array<unsigned char, 65536> buffer{};
    DWORD n{};
    DWORD error = ERROR_SUCCESS;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) error = ERROR_GEN_FAILURE;
    while (!error) {
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &n, nullptr)) { error = GetLastError(); break; }
        if (!n) break;
        if (BCryptHashData(hash, buffer.data(), n, 0) < 0) { error = ERROR_GEN_FAILURE; break; }
    }
    if (!error && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) error = ERROR_GEN_FAILURE;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!error) {
        constexpr char digits[] = "0123456789abcdef";
        for (auto b : digest) { hex += digits[b >> 4]; hex += digits[b & 15]; }
    }
    return error;
}
inline std::wstring wide(std::string_view value) { return {value.begin(), value.end()}; }
inline DWORD checkExe(const wchar_t* path, std::wstring& message) {
    File file;
    std::string actual;
    DWORD error = hashFile(path, file, actual);
    if (!error && actual != ExeSha) error = ERROR_BAD_EXE_FORMAT;
    message = error ? L"PowerSlave Exhumed Together: неподдерживаемый EXE или ошибка чтения. / Unsupported EXE or read error.\n\n" : L"PowerSlave Exhumed Together: EXE SHA-256 OK.\n\n";
    message += path;
    message += L"\nОжидаемый / Expected SHA-256: " + wide(ExeSha);
    message += L"\nФактический / Actual SHA-256: " + (actual.empty() ? L"<unreadable>" : wide(actual));
    message += L"\nWin32: " + std::to_wstring(error);
    if (error) message += L"\nМод не запущен. EXE не изменён. / Mod was not started. EXE was not modified.";
    return error;
}
}
