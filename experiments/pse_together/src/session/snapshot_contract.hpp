#pragma once
#include "thread_bridge_dto.h"
#include <array>
#include <bit>
#include <cmath>
namespace snapshot55 {
inline constexpr char Domain[]="DOC55_TOMB_RAMSES";
enum class Verdict { Accept, NotReady, Identity, Stale, EntityMismatch, Nonfinite };
// Simulation-owned, per slot. No KEX access and no assumption of a shared
// sequence for records: native sender assigns a unique sequence to each packet.
struct Guard {
    struct Slot { std::uint32_t sequence{},tick{},entity{},generation{}; };
    std::array<Slot,2> slots{};
    std::uint32_t connection{},epoch{};
    void reset(std::uint32_t gen,std::uint32_t mapEpoch) { connection=gen;epoch=mapEpoch;slots={}; }
    Verdict accept(const pse::BridgePlayerSnapshot& s,bool ready) {
        if(!ready)return Verdict::NotReady;
        if(!connection || !epoch || s.connectionGeneration!=connection || s.mapEpoch!=epoch ||
            s.slotId>=slots.size() || !s.sequence || !s.entityId || !s.entityGeneration)return Verdict::Identity;
        if(!std::isfinite(s.posX)||!std::isfinite(s.posY)||!std::isfinite(s.posZ)||!std::isfinite(s.yaw)||!std::isfinite(s.pitch))return Verdict::Nonfinite;
        auto& prior=slots[s.slotId];
        if(prior.generation && (s.entityId!=prior.entity || s.entityGeneration!=prior.generation))return Verdict::EntityMismatch;
        if(s.sequence<=prior.sequence || (prior.sequence && s.serverTick<prior.tick))return Verdict::Stale;
        prior={s.sequence,s.serverTick,s.entityId,s.entityGeneration};return Verdict::Accept;
    }
};
inline std::uint32_t bits(float f) { return std::bit_cast<std::uint32_t>(f); }
} // namespace snapshot55
