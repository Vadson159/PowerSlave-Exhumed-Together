#include "child_process.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>

namespace pse::qa {

ProcessResult runProcess(const std::wstring& executablePath,
                         const std::wstring& arguments,
                         uint32_t timeoutMs) {
    ProcessResult res;

#if defined(_WIN32)
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempFile[MAX_PATH];
    GetTempFileNameW(tempDir, L"m12a_qa", 0, tempFile);

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hFile = CreateFileW(
        tempFile,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &saAttr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        res.exitCode = -2;
        res.stdErr = "Failed to create temp output file: " + std::to_string(GetLastError());
        return res;
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hFile;
    si.hStdOutput = hFile;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmdLine = L"\"" + executablePath + L"\" " + arguments;
    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    BOOL success = CreateProcessW(
        NULL,
        cmdBuf.data(),
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(hFile); // Parent closes its handle so file can be flushed by child

    if (!success) {
        DeleteFileW(tempFile);
        res.exitCode = -3;
        res.stdErr = "CreateProcessW failed: " + std::to_string(GetLastError());
        return res;
    }

    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        res.timedOut = true;
        TerminateProcess(pi.hProcess, 101);
        WaitForSingleObject(pi.hProcess, 1000);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    res.exitCode = static_cast<int>(exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Read accumulated output from temp file
    std::ifstream inFile(tempFile, std::ios::in | std::ios::binary);
    if (inFile.is_open()) {
        std::ostringstream ss;
        ss << inFile.rdbuf();
        res.stdOut = ss.str();
        inFile.close();
    }
    DeleteFileW(tempFile);
#endif

    return res;
}

} // namespace pse::qa
