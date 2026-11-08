// P7 scalar authority contract. Draft v1.12 payload only: NOT registered with
// ENet, MsgHeader, the v1.10 worker, or identity-only v1.11. No KEX ABI is implied.
// Native capabilities default closed until each exact-build contract is proven.
#pragma once
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace pse::self_p7 {
inline constexpr std::uint16_t DraftVersion=0x0112;
inline constexpr std::size_t EncodedSize=102;
inline constexpr std::uint8_t NoPendingWeapon=0xff;
enum class Life : std::uint8_t { Alive, Dead };
struct Identity {
    std::uint8_t slot{};
    std::uint32_t connectionGeneration{},mapEpoch{},entityId{},entityGeneration{};
    std::uint32_t resetToken{},discontinuitySequence{};
    bool operator==(const Identity&) const=default;
};
struct Values {
    std::int16_t health{};
    std::array<std::uint16_t,8> ammo{};
    std::uint8_t owned{},weapon{},pendingWeapon=NoPendingWeapon;
    std::uint16_t keys{};
    std::uint32_t artifacts{},abilities{};
    std::uint16_t questItems{};
    std::uint16_t ankhCount{},ankhMask{};
    std::uint32_t teamDolls{};
    Life life=Life::Alive;
    std::uint32_t deathSequence{};
    bool operator==(const Values&) const=default;
};
struct State {
    Identity identity{};
    std::uint32_t sequence{};
    std::uint64_t stateTick{},stateTimeNs{}; // captured AFTER authoritative step
    std::uint32_t clockGeneration{};
    Values values{};
    bool operator==(const State&) const=default;
};
inline bool valid(const Identity& i) noexcept {
    return i.slot<8 && i.connectionGeneration && i.mapEpoch && i.entityId &&
        i.entityGeneration && i.resetToken && i.discontinuitySequence;
}
inline bool valid(const Values& v) noexcept {
    if(v.weapon>=8 || !(v.owned&(1u<<v.weapon)))return false;
    if(v.pendingWeapon!=NoPendingWeapon && (v.pendingWeapon>=8 || !(v.owned&(1u<<v.pendingWeapon))))return false;
    // Draft is restricted to stock content: four keys, eight quest items,
    // five unique ankhs (DOC-63 ItemHealthAnkah1..5). Duplicate-count corruption is rejected rather
    // than being used to authorize an invalid maximum health.
    if(v.artifacts&~0x3fu || v.abilities&~3u || v.keys&~0x0fu || v.questItems&~0xffu ||
        v.ankhCount>5 || v.ankhMask&~0x1fu || v.ankhCount!=std::popcount(v.ankhMask))return false;
    if(v.life==Life::Alive)return v.health>0;
    return v.life==Life::Dead && v.health<=0 && v.deathSequence;
}
inline bool valid(const State& s) noexcept {
    return valid(s.identity) && s.sequence && s.stateTick && s.stateTimeNs && s.clockGeneration && valid(s.values);
}

// Explicit scalar LE encoding, never object layout or a native structure copy.
inline bool encode(const State& s,std::array<std::uint8_t,EncodedSize>& out) noexcept {
    if(!valid(s))return false;
    std::array<std::uint8_t,EncodedSize> bytes{};std::size_t p{};
    const auto put=[&](std::uint64_t n,unsigned count){for(unsigned k=0;k<count;++k)bytes[p++]=static_cast<std::uint8_t>(n>>(8*k));};
    put('P',1);put('S',1);put('7',1);put('S',1);put(DraftVersion,2);put(1,1);put(s.identity.slot,1);
    put(s.identity.connectionGeneration,4);put(s.identity.mapEpoch,4);put(s.identity.entityId,4);
    put(s.identity.entityGeneration,4);put(s.identity.resetToken,4);put(s.identity.discontinuitySequence,4);
    put(s.sequence,4);put(s.stateTick,8);put(s.stateTimeNs,8);put(s.clockGeneration,4);
    put(static_cast<std::uint16_t>(s.values.health),2);for(auto a:s.values.ammo)put(a,2);
    put(s.values.owned,1);put(s.values.weapon,1);put(s.values.pendingWeapon,1);put(s.values.keys,2);
    put(s.values.artifacts,4);put(s.values.abilities,4);put(s.values.questItems,2);
    put(s.values.ankhCount,2);put(s.values.ankhMask,2);put(s.values.teamDolls,4);
    put(static_cast<std::uint8_t>(s.values.life),1);put(s.values.deathSequence,4);
    if(p!=bytes.size())return false;
    out=bytes;return true;
}
inline bool decode(std::span<const std::uint8_t> bytes,State& out) noexcept {
    if(bytes.size()!=EncodedSize)return false; // exact EOF, including trailing bytes
    std::size_t p{};
    const auto get=[&](unsigned count){std::uint64_t n{};for(unsigned k=0;k<count;++k)n|=static_cast<std::uint64_t>(bytes[p++])<<(8*k);return n;};
    if(get(1)!='P' || get(1)!='S' || get(1)!='7' || get(1)!='S' || get(2)!=DraftVersion || get(1)!=1)return false;
    State s;s.identity.slot=static_cast<std::uint8_t>(get(1));
    s.identity.connectionGeneration=static_cast<std::uint32_t>(get(4));s.identity.mapEpoch=static_cast<std::uint32_t>(get(4));
    s.identity.entityId=static_cast<std::uint32_t>(get(4));s.identity.entityGeneration=static_cast<std::uint32_t>(get(4));
    s.identity.resetToken=static_cast<std::uint32_t>(get(4));s.identity.discontinuitySequence=static_cast<std::uint32_t>(get(4));
    s.sequence=static_cast<std::uint32_t>(get(4));s.stateTick=get(8);s.stateTimeNs=get(8);s.clockGeneration=static_cast<std::uint32_t>(get(4));
    const auto h=static_cast<std::uint16_t>(get(2));
    s.values.health=static_cast<std::int16_t>(h<=32767u?static_cast<std::int32_t>(h):static_cast<std::int32_t>(h)-65536);
    for(auto& a:s.values.ammo)a=static_cast<std::uint16_t>(get(2));
    s.values.owned=static_cast<std::uint8_t>(get(1));s.values.weapon=static_cast<std::uint8_t>(get(1));
    s.values.pendingWeapon=static_cast<std::uint8_t>(get(1));s.values.keys=static_cast<std::uint16_t>(get(2));
    s.values.artifacts=static_cast<std::uint32_t>(get(4));s.values.abilities=static_cast<std::uint32_t>(get(4));
    s.values.questItems=static_cast<std::uint16_t>(get(2));
    s.values.ankhCount=static_cast<std::uint16_t>(get(2));s.values.ankhMask=static_cast<std::uint16_t>(get(2));
    s.values.teamDolls=static_cast<std::uint32_t>(get(4));s.values.life=static_cast<Life>(get(1));
    s.values.deathSequence=static_cast<std::uint32_t>(get(4));
    if(p!=bytes.size() || !valid(s))return false;
    out=s;return true;
}

struct NativeCapabilities {
    bool health{},allAmmo{},ownedAndSelectedWeapons{},keys{},inventory{},death{};
    bool complete() const noexcept {return health&&allAmmo&&ownedAndSelectedWeapons&&keys&&inventory&&death;}
};
struct ApplyContext {
    bool simulationThread{},liveBinding{},gameplay{},outsideUnloadBarrier{},authenticated{};
    bool safe() const noexcept {return simulationThread&&liveBinding&&gameplay&&outsideUnloadBarrier&&authenticated;}
};
struct Admission {
    Identity identity{};
    // Local handshake completion serial, NEVER a network epoch/token. The
    // authenticated reset/anchor layer issues this only after native install.
    std::uint64_t localSerial{},anchorInstalledAtNs{};
};
struct Limits {
    std::uint64_t pendingTimeoutNs{},hostLeaseTimeoutNs{};
};
// Implementations must invoke proven native operations; this contract does not
// provide offsets, pointer writes, ABI guesses, synthetic pickups, or fallback.
struct INativeState {
    virtual ~INativeState()=default;
    virtual bool read(Values& values) noexcept=0;
    virtual bool apply(const Values& before,const Values& desired) noexcept=0;
};
enum class Result { Accepted,Applied,Unchanged,NoPending,NotReady,IdentityMismatch,Invalid,
    Stale,PendingOccupied,CapabilitiesMissing,Unsafe,Busy,Expired,ClockRegressed,NativeFailure,PostconditionFailure,Interrupted };

// Simulation-thread owner only. The network worker may decode scalar DTOs but
// must deliver them through its existing bridge, never call this object itself.
class Authority {
    enum class Closure { Initial,Open,Disconnected,Barrier,Failed };
    struct Pending {State state;std::uint64_t receivedNs;};
    Closure closure_=Closure::Initial;
    Identity identity_{};
    NativeCapabilities caps_{};
    std::optional<Pending> pending_;
    std::optional<State> applied_;
    std::uint32_t highestSeen_{},lastApplied_{};
    std::uint64_t seenTick_{},seenTime_{},lastNow_{},serial_{},admissionSerial_{},leaseFromNs_{};
    Limits limits_{};
    std::uint32_t clockGeneration_{},seenDeathSequence_{};
    Life seenLife_=Life::Alive;
    bool applying_{},disconnected_{},barrier_{},seenDead_{};
    void close(Closure reason) noexcept {
        disconnected_|=reason==Closure::Disconnected;barrier_|=reason==Closure::Barrier;
        // Closure causes are independent sticky requirements. In particular,
        // a later callback failure cannot weaken disconnect or unload gates.
        closure_=disconnected_?Closure::Disconnected:barrier_?Closure::Barrier:reason;
        pending_.reset();applied_.reset();++serial_;
    }
    bool sameLease(std::uint64_t serial) const noexcept {return ready()&&serial_==serial;}
    Result fail(Result why) noexcept {close(Closure::Failed);return why;}
public:
    bool ready() const noexcept{return closure_==Closure::Open;}
    std::uint32_t highestSeen() const noexcept{return highestSeen_;}
    std::uint32_t lastApplied() const noexcept{return lastApplied_;}
    bool hasPending() const noexcept{return pending_.has_value();}
    const std::optional<State>& applied() const noexcept{return applied_;}
    // Caller supplies authenticated admission plus a successfully installed reset
    // token/native binding. This is NOT the reset/anchor handshake implementation.
    Result admit(const Admission& witness,NativeCapabilities caps,Limits limits) noexcept {
        if(applying_)return Result::Busy;
        const auto& i=witness.identity;
        if(!valid(i)||!witness.localSerial||!witness.anchorInstalledAtNs||!limits.pendingTimeoutNs||!limits.hostLeaseTimeoutNs)return Result::Invalid;
        if(witness.localSerial<=admissionSerial_)return Result::Stale;
        if(closure_!=Closure::Initial) {
            // Map epochs and reset tokens are opaque; BCrypt values are not
            // ordered. Only the local completed-admission serial is ordered.
            const bool newConnection=i.connectionGeneration!=identity_.connectionGeneration;
            const bool sameConnection=i.connectionGeneration==identity_.connectionGeneration;
            if(sameConnection&&i.slot!=identity_.slot)return Result::IdentityMismatch;
            const bool newMap=sameConnection&&i.mapEpoch!=identity_.mapEpoch;
            const bool newToken=sameConnection&&i.mapEpoch==identity_.mapEpoch&&i.resetToken!=identity_.resetToken&&
                i.entityId==identity_.entityId&&i.entityGeneration>=identity_.entityGeneration&&i.discontinuitySequence>=identity_.discontinuitySequence;
            if(disconnected_&&!newConnection)return Result::Stale;
            if(!(newConnection||newMap||newToken))return Result::Stale;
            if(barrier_&&!newConnection&&!newMap)return Result::Stale;
            // A different reset/connection token does not create a new native
            // incarnation. Death remains terminal in the same world unless
            // BOTH its native lifetime and discontinuity advance.
            if(seenDead_&&i.mapEpoch==identity_.mapEpoch&&
                !(i.entityGeneration>identity_.entityGeneration&&i.discontinuitySequence>identity_.discontinuitySequence))return Result::Stale;
        }
        if(lastNow_&&witness.anchorInstalledAtNs<lastNow_)return Result::ClockRegressed;
        identity_=i;caps_=caps;limits_=limits;admissionSerial_=witness.localSerial;
        leaseFromNs_=witness.anchorInstalledAtNs;pending_.reset();applied_.reset();
        highestSeen_=lastApplied_=clockGeneration_=seenDeathSequence_=0;seenLife_=Life::Alive;
        disconnected_=barrier_=seenDead_=false;
        seenTick_=seenTime_=0;lastNow_=witness.anchorInstalledAtNs;++serial_;closure_=Closure::Open;
        return Result::Accepted;
    }
    void disconnect() noexcept {close(Closure::Disconnected);}
    void barrier() noexcept {close(Closure::Barrier);}
    // Age/clock uncertainty validation belongs to the accepted P6 time-sync
    // layer. Until it attests this sample, no pending state is admitted.
    Result offer(const State& s,std::uint64_t nowNs,bool clockAndAgeValid) noexcept {
        if(applying_)return Result::Busy;
        if(!ready())return Result::NotReady;
        if(s.identity!=identity_)return Result::IdentityMismatch;
        if(!valid(s)||!clockAndAgeValid)return Result::Invalid;
        if(!nowNs || (lastNow_&&nowNs<lastNow_))return fail(Result::ClockRegressed);
        lastNow_=nowNs;
        if(nowNs-leaseFromNs_>=limits_.hostLeaseTimeoutNs)return fail(Result::Expired);
        if(s.sequence<=highestSeen_ || s.stateTick<seenTick_ || s.stateTimeNs<seenTime_)return Result::Stale;
        if(clockGeneration_&&clockGeneration_!=s.clockGeneration)return fail(Result::Invalid);
        // Respawn belongs to a new authenticated reset/discontinuity admission;
        // a fresh packet sequence never authorizes reviving the same generation.
        if(highestSeen_&&(s.values.deathSequence<seenDeathSequence_ ||
            (seenLife_==Life::Dead&&s.values.life==Life::Alive) ||
            (seenLife_==Life::Alive&&s.values.life==Life::Dead&&s.values.deathSequence<=seenDeathSequence_)))return fail(Result::Invalid);
        highestSeen_=s.sequence;seenTick_=s.stateTick;seenTime_=s.stateTimeNs;clockGeneration_=s.clockGeneration;
        seenLife_=s.values.life;seenDeathSequence_=s.values.deathSequence;
        seenDead_|=s.values.life==Life::Dead;
        if(pending_)return Result::PendingOccupied; // oldest waits; deadline never extends
        pending_=Pending{s,nowNs};leaseFromNs_=nowNs;return Result::Accepted;
    }
    Result step(INativeState& native,const ApplyContext& context,std::uint64_t nowNs) noexcept {
        if(applying_)return Result::Busy;
        if(!ready())return Result::NotReady;
        if(!nowNs || (lastNow_&&nowNs<lastNow_))return fail(Result::ClockRegressed);
        lastNow_=nowNs;
        if(nowNs-leaseFromNs_>=limits_.hostLeaseTimeoutNs)return fail(Result::Expired);
        if(pending_&&nowNs-pending_->receivedNs>=limits_.pendingTimeoutNs)return fail(Result::Expired);
        if(!pending_&&!applied_)return Result::NoPending;
        if(!caps_.complete())return Result::CapabilitiesMissing;
        if(!context.safe())return Result::Unsafe;
        const bool consuming=pending_.has_value();
        const State target=consuming?pending_->state:*applied_;
        const auto lease=serial_;applying_=true;
        struct Release {bool& flag;~Release(){flag=false;}} release{applying_};
        Values before;
        const bool beforeOk=native.read(before);
        if(!sameLease(lease))return Result::Interrupted;
        if(!beforeOk)return fail(Result::NativeFailure);
        const bool changed=before!=target.values;
        const bool applyOk=!changed||native.apply(before,target.values);
        if(!sameLease(lease))return Result::Interrupted;
        if(!applyOk)return fail(Result::NativeFailure);
        Values after;
        const bool afterOk=native.read(after);
        if(!sameLease(lease))return Result::Interrupted;
        if(!afterOk)return fail(Result::NativeFailure);
        if(after!=target.values)return fail(Result::PostconditionFailure);
        if(consuming) {applied_=target;lastApplied_=target.sequence;pending_.reset();}
        return changed?Result::Applied:Result::Unchanged;
    }
};
} // namespace pse::self_p7
