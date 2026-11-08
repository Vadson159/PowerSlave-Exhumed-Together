// P8 presentation policy. Pure numeric DTO input; no KEX pointers or wire changes.
#pragma once
#include "thread_bridge_dto.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <array>

namespace pse::player_art {
enum class Pose : std::uint8_t { Idle, Walk, Shoot, Death };
inline const char* name(Pose p) {
    switch(p) { case Pose::Idle:return "idle";case Pose::Walk:return "walk";
                case Pose::Shoot:return "shoot";case Pose::Death:return "death"; }
    return "invalid";
}
inline std::array<char,64> resource(unsigned slot,Pose pose,bool proxy) {
    std::array<char,64> out{};
    // Every slot shares one skin (John). The slot only gates the valid range.
    if(slot<8) std::snprintf(out.data(),out.size(),"player/pse_john_%s%s",proxy?"proxy_":"",name(pose));
    return out;
}
class Animation {
public:
    bool accept(const BridgePlayerSnapshot& s) {
        if(s.slotId>=8 || !s.entityId || !s.entityGeneration || !s.connectionGeneration || !s.mapEpoch || !s.sequence ||
           !std::isfinite(s.velX) || !std::isfinite(s.velY) || !std::isfinite(s.velZ)) return false;
        if(m_valid && (s.slotId!=m_latest.slotId || s.entityId!=m_latest.entityId ||
           s.entityGeneration!=m_latest.entityGeneration || s.connectionGeneration!=m_latest.connectionGeneration ||
           s.mapEpoch!=m_latest.mapEpoch || s.sequence<=m_latest.sequence || s.serverTick<m_latest.serverTick)) return false;
        m_latest=s;m_valid=true;
        // Health and life state outrank attack and motion. Death never loops or
        // restarts on repeated snapshots; a new entity requires explicit reset.
        if(s.health<=0 || s.lifeState!=LIFE_ALIVE) m_dead=true;
        m_pose=m_dead ? Pose::Death : (s.buttons&1u) ? Pose::Shoot :
            (static_cast<double>(s.velX)*s.velX+static_cast<double>(s.velY)*s.velY>1.0 ? Pose::Walk : Pose::Idle);
        return true;
    }
    bool matches(std::uint32_t slot,std::uint32_t sequence,std::uint32_t epoch,
                 std::uint32_t connection,std::uint32_t entity,std::uint32_t generation) const {
        return m_valid && m_latest.slotId==slot && m_latest.sequence==sequence && m_latest.mapEpoch==epoch &&
               m_latest.connectionGeneration==connection && m_latest.entityId==entity && m_latest.entityGeneration==generation;
    }
    bool valid() const { return m_valid; }
    Pose pose() const { return m_pose; }
    void reset() { *this=Animation{}; }
private:
    BridgePlayerSnapshot m_latest{};
    Pose m_pose{Pose::Idle};bool m_valid{},m_dead{};
};
}
