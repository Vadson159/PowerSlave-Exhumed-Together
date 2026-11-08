#include "world_entity.h"
#include <cmath>
#include <numbers>

namespace pse {

constexpr float PI = 3.14159265358979323846f;

const char* getEntityTypeName(EntityType t) {
    switch (t) {
        case EntityType::PLAYER:     return "Player";
        case EntityType::ENEMY:      return "Enemy";
        case EntityType::PROJECTILE: return "Projectile";
        case EntityType::PICKUP:     return "Pickup";
        case EntityType::DOOR:       return "Door";
        case EntityType::SWITCH:     return "Switch";
        default:                     return "None";
    }
}

const char* getEnemySubTypeName(EnemySubType st) {
    switch (st) {
        case EnemySubType::ANUBIS:   return "Anubis";
        case EnemySubType::MUMMY:    return "Mummy";
        case EnemySubType::BASTET:   return "Bastet";
        case EnemySubType::HAWK:     return "Hawk";
        case EnemySubType::SET_BOSS: return "Set_Boss";
        default:                     return "Unknown";
    }
}

const char* getProjectileSubTypeName(ProjectileSubType st) {
    switch (st) {
        case ProjectileSubType::BULLET:      return "Bullet";
        case ProjectileSubType::FIREBALL:    return "Fireball";
        case ProjectileSubType::GRENADE:     return "Grenade";
        case ProjectileSubType::COBRA_STAFF: return "CobraStaff";
        case ProjectileSubType::RING_BLAST:  return "RingBlast";
        default:                             return "Unknown";
    }
}

const char* getPickupSubTypeName(PickupSubType st) {
    switch (st) {
        case PickupSubType::HEALTH_SMALL: return "HealthSmall";
        case PickupSubType::HEALTH_BIG:   return "HealthBig";
        case PickupSubType::AMMO_BULLETS: return "AmmoBullets";
        case PickupSubType::KEY_POWER:    return "KeyPower";
        case PickupSubType::KEY_TIME:     return "KeyTime";
        case PickupSubType::KEY_EARTH:    return "KeyEarth";
        case PickupSubType::KEY_SOUL:     return "KeySoul";
        case PickupSubType::ANKH:         return "Ankh";
        default:                          return "Unknown";
    }
}

const char* getDoorStateName(DoorState s) {
    switch (s) {
        case DoorState::CLOSED:  return "Closed";
        case DoorState::OPENING: return "Opening";
        case DoorState::OPEN:    return "Open";
        case DoorState::CLOSING: return "Closing";
        default:                 return "Unknown";
    }
}

// ----------------------------------------------------------------------------
// PlayerNetworkEntity
// ----------------------------------------------------------------------------
void PlayerNetworkEntity::tick(float /*dt*/, WorldSimulationContext& /*ctx*/) {
    if (health <= 0) {
        health = 0;
        stateFlags |= 0x01; // Dead flag
    } else {
        stateFlags &= ~0x01;
    }
}

// ----------------------------------------------------------------------------
// EnemyNetworkEntity
// ----------------------------------------------------------------------------
void EnemyNetworkEntity::initStats(EnemySubType st) {
    switch (st) {
        case EnemySubType::ANUBIS:
            maxHealth = 80;
            health = 80;
            moveSpeed = 120.0f;
            sightDistance = 800.0f;
            attackRange = 120.0f;
            rangedRange = 450.0f;
            cooldownTime = 1.6f;
            meleeDamage = 15;
            break;
        case EnemySubType::MUMMY:
            maxHealth = 160;
            health = 160;
            moveSpeed = 70.0f;
            sightDistance = 900.0f;
            attackRange = 100.0f;
            rangedRange = 600.0f;
            cooldownTime = 2.0f;
            meleeDamage = 25;
            break;
        case EnemySubType::BASTET:
            maxHealth = 50;
            health = 50;
            moveSpeed = 160.0f;
            sightDistance = 850.0f;
            attackRange = 100.0f;
            rangedRange = 0.0f; // Pure melee
            cooldownTime = 1.0f;
            meleeDamage = 12;
            break;
        case EnemySubType::HAWK:
            maxHealth = 60;
            health = 60;
            moveSpeed = 130.0f;
            sightDistance = 1000.0f;
            attackRange = 80.0f;
            rangedRange = 500.0f;
            cooldownTime = 1.4f;
            meleeDamage = 10;
            break;
        case EnemySubType::SET_BOSS:
            maxHealth = 800;
            health = 800;
            moveSpeed = 90.0f;
            sightDistance = 1500.0f;
            attackRange = 200.0f;
            rangedRange = 800.0f;
            cooldownTime = 2.2f;
            meleeDamage = 40;
            break;
    }
}

void EnemyNetworkEntity::tick(float dt, WorldSimulationContext& ctx) {
    if (health <= 0) {
        active = false;
        return;
    }

    if (attackCooldown > 0.0f) {
        attackCooldown -= dt;
    }

    // 1. Locate closest alive player
    const PlayerNetworkEntity* closestPlayer = nullptr;
    float closestDist = 999999.0f;

    for (size_t i = 0; i < ctx.getPlayerCount(); ++i) {
        const PlayerNetworkEntity* p = ctx.getPlayerByIndex(i);
        if (p && p->isAlive()) {
            float dist = position.distance2D(p->position);
            if (dist < closestDist) {
                closestDist = dist;
                closestPlayer = p;
            }
        }
    }

    if (closestPlayer && closestDist <= sightDistance) {
        targetPlayerId = closestPlayer->entityId;

        // Turn towards target
        float dx = closestPlayer->position.x - position.x;
        float dy = closestPlayer->position.y - position.y;
        float targetYaw = std::atan2(dy, dx) * (180.0f / PI);
        rotation.yaw = targetYaw;

        // Move towards target if not yet in melee range
        if (closestDist > attackRange) {
            float rad = targetYaw * (PI / 180.0f);
            velocity.x = std::cos(rad) * moveSpeed;
            velocity.y = std::sin(rad) * moveSpeed;
            position.x += velocity.x * dt;
            position.y += velocity.y * dt;
        } else {
            velocity.x = 0.0f;
            velocity.y = 0.0f;
        }

        // Host-Authoritative Attack Decision
        if (attackCooldown <= 0.0f) {
            if (closestDist <= attackRange) {
                // Melee strike on player
                PlayerNetworkEntity* targetP = ctx.getPlayerByIndexMut(closestPlayer->slotId);
                if (targetP && targetP->isAlive()) {
                    targetP->health -= meleeDamage;
                    if (targetP->health < 0) targetP->health = 0;
                    ctx.triggerWorldEvent(0x04 /*EVENT_DAMAGE_CONFIRMED*/, entityId, targetP->entityId, meleeDamage, static_cast<float>(targetP->health));
                    attackCooldown = cooldownTime;
                }
            } else if (rangedRange > 0.0f && closestDist <= rangedRange) {
                // Ranged projectile emission towards player
                float rad = targetYaw * (PI / 180.0f);
                Vec3 pVel(std::cos(rad) * 450.0f, std::sin(rad) * 450.0f, 0.0f);
                Vec3 pStart = position;
                pStart.z += 32.0f; // Eye height
                ctx.spawnProjectile(ProjectileSubType::FIREBALL, entityId, 0xFF, pStart, pVel, 20);
                attackCooldown = cooldownTime;
            }
        }
    } else {
        // Idle patrol around spawn point
        targetPlayerId = 0;
        float distToSpawn = position.distance2D(spawnOrigin);
        if (distToSpawn > patrolRadius) {
            float dx = spawnOrigin.x - position.x;
            float dy = spawnOrigin.y - position.y;
            float returnYaw = std::atan2(dy, dx) * (180.0f / PI);
            rotation.yaw = returnYaw;
            float rad = returnYaw * (PI / 180.0f);
            velocity.x = std::cos(rad) * (moveSpeed * 0.5f);
            velocity.y = std::sin(rad) * (moveSpeed * 0.5f);
            position.x += velocity.x * dt;
            position.y += velocity.y * dt;
        } else {
            velocity.x = 0.0f;
            velocity.y = 0.0f;
        }
    }
}

// ----------------------------------------------------------------------------
// ProjectileNetworkEntity
// ----------------------------------------------------------------------------
void ProjectileNetworkEntity::initStats(ProjectileSubType pt) {
    switch (pt) {
        case ProjectileSubType::BULLET:
            collisionRadius = 14.0f;
            lifetimeRemaining = 2.5f;
            damage = 25;
            break;
        case ProjectileSubType::FIREBALL:
            collisionRadius = 22.0f;
            lifetimeRemaining = 4.0f;
            damage = 20;
            break;
        case ProjectileSubType::GRENADE:
            collisionRadius = 28.0f;
            lifetimeRemaining = 3.0f;
            damage = 60;
            break;
        case ProjectileSubType::COBRA_STAFF:
            collisionRadius = 20.0f;
            lifetimeRemaining = 3.5f;
            damage = 35;
            break;
        case ProjectileSubType::RING_BLAST:
            collisionRadius = 32.0f;
            lifetimeRemaining = 3.0f;
            damage = 75;
            break;
    }
}

void ProjectileNetworkEntity::tick(float dt, WorldSimulationContext& ctx) {
    if (!active) return;

    lifetimeRemaining -= dt;
    if (lifetimeRemaining <= 0.0f) {
        ctx.despawnEntity(entityId, DespawnReason::EXPIRED);
        return;
    }

    // Step transform
    position.x += velocity.x * dt;
    position.y += velocity.y * dt;
    position.z += velocity.z * dt;

    // Check collision
    if (shooterSlotId != 0xFF) {
        // Fired by a player -> check collision with enemies
        for (size_t i = 0; i < 512; ++i) {
            uint32_t targetId = 2000 + static_cast<uint32_t>(i);
            NetworkEntity* e = ctx.getEntity(targetId);
            if (e && e->active && e->type == EntityType::ENEMY && e->health > 0) {
                if (position.distanceTo(e->position) <= (collisionRadius + 30.0f)) {
                    // Confirmed hit on enemy!
                    e->health -= damage;
                    if (e->health < 0) e->health = 0;
                    ctx.triggerWorldEvent(0x04 /*EVENT_DAMAGE_CONFIRMED*/, shooterEntityId, e->entityId, damage, static_cast<float>(e->health));

                    if (e->health <= 0) {
                        ctx.despawnEntity(e->entityId, DespawnReason::KILLED);
                    }
                    ctx.despawnEntity(entityId, DespawnReason::EXPIRED);
                    return;
                }
            }
        }
    } else {
        // Fired by enemy -> check collision with players
        for (size_t i = 0; i < ctx.getPlayerCount(); ++i) {
            PlayerNetworkEntity* p = ctx.getPlayerByIndexMut(i);
            if (p && p->isAlive()) {
                if (position.distanceTo(p->position) <= (collisionRadius + 24.0f)) {
                    // Confirmed hit on player!
                    p->health -= damage;
                    if (p->health < 0) p->health = 0;
                    ctx.triggerWorldEvent(0x04 /*EVENT_DAMAGE_CONFIRMED*/, shooterEntityId, p->entityId, damage, static_cast<float>(p->health));
                    ctx.despawnEntity(entityId, DespawnReason::EXPIRED);
                    return;
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
// PickupNetworkEntity
// ----------------------------------------------------------------------------
void PickupNetworkEntity::tick(float /*dt*/, WorldSimulationContext& ctx) {
    if (!active || collected) return;

    for (size_t i = 0; i < ctx.getPlayerCount(); ++i) {
        PlayerNetworkEntity* p = ctx.getPlayerByIndexMut(i);
        if (p && p->isAlive()) {
            if (position.distance2D(p->position) <= collectRadius) {
                // Collect item for this player
                collected = true;
                PickupSubType st = static_cast<PickupSubType>(subType);

                switch (st) {
                    case PickupSubType::HEALTH_SMALL:
                        p->health = std::min(p->health + 10, p->maxHealth);
                        break;
                    case PickupSubType::HEALTH_BIG:
                        p->health = std::min(p->health + 25, p->maxHealth);
                        break;
                    case PickupSubType::AMMO_BULLETS:
                        p->ammo += 50;
                        break;
                    case PickupSubType::KEY_POWER:
                        ctx.addTeamKey(1u << 0);
                        break;
                    case PickupSubType::KEY_TIME:
                        ctx.addTeamKey(1u << 1);
                        break;
                    case PickupSubType::KEY_EARTH:
                        ctx.addTeamKey(1u << 2);
                        break;
                    case PickupSubType::KEY_SOUL:
                        ctx.addTeamKey(1u << 3);
                        break;
                    case PickupSubType::ANKH:
                        p->maxHealth += 25;
                        p->health = std::min(p->health + 25, p->maxHealth);
                        break;
                }

                ctx.triggerWorldEvent(0x03 /*EVENT_PICKUP_COLLECTED*/, p->entityId, entityId, static_cast<int32_t>(subType), static_cast<float>(amount));
                ctx.despawnEntity(entityId, DespawnReason::COLLECTED);
                return;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// DoorNetworkEntity
// ----------------------------------------------------------------------------
bool DoorNetworkEntity::requestOpen(uint16_t teamKeys, std::string& outReason) {
    if (requiredKeyMask != 0 && (teamKeys & requiredKeyMask) != requiredKeyMask) {
        outReason = "Locked: required key missing";
        return false;
    }

    if (state == DoorState::CLOSED || state == DoorState::CLOSING) {
        state = DoorState::OPENING;
        return true;
    }
    return true; // Already open or opening
}

void DoorNetworkEntity::forceOpen() {
    state = DoorState::OPENING;
}

void DoorNetworkEntity::tick(float dt, WorldSimulationContext& ctx) {
    if (!active) return;

    switch (state) {
        case DoorState::OPENING:
            openRatio += openSpeed * dt;
            if (openRatio >= 1.0f) {
                openRatio = 1.0f;
                state = DoorState::OPEN;
                autoCloseTimer = autoCloseDelay;
                ctx.triggerWorldEvent(0x01 /*EVENT_DOOR_STATE_CHANGE*/, entityId, 0, static_cast<int32_t>(state), openRatio);
            }
            break;
        case DoorState::OPEN:
            if (autoCloseDelay > 0.0f) {
                autoCloseTimer -= dt;
                if (autoCloseTimer <= 0.0f) {
                    state = DoorState::CLOSING;
                    ctx.triggerWorldEvent(0x01 /*EVENT_DOOR_STATE_CHANGE*/, entityId, 0, static_cast<int32_t>(state), openRatio);
                }
            }
            break;
        case DoorState::CLOSING:
            openRatio -= openSpeed * dt;
            if (openRatio <= 0.0f) {
                openRatio = 0.0f;
                state = DoorState::CLOSED;
                ctx.triggerWorldEvent(0x01 /*EVENT_DOOR_STATE_CHANGE*/, entityId, 0, static_cast<int32_t>(state), openRatio);
            }
            break;
        case DoorState::CLOSED:
            openRatio = 0.0f;
            break;
    }
}

// ----------------------------------------------------------------------------
// SwitchNetworkEntity
// ----------------------------------------------------------------------------
bool SwitchNetworkEntity::trigger(uint8_t playerSlot, WorldSimulationContext& ctx) {
    if (!toggleable && isActivated) return false;

    isActivated = !isActivated;
    ctx.triggerWorldEvent(0x02 /*EVENT_SWITCH_TRIGGERED*/, entityId, targetDoorEntityId, isActivated ? 1 : 0, static_cast<float>(playerSlot));

    if (targetDoorEntityId != 0) {
        NetworkEntity* target = ctx.getEntity(targetDoorEntityId);
        if (target && target->type == EntityType::DOOR) {
            auto* door = static_cast<DoorNetworkEntity*>(target);
            door->forceOpen();
        }
    }
    return true;
}

void SwitchNetworkEntity::tick(float /*dt*/, WorldSimulationContext& /*ctx*/) {
    // Switches are static interaction targets
}

} // namespace pse
