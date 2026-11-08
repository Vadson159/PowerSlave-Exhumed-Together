// P3 / S1.1: pure rules for the in-DLL engine-teardown witness and the native anti-ABA identity.
// Layouts are exact-build static facts (DOC-58B.2, VERIFIED static + native for one title exit):
//   tagged block header = payload - 0x60 (DAT_140504840 == 0x60): +0 magic 0x3151983, +8 alloc seq, +0x10 payload
//   size, +0x18 heap, +0x20 owner slot, +0x28 prev, +0x30 next, +0x38 locked byte, +0x40 dtor callback,
//   +0x48 ctor count, +0x50 ctor size. Purge c58b0(heap) walks heap+8 / block+0x30, dispatches c5310 per unlocked
//   block, unlinks it and frees it in the same iteration. ObjectNodes live in 0x1210-byte payloads of heap af4600
//   blocks, first slot at block+0x68, stride 0x48 (58B.2 observer, node index 17).
// The adapter copies raw bytes only while the memory is attested live; these helpers never dereference anything.
#pragma once
#include "visual_proxy.hpp"
#include <cstdint>
#include <cstring>

namespace pse::p3::witness {
using pse::proxy::EngineTeardownWitness;
inline constexpr std::uint32_t Magic=0x3151983u;
inline constexpr std::uintptr_t HeaderSize=0x60,NodeFirst=0x68,NodeStride=0x48;
inline constexpr std::uint64_t ActorSize=0x1b8,NodeBlockPayload=0x1210;
inline constexpr std::size_t RawHeader=0x58;

struct BlockHeader {
    std::uint32_t magic{},tag{};
    std::uint64_t allocSeq{},size{};
    std::uintptr_t heap{},owner{},prev{},next{};
    std::uint8_t locked{};
    std::uintptr_t dtor{};
    std::uint64_t ctorCount{},ctorSize{};
};
inline BlockHeader decode(const unsigned char (&raw)[RawHeader]) noexcept {
    BlockHeader h;
    std::memcpy(&h.magic,raw+0x00,4);std::memcpy(&h.tag,raw+0x04,4);std::memcpy(&h.allocSeq,raw+0x08,8);std::memcpy(&h.size,raw+0x10,8);
    std::memcpy(&h.heap,raw+0x18,8);std::memcpy(&h.owner,raw+0x20,8);std::memcpy(&h.prev,raw+0x28,8);std::memcpy(&h.next,raw+0x30,8);
    h.locked=raw[0x38];std::memcpy(&h.dtor,raw+0x40,8);std::memcpy(&h.ctorCount,raw+0x48,8);std::memcpy(&h.ctorSize,raw+0x50,8);
    return h;
}
// Actor1007 storage owned by the StateExit purge of af4490 (58B.2 E5).
inline bool actorBlockOk(const BlockHeader& h,std::uintptr_t actorHeap,std::uintptr_t dtorThunk) noexcept {
    return h.magic==Magic && h.heap==actorHeap && h.dtor==dtorThunk && h.size==ActorSize && h.ctorCount==1 && h.ctorSize==ActorSize && !h.locked && h.allocSeq;
}
// A node block purged by af4600 (58B.2 E3); c58b0 zeroes +0x18 only right before free.
inline bool nodeBlockOk(const BlockHeader& h,std::uintptr_t nodeHeap) noexcept {
    return h.magic==Magic && h.heap==nodeHeap && h.size==NodeBlockPayload && !h.locked;
}
inline bool nodeInBlock(std::uintptr_t node,std::uintptr_t block) noexcept {
    if(!node || !block || node<block+NodeFirst || node>=block+HeaderSize+NodeBlockPayload) return false;
    return (node-(block+NodeFirst))%NodeStride==0;
}

// Native identity of one incarnation, recorded at its factory return.
struct Identity {
    std::uintptr_t actor{},node{},actorBlock{},nodeBlock{};
    std::uint64_t allocSeq{};
    std::uint32_t incarnation{};
    bool valid{};
};
enum class Reuse : std::uint8_t { First, NewAddress, PoolReuseAfterRetire, NewBlockSameAddress, Anomaly };
inline const char* toString(Reuse r) noexcept {
    switch(r) {
        case Reuse::First: return "First";
        case Reuse::NewAddress: return "NewAddress";
        case Reuse::PoolReuseAfterRetire: return "PoolReuseAfterRetire";
        case Reuse::NewBlockSameAddress: return "NewBlockSameAddress";
        case Reuse::Anomaly: return "Anomaly";
    }
    return "Invalid";
}
// Anti-ABA classification of the next Actor against the previous incarnation. Same storage with the same alloc seq is
// legal only as a pop of the class pool after our own witnessed PoolReturn; after an engine purge the storage was
// freed, so the same address must carry a new alloc seq.
inline Reuse classify(const Identity& prev,bool prevPoolRetired,const Identity& next) noexcept {
    if(!prev.valid) return Reuse::First;
    if(next.actor!=prev.actor) return Reuse::NewAddress;
    if(next.allocSeq!=prev.allocSeq) return Reuse::NewBlockSameAddress;
    return prevPoolRetired ? Reuse::PoolReuseAfterRetire : Reuse::Anomaly;
}

// Events recorded on the attested simulation thread between the barrier and the return of the original StateExit.
struct TeardownLog {
    bool armed{},barrierBeforeUnload{},unloadBeforeActorPurge{},actorPurgeBeforeNodePurge{};
    std::uint32_t unloadEntries{},unloadReturns{},actorPurgeEntries{},actorPurgeReturns{},nodePurgeEntries{},nodePurgeReturns{};
    bool actorBlockAtEntry{},actorBlockAbsentAtReturn{},actorDispatched{},dispatchIdentityOk{},dtorEntered{},dtorReturned{};
    bool nodeBlockAtEntry{},nodeBlockAbsentAtReturn{};
    std::uint32_t violations{},nativeCallsAfterBarrier{};
};
// ArenaDestroyed only for the complete ordered chain; a purge that never ran is NotObserved; anything else Unknown.
inline EngineTeardownWitness verdict(const Identity& id,const TeardownLog& t) noexcept {
    if(!id.valid || !t.armed) return EngineTeardownWitness::Unknown;
    if(!t.actorPurgeEntries) return EngineTeardownWitness::NotObserved;
    const bool unload=t.barrierBeforeUnload && t.unloadEntries==1 && t.unloadReturns==1 && t.unloadBeforeActorPurge;
    const bool actor=t.actorPurgeEntries==1 && t.actorPurgeReturns==1 && t.actorBlockAtEntry && t.actorDispatched && t.dispatchIdentityOk &&
        t.dtorEntered && t.dtorReturned && t.actorBlockAbsentAtReturn;
    const bool node=t.nodePurgeEntries==1 && t.nodePurgeReturns==1 && t.actorPurgeBeforeNodePurge && t.nodeBlockAtEntry && t.nodeBlockAbsentAtReturn;
    return unload && actor && node && !t.violations && !t.nativeCallsAfterBarrier ? EngineTeardownWitness::ArenaDestroyed : EngineTeardownWitness::Unknown;
}

} // namespace pse::p3::witness
