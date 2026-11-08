#pragma once
#include <cstdint>

namespace pse {
// Local acceptance limits, not wire fields. INPUT v1.10 is monotonic, not
// modular: wrap needs a new authenticated generation and a low counter.
// 60 Hz * ENet's default maximum peer timeout (30 s) tolerates loss gaps.
// First packet also covers DOC49's retained counter on reconnect: its bounded
// headless controller generates at most 60 * 180 s commands per process.
// Paused/missed scheduler slots do not consume sequence numbers. These are
// ceilings, not packet-rate validation or a guarantee for unbounded senders.
inline constexpr std::uint32_t INPUT_SEQUENCE_MAX_ADVANCE = 60u * 30u;
inline constexpr std::uint32_t INPUT_SEQUENCE_MAX_INITIAL = 60u * 180u;
enum class InputSequenceResult { ACCEPT, STALE, EXCESSIVE_JUMP };

constexpr InputSequenceResult validateInputSequence(bool hasSequence,
    std::uint32_t last, std::uint32_t next) noexcept {
    if (!hasSequence) return next <= INPUT_SEQUENCE_MAX_INITIAL
        ? InputSequenceResult::ACCEPT : InputSequenceResult::EXCESSIVE_JUMP;
    if (next <= last) return InputSequenceResult::STALE;
    // Subtraction is unsigned and only after next > last. No signed casts,
    // overflow-prone last+window, half-range guesses or accidental wrap.
    return next - last <= INPUT_SEQUENCE_MAX_ADVANCE
        ? InputSequenceResult::ACCEPT : InputSequenceResult::EXCESSIVE_JUMP;
}
} // namespace pse
