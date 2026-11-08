#include <windows.h>
int main() {
    RaiseException(0xE0250001, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    return 99;
}
