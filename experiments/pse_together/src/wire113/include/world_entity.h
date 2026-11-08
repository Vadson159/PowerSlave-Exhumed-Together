#pragma once

#include "net_protocol.h"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>

namespace pse {

// Spatial Vector 3D
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    float distanceTo(const Vec3& o) const {
        float dx = x - o.x;
        float dy = y - o.y;
        float dz = z - o.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    float distance2D(const Vec3& o) const {
        float dx = x - o.x;
        float dy = y - o.y;
        return std::sqrt(dx * dx + dy * dy);
    }
};

// Rotation (Yaw and Pitch in degrees)
struct Rot2 {
    float yaw = 0.0f;
    float pitch = 0.0f;

    Rot2() = default;
    Rot2(float y, float p) : yaw(y), pitch(p) {}
};

// Network Entity Types
enum class EntityType : uint8_t {
    NONE = 0,
    PLAYER = 1,
    ENEMY = 2,
    PROJECTILE = 3,
    PICKUP = 4,
    DOOR = 5,
    SWITCH = 6
};

// Enemy Class Subtypes
enum class EnemySubType : uint16_t {
    ANUBIS = 1,     // Jackal warrior - melee & fireball
    MUMMY = 2,      // High HP staff caster
    BASTET = 3,     // Fast feline assassin
    HAWK = 4,       // Flying ranged attacker
    SET_BOSS = 5    // Heavy boss with quake attack
};

// Projectile Class Subtypes
enum class ProjectileSubType : uint16_t {
    BULLET = 1,
    FIREBALL = 2,
    GRENADE = 3,
    COBRA_STAFF = 4,
    RING_BLAST = 5
};

// Pickup Class Subtypes
enum class PickupSubType : uint16_t {
    HEALTH_SMALL = 1, // +10 HP
    HEALTH_BIG = 2,   // +25 HP
    AMMO_BULLETS = 3, // +50 Ammo
    KEY_POWER = 10,   // Key 0 (Red)
    KEY_TIME = 11,    // Key 1 (Green)
    KEY_EARTH = 12,   // Key 2 (Blue)
    KEY_SOUL = 13,    // Key 3 (Yellow)
    ANKH = 20         // Maximum Health upgrade (+Ankh counter)
};

// Interactive Door States
enum class DoorState : uint8_t {
    CLOSED = 0,
    OPENING = 1,
    OPEN = 2,
    CLOSING = 3
};

// Despawn Reason Flags
enum class DespawnReason : uint8_t {
    KILLED = 0,
    COLLECTED = 1,
    EXPIRED = 2,
    MAP_TRANSITION = 3,
    DISCONNECTED = 4,
    REPLACED = 5
};

const char* getEntityTypeName(EntityType t);
const char* getEnemySubTypeName(EnemySubType st);
const char* getProjectileSubTypeName(ProjectileSubType st);
const char* getPickupSubTypeName(PickupSubType st);
const char* getDoorStateName(DoorState s);

// Forward declarations
class NetworkEntity;
class PlayerNetworkEntity;

// Simulation context provided to entities during host tick
class WorldSimulationContext {
public:
    virtual ~WorldSimulationContext() = default;

    virtual size_t getPlayerCount() const = 0;
    virtual const PlayerNetworkEntity* getPlayerByIndex(size_t i) const = 0;
    virtual PlayerNetworkEntity* getPlayerByIndexMut(size_t i) = 0;
    virtual const PlayerNetworkEntity* getPlayerBySlot(uint8_t slotId) const = 0;
    virtual PlayerNetworkEntity* getPlayerBySlotMut(uint8_t slotId) = 0;

    virtual NetworkEntity* getEntity(uint32_t entityId) = 0;
    virtual const NetworkEntity* getEntity(uint32_t entityId) const = 0;

    virtual void spawnProjectile(ProjectileSubType subType, uint32_t shooterEntityId, uint8_t shooterSlot,
                                 const Vec3& origin, const Vec3& velocity, int32_t damage) = 0;
    virtual void despawnEntity(uint32_t entityId, DespawnReason reason) = 0;
    virtual void triggerWorldEvent(uint16_t eventType, uint32_t sourceEntityId, uint32_t targetEntityId,
                                   int32_t eventValue, float floatValue) = 0;

    virtual uint16_t getTeamKeys() const = 0;
    virtual void addTeamKey(uint16_t keyBit) = 0;
    virtual uint32_t getMapEpoch() const = 0;
};

// Base Network Entity
class NetworkEntity {
public:
    NetworkEntity(uint32_t id, uint32_t gen, EntityType t, uint16_t st = 0)
        : entityId(id), generation(gen), type(t), subType(st) {}
    virtual ~NetworkEntity() = default;

    uint32_t entityId = 0;
    uint32_t generation = 1;
    EntityType type = EntityType::NONE;
    uint16_t subType = 0;

    Vec3 position;
    Vec3 velocity;
    Rot2 rotation;

    int32_t health = 100;
    int32_t maxHealth = 100;
    uint32_t stateFlags = 0;
    bool active = true;
    uint32_t mapEpoch = 1;

    virtual void tick(float dt, WorldSimulationContext& ctx) = 0;
};

// 1. Player Entity
class PlayerNetworkEntity : public NetworkEntity {
public:
    PlayerNetworkEntity(uint32_t id, uint32_t gen, uint8_t slot)
        : NetworkEntity(id, gen, EntityType::PLAYER, 0), slotId(slot) {
        maxHealth = DEFAULT_HEALTH;
        health = DEFAULT_HEALTH;
    }

    uint8_t slotId = 0; // 0..7
    uint32_t weaponMask = DEFAULT_WEAPON_MASK; // Machete + Pistol
    uint8_t activeWeapon = WEAPON_PISTOL;       // Pistol
    uint16_t ammo = DEFAULT_PISTOL_AMMO;
    int32_t armor = DEFAULT_ARMOR;
    uint32_t deathSequence = 0;
    uint16_t teamKeyMask = 0;

    bool isAlive() const { return active && health > 0; }

    void tick(float dt, WorldSimulationContext& ctx) override;
};

// 2. Enemy Entity (Patrol, Aggro, Chase nearest player, Attack/Fire)
class EnemyNetworkEntity : public NetworkEntity {
public:
    EnemyNetworkEntity(uint32_t id, uint32_t gen, EnemySubType enemyType, const Vec3& spawnPos)
        : NetworkEntity(id, gen, EntityType::ENEMY, static_cast<uint16_t>(enemyType)) {
        position = spawnPos;
        spawnOrigin = spawnPos;
        initStats(enemyType);
    }

    Vec3 spawnOrigin;
    float patrolRadius = 250.0f;
    float moveSpeed = 110.0f;
    float sightDistance = 750.0f;
    float attackRange = 120.0f;
    float rangedRange = 450.0f;
    float attackCooldown = 0.0f;
    float cooldownTime = 1.5f;
    int32_t meleeDamage = 15;
    uint32_t targetPlayerId = 0;

    void initStats(EnemySubType st);
    void tick(float dt, WorldSimulationContext& ctx) override;
};

// 3. Projectile Entity (Linear trajectory, collision, damage, expiration)
class ProjectileNetworkEntity : public NetworkEntity {
public:
    ProjectileNetworkEntity(uint32_t id, uint32_t gen, ProjectileSubType pType,
                            uint32_t shooterId, uint8_t shooterSlot,
                            const Vec3& startPos, const Vec3& vel, int32_t dmg)
        : NetworkEntity(id, gen, EntityType::PROJECTILE, static_cast<uint16_t>(pType)),
          shooterEntityId(shooterId), shooterSlotId(shooterSlot), damage(dmg) {
        position = startPos;
        velocity = vel;
        health = 1;
        maxHealth = 1;
        initStats(pType);
    }

    uint32_t shooterEntityId = 0;
    uint8_t shooterSlotId = 0xFF;
    int32_t damage = 25;
    float collisionRadius = 18.0f;
    float lifetimeRemaining = 4.0f;

    void initStats(ProjectileSubType pt);
    void tick(float dt, WorldSimulationContext& ctx) override;
};

// 4. Pickup Entity (Health, Ammo, Keys, Ankhs with proximity collection)
class PickupNetworkEntity : public NetworkEntity {
public:
    PickupNetworkEntity(uint32_t id, uint32_t gen, PickupSubType pType, const Vec3& pos, uint16_t amt = 25)
        : NetworkEntity(id, gen, EntityType::PICKUP, static_cast<uint16_t>(pType)),
          amount(amt) {
        position = pos;
        health = 1;
        maxHealth = 1;
    }

    uint16_t amount = 25;
    float collectRadius = 45.0f;
    bool collected = false;

    void tick(float dt, WorldSimulationContext& ctx) override;
};

// 5. Door Entity (Interactive / Key gated / Switch linked)
class DoorNetworkEntity : public NetworkEntity {
public:
    DoorNetworkEntity(uint32_t id, uint32_t gen, const Vec3& pos, uint16_t keyMask = 0)
        : NetworkEntity(id, gen, EntityType::DOOR, 0), requiredKeyMask(keyMask) {
        position = pos;
        health = 9999;
        maxHealth = 9999;
    }

    DoorState state = DoorState::CLOSED;
    float openRatio = 0.0f;   // 0.0 (closed) .. 1.0 (fully open)
    float openSpeed = 1.5f;   // Open/Close cycle speed
    uint16_t requiredKeyMask = 0; // Bitmask of required keys (0 = none)
    float autoCloseDelay = 4.0f;
    float autoCloseTimer = 0.0f;
    uint32_t linkedSwitchId = 0;

    bool requestOpen(uint16_t teamKeys, std::string& outReason);
    void forceOpen();
    void tick(float dt, WorldSimulationContext& ctx) override;
};

// 6. Switch Entity (Wall or floor trigger that operates linked doors)
class SwitchNetworkEntity : public NetworkEntity {
public:
    SwitchNetworkEntity(uint32_t id, uint32_t gen, const Vec3& pos, uint32_t doorId)
        : NetworkEntity(id, gen, EntityType::SWITCH, 0), targetDoorEntityId(doorId) {
        position = pos;
        health = 9999;
        maxHealth = 9999;
    }

    bool isActivated = false;
    bool toggleable = true;
    uint32_t targetDoorEntityId = 0;
    float triggerRadius = 55.0f;

    bool trigger(uint8_t playerSlot, WorldSimulationContext& ctx);
    void tick(float dt, WorldSimulationContext& ctx) override;
};

} // namespace pse
