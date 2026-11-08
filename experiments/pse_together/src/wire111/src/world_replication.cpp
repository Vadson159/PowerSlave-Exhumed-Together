#include "world_replication.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX
#endif

namespace pse {

static inline uint64_t getSteadyTimeMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ============================================================================
// WorldReplicationHost Implementation
// ============================================================================
WorldReplicationHost::WorldReplicationHost(ENetHostServer& server)
    : m_server(server)
{
    // Initialize the 8 Player Network Entities (IDs 1001..1008)
    for (uint8_t i = 0; i < 8; ++i) {
        uint32_t pId = 1001 + i;
        m_generations[pId] = 1;
        auto player = std::make_shared<PlayerNetworkEntity>(pId, 1, i);
        m_players[i] = player;
        m_entities[pId] = player;
    }

    // Connect callbacks to server
    m_server.setCustomPacketHandler([this](ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t seq) {
        return handleClientPacket(peer, msgType, reader, seq);
    });

    m_server.onClientAuthenticatedCallback = [this](ENetPeer* peer, uint8_t slotId, uint32_t connectionGen, bool isReconnect) {
        (void)isReconnect;
        std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
        if (slotId < 8) {
            m_players[slotId]->active = true;
            m_players[slotId]->generation = connectionGen;
            m_generations[m_players[slotId]->entityId] = connectionGen;
        }
        // Send world baseline snapshot to peer
        sendWorldBaseline(peer);
    };

    m_server.onClientDisconnectedCallback = [this](uint8_t slotId) {
        std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
        if (slotId < 8) {
            m_players[slotId]->active = false;
        }
    };

    m_server.onTickCallback = [this](float dt) {
        tick(dt);
    };

    m_server.onSnapshotBroadcastCallback = [this]() {
        broadcastWorldSnapshot();
    };

    m_server.onMapChangeCallback = [this](const std::string& newMap, uint32_t epoch) {
        onMapTransition(newMap, epoch);
    };
}

WorldReplicationHost::~WorldReplicationHost() = default;

const PlayerNetworkEntity* WorldReplicationHost::getPlayerByIndex(size_t i) const {
    if (i < m_players.size()) return m_players[i].get();
    return nullptr;
}

PlayerNetworkEntity* WorldReplicationHost::getPlayerByIndexMut(size_t i) {
    if (i < m_players.size()) return m_players[i].get();
    return nullptr;
}

const PlayerNetworkEntity* WorldReplicationHost::getPlayerBySlot(uint8_t slotId) const {
    return getPlayerByIndex(slotId);
}

PlayerNetworkEntity* WorldReplicationHost::getPlayerBySlotMut(uint8_t slotId) {
    return getPlayerByIndexMut(slotId);
}

NetworkEntity* WorldReplicationHost::getEntity(uint32_t entityId) {
    auto it = m_entities.find(entityId);
    if (it != m_entities.end()) return it->second.get();
    return nullptr;
}

const NetworkEntity* WorldReplicationHost::getEntity(uint32_t entityId) const {
    auto it = m_entities.find(entityId);
    if (it != m_entities.end()) return it->second.get();
    return nullptr;
}

void WorldReplicationHost::addTeamKey(uint16_t keyBit) {
    m_teamKeys |= keyBit;
    for (auto& p : m_players) {
        if (p) p->teamKeyMask = m_teamKeys;
    }
}

uint32_t WorldReplicationHost::allocateEntityId(EntityType type) {
    uint32_t id = 0;
    switch (type) {
        case EntityType::ENEMY:
            id = m_nextEnemyId++;
            if (m_nextEnemyId > 2999) m_nextEnemyId = 2001;
            break;
        case EntityType::PROJECTILE:
            id = m_nextProjectileId++;
            if (m_nextProjectileId > 19999) m_nextProjectileId = 10001;
            break;
        case EntityType::PICKUP:
            id = m_nextPickupId++;
            if (m_nextPickupId > 29999) m_nextPickupId = 20001;
            break;
        case EntityType::DOOR:
            id = m_nextDoorId++;
            if (m_nextDoorId > 30999) m_nextDoorId = 30001;
            break;
        case EntityType::SWITCH:
            id = m_nextSwitchId++;
            if (m_nextSwitchId > 31999) m_nextSwitchId = 31001;
            break;
        default:
            id = 99999;
            break;
    }
    m_generations[id]++;
    return id;
}

uint32_t WorldReplicationHost::spawnEnemy(EnemySubType subType, const Vec3& pos) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t id = allocateEntityId(EntityType::ENEMY);
    uint32_t gen = m_generations[id];

    auto enemy = std::make_shared<EnemyNetworkEntity>(id, gen, subType, pos);
    enemy->mapEpoch = m_mapEpoch;
    m_entities[id] = enemy;

    // Broadcast reliable spawn packet
    MsgEntitySpawn spawn;
    spawn.mapEpoch = m_mapEpoch;
    spawn.entityId = id;
    spawn.entityGeneration = gen;
    spawn.type = static_cast<uint8_t>(EntityType::ENEMY);
    spawn.subType = static_cast<uint16_t>(subType);
    spawn.posX = pos.x; spawn.posY = pos.y; spawn.posZ = pos.z;
    spawn.health = enemy->health;
    spawn.stateFlags = enemy->stateFlags;

    auto encoded = encodePacket(MSG_ENTITY_SPAWN, ++m_eventSeq, spawn);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    return id;
}

void WorldReplicationHost::spawnProjectile(ProjectileSubType subType, uint32_t shooterEntityId, uint8_t shooterSlot,
                                          const Vec3& origin, const Vec3& velocity, int32_t damage) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t id = allocateEntityId(EntityType::PROJECTILE);
    uint32_t gen = m_generations[id];

    auto proj = std::make_shared<ProjectileNetworkEntity>(id, gen, subType, shooterEntityId, shooterSlot, origin, velocity, damage);
    proj->mapEpoch = m_mapEpoch;
    m_entities[id] = proj;

    MsgEntitySpawn spawn;
    spawn.mapEpoch = m_mapEpoch;
    spawn.entityId = id;
    spawn.entityGeneration = gen;
    spawn.type = static_cast<uint8_t>(EntityType::PROJECTILE);
    spawn.subType = static_cast<uint16_t>(subType);
    spawn.posX = origin.x; spawn.posY = origin.y; spawn.posZ = origin.z;
    spawn.velX = velocity.x; spawn.velY = velocity.y; spawn.velZ = velocity.z;
    spawn.health = proj->health;
    spawn.meta = static_cast<uint32_t>(damage);

    auto encoded = encodePacket(MSG_ENTITY_SPAWN, ++m_eventSeq, spawn);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
}

uint32_t WorldReplicationHost::spawnPickup(PickupSubType subType, const Vec3& pos, uint16_t amount) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t id = allocateEntityId(EntityType::PICKUP);
    uint32_t gen = m_generations[id];

    auto pickup = std::make_shared<PickupNetworkEntity>(id, gen, subType, pos, amount);
    pickup->mapEpoch = m_mapEpoch;
    m_entities[id] = pickup;

    MsgEntitySpawn spawn;
    spawn.mapEpoch = m_mapEpoch;
    spawn.entityId = id;
    spawn.entityGeneration = gen;
    spawn.type = static_cast<uint8_t>(EntityType::PICKUP);
    spawn.subType = static_cast<uint16_t>(subType);
    spawn.posX = pos.x; spawn.posY = pos.y; spawn.posZ = pos.z;
    spawn.health = 1;
    spawn.meta = amount;

    auto encoded = encodePacket(MSG_ENTITY_SPAWN, ++m_eventSeq, spawn);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    return id;
}

uint32_t WorldReplicationHost::spawnDoor(const Vec3& pos, uint16_t requiredKeyMask) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t id = allocateEntityId(EntityType::DOOR);
    uint32_t gen = m_generations[id];

    auto door = std::make_shared<DoorNetworkEntity>(id, gen, pos, requiredKeyMask);
    door->mapEpoch = m_mapEpoch;
    m_entities[id] = door;

    MsgEntitySpawn spawn;
    spawn.mapEpoch = m_mapEpoch;
    spawn.entityId = id;
    spawn.entityGeneration = gen;
    spawn.type = static_cast<uint8_t>(EntityType::DOOR);
    spawn.subType = requiredKeyMask;
    spawn.posX = pos.x; spawn.posY = pos.y; spawn.posZ = pos.z;
    spawn.health = 9999;
    spawn.meta = requiredKeyMask;

    auto encoded = encodePacket(MSG_ENTITY_SPAWN, ++m_eventSeq, spawn);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    return id;
}

uint32_t WorldReplicationHost::spawnSwitch(const Vec3& pos, uint32_t targetDoorId) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t id = allocateEntityId(EntityType::SWITCH);
    uint32_t gen = m_generations[id];

    auto sw = std::make_shared<SwitchNetworkEntity>(id, gen, pos, targetDoorId);
    sw->mapEpoch = m_mapEpoch;
    m_entities[id] = sw;

    MsgEntitySpawn spawn;
    spawn.mapEpoch = m_mapEpoch;
    spawn.entityId = id;
    spawn.entityGeneration = gen;
    spawn.type = static_cast<uint8_t>(EntityType::SWITCH);
    spawn.posX = pos.x; spawn.posY = pos.y; spawn.posZ = pos.z;
    spawn.health = 9999;
    spawn.meta = targetDoorId;

    auto encoded = encodePacket(MSG_ENTITY_SPAWN, ++m_eventSeq, spawn);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    return id;
}

void WorldReplicationHost::despawnEntity(uint32_t entityId, DespawnReason reason) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    auto it = m_entities.find(entityId);
    if (it == m_entities.end()) return;

    uint32_t gen = it->second->generation;
    it->second->active = false;
    m_entities.erase(it);

    MsgEntityDespawn despawn;
    despawn.mapEpoch = m_mapEpoch;
    despawn.entityId = entityId;
    despawn.entityGeneration = gen;
    despawn.reason = static_cast<uint8_t>(reason);

    auto encoded = encodePacket(MSG_ENTITY_DESPAWN, ++m_eventSeq, despawn);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
}

void WorldReplicationHost::triggerWorldEvent(uint16_t eventType, uint32_t sourceEntityId, uint32_t targetEntityId,
                                            int32_t eventValue, float floatValue) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t targetGen = 0;
    if (targetEntityId != 0) {
        auto it = m_generations.find(targetEntityId);
        if (it != m_generations.end()) targetGen = it->second;
    }

    MsgWorldEventConfirm evt;
    evt.mapEpoch = m_mapEpoch;
    evt.eventSequence = ++m_eventSeq;
    evt.eventType = eventType;
    evt.sourceEntityId = sourceEntityId;
    evt.targetEntityId = targetEntityId;
    evt.targetGeneration = targetGen;
    evt.eventValue = eventValue;
    evt.floatValue = floatValue;

    auto encoded = encodePacket(MSG_WORLD_EVENT_CONFIRM, m_eventSeq, evt);
    m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);

    if (m_visualObserver) {
        m_visualObserver->onWorldEvent(eventType, sourceEntityId, targetEntityId, eventValue, floatValue);
    }
}

void WorldReplicationHost::tick(float dt) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    m_serverTick++;

    // Synchronize host simulation players with network player entities
    auto& sim = m_server.getSimulation();
    for (uint8_t i = 0; i < 8; ++i) {
        if (sim.isSlotActive(i)) {
            const auto& slot = sim.getSlot(i);
            auto& p = m_players[i];
            p->active = true;
            p->position = Vec3(slot.posX, slot.posY, slot.posZ);
            p->velocity = Vec3(slot.velX, slot.velY, slot.velZ);
            p->rotation = Rot2(slot.yaw, slot.pitch);
            p->health = slot.health;
            p->ammo = slot.ammo;
            p->activeWeapon = slot.activeWeapon;
            p->weaponMask = slot.weaponMask;
            p->deathSequence = slot.deathCounter;
        } else {
            m_players[i]->active = false;
        }
    }

    // Tick active entities
    std::vector<std::shared_ptr<NetworkEntity>> activeList;
    activeList.reserve(m_entities.size());
    for (auto& [id, entity] : m_entities) {
        if (entity && entity->active) {
            activeList.push_back(entity);
        }
    }

    for (auto& entity : activeList) {
        if (entity->active) {
            entity->tick(dt, *this);
        }
    }
}

void WorldReplicationHost::broadcastWorldSnapshot() {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    std::vector<EntitySnapshot> snaps;
    snaps.reserve(m_entities.size());

    for (const auto& [id, entity] : m_entities) {
        if (!entity || !entity->active) continue;

        EntitySnapshot s;
        s.entityId = entity->entityId;
        s.generation = entity->generation;
        s.type = static_cast<uint8_t>(entity->type);
        s.subType = entity->subType;
        s.posX = entity->position.x;
        s.posY = entity->position.y;
        s.posZ = entity->position.z;
        s.velX = entity->velocity.x;
        s.velY = entity->velocity.y;
        s.velZ = entity->velocity.z;
        s.yaw = entity->rotation.yaw;
        s.pitch = entity->rotation.pitch;
        s.health = entity->health;
        s.stateFlags = entity->stateFlags;

        if (entity->type == EntityType::DOOR) {
            auto* d = static_cast<DoorNetworkEntity*>(entity.get());
            s.meta = static_cast<uint32_t>(d->openRatio * 1000.0f);
        } else if (entity->type == EntityType::SWITCH) {
            auto* sw = static_cast<SwitchNetworkEntity*>(entity.get());
            s.meta = sw->isActivated ? 1 : 0;
        } else {
            s.meta = 0;
        }
        snaps.push_back(s);
    }

    // Dynamic calculation of max entities per batch based on MTU budget (e.g. 1200 bytes)
    size_t batchSize = WorldScaleConfig::calculateMaxEntitiesPerBatch(m_maxPacketPayloadBytes);
    if (batchSize == 0) batchSize = 1;

    uint8_t totalBatches = static_cast<uint8_t>((snaps.size() + batchSize - 1) / batchSize);
    if (totalBatches == 0) totalBatches = 1;

    m_snapshotSeq++;

    m_totalWorldSnapshotsBroadcast++;
    m_totalWorldEntitiesSerialized += snaps.size();
    m_totalWorldBatchesBroadcast += totalBatches;
    m_lastSnapshotEntityCount = snaps.size();

    for (uint8_t b = 0; b < totalBatches; ++b) {
        MsgWorldSnapshot msg;
        msg.snapshotSequence = m_snapshotSeq;
        msg.serverTick = m_serverTick;
        msg.mapEpoch = m_mapEpoch;
        msg.batchIndex = b;
        msg.totalBatches = totalBatches;

        size_t startIdx = b * batchSize;
        size_t endIdx = (std::min)(startIdx + batchSize, snaps.size());
        for (size_t i = startIdx; i < endIdx; ++i) {
            msg.entities.push_back(snaps[i]);
        }

        auto encoded = encodePacket(MSG_WORLD_SNAPSHOT, m_snapshotSeq, msg);
        // Transmit across dedicated CHANNEL_WORLD_SNAPSHOT = 3 with unsequenced delivery
        m_server.broadcastPacket(CHANNEL_WORLD_SNAPSHOT, encoded, false);
    }
}

void WorldReplicationHost::sendWorldBaseline(ENetPeer* peer) {
    if (!peer) return;

    std::vector<MsgEntitySpawn> baselineEntities;
    baselineEntities.reserve(m_entities.size());

    for (const auto& [id, entity] : m_entities) {
        if (!entity || !entity->active) continue;

        MsgEntitySpawn s;
        s.mapEpoch = m_mapEpoch;
        s.entityId = entity->entityId;
        s.entityGeneration = entity->generation;
        s.type = static_cast<uint8_t>(entity->type);
        s.subType = entity->subType;
        s.posX = entity->position.x;
        s.posY = entity->position.y;
        s.posZ = entity->position.z;
        s.velX = entity->velocity.x;
        s.velY = entity->velocity.y;
        s.velZ = entity->velocity.z;
        s.yaw = entity->rotation.yaw;
        s.pitch = entity->rotation.pitch;
        s.health = entity->health;
        s.stateFlags = entity->stateFlags;

        if (entity->type == EntityType::DOOR) {
            auto* d = static_cast<DoorNetworkEntity*>(entity.get());
            s.meta = static_cast<uint32_t>(d->openRatio * 1000.0f);
        } else if (entity->type == EntityType::SWITCH) {
            auto* sw = static_cast<SwitchNetworkEntity*>(entity.get());
            s.meta = sw->isActivated ? 1 : 0;
        } else if (entity->type == EntityType::PICKUP) {
            auto* p = static_cast<PickupNetworkEntity*>(entity.get());
            s.meta = p->amount;
        }
        baselineEntities.push_back(s);
    }

    size_t batchSize = WorldScaleConfig::calculateMaxBaselineEntitiesPerBatch(m_maxPacketPayloadBytes);
    if (batchSize == 0) batchSize = 1;

    uint8_t totalBatches = static_cast<uint8_t>((baselineEntities.size() + batchSize - 1) / batchSize);
    if (totalBatches == 0) totalBatches = 1;

    for (uint8_t b = 0; b < totalBatches; ++b) {
        MsgWorldBaseline msg;
        msg.mapEpoch = m_mapEpoch;
        msg.serverTick = m_serverTick;
        msg.teamKeyMask = m_teamKeys;
        msg.mapName = m_mapName;
        msg.batchIndex = b;
        msg.totalBatches = totalBatches;

        size_t startIdx = b * batchSize;
        size_t endIdx = (std::min)(startIdx + batchSize, baselineEntities.size());
        for (size_t i = startIdx; i < endIdx; ++i) {
            msg.entities.push_back(baselineEntities[i]);
        }

        auto encoded = encodePacket(MSG_WORLD_BASELINE, ++m_eventSeq, msg);
        m_server.sendPacket(peer, CHANNEL_RELIABLE_CONTROL, encoded, true);
    }
}

void WorldReplicationHost::onMapTransition(const std::string& newMapName, uint32_t newMapEpoch) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    m_mapName = newMapName;
    m_mapEpoch = newMapEpoch;

    // Remove all transient in-flight projectiles immediately
    std::vector<uint32_t> toRemove;
    for (const auto& [id, entity] : m_entities) {
        if (entity && entity->type == EntityType::PROJECTILE) {
            toRemove.push_back(id);
        }
    }
    for (uint32_t id : toRemove) {
        despawnEntity(id, DespawnReason::MAP_TRANSITION);
    }
}

bool WorldReplicationHost::handleClientPacket(ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t /*seq*/) {
    if (msgType == MSG_INTERACTION_REQUEST) {
        MsgInteractionRequest req;
        if (!req.deserialize(reader)) return false;

        std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
        // Epoch verification gate
        if (req.mapEpoch != m_mapEpoch) {
            return false; // Stale epoch interaction rejected
        }

        int8_t slot = m_server.getSlotForPeer(peer);
        if (slot < 0 || slot >= 8) return false;

        PlayerNetworkEntity* player = m_players[slot].get();
        if (!player || !player->isAlive()) return false;

        NetworkEntity* target = getEntity(req.targetEntityId);
        if (!target || !target->active) return false;

        // Generational check: generation = 0 is strictly forbidden; must match current active generation
        if (req.targetGeneration == 0 || target->generation != req.targetGeneration) {
            return false; // generation=0 or stale generation targeting older/different generation of reused ID rejected
        }

        // Verify distance
        float dist = player->position.distance2D(target->position);

        if (target->type == EntityType::DOOR) {
            if (dist > 140.0f) return false; // Too far
            auto* door = static_cast<DoorNetworkEntity*>(target);
            std::string reason;
            if (door->requestOpen(m_teamKeys, reason)) {
                triggerWorldEvent(EVENT_DOOR_STATE_CHANGE, door->entityId, player->entityId, static_cast<int32_t>(door->state), door->openRatio);
            }
            return true;
        } else if (target->type == EntityType::SWITCH) {
            if (dist > 120.0f) return false; // Too far
            auto* sw = static_cast<SwitchNetworkEntity*>(target);
            sw->trigger(static_cast<uint8_t>(slot), *this);
            return true;
        }
        return true;
    }
    return false;
}

size_t WorldReplicationHost::getActiveEntityCount() const {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    size_t count = 0;
    for (const auto& [id, entity] : m_entities) {
        if (entity && entity->active) count++;
    }
    return count;
}

size_t WorldReplicationHost::getEntityCountByType(EntityType t) const {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    size_t count = 0;
    for (const auto& [id, entity] : m_entities) {
        if (entity && entity->active && entity->type == t) count++;
    }
    return count;
}

uint32_t WorldReplicationHost::getEntityGeneration(uint32_t entityId) const {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    auto it = m_generations.find(entityId);
    if (it != m_generations.end()) return it->second;
    return 0;
}

// ----------------------------------------------------------------------------
// IEngineReplicationAdapter Implementation for Sol Max
// ----------------------------------------------------------------------------
uint32_t WorldReplicationHost::registerActor(EntityType type, uint16_t subType, const Transform3D& initialTransform,
                                            const ActorStateInfo& initialState, uint32_t* outGeneration) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    uint32_t id = allocateEntityId(type);
    uint32_t gen = m_generations[id];
    if (outGeneration) {
        *outGeneration = gen;
    }

    std::shared_ptr<NetworkEntity> entity;
    Vec3 pos(initialTransform.x, initialTransform.y, initialTransform.z);

    if (type == EntityType::ENEMY) {
        entity = std::make_shared<EnemyNetworkEntity>(id, gen, static_cast<EnemySubType>(subType), pos);
    } else if (type == EntityType::PICKUP) {
        entity = std::make_shared<PickupNetworkEntity>(id, gen, static_cast<PickupSubType>(subType), pos, static_cast<uint16_t>(initialState.meta));
    } else if (type == EntityType::DOOR) {
        entity = std::make_shared<DoorNetworkEntity>(id, gen, pos, subType);
    } else if (type == EntityType::SWITCH) {
        entity = std::make_shared<SwitchNetworkEntity>(id, gen, pos, initialState.meta);
    }

    if (entity) {
        entity->mapEpoch = m_mapEpoch;
        entity->rotation = Rot2(initialTransform.yaw, initialTransform.pitch);
        entity->health = initialState.health;
        entity->maxHealth = initialState.maxHealth;
        entity->stateFlags = initialState.stateFlags;
        m_entities[id] = entity;

        MsgEntitySpawn spawn;
        spawn.mapEpoch = m_mapEpoch;
        spawn.entityId = id;
        spawn.entityGeneration = gen;
        spawn.type = static_cast<uint8_t>(type);
        spawn.subType = subType;
        spawn.posX = pos.x; spawn.posY = pos.y; spawn.posZ = pos.z;
        spawn.yaw = initialTransform.yaw; spawn.pitch = initialTransform.pitch;
        spawn.health = initialState.health;
        spawn.stateFlags = initialState.stateFlags;
        spawn.meta = initialState.meta;

        auto encoded = encodePacket(MSG_ENTITY_SPAWN, ++m_eventSeq, spawn);
        m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
    }
    return id;
}

bool WorldReplicationHost::updateActorTransform(uint32_t entityId, const Transform3D& transform, const Velocity3D& velocity, uint32_t generation) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (generation == 0) return false; // Generation 0 is strictly forbidden for real network entities
    auto it = m_entities.find(entityId);
    if (it == m_entities.end() || !it->second->active) return false;
    if (it->second->generation != generation) {
        return false; // Generational mismatch (stale update rejected)
    }

    it->second->position = Vec3(transform.x, transform.y, transform.z);
    it->second->velocity = Vec3(velocity.vx, velocity.vy, velocity.vz);
    it->second->rotation = Rot2(transform.yaw, transform.pitch);
    return true;
}

bool WorldReplicationHost::updateActorState(uint32_t entityId, const ActorStateInfo& state, uint32_t generation) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (generation == 0) return false; // Generation 0 is strictly forbidden for real network entities
    auto it = m_entities.find(entityId);
    if (it == m_entities.end() || !it->second->active) return false;
    if (it->second->generation != generation) {
        return false; // Generational mismatch (stale update rejected)
    }

    it->second->health = state.health;
    it->second->maxHealth = state.maxHealth;
    it->second->stateFlags = state.stateFlags;
    return true;
}

void WorldReplicationHost::unregisterActor(uint32_t entityId, DespawnReason reason, uint32_t generation) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (generation == 0) return; // Generation 0 is strictly forbidden for real network entities
    auto it = m_entities.find(entityId);
    if (it == m_entities.end()) return;
    if (it->second->generation != generation) {
        return; // Generational mismatch (stale despawn rejected)
    }
    despawnEntity(entityId, reason);
}

void WorldReplicationHost::notifyDamage(uint32_t targetEntityId, uint32_t sourceEntityId, int32_t damageAmount, uint32_t targetGeneration) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (targetGeneration == 0) return; // Generation 0 is strictly forbidden for real network entities
    NetworkEntity* target = getEntity(targetEntityId);
    if (target && target->active) {
        if (target->generation != targetGeneration) {
            return; // Generational mismatch (stale damage rejected)
        }
        target->health -= damageAmount;
        if (target->health < 0) target->health = 0;

        MsgWorldEventConfirm evt;
        evt.mapEpoch = m_mapEpoch;
        evt.eventSequence = ++m_eventSeq;
        evt.eventType = EVENT_DAMAGE_CONFIRMED;
        evt.sourceEntityId = sourceEntityId;
        evt.targetEntityId = targetEntityId;
        evt.targetGeneration = target->generation;
        evt.eventValue = damageAmount;
        evt.floatValue = static_cast<float>(target->health);

        auto encoded = encodePacket(MSG_WORLD_EVENT_CONFIRM, m_eventSeq, evt);
        m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);

        if (m_visualObserver) {
            m_visualObserver->onWorldEvent(EVENT_DAMAGE_CONFIRMED, sourceEntityId, targetEntityId, damageAmount, static_cast<float>(target->health));
        }

        if (target->health <= 0 && target->type != EntityType::PLAYER) {
            despawnEntity(targetEntityId, DespawnReason::KILLED);
        }
    }
}

void WorldReplicationHost::notifyPickupCollected(uint32_t pickupEntityId, uint8_t playerSlot, uint32_t pickupGeneration) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (pickupGeneration == 0) return; // Generation 0 is strictly forbidden for real network entities
    auto it = m_entities.find(pickupEntityId);
    if (it != m_entities.end() && it->second->active) {
        if (it->second->generation != pickupGeneration) {
            return; // Generational mismatch
        }
        uint32_t gen = it->second->generation;
        despawnEntity(pickupEntityId, DespawnReason::COLLECTED);

        MsgWorldEventConfirm evt;
        evt.mapEpoch = m_mapEpoch;
        evt.eventSequence = ++m_eventSeq;
        evt.eventType = EVENT_PICKUP_COLLECTED;
        evt.sourceEntityId = 1001 + playerSlot;
        evt.targetEntityId = pickupEntityId;
        evt.targetGeneration = gen;
        evt.eventValue = 0;
        evt.floatValue = 0.0f;

        auto encoded = encodePacket(MSG_WORLD_EVENT_CONFIRM, m_eventSeq, evt);
        m_server.broadcastPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);

        if (m_visualObserver) {
            m_visualObserver->onWorldEvent(EVENT_PICKUP_COLLECTED, 1001 + playerSlot, pickupEntityId, 0, 0.0f);
        }
    }
}

void WorldReplicationHost::notifyDoorStateChanged(uint32_t doorEntityId, DoorState newState, float openRatio, uint32_t doorGeneration) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (doorGeneration == 0) return; // Generation 0 is strictly forbidden for real network entities
    NetworkEntity* target = getEntity(doorEntityId);
    if (target && target->active) {
        if (target->generation != doorGeneration) return;
        triggerWorldEvent(EVENT_DOOR_STATE_CHANGE, doorEntityId, 0, static_cast<int32_t>(newState), openRatio);
    }
}

void WorldReplicationHost::notifySwitchToggled(uint32_t switchEntityId, uint8_t playerSlot, uint32_t switchGeneration) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    if (switchGeneration == 0) return; // Generation 0 is strictly forbidden for real network entities
    NetworkEntity* target = getEntity(switchEntityId);
    if (target && target->active) {
        if (target->generation != switchGeneration) return;
        triggerWorldEvent(EVENT_SWITCH_TRIGGERED, switchEntityId, 0, 1, static_cast<float>(playerSlot));
    }
}

void WorldReplicationHost::setStarterLoadout(const StarterLoadout& loadout) {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    m_starterLoadout = loadout;
    m_server.getSimulation().setStarterLoadout(loadout);
    for (auto& p : m_players) {
        if (p) {
            p->health = loadout.health;
            p->maxHealth = loadout.maxHealth;
            p->armor = loadout.armor;
            p->weaponMask = loadout.weaponMask;
            p->activeWeapon = loadout.activeWeapon;
            p->ammo = loadout.ammo;
        }
    }
}

StarterLoadout WorldReplicationHost::getStarterLoadout() const {
    std::lock_guard<std::recursive_mutex> lock(m_hostMutex);
    return m_starterLoadout;
}

// ============================================================================
// WorldReplicationClient Implementation
// ============================================================================
WorldReplicationClient::WorldReplicationClient(ENetClientNode& client)
    : m_client(client)
{
    m_client.setCustomPacketHandler([this](uint8_t msgType, ByteReader& reader, uint32_t seq) {
        return handleServerPacket(msgType, reader, seq);
    });

    m_client.onMapChangeCallback = [this](const MsgMapChange& c) {
        std::lock_guard<std::mutex> lock(m_clientMutex);
        m_mapEpoch = c.newMapEpoch;
        m_highestSnapshotSeq = 0;
        m_currentFrameSeq = 0;
        m_currentFrameBatchMask = 0;
        m_currentFrameTotalBatches = 0;
        m_baselineEpoch = 0;
        m_baselineBatchMask = 0;
        m_baselineTotalBatches = 0;

        // Clean up transient projectiles on map transition
        std::vector<uint32_t> toRemove;
        for (const auto& [id, entity] : m_replicatedEntities) {
            if (entity && entity->type == EntityType::PROJECTILE) {
                toRemove.push_back(id);
            }
        }
        for (uint32_t id : toRemove) {
            m_replicatedEntities.erase(id);
            m_interpolator.onEntityDespawned(id);
        }
    };
}

WorldReplicationClient::~WorldReplicationClient() = default;

bool WorldReplicationClient::handleServerPacket(uint8_t msgType, ByteReader& reader, uint32_t seq) {
    switch (msgType) {
        case MSG_ENTITY_SPAWN:
            onMsgEntitySpawn(reader, seq);
            return true;
        case MSG_ENTITY_DESPAWN:
            onMsgEntityDespawn(reader, seq);
            return true;
        case MSG_WORLD_SNAPSHOT:
            onMsgWorldSnapshot(reader, seq);
            return true;
        case MSG_WORLD_BASELINE:
            onMsgWorldBaseline(reader, seq);
            return true;
        case MSG_WORLD_EVENT_CONFIRM:
            onMsgWorldEventConfirm(reader, seq);
            return true;
        default:
            return false;
    }
}

void WorldReplicationClient::onMsgEntitySpawn(ByteReader& reader, uint32_t /*seq*/) {
    MsgEntitySpawn spawn;
    if (!spawn.deserialize(reader)) return;

    std::lock_guard<std::mutex> lock(m_clientMutex);
    // 1. Epoch mismatch protection
    if (spawn.mapEpoch != m_mapEpoch) {
        m_staleEpochDropCount++;
        return;
    }

    // Generation 0 is forbidden for real network entities
    if (spawn.entityGeneration == 0) {
        return;
    }

    // 2. Idempotency & Duplicate Spawn Protection & Generational Re-use
    auto it = m_replicatedEntities.find(spawn.entityId);
    if (it != m_replicatedEntities.end()) {
        if (spawn.entityGeneration == it->second->generation) {
            // Duplicate message -> safely ignore without re-allocating
            m_duplicateSpawnCount++;
            return;
        }
        if (spawn.entityGeneration < it->second->generation) {
            // Stale message for older generation -> ignore
            return;
        }
        // Newer generation reusing the same entityId: retire the old entity first
        if (m_visualObserver) {
            m_visualObserver->onEntityVisualDespawn(spawn.entityId, it->second->generation, DespawnReason::REPLACED);
        }
        m_interpolator.onEntityDespawned(spawn.entityId);
        m_replicatedEntities.erase(it);
    }

    // Instantiate replicated entity
    std::shared_ptr<NetworkEntity> entity;
    Vec3 pos(spawn.posX, spawn.posY, spawn.posZ);
    EntityType type = static_cast<EntityType>(spawn.type);

    if (type == EntityType::PLAYER) {
        entity = std::make_shared<PlayerNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<uint8_t>(spawn.subType));
    } else if (type == EntityType::ENEMY) {
        entity = std::make_shared<EnemyNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<EnemySubType>(spawn.subType), pos);
    } else if (type == EntityType::PROJECTILE) {
        Vec3 vel(spawn.velX, spawn.velY, spawn.velZ);
        entity = std::make_shared<ProjectileNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<ProjectileSubType>(spawn.subType),
                                                          0u, static_cast<uint8_t>(0xFF), pos, vel, static_cast<int32_t>(spawn.meta));
    } else if (type == EntityType::PICKUP) {
        entity = std::make_shared<PickupNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<PickupSubType>(spawn.subType), pos, static_cast<uint16_t>(spawn.meta));
    } else if (type == EntityType::DOOR) {
        entity = std::make_shared<DoorNetworkEntity>(spawn.entityId, spawn.entityGeneration, pos, spawn.subType);
    } else if (type == EntityType::SWITCH) {
        entity = std::make_shared<SwitchNetworkEntity>(spawn.entityId, spawn.entityGeneration, pos, spawn.meta);
    }

    if (entity) {
        entity->mapEpoch = spawn.mapEpoch;
        entity->rotation = Rot2(spawn.yaw, spawn.pitch);
        entity->health = spawn.health;
        entity->stateFlags = spawn.stateFlags;
        m_replicatedEntities[spawn.entityId] = entity;
        m_entityGenerations[spawn.entityId] = spawn.entityGeneration;

        uint64_t now = getSteadyTimeMs();
        m_interpolator.onEntitySpawned(spawn.entityId, spawn.entityGeneration, type, spawn.subType,
                                       pos, entity->rotation, spawn.health, now, 0);

        if (m_visualObserver) {
            Transform3D t(pos.x, pos.y, pos.z, spawn.yaw, spawn.pitch);
            ActorStateInfo st(spawn.health, 100, spawn.stateFlags, spawn.meta);
            m_visualObserver->onEntityVisualSpawn(spawn.entityId, spawn.entityGeneration, type, spawn.subType, t, st);
        }
    }
}

void WorldReplicationClient::onMsgEntityDespawn(ByteReader& reader, uint32_t /*seq*/) {
    MsgEntityDespawn despawn;
    if (!despawn.deserialize(reader)) return;

    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (despawn.mapEpoch != m_mapEpoch) {
        m_staleEpochDropCount++;
        return;
    }

    auto it = m_replicatedEntities.find(despawn.entityId);
    if (it != m_replicatedEntities.end()) {
        if (despawn.entityGeneration == 0 || despawn.entityGeneration != it->second->generation) {
            return; // Stale or invalid generation despawn ignored
        }
        DespawnReason reason = static_cast<DespawnReason>(despawn.reason);
        m_interpolator.onEntityDespawned(despawn.entityId);
        if (m_visualObserver) {
            m_visualObserver->onEntityVisualDespawn(despawn.entityId, despawn.entityGeneration, reason);
        }
        m_replicatedEntities.erase(it);
    }
}

void WorldReplicationClient::onMsgWorldSnapshot(ByteReader& reader, uint32_t /*seq*/) {
    MsgWorldSnapshot snap;
    if (!snap.deserialize(reader)) return;

    std::lock_guard<std::mutex> lock(m_clientMutex);
    // 1. Epoch gate
    if (snap.mapEpoch != m_mapEpoch) {
        m_staleEpochDropCount++;
        return;
    }

    // 2. Validate batch metadata boundaries (1..64 batches supported by 64-bit mask)
    if (snap.totalBatches == 0 || snap.totalBatches > 64 || snap.batchIndex >= snap.totalBatches) {
        return; // Malformed batch dropped
    }

    // 3. Multi-batch frame reassembly logic with RFC 1982 sequence distance (UINT32_MAX rollover safe)
    int32_t seqDiff = (m_currentFrameSeq == 0) ? 1 : static_cast<int32_t>(snap.snapshotSequence - m_currentFrameSeq);

    if (seqDiff < 0) {
        m_staleSnapshotCount++;
        m_staleFrameDropCount++;
        return; // Older snapshot frame dropped
    }

    if (seqDiff > 0) {
        // A newer frame has arrived: check if previous frame was incomplete
        if (m_currentFrameSeq > 0 && m_currentFrameTotalBatches > 0) {
            uint64_t neededMask = computeBatchMask(m_currentFrameTotalBatches);
            if ((m_currentFrameBatchMask & neededMask) != neededMask) {
                m_partialFramesCount++;
            }
        }
        m_currentFrameSeq = snap.snapshotSequence;
        m_currentFrameTotalBatches = snap.totalBatches;
        m_currentFrameBatchMask = 0;
    } else {
        // seqDiff == 0 (same frame): check totalBatches consistency
        if (snap.totalBatches != m_currentFrameTotalBatches) {
            return; // Mid-frame inconsistent totalBatches dropped
        }
    }

    // Check duplicate batch within the current frame
    uint64_t batchBit = (1ULL << snap.batchIndex);
    if ((m_currentFrameBatchMask & batchBit) != 0) {
        m_duplicateBatchCount++;
        return; // Duplicate batch within the same frame ignored
    }
    m_currentFrameBatchMask |= batchBit;

    m_totalBatchesReceived++;
    m_totalEntitiesReceived += snap.entities.size();

    uint64_t now = getSteadyTimeMs();

    for (const auto& s : snap.entities) {
        auto it = m_replicatedEntities.find(s.entityId);
        if (it != m_replicatedEntities.end()) {
            if (s.generation == 0 || s.generation != it->second->generation) continue;

            it->second->position = Vec3(s.posX, s.posY, s.posZ);
            it->second->velocity = Vec3(s.velX, s.velY, s.velZ);
            it->second->rotation = Rot2(s.yaw, s.pitch);
            it->second->health = s.health;
            it->second->stateFlags = s.stateFlags;

            if (it->second->type == EntityType::DOOR) {
                auto* door = static_cast<DoorNetworkEntity*>(it->second.get());
                door->openRatio = static_cast<float>(s.meta) / 1000.0f;
                if (m_visualObserver) {
                    m_visualObserver->onDoorVisualState(s.entityId, door->state, door->openRatio);
                }
            }
        }
        m_interpolator.onSnapshotReceived(s, now, snap.serverTick);
    }

    // Check if frame is fully assembled
    uint64_t expectedMask = computeBatchMask(m_currentFrameTotalBatches);
    if ((m_currentFrameBatchMask & expectedMask) == expectedMask) {
        m_completedFramesCount++;
        m_highestSnapshotSeq = m_currentFrameSeq;
        m_totalSnapshotsReceived++;
    }
}

void WorldReplicationClient::onMsgWorldBaseline(ByteReader& reader, uint32_t /*seq*/) {
    MsgWorldBaseline base;
    if (!base.deserialize(reader)) return;

    std::lock_guard<std::mutex> lock(m_clientMutex);

    // Validate baseline batch metadata boundaries
    if (base.totalBatches == 0 || base.totalBatches > 64 || base.batchIndex >= base.totalBatches) {
        return;
    }

    // Initialize or reset if new epoch or first baseline
    if (base.mapEpoch != m_baselineEpoch || m_baselineEpoch == 0) {
        m_baselineEpoch = base.mapEpoch;
        m_mapEpoch = base.mapEpoch;
        m_teamKeys = static_cast<uint16_t>(base.teamKeyMask);
        m_baselineTotalBatches = base.totalBatches;
        m_baselineBatchMask = 0;
        m_replicatedEntities.clear();
        m_interpolator.clear();
        m_highestSnapshotSeq = 0;
        m_currentFrameSeq = 0;
        m_currentFrameBatchMask = 0;
        m_currentFrameTotalBatches = 0;
    } else {
        if (base.totalBatches != m_baselineTotalBatches) {
            return; // Inconsistent baseline batch count dropped
        }
    }

    uint64_t batchBit = (1ULL << base.batchIndex);
    if ((m_baselineBatchMask & batchBit) != 0) {
        return; // Duplicate baseline batch ignored
    }
    m_baselineBatchMask |= batchBit;

    uint64_t now = getSteadyTimeMs();

    for (const auto& spawn : base.entities) {
        std::shared_ptr<NetworkEntity> entity;
        Vec3 pos(spawn.posX, spawn.posY, spawn.posZ);
        EntityType type = static_cast<EntityType>(spawn.type);

        if (type == EntityType::PLAYER) {
            entity = std::make_shared<PlayerNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<uint8_t>(spawn.subType));
        } else if (type == EntityType::ENEMY) {
            entity = std::make_shared<EnemyNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<EnemySubType>(spawn.subType), pos);
        } else if (type == EntityType::PROJECTILE) {
            Vec3 vel(spawn.velX, spawn.velY, spawn.velZ);
            entity = std::make_shared<ProjectileNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<ProjectileSubType>(spawn.subType),
                                                              0u, static_cast<uint8_t>(0xFF), pos, vel, static_cast<int32_t>(spawn.meta));
        } else if (type == EntityType::PICKUP) {
            entity = std::make_shared<PickupNetworkEntity>(spawn.entityId, spawn.entityGeneration, static_cast<PickupSubType>(spawn.subType), pos, static_cast<uint16_t>(spawn.meta));
        } else if (type == EntityType::DOOR) {
            auto door = std::make_shared<DoorNetworkEntity>(spawn.entityId, spawn.entityGeneration, pos, spawn.subType);
            door->openRatio = static_cast<float>(spawn.meta) / 1000.0f;
            if (door->openRatio >= 1.0f) door->state = DoorState::OPEN;
            else if (door->openRatio > 0.0f) door->state = DoorState::OPENING;
            entity = door;
        } else if (type == EntityType::SWITCH) {
            auto sw = std::make_shared<SwitchNetworkEntity>(spawn.entityId, spawn.entityGeneration, pos, 0);
            sw->isActivated = (spawn.meta != 0);
            entity = sw;
        }

        if (entity) {
            entity->mapEpoch = spawn.mapEpoch;
            entity->rotation = Rot2(spawn.yaw, spawn.pitch);
            entity->health = spawn.health;
            entity->stateFlags = spawn.stateFlags;
            m_replicatedEntities[spawn.entityId] = entity;
            m_entityGenerations[spawn.entityId] = spawn.entityGeneration;

            m_interpolator.onEntitySpawned(spawn.entityId, spawn.entityGeneration, type, spawn.subType,
                                           pos, entity->rotation, spawn.health, now, base.serverTick);

            if (m_visualObserver) {
                Transform3D t(pos.x, pos.y, pos.z, spawn.yaw, spawn.pitch);
                ActorStateInfo st(spawn.health, 100, spawn.stateFlags, spawn.meta);
                m_visualObserver->onEntityVisualSpawn(spawn.entityId, spawn.entityGeneration, type, spawn.subType, t, st);
            }
        }
    }

    uint64_t targetMask = computeBatchMask(m_baselineTotalBatches);
    if ((m_baselineBatchMask & targetMask) == targetMask) {
        m_completedBaselinesCount++;
    }
}

void WorldReplicationClient::onMsgWorldEventConfirm(ByteReader& reader, uint32_t /*seq*/) {
    MsgWorldEventConfirm evt;
    if (!evt.deserialize(reader)) return;

    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (evt.mapEpoch != m_mapEpoch) {
        m_staleEpochDropCount++;
        return;
    }

    if (evt.eventType == EVENT_DOOR_STATE_CHANGE) {
        auto it = m_replicatedEntities.find(evt.sourceEntityId);
        if (it != m_replicatedEntities.end() && it->second->type == EntityType::DOOR) {
            auto* door = static_cast<DoorNetworkEntity*>(it->second.get());
            door->state = static_cast<DoorState>(evt.eventValue);
            door->openRatio = evt.floatValue;
            if (m_visualObserver) {
                m_visualObserver->onDoorVisualState(door->entityId, door->state, door->openRatio);
            }
        }
    } else if (evt.eventType == EVENT_SWITCH_TRIGGERED) {
        auto it = m_replicatedEntities.find(evt.sourceEntityId);
        if (it != m_replicatedEntities.end() && it->second->type == EntityType::SWITCH) {
            auto* sw = static_cast<SwitchNetworkEntity*>(it->second.get());
            sw->isActivated = (evt.eventValue != 0);
        }
    } else if (evt.eventType == EVENT_PICKUP_COLLECTED) {
        auto it = m_replicatedEntities.find(evt.targetEntityId);
        if (it != m_replicatedEntities.end()) {
            // Strict generational gate: generation 0 is invalid; must match target generation
            if (evt.targetGeneration != 0 && it->second->generation == evt.targetGeneration) {
                m_interpolator.onEntityDespawned(evt.targetEntityId);
                m_replicatedEntities.erase(it);
            }
        }
    } else if (evt.eventType == EVENT_DAMAGE_CONFIRMED) {
        auto it = m_replicatedEntities.find(evt.targetEntityId);
        if (it != m_replicatedEntities.end()) {
            // Strict generational gate: generation 0 is invalid; must match target generation
            if (evt.targetGeneration != 0 && it->second->generation == evt.targetGeneration) {
                it->second->health = static_cast<int32_t>(evt.floatValue);
            }
        }
    }

    if (m_visualObserver) {
        m_visualObserver->onWorldEvent(evt.eventType, evt.sourceEntityId, evt.targetEntityId, evt.eventValue, evt.floatValue);
    }
}

void WorldReplicationClient::renderTick(uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    if (!m_visualObserver) return;

    for (const auto& [id, entity] : m_replicatedEntities) {
        InterpolatedState state;
        if (m_interpolator.getInterpolatedState(id, nowMs, state)) {
            Transform3D t(state.position.x, state.position.y, state.position.z,
                          state.rotation.yaw, state.rotation.pitch);
            Velocity3D v(state.velocity.x, state.velocity.y, state.velocity.z);
            ActorStateInfo info(state.health, 100, state.stateFlags, state.meta);
            m_visualObserver->onEntityVisualUpdate(id, state.generation, t, v, info);
        }
    }
}

void WorldReplicationClient::requestUse(uint32_t targetEntityId, uint32_t targetGeneration) {
    if (!m_client.isAuthenticated()) return;
    if (targetEntityId == 0 || targetGeneration == 0) return; // Generation 0 is strictly forbidden

    MsgInteractionRequest req;
    req.mapEpoch = m_mapEpoch;
    req.slotId = m_client.getAssignedSlot();
    req.targetEntityId = targetEntityId;
    req.targetGeneration = targetGeneration;
    req.interactionType = 0; // USE

    auto encoded = encodePacket(MSG_INTERACTION_REQUEST, 0, req);
    m_client.sendPacket(CHANNEL_RELIABLE_CONTROL, encoded, true);
}

size_t WorldReplicationClient::getReplicatedEntityCount() const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    return m_replicatedEntities.size();
}

const NetworkEntity* WorldReplicationClient::getReplicatedEntity(uint32_t entityId) const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    auto it = m_replicatedEntities.find(entityId);
    if (it != m_replicatedEntities.end()) return it->second.get();
    return nullptr;
}

uint32_t WorldReplicationClient::getEntityGeneration(uint32_t entityId) const {
    std::lock_guard<std::mutex> lock(m_clientMutex);
    auto it = m_entityGenerations.find(entityId);
    if (it != m_entityGenerations.end()) return it->second;
    return 0;
}

// ----------------------------------------------------------------------------
// Populate Synthetic World Helper
// ----------------------------------------------------------------------------
void populateSyntheticWorld(WorldReplicationHost& host, const WorldScaleConfig& cfg) {
    // 1. Enemies
    for (int i = 0; i < cfg.numEnemies; ++i) {
        EnemySubType st = static_cast<EnemySubType>((i % 4) + 1);
        float angle = (2.0f * 3.14159f * i) / static_cast<float>(cfg.numEnemies);
        float dist = 250.0f + (i * 20.0f);
        Vec3 pos(std::cos(angle) * dist, std::sin(angle) * dist, 0.0f);
        host.spawnEnemy(st, pos);
    }

    // 2. Projectiles (in-flight)
    for (int i = 0; i < cfg.numProjectiles; ++i) {
        ProjectileSubType pt = static_cast<ProjectileSubType>((i % 3) + 1);
        float angle = (2.0f * 3.14159f * i) / static_cast<float>(cfg.numProjectiles);
        Vec3 origin(std::cos(angle) * 100.0f, std::sin(angle) * 100.0f, 32.0f);
        Vec3 vel(std::cos(angle) * 350.0f, std::sin(angle) * 350.0f, 0.0f);
        host.spawnProjectile(pt, 1001, 0, origin, vel, 25);
    }

    // 3. Pickups
    for (int i = 0; i < cfg.numPickups; ++i) {
        PickupSubType st;
        if (i < 10) st = PickupSubType::HEALTH_SMALL;
        else if (i < 15) st = PickupSubType::HEALTH_BIG;
        else if (i < 25) st = PickupSubType::AMMO_BULLETS;
        else if (i == 25) st = PickupSubType::KEY_POWER;
        else if (i == 26) st = PickupSubType::KEY_TIME;
        else if (i == 27) st = PickupSubType::KEY_EARTH;
        else if (i == 28) st = PickupSubType::KEY_SOUL;
        else st = PickupSubType::ANKH;

        float angle = (2.0f * 3.14159f * i) / static_cast<float>(cfg.numPickups);
        float dist = 180.0f + (i * 15.0f);
        Vec3 pos(std::cos(angle) * dist, std::sin(angle) * dist, 0.0f);
        host.spawnPickup(st, pos, (st == PickupSubType::AMMO_BULLETS ? 50 : 25));
    }

    // 4. Interactive Doors & Switches (numInteractive / 2 pairs)
    int pairs = cfg.numInteractive / 2;
    for (int i = 0; i < pairs; ++i) {
        float angle = (2.0f * 3.14159f * i) / static_cast<float>(pairs);
        Vec3 doorPos(std::cos(angle) * 500.0f, std::sin(angle) * 500.0f, 0.0f);
        uint16_t key = (i == 0) ? (1u << 0) : 0; // First door requires red key
        uint32_t doorId = host.spawnDoor(doorPos, key);

        Vec3 swPos(std::cos(angle) * 440.0f, std::sin(angle) * 440.0f, 0.0f);
        host.spawnSwitch(swPos, doorId);
    }
}

} // namespace pse
