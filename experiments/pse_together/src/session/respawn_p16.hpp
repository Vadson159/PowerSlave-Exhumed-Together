// P16 value-only policy. Native lifecycle/touch witnesses are supplied by an
// adapter; passing these contracts does not attest that adapter in KEX.
#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace pse::respawn_p16 {
constexpr unsigned Capacity=8, PickupCapacity=256;
enum class Life { Absent, Alive, Dead, Respawning, Faulted };
enum class Result { Accepted, Replay, Invalid, Stale, Unsafe, Busy, Failed, Exhausted };
struct Identity {
    std::uint8_t slot{};
    std::uint32_t connection{},epoch{};
    bool operator==(const Identity&) const=default;
};
struct Request {
    Identity who{};
    std::uint32_t death{};
    bool operator==(const Request&) const=default;
};
struct Gates {
    bool simulationThread{},mapReady{},outsideUnload{},outsideCinematic{};
    bool safe() const noexcept {return simulationThread&&mapReady&&outsideUnload&&outsideCinematic;}
};
// C19/C23 observed 200 HP / 60 pistol. P16 clears native Reset's other ammo
// defaults through native scalar-restore. No wire default is changed.
struct Loadout {
    std::int16_t health=200;
    std::uint8_t weapons=3;
    std::array<std::uint16_t,8> ammo{0,60,0,0,0,0,0,0};
    bool operator==(const Loadout&) const=default;
};
struct Plan {
    Request request{};
    std::uint32_t oldGeneration{},newGeneration{};
    std::uint64_t ticket{};
    bool operator==(const Plan&) const=default;
};
struct Confirmation {
    Request request{};
    std::uint32_t generation{};
    Loadout loadout{};
    bool operator==(const Confirmation&) const=default;
};
struct Slot {
    Identity who{};
    Life life=Life::Absent;
    std::uint32_t generation{},death{};
    std::optional<Plan> pending;
    std::optional<Confirmation> confirmed;
};
struct Counts {unsigned connected{},alive{},dead{},respawning{},faulted{};
    bool totalPartyKill() const noexcept {return connected>=2&&dead==connected;}
};

class Team {
    struct Pickup {std::uint32_t id{};std::uint8_t weapon{};std::array<std::uint32_t,Capacity> consumedLife{};};
    std::array<Slot,Capacity> slots_{};
    std::array<Pickup,PickupCapacity> pickups_{};
    std::uint32_t epoch_{};
    std::uint64_t serial_{};
    bool valid(const Identity& id) const noexcept {
        return id.slot<Capacity&&id.connection&&id.epoch&&id.epoch==epoch_;
    }
    bool bound(const Identity& id) const noexcept {
        return valid(id)&&slots_[id.slot].life!=Life::Absent&&slots_[id.slot].who==id;
    }
public:
    // MapReady witness is external. A barrier must precede this call; never
    // use it to hide a failed native mutation or to synthesize a native spawn.
    Result map(std::uint32_t epoch,bool barrierWitness) noexcept {
        if(!barrierWitness||!epoch)return Result::Unsafe;
        if(epoch<=epoch_)return Result::Stale;
        epoch_=epoch;slots_={};pickups_={};return Result::Accepted;
    }
    Result connect(Identity id,std::uint32_t nativeGeneration) noexcept {
        if(!valid(id)||!nativeGeneration)return Result::Invalid;
        auto& s=slots_[id.slot];
        if(s.life!=Life::Absent)return Result::Busy;
        // Connection generations are monotonic within this map, including
        // a disconnected tombstone. Reused slot != the previous occupant.
        if(s.who.connection&&id.connection<=s.who.connection)return Result::Stale;
        if(s.generation&&nativeGeneration<=s.generation)return Result::Stale;
        s={id,Life::Alive,nativeGeneration,0,{},{}};
        for(auto& p:pickups_)p.consumedLife[id.slot]=0;
        return Result::Accepted;
    }
    Result disconnect(Identity id) noexcept {
        if(!bound(id))return Result::Stale;
        auto& s=slots_[id.slot];
        // Reserve even an interrupted spawn generation: a delayed broadcast
        // confirm has no connection token in v1.12 and must never match a new
        // occupant's next incarnation in the same epoch.
        if(s.pending)s.generation=s.pending->newGeneration;
        s.life=Life::Absent;s.pending.reset();s.confirmed.reset();return Result::Accepted;
    }
    Result observeDeath(Identity id,std::uint32_t nativeGeneration) noexcept {
        if(!bound(id)||slots_[id.slot].generation!=nativeGeneration)return Result::Stale;
        auto& s=slots_[id.slot];
        if(s.life==Life::Dead)return Result::Replay;
        if(s.life!=Life::Alive)return Result::Busy;
        if(s.death==UINT32_MAX){s.life=Life::Faulted;return Result::Exhausted;}
        ++s.death;s.life=Life::Dead;s.confirmed.reset();return Result::Accepted;
    }
    // Accepted returns a value-only plan. No native call is made here.
    // Replay returns the cached confirmation; caller re-sends it without
    // Remove/Reset/Spawn. This also covers confirm loss + client timeout.
    Result request(const Request& r,const Gates& gates,Plan& plan,Confirmation& cached) noexcept {
        if(!bound(r.who)||!r.death)return Result::Stale;
        auto& s=slots_[r.who.slot];
        if(r.death!=s.death)return Result::Stale;
        if(!gates.safe())return Result::Unsafe;
        if(s.confirmed&&s.confirmed->request==r){cached=*s.confirmed;return Result::Replay;}
        if(s.life==Life::Respawning)return Result::Busy;
        if(s.life!=Life::Dead)return Result::Invalid;
        if(s.generation==UINT32_MAX||serial_==UINT64_MAX){s.life=Life::Faulted;return Result::Exhausted;}
        plan={r,s.generation,s.generation+1,++serial_};
        s.pending=plan;s.life=Life::Respawning;return Result::Accepted;
    }
    // Commit requires native readback (current map/start/reciprocal graph,
    // old-generation isolation, starter kit, preserved story/world). A partial
    // native operation fails CLOSED; policy never retries it automatically.
    Result complete(const Plan& plan,const Gates& gates,bool nativeReadback,
                    const Loadout& kit,Confirmation& out) noexcept {
        if(!bound(plan.request.who))return Result::Stale;
        auto& s=slots_[plan.request.who.slot];
        if(s.life!=Life::Respawning||!s.pending||*s.pending!=plan)return Result::Stale;
        if(!gates.safe()||!nativeReadback||kit!=Loadout{}){
            s.life=Life::Faulted;s.pending.reset();return Result::Failed;
        }
        out={plan.request,plan.newGeneration,kit};
        s.generation=plan.newGeneration;s.life=Life::Alive;s.pending.reset();s.confirmed=out;
        return Result::Accepted;
    }
    const Slot* slot(unsigned slot) const noexcept {return slot<Capacity?&slots_[slot]:nullptr;}
    Counts counts() const noexcept {
        Counts c;
        for(const auto& s:slots_)switch(s.life){
            case Life::Absent:break;
            case Life::Alive:++c.connected;++c.alive;break;
            case Life::Dead:++c.connected;++c.dead;break;
            case Life::Respawning:++c.connected;++c.respawning;break;
            case Life::Faulted:++c.connected;++c.faulted;break;
        }
        return c;
    }
    // BLK-11: remember a host-verified ORIGINAL weapon's logical identity.
    // Death changes the player's generation, making it available again to
    // that player. Nothing revives/removes the original MAP Actor. The latest
    // user rule keeps that Actor permanently present and grants no ammo.
    // This ledger alone does not implement native pickup or visibility.
    Result registerWeapon(std::uint32_t epoch,std::uint32_t pickup,std::uint8_t weapon) noexcept {
        if(!epoch_||epoch!=epoch_||!pickup||weapon==0||weapon>=8)return Result::Invalid;
        for(const auto& p:pickups_)if(p.id==pickup)return p.weapon==weapon?Result::Replay:Result::Invalid;
        for(auto& p:pickups_)if(!p.id){p.id=pickup;p.weapon=weapon;return Result::Accepted;}
        return Result::Exhausted; // no silent eviction / identity alias
    }
    bool weaponAvailable(Identity id,std::uint32_t pickup) const noexcept {
        if(!bound(id)||slots_[id.slot].life!=Life::Alive)return false;
        for(const auto& p:pickups_)if(p.id==pickup&&pickup)return p.consumedLife[id.slot]!=slots_[id.slot].generation;
        return false;
    }
    // Only after the native grant + proximity/touch + inventory readback.
    Result weaponGranted(Identity id,std::uint32_t pickup,std::uint32_t generation,bool nativeGrantWitness) noexcept {
        if(!bound(id)||slots_[id.slot].generation!=generation)return Result::Stale;
        if(!nativeGrantWitness||!weaponAvailable(id,pickup))return Result::Unsafe;
        for(auto& p:pickups_)if(p.id==pickup){p.consumedLife[id.slot]=generation;return Result::Accepted;}
        return Result::Invalid;
    }
};

class DeadInput {
    Identity who_{};
    bool down_{},dead_{},pending_{};
    std::uint32_t death_{},generation_{};
    std::uint64_t sentAt_{};
public:
    // New admission starts disarmed until E is released. Call sample also
    // while ALIVE so holding Use through death cannot request respawn.
    void bind(Identity id,std::uint32_t generation) noexcept {
        who_=id;generation_=generation;death_=0;down_=true;dead_=pending_=false;sentAt_=0;
    }
    void invalidate() noexcept {*this=DeadInput{};}
    void observe(bool dead,std::uint32_t death) noexcept {
        if(dead&&death&&death>death_){pending_=false;dead_=true;death_=death;}
        // A mere ALIVE snapshot cannot authorize a new native incarnation.
    }
    std::optional<Request> sample(bool useDown,std::uint64_t nowMs,bool safe) noexcept {
        const bool edge=useDown&&!down_;down_=useDown;
        if(pending_&&nowMs>=sentAt_&&nowMs-sentAt_>=1000)pending_=false;
        if(!safe||who_.slot>=Capacity||!who_.connection||!who_.epoch||!generation_||!dead_||!edge||pending_||!nowMs)return {};
        pending_=true;sentAt_=nowMs;return Request{who_,death_};
    }
    bool expects(const Confirmation& c) const noexcept {
        return dead_&&sentAt_&&c.request==Request{who_,death_}&&generation_!=UINT32_MAX&&
            c.generation==generation_+1&&c.loadout==Loadout{};
    }
    bool confirm(const Confirmation& c,bool nativeRebindReadback) noexcept {
        if(!nativeRebindReadback||!expects(c))return false;
        generation_=c.generation;dead_=pending_=false;sentAt_=0;return true;
    }
};
} // namespace pse::respawn_p16
