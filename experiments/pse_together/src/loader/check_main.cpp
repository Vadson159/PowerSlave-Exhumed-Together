#include "loader_common.hpp"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U16TEXT);
    if (argc != 2) { std::wprintf(L"Usage: pe-check.exe <game.exe>\n"); return ERROR_INVALID_PARAMETER; }
    std::wstring message;
    const DWORD result = pse::loader::checkExe(argv[1], message);
    std::wprintf(L"%ls\n", message.c_str());
    return static_cast<int>(result);
}
