// DOC53 policy only. No transport connectivity or P2 existence substitutes for READY.
#pragma once
#include <cstdint>
namespace background {
struct Conditions {
    bool simulationThread{}, running{}, ready{}, gameplay{}, stableWorld{}, menu{};
    bool stopping{}, failed{}, focusKnown{}, focusLost{};
};
constexpr bool eligible(Conditions c) {
    return c.simulationThread && c.running && c.ready && c.gameplay && c.stableWorld &&
        !c.menu && !c.stopping && !c.failed && c.focusKnown && c.focusLost;
}
// Lease only the proven focus byte, never menu/inventory/modal state. An event
// delivered during native Tick owns the newer value, including focus gain=0.
struct FocusLease {
    bool applied{};
    std::uint64_t serial{};
    bool acquire(unsigned char& focusByte, std::uint64_t eventSerial, bool allowed) {
        if (applied || !allowed || focusByte != 1) return false;
        serial=eventSerial; applied=true; focusByte=0; return true;
    }
    bool release(unsigned char& focusByte, std::uint64_t eventSerial) {
        if (!applied) return false;
        const bool restore=serial==eventSerial && focusByte==0;
        if (restore) focusByte=1;
        applied=false; return restore;
    }
};
} // namespace background
