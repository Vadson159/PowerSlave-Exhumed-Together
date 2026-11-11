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
    // One skin per slot: the host (slot 0) is plain John, slot S wears the coloured hat sheet pS+1 (build_john_sprites.py).
    if(slot==0) std::snprintf(out.data(),out.size(),"player/pse_john_%s%s",proxy?"proxy_":"",name(pose));
    else if(slot<8) std::snprintf(out.data(),out.size(),"player/pse_john_p%u_%s%s",slot+1,proxy?"proxy_":"",name(pose));
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
        // Motion: the native movement buttons (forward/back/strafe) or a real horizontal move since the previous snapshot of this body. The velocity
        // field alone flickers: a remote player's body on the host is re-placed to its client's pose every tick, which zeroes it.
        bool moving=(s.buttons&MoveButtons)!=0 || static_cast<double>(s.velX)*s.velX+static_cast<double>(s.velY)*s.velY>1.0;
        if(m_valid && s.serverTick>m_latest.serverTick && s.serverTick-m_latest.serverTick<=30) {
            const double dx=static_cast<double>(s.posX)-m_latest.posX,dy=static_cast<double>(s.posY)-m_latest.posY,dt=s.serverTick-m_latest.serverTick;
            moving=moving || (dx*dx+dy*dy)/(dt*dt)>1.0;
        }
        m_latest=s;m_valid=true;
        // A pose holds for a few ticks after its cause: every pose change rebinds the animation from its first frame, so a pose that flips on each
        // snapshot (a click shows one frame of the attack, walking that stutters to idle) plays as random frames.
        if(moving) m_walkUntil=s.serverTick+HoldTicks;
        if(s.buttons&1u) m_shootUntil=s.serverTick+HoldTicks;
        // Health and life state outrank attack and motion. Death never loops or
        // restarts on repeated snapshots; a new entity requires explicit reset.
        if(s.health<=0 || s.lifeState!=LIFE_ALIVE) m_dead=true;
        m_pose=m_dead ? Pose::Death : s.serverTick<m_shootUntil ? Pose::Shoot : s.serverTick<m_walkUntil ? Pose::Walk : Pose::Idle;
        m_history[m_next]={s.sequence,m_pose};m_next=(m_next+1)%History;if(m_count<History) ++m_count;
        return true;
    }
    // The proxy shows its snapshots through a presentation buffer ~160 ms (about 10 snapshots) behind the newest one: the command on screen
    // almost never is the newest snapshot. The pose has to follow the command on screen, not wait until the stream stops (it used to: the pose
    // was applied only when the presented sequence equalled the newest accepted one, so walking/attacking frames almost never showed).
    bool sameEntity(std::uint32_t slot,std::uint32_t epoch,std::uint32_t connection,std::uint32_t entity,std::uint32_t generation) const {
        return m_valid && m_latest.slotId==slot && m_latest.mapEpoch==epoch && m_latest.connectionGeneration==connection &&
               m_latest.entityId==entity && m_latest.entityGeneration==generation;
    }
    // Pose of the newest accepted snapshot not newer than the presented one; older than the whole history: the oldest pose kept.
    Pose poseAt(std::uint32_t sequence) const {
        if(!m_count) return m_pose;
        bool found=false;std::uint32_t bestSeq=0,oldestSeq=0;Pose best=m_pose,oldest=m_pose;
        for(std::size_t i=0;i<m_count;++i) {
            const auto& h=m_history[i];
            if(i==0 || h.sequence<oldestSeq) { oldestSeq=h.sequence;oldest=h.pose; }
            if(h.sequence<=sequence && (!found || h.sequence>bestSeq)) { found=true;bestSeq=h.sequence;best=h.pose; }
        }
        return found ? best : oldest;
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
    // Native Player+0 bits: 2 forward, 3 back, 6/7 strafe left/right (attack is bit 0). 12 ticks = the shoot animation (2 frames x 5) plus a margin.
    static constexpr std::uint32_t MoveButtons=(1u<<2)|(1u<<3)|(1u<<6)|(1u<<7);
    static constexpr std::uint32_t HoldTicks=12;
    static constexpr std::size_t History=64; // ~1 s of snapshots: far more than the 160 ms presentation delay
    struct Entry { std::uint32_t sequence{}; Pose pose{Pose::Idle}; };
    BridgePlayerSnapshot m_latest{};
    Pose m_pose{Pose::Idle};bool m_valid{},m_dead{};
    std::uint32_t m_walkUntil{},m_shootUntil{};
    std::array<Entry,History> m_history{};std::size_t m_next{},m_count{};
};
}
