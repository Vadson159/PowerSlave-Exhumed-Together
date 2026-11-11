#pragma once
// Journal lines leave the game's thread at once: append() only copies the line into memory, a writer thread of its own puts it into the
// file. A synchronous WriteFile on the simulation thread stalled a game tick for ~0.2 s every 30-60 s (always between two journal lines:
// the disk or a real-time scanner held the growing file), which the host felt as micro freezes and the client's input as a timeout.
// flush() drains on the caller's thread (faults, shutdown, final exit). Lines that do not fit (writer far behind) are counted, not kept.
#include <windows.h>
#include <cstddef>
#include <string>

namespace pse::journal {
class AsyncWriter {
public:
    using Sink = void (*)(const char* data, std::size_t size, bool hard);
    void bind(Sink sink) { sink_ = sink; }
    void append(const char* data, std::size_t size) {
        if (!sink_ || !data || !size) return;
        AcquireSRWLockExclusive(&lock_);
        if (pending_.size() + size <= Cap) pending_.append(data, size); else ++dropped_;
        const bool start = !started_; started_ = true;
        ReleaseSRWLockExclusive(&lock_);
        if (start) { // never from DllMain: the first line is written long after the loader lock is gone
            HANDLE t = CreateThread(nullptr, 0, &AsyncWriter::run, this, 0, nullptr);
            if (t) CloseHandle(t); else flush(false);
        }
    }
    void flush(bool hard) {
        std::string out; unsigned long long dropped = 0;
        AcquireSRWLockExclusive(&lock_); out.swap(pending_); dropped = dropped_; dropped_ = 0; ReleaseSRWLockExclusive(&lock_);
        AcquireSRWLockExclusive(&io_);
        if (dropped) { char note[128]{}; const int n = _snprintf_s(note, sizeof(note), _TRUNCATE, "{\"event\":\"journal_lines_dropped\",\"count\":%llu}\n", dropped); if (n > 0 && sink_) sink_(note, static_cast<std::size_t>(n), false); }
        if (sink_ && (!out.empty() || hard)) sink_(out.data(), out.size(), hard);
        ReleaseSRWLockExclusive(&io_);
    }
private:
    static constexpr std::size_t Cap = 8u << 20;
    static DWORD WINAPI run(void* self) {
        auto* w = static_cast<AsyncWriter*>(self);
        for (;;) { Sleep(100); w->flush(false); }
    }
    Sink sink_{};
    SRWLOCK lock_ = SRWLOCK_INIT, io_ = SRWLOCK_INIT;
    std::string pending_;
    unsigned long long dropped_{};
    bool started_{};
};
} // namespace pse::journal
