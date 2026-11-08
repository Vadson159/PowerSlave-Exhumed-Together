// External diagnostics for our standalone ENet executables, never for KEX.
// No DLL injection, hooks, registry settings or game/profile access.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
namespace fs = std::filesystem;

static std::wstring quote(const std::wstring& s) {
    std::wstring out = L"\"";
    unsigned slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out += c;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
static void copyOutput(HANDLE pipe, HANDLE log) {
    char buffer[4096]; DWORD count = 0, written = 0;
    while (ReadFile(pipe, buffer, sizeof buffer, &count, nullptr) && count) {
        WriteFile(log, buffer, count, &written, nullptr);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, count, &written, nullptr);
    }
    FlushFileBuffers(log);
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--help") {
        puts("P25 external crash capture: standalone ENet only\nUsage: pse-diagnostics SESSION_DIR host|client [CLI arguments]");
        return 0;
    }
    if (argc < 3) return 87;
    const std::wstring role = argv[2];
    if (role != L"host" && role != L"client") return 87;
    wchar_t own[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, own, MAX_PATH)) return 2;
    const auto exe = fs::path(own).parent_path() / (L"pse_" + role + L".exe");
    const auto session = fs::absolute(argv[1]);
    // Exclusive new directory; no truncation of earlier logs/dumps.
    std::error_code ec;
    if (!fs::create_directory(session, ec)) return ERROR_ALREADY_EXISTS;
    HANDLE log = CreateFileW((session / L"console.log").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE events = CreateFileW((session / L"diagnostics.log").c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE || events == INVALID_HANDLE_VALUE) return 5;
    auto record = [&](const char* kind, DWORD a, DWORD b) {
        char line[160];
        int n = sprintf_s(line, "%s code=%lu detail=%lu\r\n", kind, a, b);
        DWORD written; WriteFile(events, line, n, &written, nullptr); FlushFileBuffers(events);
    };
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE outputRead = nullptr, outputWrite = nullptr;
    if (!CreatePipe(&outputRead, &outputWrite, &sa, 0)) return 5;
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    // Use an explicit handle list: do not inherit arbitrary parent handles.
    HANDLE input = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(STD_INPUT_HANDLE),
        GetCurrentProcess(), &input, 0, TRUE, DUPLICATE_SAME_ACCESS)) return 5;
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<unsigned char> attrs(attrSize);
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = input;
    si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = outputWrite;
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
    HANDLE handles[] = {input, outputWrite};
    if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize) ||
        !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles, sizeof handles, nullptr, nullptr)) return 5;
    std::wstring command = quote(exe.wstring());
    for (int i = 3; i < argc; ++i) command += L" " + quote(argv[i]);
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
        DEBUG_ONLY_THIS_PROCESS | EXTENDED_STARTUPINFO_PRESENT, nullptr,
        exe.parent_path().c_str(), &si.StartupInfo, &pi);
    DWORD startError = started ? 0 : GetLastError();
    DeleteProcThreadAttributeList(si.lpAttributeList);
    CloseHandle(input); CloseHandle(outputWrite);
    if (!started) { record("start_failed", startError, 0); return static_cast<int>(startError); }
    record("started", pi.dwProcessId, pi.dwThreadId);
    std::thread output(copyOutput, outputRead, log);
    // A debugger never outlives its owned process if this console is closed.
    DebugSetProcessKillOnExit(TRUE);
    DWORD exitCode = 1;
    bool complete = false;
    while (!complete) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, INFINITE)) {
            record("debug_wait_failed", GetLastError(), 0);
            TerminateProcess(pi.hProcess, 1); break;
        }
        DWORD disposition = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            CloseHandle(event.u.CreateThread.hThread); break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const auto& info = event.u.Exception;
            disposition = DBG_EXCEPTION_NOT_HANDLED;
            // Only the loader's initial first-chance breakpoint is consumed.
            static bool initialBreakpoint = true;
            if (initialBreakpoint && info.dwFirstChance && info.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT) {
                initialBreakpoint = false; disposition = DBG_CONTINUE;
            } else if (!info.dwFirstChance) {
                record("unhandled_exception", info.ExceptionRecord.ExceptionCode, event.dwThreadId);
                HANDLE file = CreateFileW((session / L"crash.dmp").c_str(), GENERIC_WRITE,
                    0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId);
                CONTEXT context{}; context.ContextFlags = CONTEXT_ALL;
                EXCEPTION_RECORD exception = info.ExceptionRecord;
                EXCEPTION_POINTERS pointers{&exception, &context};
                MINIDUMP_EXCEPTION_INFORMATION dumpException{event.dwThreadId, &pointers, FALSE};
                BOOL dumped = FALSE; DWORD error = GetLastError();
                if (file != INVALID_HANDLE_VALUE && thread && GetThreadContext(thread, &context)) {
                    dumped = MiniDumpWriteDump(pi.hProcess, pi.dwProcessId, file, MiniDumpNormal,
                        &dumpException, nullptr, nullptr);
                    error = dumped ? 0 : GetLastError();
                }
                record(dumped ? "crash_dump_written" : "crash_dump_failed", error, event.dwThreadId);
                if (thread) CloseHandle(thread);
                if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            exitCode = event.u.ExitProcess.dwExitCode; complete = true;
            record("exit", exitCode, 0); break;
        default: break;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, disposition)) {
            record("debug_continue_failed", GetLastError(), 0);
            TerminateProcess(pi.hProcess, 1); break;
        }
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    output.join();
    CloseHandle(outputRead); CloseHandle(log); CloseHandle(events);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return static_cast<int>(exitCode);
}
