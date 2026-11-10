#pragma once
#include "enet_worker.h"
#include "map_identity.hpp"
#include <deque>
#include <cstdint>

namespace m12a {
using Logger = void(*)(const char*, const char*, ...);
// Set by the host DLL: receives the verified respawn request of the remote player (simulation thread only).
extern void (*respawnHandler)(const pse::BridgeRespawnRequest&);
// Set by the host DLL: world requests of the remote player (mover spawned / key picked up on the client), simulation thread only.
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
class Session {
public:
    explicit Session(Logger logger,InputTimeout timeout={}) : worker(bridge, pse::ENetWorkerMode::HOST), log(logger), inputTimeout(timeout) {}
    bool start(std::uint16_t port = 28745);
    void tick(std::uint64_t tick, bool gameplay);
    void stop(const char* reason);
    bool wantsPlayer() const { return active && ready && !stopped; }
    std::uint32_t nativeButtons() const { return buttons; }
    // Rising edges (attack/jump/use/weapon) seen since the last call, as native button bits: a one-tick tap must reach P2 even if
    // the latest-state coalescing swallowed the held state. Consumed once per P2 update.
    std::uint32_t takeNativeEdges() { const auto e = pendingEdges; pendingEdges = 0; return e; }
    // The remote player's own view and position (v1.14). valid only while commands keep arriving.
    struct Pose { float yaw{}, pitch{}, x{}, y{}, z{}; int sector{-1}; bool valid{}; };
    const Pose& pose() const { return remotePose; }
    // The remote player's active weapon (0..7) from its latest command; 0xff = unknown.
    std::uint8_t selectedWeapon() const { return remoteWeapon; }
    std::uint32_t generation() const { return gen; }
    std::uint32_t sequence() const { return lastSeq; }
    std::uint64_t acceptedAt() const { return lastInputTick; }
    bool running() const { return started && !stopped; }
    // Optional, first-level diagnostic contract; configure before start().
    bool configureNativeSnapshots(std::uint32_t levelEpoch);
    bool publishNativeSnapshot(pse::BridgePlayerSnapshot snapshot);
    // Reliable world event to the connected client (doors, keys). False when no client is ready in this world.
    // Best-effort world snapshot (enemies) to the connected client; dropped when the queue is full or no client is ready.
    bool publishWorldSnapshot(std::vector<pse::EntitySnapshot>&& entities);
    bool publishWorldEvent(std::uint16_t type,std::uint32_t source,std::uint32_t target,std::int32_t value);
    std::uint32_t mapEpoch() const { return epoch; }
    // DOC56 opt-in: native map identity bound to a non-constant epoch; configure before start().
    bool configureNativeMap(const map56::Identity& identity,std::uint32_t levelEpoch);
    // Every SIMULATION tick before tick(): current native observation result.
    void verifyNativeMap(map56::Fault fault,const map56::Identity& current);
    bool snapshotEligible() const;
    const map56::HostGate& mapGate() const { return gate; }
    // Test pressure uses the real transport, with the same single SIMULATION owner.
    pse::ThreadBridge bridge;
    pse::ENetWorker worker;
private:
    void enqueue(pse::BridgeMessage message);
    void flush();
    void consume(pse::BridgeMessage& message);
    void neutral(const char* reason);
    void recover(std::uint32_t reasons);
    void baseline();
    Logger log;
    InputTimeout inputTimeout;
    std::deque<pse::BridgeMessage> outbox;
    std::uint64_t nowTick{}, lastInputTick{}, joinedTick{};
    std::uint64_t lastInputMs{};
    std::uint32_t gen{}, lastSeq{}, buttons{};
    bool active{}, ready{}, started{}, stopped{}, levelReady{}, hasSeq{};
    bool overflowed{};
    std::uint32_t epoch{1};
    bool nativeSnapshots{};
    bool configureSnapshots(const std::string& domain,std::uint32_t levelEpoch);
    const char* domain() const;
    map56::HostGate gate;
    std::string nativeDomain;
    bool nativeMap{};
    std::uint32_t worldSequence{}, worldSnapshotSequence{};
    std::uint32_t pendingEdges{};
    Pose remotePose{};
    std::uint8_t remoteWeapon{0xff};
};
} // namespace m12a
