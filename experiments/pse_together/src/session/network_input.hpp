#pragma once
#include "enet_worker.h"
#include "map_identity.hpp"
#include <array>
#include <deque>
#include <string>
#include <cstdint>

namespace m12a {
using Logger = void(*)(const char*, const char*, ...);
// Set by the host DLL: receives the verified respawn request of a remote player (simulation thread only).
extern void (*respawnHandler)(const pse::BridgeRespawnRequest&);
// Set by the host DLL: world requests of a remote player (mover spawned / key picked up on the client), simulation thread only.
extern void (*worldRequestHandler)(const pse::BridgeInteractionRequest&);
struct InputTimeout {
    std::uint64_t ticks=6;
    std::uint64_t wallMs=150;
    bool expired(std::uint64_t elapsedTicks,std::uint64_t elapsedMs) const {
        return elapsedTicks>=ticks || elapsedMs>=wallMs;
    }
};
// Simulation-thread owner. The DLL retains this object until process reclaim;
// stop() joins the worker before native unbind and retires all queued events.
// Slot 0 is the host (no peer); slots 1..7 are remote clients, each with its own generation, input state and map gate.
class Session {
public:
    static constexpr unsigned MaxSlots = 8;
    explicit Session(Logger logger,InputTimeout timeout={}) : worker(bridge, pse::ENetWorkerMode::HOST), log(logger), inputTimeout(timeout) {}
    bool start(std::uint16_t port = 28745);
    void tick(std::uint64_t tick, bool gameplay);
    void stop(const char* reason);
    // The remote player's own view and position (v1.14). valid only while commands keep arriving.
    struct Pose { float yaw{}, pitch{}, x{}, y{}, z{}; int sector{-1}; bool valid{}; };
    // Peer is connected, has completed MapReady and the session is running (slots 1..7; false for anything else).
    bool wantsPlayer(unsigned slot) const { const auto* p=peerOf(slot); return p && p->active && p->ready && !stopped; }
    bool anyWantsPlayer() const { return connected()!=0; }
    unsigned connected() const { unsigned n=0;for(unsigned s=1;s<MaxSlots;++s)n+=wantsPlayer(s);return n; }
    std::uint32_t nativeButtons(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->buttons : 0; }
    // Rising edges (attack/jump/use/weapon) seen since the last call, as native button bits: a one-tick tap must reach the
    // player even if the latest-state coalescing swallowed the held state. Consumed once per update of that player.
    std::uint32_t takeNativeEdges(unsigned slot) { auto* p=peerOf(slot);if(!p)return 0;const auto e=p->pendingEdges;p->pendingEdges=0;return e; }
    const Pose& pose(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->remotePose : noPose; }
    // The remote player's active weapon (0..7) from its latest command; 0xff = unknown.
    std::uint8_t selectedWeapon(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->remoteWeapon : std::uint8_t{0xff}; }
    // Stable identity of the client occupying the slot (its join participantId): survives map changes and rejoins in any
    // order, unlike the slot number. Empty when the slot is not connected.
    std::string participantKey(unsigned slot) const { const auto* p=peerOf(slot);return p && p->active ? p->participant : std::string{}; }
    std::uint32_t generation(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->gen : 0; }
    std::uint32_t sequence(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->lastSeq : 0; }
    std::uint64_t acceptedAt(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->lastInputTick : 0; }
    // Increments every time any peer becomes ready: the host re-publishes its shared world state to the newcomer.
    std::uint32_t joinSerial() const { return joinCounter; }
    bool running() const { return started && !stopped; }
    // Optional, first-level diagnostic contract; configure before start().
    bool configureNativeSnapshots(std::uint32_t levelEpoch);
    // Unicast to one client with that client's own connection generation (subject = s.slotId, any slot). False when that
    // peer is not snapshot-eligible.
    bool publishNativeSnapshot(unsigned recipientSlot,pse::BridgePlayerSnapshot snapshot);
    // Reliable world event: one event sequence, delivered to every connected client. False when no client is ready in this world.
    bool publishWorldEvent(std::uint16_t type,std::uint32_t source,std::uint32_t target,std::int32_t value);
    // Best-effort world snapshot (enemies) to all connected clients; dropped when the queue is full or no client is ready.
    bool publishWorldSnapshot(std::vector<pse::EntitySnapshot>&& entities);
    std::uint32_t mapEpoch() const { return epoch; }
    // Milestone 4: the host is leaving for another map. Reliable "follow me" unicast to EACH connected client with its own
    // generation (MapChange with epoch 0 = an order, not a session map). False when no client is connected.
    bool announceNextMap(const std::string& name);
    // DOC56 opt-in: native map identity bound to a non-constant epoch; configure before start(). Binds every peer gate.
    bool configureNativeMap(const map56::Identity& identity,std::uint32_t levelEpoch);
    // Every SIMULATION tick before tick(): current native observation result (verifies every gate).
    void verifyNativeMap(map56::Fault fault,const map56::Identity& current);
    bool snapshotEligible(unsigned slot) const;
    bool anySnapshotEligible() const { for(unsigned s=1;s<MaxSlots;++s)if(snapshotEligible(s))return true;return false; }
    // Per-peer gate; all gates are bound to the same identity/epoch.
    const map56::HostGate& mapGate(unsigned slot) const { const auto* p=peerOf(slot);return p ? p->gate : peers[0].gate; }
    // Test pressure uses the real transport, with the same single SIMULATION owner.
    pse::ThreadBridge bridge;
    pse::ENetWorker worker;
private:
    struct Peer {
        map56::HostGate gate;
        std::uint64_t lastInputTick{}, joinedTick{}, lastInputMs{};
        std::uint32_t gen{}, lastSeq{}, buttons{}, pendingEdges{};
        bool active{}, ready{}, hasSeq{};
        Pose remotePose{};
        std::uint8_t remoteWeapon{0xff};
        std::string participant;
    };
    static inline const Pose noPose{};
    // nullptr for slot 0 (the host) and anything outside 1..7.
    const Peer* peerOf(unsigned slot) const { return slot>=1 && slot<MaxSlots ? &peers[slot] : nullptr; }
    Peer* peerOf(unsigned slot) { return slot>=1 && slot<MaxSlots ? &peers[slot] : nullptr; }
    void enqueue(pse::BridgeMessage message);
    void flush();
    void consume(pse::BridgeMessage& message);
    void neutral(unsigned slot,const char* reason);
    void recover(unsigned slot,std::uint32_t reasons);   // one peer: kick it and retire its queued unicasts only
    void recoverAll(std::uint32_t reasons);              // global resync (outbox loss): kick every connected peer, retire the outbox
    void kick(unsigned slot);
    void baseline(unsigned slot);
    Logger log;
    InputTimeout inputTimeout;
    std::deque<pse::BridgeMessage> outbox;
    std::uint64_t nowTick{};
    bool started{}, stopped{}, levelReady{};
    bool overflowed{};
    std::uint32_t epoch{1};
    bool nativeSnapshots{};
    bool configureSnapshots(const std::string& domain,std::uint32_t levelEpoch);
    const char* domain() const;
    std::array<Peer,MaxSlots> peers{};
    std::string nativeDomain;
    bool nativeMap{};
    std::uint32_t worldSequence{}, worldSnapshotSequence{}, joinCounter{};
};
} // namespace m12a
