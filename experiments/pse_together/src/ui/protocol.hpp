#pragma once
#include "model.hpp"
#include "net_protocol.h"
#include <limits>
#include <stdexcept>

namespace pse::ui {
static_assert(NET_PROTOCOL_VERSION == 0x0114, "P23 must never compile against a frozen protocol");
struct Request {
    std::uint64_t sequence{}; std::uint32_t generation{}; std::string message;
    bool valid() const { return sequence && generation && chat(message); }
    void serialize(ByteWriter& w) const {
        if (!valid()) throw std::invalid_argument("P23 chat request");
        w.writeUint64(sequence); w.writeUint32(generation); w.writeString(message, ChatBytes);
    }
    bool deserialize(ByteReader& r) {
        Request p;
        if (!r.readUint64(p.sequence) || !r.readUint32(p.generation) ||
            !r.readString(p.message, ChatBytes) || !p.valid()) return false;
        *this = std::move(p); return true;
    }
};
struct Broadcast {
    Chat line;
    bool valid() const {
        return line.sequence && line.slot < Capacity && line.generation && name(line.displayName) && chat(line.message);
    }
    void serialize(ByteWriter& w) const {
        if (!valid()) throw std::invalid_argument("P23 chat broadcast");
        w.writeUint64(line.sequence); w.writeUint8(line.slot); w.writeUint32(line.generation);
        w.writeString(line.displayName, 31); w.writeString(line.message, ChatBytes);
    }
    bool deserialize(ByteReader& r) {
        Broadcast b;
        if (!r.readUint64(b.line.sequence) || !r.readUint8(b.line.slot) || !r.readUint32(b.line.generation) ||
            !r.readString(b.line.displayName, 31) || !r.readString(b.line.message, ChatBytes) || !b.valid()) return false;
        *this = std::move(b); return true;
    }
};
struct Roster {
    std::uint64_t sequence{}; std::uint32_t epoch{}; std::vector<Player> entries;
    bool valid() const {
        if (!sequence || !epoch || entries.empty() || entries.size() > Capacity) return false;
        std::array<bool, Capacity> used{};
        for (const auto& p : entries) { if (!ui::valid(p) || used[p.slot]) return false; used[p.slot] = true; }
        return used[0];
    }
    void serialize(ByteWriter& w) const {
        if (!valid()) throw std::invalid_argument("P23 roster");
        w.writeUint64(sequence); w.writeUint32(epoch); w.writeUint8(static_cast<std::uint8_t>(entries.size()));
        for (const auto& p : entries) {
            w.writeUint8(p.slot); w.writeUint32(p.generation); w.writeString(p.displayName, 31);
            w.writeUint8(static_cast<std::uint8_t>(p.state)); w.writeUint16(p.ping);
        }
    }
    bool deserialize(ByteReader& r) {
        Roster b; std::uint8_t count{};
        if (!r.readUint64(b.sequence) || !r.readUint32(b.epoch) || !r.readUint8(count) || count > Capacity) return false;
        for (unsigned i = 0; i < count; ++i) {
            Player p; std::uint8_t state{};
            if (!r.readUint8(p.slot) || !r.readUint32(p.generation) || !r.readString(p.displayName, 31) ||
                !r.readUint8(state) || !r.readUint16(p.ping)) return false;
            p.state = static_cast<State>(state); b.entries.push_back(std::move(p));
        }
        if (!b.valid()) return false;
        *this = std::move(b); return true;
    }
};
struct PosePacket {
    Pose pose;
    bool valid() const {
        return pose.slot < Capacity && pose.generation && pose.epoch && pose.sequence &&
            std::isfinite(pose.x) && std::isfinite(pose.y) && std::isfinite(pose.z) && std::isfinite(pose.yawRadians);
    }
    void serialize(ByteWriter& w) const {
        if (!valid()) throw std::invalid_argument("P23 pose");
        w.writeUint8(pose.slot); w.writeUint32(pose.generation); w.writeUint32(pose.epoch); w.writeUint32(pose.sequence);
        w.writeFloat(pose.x); w.writeFloat(pose.y); w.writeFloat(pose.z); w.writeFloat(pose.yawRadians);
        w.writeUint64(pose.sampleNs);
    }
    bool deserialize(ByteReader& r) {
        PosePacket p;
        if (!r.readUint8(p.pose.slot) || !r.readUint32(p.pose.generation) || !r.readUint32(p.pose.epoch) || !r.readUint32(p.pose.sequence) ||
            !r.readFloat(p.pose.x) || !r.readFloat(p.pose.y) || !r.readFloat(p.pose.z) || !r.readFloat(p.pose.yawRadians) || !r.readUint64(p.pose.sampleNs) || !p.valid()) return false;
        *this=std::move(p);return true;
    }
};
template<class T> bool complete(ByteReader& r, T& value) {
    T next;
    if (!next.deserialize(r) || r.remaining()) return false;
    value = std::move(next); return true;
}
// Monotonic 64-bit sequence, exhaustion closes admission rather than wrapping.
inline std::uint64_t next(std::uint64_t& n) {
    if (n == (std::numeric_limits<std::uint64_t>::max)()) throw std::overflow_error("P23 sequence exhausted");
    return ++n;
}
} // namespace pse::ui
