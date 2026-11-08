#pragma once

#include "net_protocol.h"
#include "replication_protocol.h"
#include <cstdint>
#include <cstddef>
#include <algorithm>

namespace pse {

// Scalability Benchmark Configuration for Synthetic World Simulation
// Note: These quantities are synthetic load test scenarios to evaluate
// network bandwidth, packet sizes, CPU/RAM, and latency resilience,
// not hard limits of the real PowerSlave engine.
struct WorldScaleConfig {
    int numPlayers = 8;       // Host + 7 Clients
    int numEnemies = 20;      // Anubis, Mummies, Bastet
    int numProjectiles = 50;  // In-flight bullets & fireballs
    int numPickups = 30;      // Health, Ammo, Keys, Ankh
    int numInteractive = 20;  // 10 Doors + 10 Switches

    // Conservative application-level packet payload budget to prevent IP fragmentation
    size_t maxPacketPayloadBytes = 1200;

    int totalEntities() const {
        return numPlayers + numEnemies + numProjectiles + numPickups + numInteractive;
    }

    // Packet overheads: MsgHeader (11) + MsgWorldSnapshot header (16) = 27 bytes
    static constexpr size_t SNAPSHOT_PACKET_OVERHEAD = MsgHeader::SERIALIZED_SIZE + MsgWorldSnapshot::HEADER_SIZE;

    // Baseline packet overhead: MsgHeader (11) + MsgWorldBaseline header (max ~50)
    static constexpr size_t BASELINE_PACKET_OVERHEAD = MsgHeader::SERIALIZED_SIZE + 50;

    // Calculate maximum entity snapshots that fit within the specified MTU budget
    static constexpr size_t calculateMaxEntitiesPerBatch(size_t maxPayloadBytes = 1200) {
        if (maxPayloadBytes <= SNAPSHOT_PACKET_OVERHEAD + EntitySnapshot::SERIALIZED_SIZE) {
            return 1;
        }
        return (maxPayloadBytes - SNAPSHOT_PACKET_OVERHEAD) / EntitySnapshot::SERIALIZED_SIZE;
    }

    // Calculate number of batches required to send an entity snapshot frame
    static constexpr size_t calculateBatchesForEntityCount(size_t entityCount, size_t maxPayloadBytes = 1200) {
        if (entityCount == 0) return 1;
        size_t perBatch = calculateMaxEntitiesPerBatch(maxPayloadBytes);
        return (entityCount + perBatch - 1) / perBatch;
    }

    // Calculate maximum spawn items per baseline batch
    static constexpr size_t calculateMaxBaselineEntitiesPerBatch(size_t maxPayloadBytes = 1200) {
        if (maxPayloadBytes <= BASELINE_PACKET_OVERHEAD + MsgEntitySpawn::SERIALIZED_SIZE) {
            return 1;
        }
        return (maxPayloadBytes - BASELINE_PACKET_OVERHEAD) / MsgEntitySpawn::SERIALIZED_SIZE;
    }
};

} // namespace pse
