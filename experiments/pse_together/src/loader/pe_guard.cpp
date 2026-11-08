#include "pe_guard.hpp"
#include "MinHook.h"

// Hook bodies allocate nothing on the heap and call only kernel32 file-attribute/path APIs
// that never re-enter NtCreateFile/NtOpenFile with write intent (the re-entrant read-only
// opens made by GetFinalPathNameByHandleW take the fast-allow path below).

namespace pse::guard {
namespace {
constexpr unsigned PATH_CAP = 4096;
constexpr unsigned LOG_CAP = 256;
constexpr LONG StatusDenied = static_cast<LONG>(0xc0000022u);
// Same write-intent mask as the accepted Frida guard (isolation_guard.js `decide`).
constexpr ULONG WriteMask = 0x520d0156u;
constexpr ULONG OptDirectory = 0x1;
constexpr ULONG OptDeleteOnClose = 0x1000;
constexpr ULONG OptByFileId = 0x2000;
constexpr ULONG DispOpen = 1;
constexpr ULONG DispCreate = 2;
constexpr ULONG DispOpenIf = 3;

struct UStr { USHORT length, maximum; ULONG pad; PWSTR buffer; };
struct ObjAttr { ULONG length; ULONG pad; HANDLE root; UStr* name; ULONG attributes; ULONG pad2; PVOID security; PVOID qos; };
static_assert(sizeof(UStr) == 16 && sizeof(ObjAttr) == 48, "NT structure layout");
using CreateFn = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, ObjAttr*, PVOID, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
using OpenFn = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, ObjAttr*, PVOID, ULONG, ULONG);

enum class Kind { Allow, Deny, Redirect };
enum Reason : unsigned { rDisk, rDevice, rExternal, rReparse, rById, rInvalid, rOverflow, rRedirect, rDirExists, rRedirectRead, rCount };
constexpr const char* ReasonText[rCount] = {"disk", "device_exemption", "unresolved_or_external_namespace", "reparse_ancestor",
    "FILE_OPEN_BY_FILE_ID", "invalid_native_attributes", "native_buffer_overflow", "redirected_external_write", "existing_directory_open", "redirected_external_read"};

struct State {
    wchar_t root[PATH_CAP]; unsigned rootLen;
    wchar_t profile[PATH_CAP]; unsigned profileLen;
    HANDLE log;
    CreateFn createOriginal; OpenFn openOriginal;
    SRWLOCK lock;
    volatile LONG installedFlag;
    volatile LONG64 counts[8];
    unsigned recordCount;
    struct Record { Kind kind; wchar_t path[512]; } records[LOG_CAP];
};
State g{};
enum Count { cWrite, cAllowed, cRedirected, cDenied, cDevice, cErrors, cOverflow, cUnique };

wchar_t lowerc(wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + 32) : c; }
unsigned len(const wchar_t* s, unsigned cap) { unsigned i = 0; while (i < cap && s[i]) ++i; return i; }
bool eqCi(const wchar_t* a, unsigned an, const wchar_t* lit) {
    unsigned i = 0;
    for (; lit[i]; ++i) if (i >= an || lowerc(a[i]) != lowerc(lit[i])) return false;
    return i == an;
}
bool startsCi(const wchar_t* a, const wchar_t* lit) {
    for (unsigned i = 0; lit[i]; ++i) if (lowerc(a[i]) != lowerc(lit[i])) return false;
    return true;
}
bool within(const wchar_t* path, const wchar_t* root, unsigned rootLen) {
    for (unsigned i = 0; i < rootLen; ++i) if (lowerc(path[i]) != lowerc(root[i])) return false;
    return !path[rootLen] || path[rootLen] == L'\\';
}
bool readable(const void* value, SIZE_T size, bool writing) {
    auto at = reinterpret_cast<ULONG_PTR>(value);
    const ULONG_PTR end = at + size;
    if (!at || end < at) return false;
    while (at < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        if (!(info.Protect & (writing ? (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)
                                      : (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))) return false;
        const auto next = reinterpret_cast<ULONG_PTR>(info.BaseAddress) + info.RegionSize;
        if (next <= at) return false;
        at = next;
    }
    return true;
}

bool deviceName(const wchar_t* path) {
    const wchar_t* tail;
    if (startsCi(path, L"\\Device\\")) {
        tail = path + 8;
        const unsigned n = len(tail, PATH_CAP);
        if (eqCi(tail, n, L"Null") || startsCi(tail, L"NamedPipe\\") || startsCi(tail, L"ConDrv\\") || startsCi(tail, L"Afd\\") ||
            eqCi(tail, n, L"Nsi") || eqCi(tail, n, L"KsecDD") || eqCi(tail, n, L"MMCSS\\MmThread")) return true;
        if (startsCi(tail, L"000000")) {
            unsigned i = 6;
            while ((tail[i] >= L'0' && tail[i] <= L'9') || (lowerc(tail[i]) >= L'a' && lowerc(tail[i]) <= L'f')) ++i;
            if (i > 6 && !tail[i]) return true;
        }
    }
    if (startsCi(path, L"\\??\\")) tail = path + 4;
    else if (startsCi(path, L"\\DosDevices\\")) tail = path + 12;
    else return false;
    const unsigned n = len(tail, PATH_CAP);
    if (eqCi(tail, n, L"NUL") || eqCi(tail, n, L"CONIN$") || eqCi(tail, n, L"CONOUT$") || startsCi(tail, L"PIPE\\")) return true;
    if (startsCi(tail, L"HID#")) {
        unsigned i = 4;
        while (tail[i] && tail[i] != L'\\') ++i;
        if (i > 4 && (!tail[i] || eqCi(tail + i, len(tail + i, PATH_CAP), L"\\kbd"))) return true;
    }
    return false;
}

struct Seg { const wchar_t* p; unsigned n; };
int segments(const wchar_t* path, Seg* out, unsigned max) {
    unsigned count = 0, i = 0;
    while (path[i]) {
        while (path[i] == L'\\') ++i;
        if (!path[i]) break;
        const unsigned start = i;
        while (path[i] && path[i] != L'\\') ++i;
        if (count >= max) return -1;
        out[count++] = {path + start, i - start};
    }
    return static_cast<int>(count);
}
bool segIs(const Seg& s, const wchar_t* lit) { return eqCi(s.p, s.n, lit); }
unsigned append(wchar_t* out, unsigned at, unsigned cap, const wchar_t* text, unsigned n) {
    if (at + n >= cap) return cap;  // overflow marker
    for (unsigned i = 0; i < n; ++i) out[at + i] = text[i];
    out[at + n] = 0;
    return at + n;
}
// The four external-write classes the Frida stand redirected (observer.js isolatedFilePath).
bool mapExternalBuf(const wchar_t* path, const wchar_t* profile, unsigned profileLen, wchar_t* out, unsigned cap) {
    Seg s[96];
    const int n = segments(path, s, 96);
    if (n < 3) return false;
    const wchar_t* dir = nullptr;
    const wchar_t* rest = nullptr;
    if (n >= 7 && segIs(s[1], L"Users") && segIs(s[3], L"Saved Games") && segIs(s[4], L"Nightdive Studios") && segIs(s[5], L"Powerslave EX")) {
        dir = L"\\early_native_logs\\"; rest = s[6].p;
    } else if (n >= 4 && segIs(s[n - 2], L"logs") && segIs(s[n - 3], L"Steam") && startsCi(s[n - 1].p, L"gameoverlay_renderer") &&
               s[n - 1].n > 4 && eqCi(s[n - 1].p + s[n - 1].n - 4, 4, L".txt")) {
        dir = L"\\steam_overlay\\"; rest = s[n - 1].p;
    } else if (n == 5 && segIs(s[1], L".cache") && segIs(s[2], L"AMD") && segIs(s[3], L"DxcCache")) {
        dir = L"\\gpu_cache\\"; rest = s[4].p;
    } else {
        for (int i = 1; i + 3 < n; ++i)
            if (segIs(s[i], L"steamapps") && segIs(s[i + 1], L"shadercache") && segIs(s[i + 2], L"1678430")) {
                dir = L"\\gpu_cache\\steam\\"; rest = s[i + 3].p; break;
            }
    }
    if (!dir) return false;
    unsigned at = append(out, 0, cap, profile, profileLen);
    if (at >= cap) return false;
    at = append(out, at, cap, dir, len(dir, 64));
    if (at >= cap) return false;
    at = append(out, at, cap, rest, len(rest, PATH_CAP));
    return at < cap;
}

void jsonLine(const char* kind, const char* api, const char* reason, const wchar_t* path, const wchar_t* to, ACCESS_MASK access, ULONG disposition, ULONG options) {
    if (g.log == INVALID_HANDLE_VALUE) return;
    char line[2600];
    unsigned at = 0;
    const auto put = [&](const char* text) { while (*text && at < sizeof(line) - 8) line[at++] = *text++; };
    const auto putPath = [&](const wchar_t* w) {
        for (unsigned i = 0; w[i] && at < sizeof(line) - 12; ++i) {
            const wchar_t c = w[i];
            if (c == L'\\' || c == L'"') { line[at++] = '\\'; line[at++] = static_cast<char>(c); }
            else if (c < 32 || c > 126) {
                static const char hex[] = "0123456789abcdef";
                line[at++] = '\\'; line[at++] = 'u';
                line[at++] = hex[(c >> 12) & 15]; line[at++] = hex[(c >> 8) & 15]; line[at++] = hex[(c >> 4) & 15]; line[at++] = hex[c & 15];
            } else line[at++] = static_cast<char>(c);
        }
    };
    put("{\"kind\":\""); put(kind); put("\",\"api\":\""); put(api); put("\",\"reason\":\""); put(reason); put("\",\"path\":\""); putPath(path);
    if (to) { put("\",\"to\":\""); putPath(to); }
    char tail[96];
    sprintf_s(tail, "\",\"access\":\"0x%lx\",\"disposition\":%lu,\"options\":\"0x%lx\"}\n", static_cast<unsigned long>(access),
              static_cast<unsigned long>(disposition), static_cast<unsigned long>(options));
    put(tail);
    DWORD written = 0;
    WriteFile(g.log, line, at, &written, nullptr);
}

// Unique ledger per (kind,path); every unique denied/redirected path is also appended to the JSONL file.
void record(Kind kind, unsigned api, unsigned reason, const wchar_t* path, const wchar_t* to, ACCESS_MASK access, ULONG disposition, ULONG options) {
    AcquireSRWLockExclusive(&g.lock);
    bool known = false;
    for (unsigned i = 0; i < g.recordCount && !known; ++i) {
        known = g.records[i].kind == kind && len(path, 512) == len(g.records[i].path, 512);
        for (unsigned j = 0; known && path[j] && j < 511; ++j) known = lowerc(path[j]) == lowerc(g.records[i].path[j]);
    }
    if (!known) {
        if (g.recordCount >= LOG_CAP) InterlockedIncrement64(&g.counts[cOverflow]);
        else {
            auto& item = g.records[g.recordCount++];
            item.kind = kind;
            unsigned i = 0;
            for (; path[i] && i < 511; ++i) item.path[i] = path[i];
            item.path[i] = 0;
            InterlockedIncrement64(&g.counts[cUnique]);
            jsonLine(kind == Kind::Deny ? "denied" : "redirected", api ? "NtOpenFile" : "NtCreateFile", ReasonText[reason], path, to, access, disposition, options);
        }
    }
    ReleaseSRWLockExclusive(&g.lock);
}

// 1 resolved (NT or DOS spelling in `path`), 0 unresolved, -1 invalid, -2 overflow.
int objectPath(ObjAttr* oa, wchar_t* path) {
    const auto text = [&](const char* s) { unsigned i = 0; while (s[i] && i < PATH_CAP - 1) { path[i] = static_cast<unsigned char>(s[i]); ++i; } path[i] = 0; };
    if (!readable(oa, sizeof(ObjAttr), false) || oa->length != sizeof(ObjAttr)) { text("<invalid OBJECT_ATTRIBUTES>"); return -1; }
    UStr* name = oa->name;
    if (!name) { text("<unnamed write-open>"); return 0; }
    if (!readable(name, sizeof(UStr), false) || (name->length & 1)) { text("<invalid UNICODE_STRING>"); return -1; }
    const unsigned n = name->length / 2;
    if (n >= PATH_CAP) { text("<native path overflow>"); return -2; }
    const wchar_t* buffer = name->buffer;
    if (n && !readable(buffer, static_cast<SIZE_T>(n) * 2, false)) { text("<unreadable UNICODE_STRING>"); return -1; }
    for (unsigned i = 0; i < n; ++i) { if (!buffer[i]) { text("<embedded native path NUL>"); return -1; } path[i] = buffer[i]; }
    path[n] = 0;
    if (n && path[0] == L'\\') return 1;
    if (!oa->root) { if (!n) text("<empty native path>"); return 0; }
    wchar_t rootPath[PATH_CAP];
    const DWORD rootLen = GetFinalPathNameByHandleW(oa->root, rootPath, PATH_CAP, 0);
    if (!rootLen) { text("<unresolved RootDirectory>"); return 0; }
    if (rootLen >= PATH_CAP || rootLen + (n ? 1 + n : 0) >= PATH_CAP) { text("<RootDirectory path overflow>"); return -2; }
    unsigned total = rootLen;
    for (unsigned i = 0; i < rootLen; ++i) path[i] = rootPath[i];
    if (n) { path[total++] = L'\\'; for (unsigned i = 0; i < n; ++i) path[total + i] = buffer[i]; total += n; }
    path[total] = 0;
    return 1;
}

bool noReparse(const wchar_t* canonical) {
    wchar_t work[PATH_CAP];
    unsigned n = len(canonical, PATH_CAP);
    if (n >= PATH_CAP) return false;
    for (unsigned i = 0; i <= n; ++i) work[i] = canonical[i];
    while (n > 3) {
        const DWORD attrs = GetFileAttributesW(work);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        while (n > 3 && work[n - 1] != L'\\') --n;
        if (n > 3) --n;
        work[n] = 0;
    }
    return true;
}

struct Decision { Kind kind; wchar_t nt[PATH_CAP + 8]; };

bool hasMarker(const wchar_t* path) {
    // Cheap pre-filter for read-only opens: only paths of the four redirect classes need resolving.
    static const wchar_t* const markers[] = {L"Powerslave EX", L"gameoverlay_renderer", L"shadercache", L"DxcCache"};
    for (const wchar_t* m : markers) {
        const unsigned mlen = len(m, 64);
        for (unsigned i = 0; path[i]; ++i) {
            unsigned j = 0;
            while (j < mlen && path[i + j] && lowerc(path[i + j]) == lowerc(m[j])) ++j;
            if (j == mlen) return true;
        }
    }
    return false;
}

// Read-only open: the profile copy of the four external classes must be what the engine sees (the Frida stand remapped
// every CreateFileW of these paths, so the user's real config never leaked into the test profile and the profile
// kexengine.cfg could select the renderer). Everything else passes through untouched.
Kind decideRead(unsigned api, ACCESS_MASK access, ObjAttr* oa, ULONG disposition, ULONG options, Decision& out) {
    wchar_t path[PATH_CAP], canonical[PATH_CAP], mapped[PATH_CAP];
    if (objectPath(oa, path) != 1 || !hasMarker(path)) return Kind::Allow;
    if (canonicalize(path, canonical, PATH_CAP) != 1 || within(canonical, g.root, g.rootLen)) return Kind::Allow;
    if (!mapExternalBuf(canonical, g.profile, g.profileLen, mapped, PATH_CAP) || !within(mapped, g.root, g.rootLen) || !noReparse(mapped)) return Kind::Allow;
    unsigned at = 0;
    for (const wchar_t* p = L"\\??\\"; *p; ++p) out.nt[at++] = *p;
    for (unsigned i = 0; mapped[i] && at < PATH_CAP + 7; ++i) out.nt[at++] = mapped[i];
    out.nt[at] = 0;
    InterlockedIncrement64(&g.counts[cRedirected]);
    record(Kind::Redirect, api, rRedirectRead, canonical, out.nt, access, disposition, options);
    out.kind = Kind::Redirect;
    return Kind::Redirect;
}

Kind decide(unsigned api, ACCESS_MASK access, ObjAttr* oa, ULONG disposition, ULONG options, Decision& out) {
    out.kind = Kind::Allow;
    if (!(access & WriteMask) && disposition == DispOpen && !(options & OptDeleteOnClose)) return decideRead(api, access, oa, disposition, options, out);
    InterlockedIncrement64(&g.counts[cWrite]);
    wchar_t path[PATH_CAP], canonical[PATH_CAP];
    Kind kind = Kind::Deny;
    unsigned reason = rExternal;
    const wchar_t* logPath = path;
    const wchar_t* logTo = nullptr;
    bool error = false;
    int r = objectPath(oa, path);
    if (r < 0) { reason = r == -2 ? rOverflow : rInvalid; error = r == -1; }
    else if (r == 1) {
        r = canonicalize(path, canonical, PATH_CAP);
        if (r < 0) { reason = r == -2 ? rOverflow : rInvalid; error = r == -1; }
        else if (r == 1) {
            reason = rDisk; logPath = canonical;
            if (options & OptByFileId) reason = rById;
            else if (within(canonical, g.root, g.rootLen)) {
                if (noReparse(canonical)) kind = Kind::Allow; else reason = rReparse;
            } else if ((disposition == DispOpenIf || disposition == DispCreate) && (options & OptDirectory) && !(access & WriteMask) &&
                       (GetFileAttributesW(canonical) != INVALID_FILE_ATTRIBUTES) &&
                       (GetFileAttributesW(canonical) & FILE_ATTRIBUTE_DIRECTORY)) {
                kind = Kind::Allow; reason = rDirExists;  // CreateDirectory/known-folder CREATE of an EXISTING directory: OS returns open/collision, nothing is created
            } else {
                wchar_t mapped[PATH_CAP];
                if (mapExternalBuf(canonical, g.profile, g.profileLen, mapped, PATH_CAP) && within(mapped, g.root, g.rootLen) && noReparse(mapped)) {
                    unsigned at = 0;
                    for (const wchar_t* p = L"\\??\\"; *p; ++p) out.nt[at++] = *p;
                    for (unsigned i = 0; mapped[i] && at < PATH_CAP + 7; ++i) out.nt[at++] = mapped[i];
                    out.nt[at] = 0;
                    kind = Kind::Redirect; reason = rRedirect; logTo = out.nt;
                }
            }
        } else if (deviceName(path)) { kind = Kind::Allow; reason = rDevice; }
    }
    if (kind == Kind::Allow) { InterlockedIncrement64(&g.counts[reason == rDevice ? cDevice : cAllowed]); return Kind::Allow; }
    if (kind == Kind::Redirect) { InterlockedIncrement64(&g.counts[cRedirected]); record(Kind::Redirect, api, reason, logPath, logTo, access, disposition, options); out.kind = kind; return kind; }
    InterlockedIncrement64(&g.counts[cDenied]);
    if (error) InterlockedIncrement64(&g.counts[cErrors]);
    record(Kind::Deny, api, reason, logPath, nullptr, access, disposition, options);
    return Kind::Deny;
}

LONG denyResult(PHANDLE out, PVOID iosb) {
    if (readable(out, sizeof(HANDLE), true)) *out = nullptr;
    if (readable(iosb, 16, true)) { auto* p = static_cast<ULONG_PTR*>(iosb); p[0] = static_cast<ULONG_PTR>(static_cast<ULONG>(StatusDenied)); p[1] = 0; }
    return StatusDenied;
}

LONG NTAPI hookCreate(PHANDLE out, ACCESS_MASK access, ObjAttr* oa, PVOID iosb, PLARGE_INTEGER alloc, ULONG attrs, ULONG share, ULONG disposition, ULONG options, PVOID ea, ULONG eaSize) {
    const DWORD error = GetLastError();
    Decision d;
    const Kind kind = decide(0, access, oa, disposition, options, d);
    SetLastError(error);
    if (kind == Kind::Deny) return denyResult(out, iosb);
    if (kind == Kind::Redirect) {
        UStr name{static_cast<USHORT>(len(d.nt, PATH_CAP) * 2), static_cast<USHORT>((len(d.nt, PATH_CAP) + 1) * 2), 0, d.nt};
        ObjAttr copy = *oa; copy.root = nullptr; copy.name = &name;
        return g.createOriginal(out, access, &copy, iosb, alloc, attrs, share, disposition, options, ea, eaSize);
    }
    return g.createOriginal(out, access, oa, iosb, alloc, attrs, share, disposition, options, ea, eaSize);
}
LONG NTAPI hookOpen(PHANDLE out, ACCESS_MASK access, ObjAttr* oa, PVOID iosb, ULONG share, ULONG options) {
    const DWORD error = GetLastError();
    Decision d;
    const Kind kind = decide(1, access, oa, DispOpen, options, d);
    SetLastError(error);
    if (kind == Kind::Deny) return denyResult(out, iosb);
    if (kind == Kind::Redirect) {
        UStr name{static_cast<USHORT>(len(d.nt, PATH_CAP) * 2), static_cast<USHORT>((len(d.nt, PATH_CAP) + 1) * 2), 0, d.nt};
        ObjAttr copy = *oa; copy.root = nullptr; copy.name = &name;
        return g.openOriginal(out, access, &copy, iosb, share, options);
    }
    return g.openOriginal(out, access, oa, iosb, share, options);
}

bool absoluteInside(const std::wstring& value, const std::wstring& root) {
    wchar_t a[PATH_CAP], b[PATH_CAP];
    return canonicalize(value.c_str(), a, PATH_CAP) == 1 && canonicalize(root.c_str(), b, PATH_CAP) == 1 &&
        len(a, PATH_CAP) > len(b, PATH_CAP) && within(a, b, len(b, PATH_CAP));
}
}  // namespace

int canonicalize(const wchar_t* value, wchar_t* out, unsigned capacity) {
    if (!value || !out || capacity < 8) return -1;
    const wchar_t* path = value;
    if (startsCi(path, L"\\\\?\\") || startsCi(path, L"\\??\\")) path += 4;
    else if (startsCi(path, L"\\DosDevices\\")) path += 12;
    if (lowerc(path[0]) < L'a' || lowerc(path[0]) > L'z' || path[1] != L':' || (path[2] != L'\\' && path[2] != L'/')) return 0;
    out[0] = path[0]; out[1] = L':'; out[2] = L'\\'; out[3] = 0;
    unsigned n = 3, i = 3;
    while (path[i]) {
        while (path[i] == L'\\' || path[i] == L'/') ++i;
        if (!path[i]) break;
        const unsigned start = i;
        while (path[i] && path[i] != L'\\' && path[i] != L'/') ++i;
        const unsigned size = i - start;
        if (size == 1 && path[start] == L'.') continue;
        if (size == 2 && path[start] == L'.' && path[start + 1] == L'.') {
            if (n <= 3) return -1;
            while (n > 3 && out[n - 1] != L'\\') --n;
            if (n > 3) --n;
            out[n] = 0;
            continue;
        }
        const wchar_t last = path[i - 1];
        if (last == L'.' || last == L' ') return -1;
        for (unsigned j = 0; j < size; ++j) if (path[start + j] == L':' || path[start + j] == L'*' || path[start + j] == L'?' || path[start + j] == L'"' ||
                                                path[start + j] == L'<' || path[start + j] == L'>' || path[start + j] == L'|') return -1;
        if (n > 3) { if (n + 1 >= capacity) return -2; out[n++] = L'\\'; }
        if (n + size >= capacity) return -2;
        for (unsigned j = 0; j < size; ++j) out[n++] = path[start + j];
        out[n] = 0;
    }
    return 1;
}

std::wstring mapExternal(const std::wstring& canonical, const std::wstring& profile) {
    wchar_t out[PATH_CAP];
    return mapExternalBuf(canonical.c_str(), profile.c_str(), static_cast<unsigned>(profile.size()), out, PATH_CAP) ? std::wstring(out) : std::wstring();
}

bool installed() { return g.installedFlag == 1; }

Stats stats() {
    Stats s{};
    AcquireSRWLockExclusive(&g.lock);
    s = {static_cast<unsigned long long>(g.counts[cWrite]), static_cast<unsigned long long>(g.counts[cAllowed]), static_cast<unsigned long long>(g.counts[cRedirected]),
         static_cast<unsigned long long>(g.counts[cDenied]), static_cast<unsigned long long>(g.counts[cDevice]), static_cast<unsigned long long>(g.counts[cErrors]),
         static_cast<unsigned long long>(g.counts[cOverflow]), static_cast<unsigned long long>(g.counts[cUnique])};
    ReleaseSRWLockExclusive(&g.lock);
    return s;
}

DWORD install(const Config& config) {
    if (InterlockedCompareExchange(&g.installedFlag, 2, 0) != 0) return ERROR_ALREADY_EXISTS;
    const auto fail = [&](DWORD code) { InterlockedExchange(&g.installedFlag, 0); return code; };
    wchar_t root[PATH_CAP];
    if (canonicalize(config.root.c_str(), root, PATH_CAP) != 1) return fail(ERROR_BAD_PATHNAME);
    if (!absoluteInside(config.profile, config.root)) return fail(ERROR_BAD_PATHNAME);
    wchar_t profile[PATH_CAP];
    if (canonicalize(config.profile.c_str(), profile, PATH_CAP) != 1) return fail(ERROR_BAD_PATHNAME);
    g.rootLen = len(root, PATH_CAP);
    for (unsigned i = 0; i <= g.rootLen; ++i) g.root[i] = root[i];
    g.profileLen = len(profile, PATH_CAP);
    for (unsigned i = 0; i <= g.profileLen; ++i) g.profile[i] = profile[i];
    g.log = INVALID_HANDLE_VALUE;
    if (!config.log.empty()) {
        if (!absoluteInside(config.log, config.root)) return fail(ERROR_BAD_PATHNAME);
        g.log = CreateFileW(config.log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g.log == INVALID_HANDLE_VALUE) return fail(GetLastError());
    }
    InitializeSRWLock(&g.lock);
    const auto rollback = [&](DWORD code) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (g.log != INVALID_HANDLE_VALUE) { CloseHandle(g.log); g.log = INVALID_HANDLE_VALUE; }
        return fail(code);
    };
    if (MH_Initialize() != MH_OK) return rollback(ERROR_DLL_INIT_FAILED);
    void* createOriginal = nullptr;
    void* openOriginal = nullptr;
    if (MH_CreateHookApi(L"ntdll.dll", "NtCreateFile", reinterpret_cast<void*>(&hookCreate), &createOriginal) != MH_OK ||
        MH_CreateHookApi(L"ntdll.dll", "NtOpenFile", reinterpret_cast<void*>(&hookOpen), &openOriginal) != MH_OK) return rollback(ERROR_PROC_NOT_FOUND);
    g.createOriginal = reinterpret_cast<CreateFn>(createOriginal);
    g.openOriginal = reinterpret_cast<OpenFn>(openOriginal);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) return rollback(ERROR_ACCESS_DENIED);
    InterlockedExchange(&g.installedFlag, 1);
    return ERROR_SUCCESS;
}
}  // namespace pse::guard
