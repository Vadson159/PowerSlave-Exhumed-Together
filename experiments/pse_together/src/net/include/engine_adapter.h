#pragma once

#include "world_entity.h"
#include "player_state.h"
#include <cstdint>
#include <string>

namespace pse {

// ============================================================================
// Threading & SPSC Queue Architecture Contract
// ============================================================================
// The native KEX 2022 engine runs a synchronous main simulation thread.
// Network transport runs on a dedicated background ENet worker thread.
//
// Concurrency Model: Exactly TWO bounded Single-Producer Single-Consumer (SPSC)
// lock-free ring buffers decouple the two execution domains:
//
// 1. Inbound SPSC Queue (ENet Worker -> KEX Simulation Thread):
//    - Producer: ENet worker thread (network packet reception & decoding).
//    - Consumer: KEX simulation thread (processed at the start of game tick).
//    - Payloads: Inbound player inputs, remote player snapshot data, world events.
//    - Capacity: Bounded (e.g. 128-256 elements), drop oldest/stale on overflow.
//
// 2. Outbound SPSC Queue (KEX Simulation Thread -> ENet Worker):
//    - Producer: KEX simulation thread (local state changes, outbound input commands).
//    - Consumer: ENet worker thread (packet packing, MTU batching, enet_peer_send).
//    - Payloads: Local player inputs, entity transform/state mutations, RPC events.
//    - Capacity: Bounded (e.g. 256-512 elements).
//
// Direct cross-thread invocations between ENet callbacks and KEX engine methods
// are STRICTLY PROHIBITED to avoid native re-entrancy and deadlocks.
// ============================================================================

// Transform in 3D game coordinates
struct Transform3D {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;

    Transform3D() = default;
    Transform3D(float x_, float y_, float z_, float y = 0.0f, float p = 0.0f, float r = 0.0f)
        : x(x_), y(y_), z(z_), yaw(y), pitch(p), roll(r) {}
};

// Velocity vector
struct Velocity3D {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;

    Velocity3D() = default;
    Velocity3D(float x, float y, float z) : vx(x), vy(y), vz(z) {}
};

// High-level actor attributes
struct ActorStateInfo {
    int32_t health = DEFAULT_HEALTH;
    int32_t maxHealth = DEFAULT_HEALTH;
    uint32_t stateFlags = 0;
    uint32_t meta = 0;

    ActorStateInfo() = default;
    ActorStateInfo(int32_t hp, int32_t maxHp = DEFAULT_HEALTH, uint32_t flags = 0, uint32_t m = 0)
        : health(hp), maxHealth(maxHp), stateFlags(flags), meta(m) {}
};

// Client Visual Observer: Receives replicated states for rendering
// without touching host authoritative game logic
class IClientVisualObserver {
public:
    virtual ~IClientVisualObserver() = default;

    virtual void onEntityVisualSpawn(uint32_t entityId, uint32_t generation, EntityType type, uint16_t subType,
                                     const Transform3D& transform, const ActorStateInfo& state) = 0;

    virtual void onEntityVisualUpdate(uint32_t entityId, uint32_t generation,
                                      const Transform3D& interpolatedTransform,
                                      const Velocity3D& velocity,
                                      const ActorStateInfo& state) = 0;

    virtual void onEntityVisualDespawn(uint32_t entityId, uint32_t generation, DespawnReason reason) = 0;

    virtual void onDoorVisualState(uint32_t doorId, DoorState state, float openRatio) = 0;

    virtual void onWorldEvent(uint16_t eventType, uint32_t sourceEntityId, uint32_t targetEntityId,
                              int32_t eventValue, float floatValue) = 0;
};

// Engine Replication Adapter: Abstract interface for Sol Max
// Bridges native KEX actor life cycle with network replication
class IEngineReplicationAdapter {
public:
    virtual ~IEngineReplicationAdapter() = default;

    // 1. Entity registration & lifecycle
    // Allows passing outGeneration to obtain the host-allocated generation identifier
    virtual uint32_t registerActor(EntityType type, uint16_t subType, const Transform3D& initialTransform,
                                   const ActorStateInfo& initialState, uint32_t* outGeneration = nullptr) = 0;

    // Updates with mandatory generational validation (generation > 0, ensures ABA-safety)
    virtual bool updateActorTransform(uint32_t entityId, const Transform3D& transform, const Velocity3D& velocity, uint32_t generation) = 0;
    virtual bool updateActorState(uint32_t entityId, const ActorStateInfo& state, uint32_t generation) = 0;
    virtual void unregisterActor(uint32_t entityId, DespawnReason reason, uint32_t generation) = 0;

    // 2. Gameplay events with mandatory target generational validation (generation > 0)
    virtual void notifyDamage(uint32_t targetEntityId, uint32_t sourceEntityId, int32_t damageAmount, uint32_t targetGeneration) = 0;
    virtual void notifyPickupCollected(uint32_t pickupEntityId, uint8_t playerSlot, uint32_t pickupGeneration) = 0;
    virtual void notifyDoorStateChanged(uint32_t doorEntityId, DoorState newState, float openRatio, uint32_t doorGeneration) = 0;
    virtual void notifySwitchToggled(uint32_t switchEntityId, uint8_t playerSlot, uint32_t switchGeneration) = 0;

    // 3. Campaign & Map Transitions
    virtual void onMapTransition(const std::string& newMapName, uint32_t newMapEpoch) = 0;

    // 4. Observer binding for client rendering
    virtual void setVisualObserver(IClientVisualObserver* observer) = 0;

    // 5. Starter loadout configuration for dynamic level/difficulty conditions
    virtual void setStarterLoadout(const StarterLoadout& loadout) = 0;
    virtual StarterLoadout getStarterLoadout() const = 0;
};

} // namespace pse
