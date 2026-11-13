#pragma once
// P23 value-only UI. No KEX memory, native ABI, socket, or render state access.
// Owned by a single UI/network thread; copy View across threads, never Player*.
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pse::ui {
constexpr std::size_t Capacity = 8, HistoryLimit = 64, ChatScalars = 90, ChatBytes = 360;
enum class State : std::uint8_t { Alive, Dead, Waiting };

// Reject malformed UTF-8, overlong forms, surrogates, controls/formatting and
// embedded NUL. A rejected string is never truncated into a different identity.
inline bool text(std::string_view s, std::size_t bytes, std::size_t scalars) {
    if (s.empty() || s.size() > bytes) return false;
    std::size_t count = 0; bool visible = false;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]); std::uint32_t cp{}; unsigned n{};
        if (c < 0x80) cp = c;
        else if (c >= 0xc2 && c <= 0xdf) { cp = c & 31; n = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15; n = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7; n = 3; }
        else return false;
        if (i + n > s.size()) return false;
        for (unsigned j = 0; j < n; ++j) {
            c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (c & 63);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
            cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || cp < 32 ||
            (cp >= 0x7f && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029 ||
            (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x202a && cp <= 0x202e) ||
            (cp >= 0x2060 && cp <= 0x206f) || cp == 0xfeff) return false;
        visible |= cp != 32 && cp != 0xa0 && cp != 0x3000;
        if (++count > scalars) return false;
    }
    return visible;
}
inline bool name(std::string_view s) { return text(s, 31, 31); }
// A chat line that starts with this mark is a game notice about its sender ("vadso died"): shown as "<name> <text>", without the colon.
inline constexpr const char* NoticeMark = "\xC2\xA0";
inline bool isNotice(std::string_view m) { return m.size() > 2 && m.substr(0, 2) == NoticeMark; }
inline bool chat(std::string_view s) { return text(s, ChatBytes, ChatScalars); }
inline bool participant(std::string_view s) {
    if (s.empty() || s.size() > 48) return false;
    for (const char c : s) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
inline std::optional<std::uint16_t> port(std::string_view s) {
    if (s.empty() || s.size() > 5) return {};
    unsigned p = 0;
    for (char c : s) { if (c < '0' || c > '9') return {}; p = p * 10 + static_cast<unsigned>(c - '0'); }
    if (!p || p > 65535) return {};
    return static_cast<std::uint16_t>(p);
}
inline bool ipv4(std::string_view s) {
    unsigned groups = 0; std::size_t start = 0;
    while (start < s.size()) {
        auto end = s.find('.', start); if (end == s.npos) end = s.size();
        if (end == start || end - start > 3) return false;
        unsigned value = 0;
        for (auto i = start; i < end; ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            value = value * 10 + static_cast<unsigned>(s[i] - '0');
        }
        if (value > 255 || (end - start > 1 && s[start] == '0')) return false;
        ++groups; start = end + 1;
    }
    return groups == 4 && !s.empty() && s.back() != '.' && s != "0.0.0.0" && s != "255.255.255.255";
}
struct Menu {
    bool host{};
    std::string displayName, address = "127.0.0.1", portText = "28745", token;
    bool valid() const {
        return name(displayName) && port(portText).has_value() &&
            (ipv4(address) || (host && address == "0.0.0.0")) && token.size() <= 64 &&
            (token.empty() || text(token, 64, 64));
    }
};
struct Player {
    std::uint8_t slot{}; std::uint32_t generation{};
    std::string displayName; State state = State::Waiting;
    std::uint16_t ping{};
};
inline bool valid(const Player& p) {
    return p.slot < Capacity && p.generation && name(p.displayName) &&
        static_cast<unsigned>(p.state) <= static_cast<unsigned>(State::Waiting);
}
struct Chat {
    std::uint64_t sequence{};
    std::uint8_t slot{}; std::uint32_t generation{};
    std::string displayName, message;
};
struct Pose {
    std::uint8_t slot{}; std::uint32_t generation{}, epoch{}, sequence{};
    float x{}, y{}, z{}, yawRadians{};
    std::uint64_t receivedMs{};
    std::uint64_t sampleNs{}; // accepted host QPC domain, native bounded same-PC gate
};
struct Marker {
    std::uint8_t slot{}; float x{}, y{}, yawRadians{}, radius{};
    std::array<std::uint8_t, 4> rgba{};
};
constexpr std::array<std::array<std::uint8_t, 4>, Capacity> Colors{{
    {255,255,255,255},{0,255,255,255},{0,255,0,255},{255,255,0,255},
    {255,0,255,255},{255,127,0,255},{153,51,255,255},{0,255,128,255}}};
class Model {
    std::array<std::optional<Player>, Capacity> players_{};
    std::array<std::optional<Pose>, Capacity> poses_{};
    std::deque<Chat> chats_;
    std::deque<std::string> notices_;
    std::uint64_t rosterSequence_{}, chatSequence_{};
    std::uint32_t epoch_{};
    std::uint8_t local_ = 255;
    void notice(std::string s) { if (notices_.size() == HistoryLimit) notices_.pop_front(); notices_.push_back(std::move(s)); }
public:
    void reset(std::uint8_t local = 255) { *this = Model{}; local_ = local; }
    // Apply full authoritative roster atomically, including removals. No client
    // message is allowed to choose a sender or directly change this ledger.
    bool roster(std::uint64_t seq, std::uint32_t epoch, const std::vector<Player>& entries) {
        if (!seq || seq <= rosterSequence_ || !epoch || entries.empty() || entries.size() > Capacity) return false;
        std::array<std::optional<Player>, Capacity> next{};
        for (const auto& p : entries) {
            if (!valid(p) || next[p.slot]) return false;
            next[p.slot] = p;
        }
        if (!next[0] || (local_ < Capacity && !next[local_])) return false;
        for (std::size_t i = 0; i < Capacity; ++i) {
            const bool same = players_[i] && next[i] && players_[i]->generation == next[i]->generation;
            if (players_[i] && !same) notice(players_[i]->displayName + " left");
            if (next[i] && !same) notice(next[i]->displayName + " joined");
            if (!same || epoch != epoch_) poses_[i].reset();
        }
        players_ = std::move(next); epoch_ = epoch; rosterSequence_ = seq; return true;
    }
    bool message(Chat c) {
        if (!c.sequence || c.sequence <= chatSequence_ || c.slot >= Capacity || !chat(c.message)) return false;
        const auto& p = players_[c.slot];
        if (!p || c.generation != p->generation || c.displayName != p->displayName) return false;
        chatSequence_ = c.sequence;
        if (chats_.size() == HistoryLimit) chats_.pop_front();
        chats_.push_back(std::move(c)); return true;
    }
    bool pose(const Pose& p) {
        if (p.slot >= Capacity || p.epoch != epoch_ || !p.sequence || !players_[p.slot] ||
            players_[p.slot]->generation != p.generation || !std::isfinite(p.x) || !std::isfinite(p.y) ||
            !std::isfinite(p.z) || !std::isfinite(p.yawRadians)) return false;
        const auto& old = poses_[p.slot];
        if (old && (p.sequence <= old->sequence || p.receivedMs < old->receivedMs)) return false;
        poses_[p.slot] = p; return true;
    }
    std::vector<Marker> markers(std::uint64_t now, float localZ, bool enabled,std::uint64_t sampleNowNs=0) const {
        std::vector<Marker> out;
        if (!enabled || !std::isfinite(localZ) || local_ >= Capacity) return out;
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (i == local_ || !players_[i] || players_[i]->state == State::Waiting || !poses_[i]) continue;
            const auto& p = *poses_[i];
            if (now < p.receivedMs || now - p.receivedMs > 1000) continue;
            if(sampleNowNs && (!p.sampleNs || p.sampleNs>sampleNowNs+1000000ULL ||
                (sampleNowNs>p.sampleNs && sampleNowNs-p.sampleNs>1000000000ULL)))continue;
            Marker m{static_cast<std::uint8_t>(i),p.x,p.y,p.yawRadians,16,Colors[i]};
            if (players_[i]->state == State::Dead) m.rgba = {128,128,128,255};
            if (p.z > localZ + 256) m.radius *= 1.25f;
            else if (p.z < localZ - 256) { m.radius *= 0.75f; m.rgba[3] = 180; }
            out.push_back(m);
        }
        return out;
    }
    const auto& players() const { return players_; }
    bool admitted(std::uint32_t epoch,std::uint8_t local,std::uint32_t generation) const {
        return epoch && generation && epoch==epoch_ && local==local_ && local<Capacity &&
            players_[local] && players_[local]->generation==generation;
    }
    std::optional<float> localHeight(std::uint64_t now) const {
        if (local_ >= Capacity || !poses_[local_]) return {};
        const auto& p = *poses_[local_];
        if (now < p.receivedMs || now-p.receivedMs > 1000) return {};
        return p.z;
    }
    const auto& messages() const { return chats_; }
    const auto& notices() const { return notices_; }
    auto epoch() const { return epoch_; }
    auto local() const { return local_; }
    auto rosterSequence() const { return rosterSequence_; }
};
// One limiter per authenticated generation. Invalid/replayed sequence is consumed
// at the boundary; reconnect constructs a fresh limiter. No wrap within a session.
struct ChatLimit {
    std::uint64_t lastSequence{}, lastMs{}; bool sent{};
    bool accept(std::uint64_t seq, std::uint64_t now) {
        if (!seq || seq <= lastSequence) return false;
        lastSequence = seq;
        if (sent && (now < lastMs || now - lastMs < 500)) return false;
        lastMs = now; sent = true; return true;
    }
};
} // namespace pse::ui
