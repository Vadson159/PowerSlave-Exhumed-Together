// DOC56 native map identity, epoch and MapReady gate. Pure SIMULATION-owned logic:
// no KEX access here. DLL adapters fill Observation on the attested simulation thread.
#pragma once
#include "net_protocol.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace map56 {
// Exact-build static coordinates; binary SHA is profile::Sha256, runtime = image base + RVA.
// GameInstance (inline, RVA af33a0): kexStr {char* data; size_t length; size_t capacity} at +1e0,
// written by `map <filename>` (G_FUNC_1401aeb10 via assign G_FUNC_14008d650) and passed by
// EnterState case 4 (G_FUNC_1401b6a00) to kexSlaveLevelLocal::LoadMap (G_FUNC_1401c7b70).
// +1f8 = matched mapInfo entry+8; its entry+58 (= +50 from that value) is the char* key that the
// native case-insensitive compare (G_FUNC_140090870) accepted for this name.
inline constexpr std::uint32_t MapNameString=0x1e0, MapInfoEntry=0x1f8, MapInfoKey=0x50;
// Inline kexSlaveLevelLocal (RVA a45940), vtable RVA 52b478 (G_MEMORY_14052b478): LoadMap writes
// PSM version byte +208 and file-derived array counts +d8/+18; Unload (G_FUNC_1401c90e0) zeroes them.
inline constexpr std::uint32_t LevelObject=0xa45940, LevelVtable=0x52b478;
inline constexpr std::uint32_t LevelVersion=0x208, LevelCountA=0xd8, LevelCountB=0x18;
inline constexpr std::size_t NameReadMax=63, KeyReadMax=63;
inline constexpr std::uint32_t StableTicks=30;

// Raw values copied by the adapter. Bounded copies; readable=false means SEH/bounds refusal.
struct Observation {
    int state{}, next{}, pending{};
    bool nameReadable{};
    std::uint64_t nameLength{}, nameCapacity{};
    std::array<char,NameReadMax+1> name{};
    std::uintptr_t mapInfo{};
    bool keyReadable{};
    std::array<char,KeyReadMax+1> key{};
    bool levelVtable{};
    std::uint8_t version{};
    std::uint64_t countA{}, countB{};
};
// Comparable across processes: name only (the v1.10 wire carries it). Remaining fields are a
// local structural witness for change/unload detection; mapInfo is a process-local address.
struct Identity {
    std::string name;
    std::uintptr_t mapInfo{};
    std::uint8_t version{};
    std::uint64_t countA{}, countB{};
    bool operator==(const Identity&) const = default;
};
enum class Fault : unsigned { None, NotGameplay, Transition, NameUnreadable, NameInvalid, NameTooLongForWire,
    NoMapInfo, MapInfoKeyMismatch, LevelVtable, LevelUnloaded };
inline const char* text(Fault f) {
    constexpr const char* names[]={"none","not_gameplay","transition","name_unreadable","name_invalid",
        "name_too_long_for_wire_v110","no_mapinfo","mapinfo_key_mismatch","level_vtable","level_unloaded"};
    return names[static_cast<unsigned>(f)];
}
inline bool sameIgnoringCase(const char* a,const char* b) {
    for(;;++a,++b) {
        const auto x=static_cast<unsigned char>(*a),y=static_cast<unsigned char>(*b);
        const auto lx=x>='A'&&x<='Z'?x+32:x,ly=y>='A'&&y<='Z'?y+32:y;
        if(lx!=ly)return false;
        if(!x)return true;
    }
}
inline Fault derive(const Observation& o,Identity& out) {
    out={};
    if(o.state!=3)return Fault::NotGameplay;
    if(o.next!=3 || o.pending!=0)return Fault::Transition;
    if(!o.nameReadable)return Fault::NameUnreadable;
    if(!o.nameLength || o.nameLength>NameReadMax || o.nameCapacity<=o.nameLength || o.name[o.nameLength]!=0)return Fault::NameInvalid;
    // Printable, no quote/backslash: journals embed it verbatim and stock paths use '/'.
    for(std::size_t i=0;i<o.nameLength;++i)if(o.name[i]<0x21 || o.name[i]>0x7e || o.name[i]=='"' || o.name[i]=='\\')return Fault::NameInvalid;
    // writeString would silently truncate; an identity that cannot travel intact is refused.
    if(o.nameLength>pse::MAX_STRING_LEN)return Fault::NameTooLongForWire;
    if(!o.mapInfo)return Fault::NoMapInfo;
    if(!o.keyReadable || !sameIgnoringCase(o.key.data(),o.name.data()))return Fault::MapInfoKeyMismatch;
    if(!o.levelVtable)return Fault::LevelVtable;
    if(!o.countA || !o.countB)return Fault::LevelUnloaded;
    out.name.assign(o.name.data(),static_cast<std::size_t>(o.nameLength));
    out.mapInfo=o.mapInfo;out.version=o.version;out.countA=o.countA;out.countB=o.countB;
    return Fault::None;
}
// Epoch: per-process random base, never 0, never the legacy constant 1, never the previous one.
inline std::uint32_t nextEpoch(std::uint32_t random,std::uint32_t previous) {
    std::uint32_t e=random;
    while(e<=1 || e==previous)e=e*2654435761u+0x9e3779b9u;
    return e;
}

// Host: one native world binding per Session lifetime and one current authenticated peer
// generation for slot 1. Invalidation is terminal for the binding (no transitions in DOC56).
class HostGate {
public:
    enum class World { Unbound, Verified, Invalidated };
    enum class Peer { Disconnected, AwaitMapReady, MapReadyAccepted };
    enum class Ready { Accepted, WorldNotVerified, LevelNotReady, NoPeer, ForeignSlot, StaleGeneration, StaleEpoch, Duplicate };
    static const char* text(Ready r) {
        constexpr const char* names[]={"accepted","world_not_verified","level_not_ready","no_peer","foreign_slot","stale_generation","stale_epoch","duplicate"};
        return names[static_cast<unsigned>(r)];
    }
    bool bind(const Identity& id,std::uint32_t mapEpoch) {
        if(worldState!=World::Unbound || mapEpoch<=1 || id.name.empty())return false;
        identity=id;epoch=mapEpoch;worldState=World::Verified;return true;
    }
    // Every simulation tick. Any fault or any identity difference invalidates the binding.
    bool verify(Fault f,const Identity& current) {
        if(worldState!=World::Verified)return false;
        if(f!=Fault::None || !(current==identity)) { invalidate(); return false; }
        return true;
    }
    void invalidate() { if(worldState!=World::Unbound)worldState=World::Invalidated; peerState=peerState==Peer::Disconnected?Peer::Disconnected:Peer::AwaitMapReady; }
    void authenticated(std::uint8_t peerSlot,std::uint32_t gen) { slot=peerSlot;generation=gen;peerState=Peer::AwaitMapReady; }
    void left(std::uint32_t gen) { if(gen==generation)peerState=Peer::Disconnected; }
    Ready mapReady(std::uint8_t peerSlot,std::uint32_t gen,std::uint32_t mapEpoch,bool levelReady) {
        if(worldState!=World::Verified)return Ready::WorldNotVerified;
        if(!levelReady)return Ready::LevelNotReady;
        if(peerState==Peer::Disconnected || !generation)return Ready::NoPeer;
        if(peerSlot!=slot)return Ready::ForeignSlot;
        if(gen!=generation)return Ready::StaleGeneration;
        if(mapEpoch!=epoch)return Ready::StaleEpoch;
        if(peerState==Peer::MapReadyAccepted)return Ready::Duplicate;
        peerState=Peer::MapReadyAccepted;return Ready::Accepted;
    }
    bool snapshotEligible(std::uint32_t gen,bool levelReady) const {
        return worldState==World::Verified && levelReady && peerState==Peer::MapReadyAccepted && gen && gen==generation;
    }
    World world() const { return worldState; }
    Peer peer() const { return peerState; }
    std::uint32_t mapEpoch() const { return epoch; }
    const Identity& bound() const { return identity; }
private:
    Identity identity;
    std::uint32_t epoch{}, generation{};
    std::uint8_t slot{};
    World worldState{World::Unbound};
    Peer peerState{Peer::Disconnected};
};

// Client: MapReady only after authenticated host identity == local native identity, locked
// local identity unchanged and StableTicks consecutive stable gameplay observations.
class ClientGate {
public:
    enum class State { Disconnected, AwaitNativeMap, IdentityVerified, MapReadySent, Ready, Mismatch, Invalidated };
    static const char* text(State s) {
        constexpr const char* names[]={"disconnected","await_native_map","identity_verified","map_ready_sent","ready","mismatch","invalidated"};
        return names[static_cast<unsigned>(s)];
    }
    bool welcome(std::uint8_t peerSlot,std::uint32_t gen,std::uint32_t mapEpoch,const std::string& hostName) {
        if(terminal() || !gen || gen<=generation || !peerSlot || peerSlot>=pse::MAX_PLAYERS || mapEpoch<=1 ||
            hostName.empty() || hostName.size()>pse::MAX_STRING_LEN)return false;
        slot=peerSlot;generation=gen;epoch=mapEpoch;host=hostName;stable=0;current=State::AwaitNativeMap;return true;
    }
    // Every local simulation tick, before deciding to send MapReady.
    void observe(Fault f,const Identity& local) {
        if(terminal())return;
        const bool ok=f==Fault::None;
        if(ok && !locked) { lockedIdentity=local;locked=true; }
        if(locked && (!ok || !(local==lockedIdentity))) {
            // Before any READY a transient pre-lock fault only resets stability; after the
            // identity was locked, any change or fault is a world loss for this session.
            if(current==State::MapReadySent || current==State::Ready || (ok && !(local==lockedIdentity))) { current=State::Invalidated;return; }
            stable=0;verifiedThisTick=false;return;
        }
        verifiedThisTick=ok;
        if(!ok) { stable=0;return; }
        if(current==State::Disconnected)return;
        if(local.name!=host) { current=State::Mismatch;return; }
        if(stable<StableTicks)++stable;
        if(current==State::AwaitNativeMap && stable>=StableTicks)current=State::IdentityVerified;
    }
    bool shouldSendReady() const { return current==State::IdentityVerified && verifiedThisTick; }
    void readyQueued() { if(current==State::IdentityVerified)current=State::MapReadySent; }
    bool baseline(std::uint32_t gen,std::uint32_t mapEpoch,const std::string& name) {
        if(current!=State::MapReadySent || gen!=generation || mapEpoch!=epoch || name!=host)return false;
        current=State::Ready;return true;
    }
    // Any announced change of world while bound is unsupported in DOC56: fail closed.
    bool mapChange(std::uint32_t gen,std::uint32_t mapEpoch,const std::string& name) {
        if(gen!=generation)return true; // other session's control message: ignored
        if(mapEpoch==epoch && name==host)return true;
        if(!terminal())current=State::Invalidated;
        return false;
    }
    void lost() { if(!terminal()) { current=State::Disconnected;stable=0; } }
    bool snapshotEligible(std::uint32_t gen,std::uint32_t mapEpoch) const {
        return current==State::Ready && verifiedThisTick && gen==generation && mapEpoch==epoch;
    }
    bool terminal() const { return current==State::Mismatch || current==State::Invalidated; }
    State state() const { return current; }
    std::uint32_t mapEpoch() const { return epoch; }
    std::uint32_t connection() const { return generation; }
    const std::string& hostName() const { return host; }
    std::uint32_t stableTicks() const { return stable; }
private:
    Identity lockedIdentity;
    bool locked{}, verifiedThisTick{};
    std::string host;
    std::uint32_t generation{}, epoch{}, stable{};
    std::uint8_t slot{};
    State current{State::Disconnected};
};
} // namespace map56
