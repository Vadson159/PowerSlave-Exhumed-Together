#pragma once

#include "world_entity.h"
#include "replication_protocol.h"
#include "client_interpolator.h"
#include "engine_adapter.h"
#include "world_scale_config.h"
#include "enet_host.h"
#include "enet_client.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>
#include <mutex>

namespace pse {

// ============================================================================
// World Replication Host (Host-Authoritative World Management)
// ============================================================================
class WorldReplicationHost : public WorldSimulationContext, public IEngineReplicationAdapter {
public:
    explicit WorldReplicationHost(ENetHostServer& server);
    ~WorldReplicationHost() override;

    // Simulation context implementation
    size_t getPlayerCount() const override { return 8; }
    const PlayerNetworkEntity* getPlayerByIndex(size_t i) const override;
    PlayerNetworkEntity* getPlayerByIndexMut(size_t i) override;
    const PlayerNetworkEntity* getPlayerBySlot(uint8_t slotId) const override;
    PlayerNetworkEntity* getPlayerBySlotMut(uint8_t slotId) override;

    NetworkEntity* getEntity(uint32_t entityId) override;
    const NetworkEntity* getEntity(uint32_t entityId) const override;

    void spawnProjectile(ProjectileSubType subType, uint32_t shooterEntityId, uint8_t shooterSlot,
                         const Vec3& origin, const Vec3& velocity, int32_t damage) override;
    void despawnEntity(uint32_t entityId, DespawnReason reason) override;
    void triggerWorldEvent(uint16_t eventType, uint32_t sourceEntityId, uint32_t targetEntityId,
                           int32_t eventValue, float floatValue) override;

    uint16_t getTeamKeys() const override { return m_teamKeys; }
    void addTeamKey(uint16_t keyBit) override;
    uint32_t getMapEpoch() const override { return m_mapEpoch; }

    // IEngineReplicationAdapter implementation for Sol Max
    uint32_t registerActor(EntityType type, uint16_t subType, const Transform3D& initialTransform,
                           const ActorStateInfo& initialState, uint32_t* outGeneration = nullptr) override;
    bool updateActorTransform(uint32_t entityId, const Transform3D& transform, const Velocity3D& velocity, uint32_t generation) override;
    bool updateActorState(uint32_t entityId, const ActorStateInfo& state, uint32_t generation) override;
    void unregisterActor(uint32_t entityId, DespawnReason reason, uint32_t generation) override;
    void notifyDamage(uint32_t targetEntityId, uint32_t sourceEntityId, int32_t damageAmount, uint32_t targetGeneration) override;
    void notifyPickupCollected(uint32_t pickupEntityId, uint8_t playerSlot, uint32_t pickupGeneration) override;
    void notifyDoorStateChanged(uint32_t doorEntityId, DoorState newState, float openRatio, uint32_t doorGeneration) override;
    void notifySwitchToggled(uint32_t switchEntityId, uint8_t playerSlot, uint32_t switchGeneration) override;
    void onMapTransition(const std::string& newMapName, uint32_t newMapEpoch) override;
    void setVisualObserver(IClientVisualObserver* observer) override { m_visualObserver = observer; }
    void setStarterLoadout(const StarterLoadout& loadout) override;
    StarterLoadout getStarterLoadout() const override;

    // MTU Budget Configuration
    void setMaxPacketPayloadBytes(size_t bytes) { m_maxPacketPayloadBytes = bytes; }
    size_t getMaxPacketPayloadBytes() const { return m_maxPacketPayloadBytes; }

    // Replication telemetry
    uint64_t getTotalWorldSnapshotsBroadcast() const { return m_totalWorldSnapshotsBroadcast; }
    uint64_t getTotalWorldBatchesBroadcast() const { return m_totalWorldBatchesBroadcast; }
    uint64_t getTotalWorldEntitiesSerialized() const { return m_totalWorldEntitiesSerialized; }
    size_t   getLastSnapshotEntityCount() const { return m_lastSnapshotEntityCount; }
    void     resetReplicationMetrics() {
        m_totalWorldSnapshotsBroadcast = 0;
        m_totalWorldBatchesBroadcast = 0;
        m_totalWorldEntitiesSerialized = 0;
        m_lastSnapshotEntityCount = 0;
    }

    // Lifecycle and tick pumping
    void tick(float dt);
    void broadcastWorldSnapshot();
    void sendWorldBaseline(ENetPeer* peer);

    // Entity inspection
    size_t getActiveEntityCount() const;
    size_t getEntityCountByType(EntityType t) const;
    uint32_t getEntityGeneration(uint32_t entityId) const;

    // Helpers to spawn specific model objects
    uint32_t spawnEnemy(EnemySubType subType, const Vec3& pos);
    uint32_t spawnPickup(PickupSubType subType, const Vec3& pos, uint16_t amount = 25);
    uint32_t spawnDoor(const Vec3& pos, uint16_t requiredKeyMask = 0);
    uint32_t spawnSwitch(const Vec3& pos, uint32_t targetDoorId);

    // Packet reception handler
    bool handleClientPacket(ENetPeer* peer, uint8_t msgType, ByteReader& reader, uint32_t seq);

private:
    uint32_t allocateEntityId(EntityType type);

private:
    ENetHostServer& m_server;
    mutable std::recursive_mutex m_hostMutex;
    uint32_t m_mapEpoch = 1;
    std::string m_mapName = "KARNAK_SANCTUARY";
    uint32_t m_serverTick = 0;
    uint32_t m_snapshotSeq = 0;
    uint32_t m_eventSeq = 0;
    uint16_t m_teamKeys = 0;
    size_t   m_maxPacketPayloadBytes = 1200;
    StarterLoadout m_starterLoadout{};

    IClientVisualObserver* m_visualObserver = nullptr;

    // Generational tracking
    std::unordered_map<uint32_t, uint32_t> m_generations;

    // ID pool allocation markers
    uint32_t m_nextEnemyId = 2001;
    uint32_t m_nextProjectileId = 10001;
    uint32_t m_nextPickupId = 20001;
    uint32_t m_nextDoorId = 30001;
    uint32_t m_nextSwitchId = 31001;

    // Active entities
    std::array<std::shared_ptr<PlayerNetworkEntity>, 8> m_players;
    std::unordered_map<uint32_t, std::shared_ptr<NetworkEntity>> m_entities;

    // Replication telemetry
    uint64_t m_totalWorldSnapshotsBroadcast = 0;
    uint64_t m_totalWorldBatchesBroadcast = 0;
    uint64_t m_totalWorldEntitiesSerialized = 0;
    size_t   m_lastSnapshotEntityCount = 0;
};

// ============================================================================
// World Replication Client (Client Replicated World State)
// ============================================================================
class WorldReplicationClient {
public:
    explicit WorldReplicationClient(ENetClientNode& client);
    ~WorldReplicationClient();

    void setVisualObserver(IClientVisualObserver* observer) { m_visualObserver = observer; }
    IClientVisualObserver* getVisualObserver() const { return m_visualObserver; }

    // Packet reception handler
    bool handleServerPacket(uint8_t msgType, ByteReader& reader, uint32_t seq);

    // Visual render update tick (drives client interpolation)
    void renderTick(uint64_t nowMs);

    // Client interaction request with mandatory generational validation (targetGeneration > 0)
    void requestUse(uint32_t targetEntityId, uint32_t targetGeneration);

    // Status inspection
    size_t getReplicatedEntityCount() const;
    const NetworkEntity* getReplicatedEntity(uint32_t entityId) const;
    uint32_t getEntityGeneration(uint32_t entityId) const;
    uint32_t getCurrentMapEpoch() const { return m_mapEpoch; }
    uint16_t getTeamKeys() const { return m_teamKeys; }
    ClientEntityInterpolator& getInterpolator() { return m_interpolator; }

    // Safe 64-bit frame reassembly mask helper (bounds-checked: 1..64, UB-free)
    static inline uint64_t computeBatchMask(uint8_t totalBatches) {
        if (totalBatches == 0 || totalBatches > 64) return 0ULL;
        if (totalBatches == 64) return ~0ULL;
        return (1ULL << totalBatches) - 1ULL;
    }

    // Statistics for duplicate, frame reassembly, and stale packet rejection
    uint32_t getDuplicateSpawnCount() const { return m_duplicateSpawnCount; }
    uint32_t getStaleSnapshotCount() const { return m_staleSnapshotCount; }
    uint32_t getStaleEpochDropCount() const { return m_staleEpochDropCount; }
    uint32_t getCurrentFrameSeq() const { return m_currentFrameSeq; }
    uint64_t getCurrentFrameBatchMask() const { return m_currentFrameBatchMask; }
    uint32_t getCompletedFramesCount() const { return m_completedFramesCount; }
    uint32_t getPartialFramesCount() const { return m_partialFramesCount; }
    uint32_t getDuplicateBatchCount() const { return m_duplicateBatchCount; }
    uint32_t getStaleFrameDropCount() const { return m_staleFrameDropCount; }
    uint32_t getCompletedBaselinesCount() const { return m_completedBaselinesCount; }

    // Client throughput telemetry
    uint64_t getTotalSnapshotsReceived() const { return m_totalSnapshotsReceived; }
    uint64_t getTotalBatchesReceived() const { return m_totalBatchesReceived; }
    uint64_t getTotalEntitiesReceived() const { return m_totalEntitiesReceived; }
    void     resetReplicationMetrics() {
        m_totalSnapshotsReceived = 0;
        m_totalBatchesReceived = 0;
        m_totalEntitiesReceived = 0;
    }

private:
    void onMsgEntitySpawn(ByteReader& reader, uint32_t seq);
    void onMsgEntityDespawn(ByteReader& reader, uint32_t seq);
    void onMsgWorldSnapshot(ByteReader& reader, uint32_t seq);
    void onMsgWorldBaseline(ByteReader& reader, uint32_t seq);
    void onMsgWorldEventConfirm(ByteReader& reader, uint32_t seq);

private:
    ENetClientNode& m_client;
    mutable std::mutex m_clientMutex;
    uint32_t m_mapEpoch = 1;
    uint16_t m_teamKeys = 0;
    uint32_t m_highestSnapshotSeq = 0;

    IClientVisualObserver* m_visualObserver = nullptr;
    ClientEntityInterpolator m_interpolator;

    std::unordered_map<uint32_t, std::shared_ptr<NetworkEntity>> m_replicatedEntities;
    std::unordered_map<uint32_t, uint32_t> m_entityGenerations;

    // Multi-batch snapshot frame reassembly
    uint32_t m_currentFrameSeq = 0;
    uint8_t  m_currentFrameTotalBatches = 0;
    uint64_t m_currentFrameBatchMask = 0;
    uint32_t m_completedFramesCount = 0;
    uint32_t m_partialFramesCount = 0;
    uint32_t m_duplicateBatchCount = 0;
    uint32_t m_staleFrameDropCount = 0;

    // Multi-batch baseline reassembly
    uint32_t m_baselineEpoch = 0;
    uint8_t  m_baselineTotalBatches = 0;
    uint64_t m_baselineBatchMask = 0;
    uint32_t m_completedBaselinesCount = 0;

    // Resilience telemetry
    uint32_t m_duplicateSpawnCount = 0;
    uint32_t m_staleSnapshotCount = 0;
    uint32_t m_staleEpochDropCount = 0;

    // Throughput telemetry
    uint64_t m_totalSnapshotsReceived = 0;
    uint64_t m_totalBatchesReceived = 0;
    uint64_t m_totalEntitiesReceived = 0;
};

// Helper to populate synthetic benchmark scenario
void populateSyntheticWorld(WorldReplicationHost& host, const WorldScaleConfig& cfg);

} // namespace pse
