# PowerSlave Exhumed Together — ENet Prototype Test Execution Results

- **Date:** 2026-11-01
- **Platform:** Windows x64 (MSVC / C++20)
- **Transport:** ENet v1.3.18 over UDP
- **Target Architecture:** Host-Authoritative Listen Server (1 Host + 7 Clients)

## Automated Scenario Results

| ID | Scenario | Status | Duration (ms) | Details |
|---|---|---|---|---|
| 1 | Connect All 7 Clients (1 Host + 7 Clients) | **PASS** | 808.2 | Authenticated: 7/7 |
| 2 | Player Slot Uniqueness (P1-P8) | **PASS** | 0.0 | Distinct slots count: 8 |
| 3 | Input Command & Authoritative Snapshot Transmission | **PASS** | 942.8 | Delta X: 140.000031 |
| 4 | Reliable Event Delivery (Map Change Event) | **PASS** | 300.7 | Confirmed by Client P3 |
| 5 | Ignore Stale / Out-of-Order Snapshots | **PASS** | 0.0 | Monotonic sequence enforced |
| 6 | Disconnect Single Participant (Slot P8) | **PASS** | 505.7 | Slot marked disconnected |
| 7 | Reconnect to Same Slot & Anti-Hijack Gate | **PASS** | 927.7 | Hijack rejected, legitimate reassigned P8 (Gen 2) |
| 8 | Player Death & Manual Respawn on 'E' (Starter Loadout Reset) | **PASS** | 516.6 | HP: 100, Weapons reset verified |
| 9 | Duplicate / Invalid Respawn Protection | **PASS** | 0.0 | Host rejection code: 2 |
| 10 | Map Epoch Advancement & Synchronized Transition | **PASS** | 300.6 | Epoch 2 -> 3 |
| 11 | Stale MapEpoch Packet Rejection | **PASS** | 111.9 | Rejected with REJECT_EPOCH_MISMATCH (code 3) |
| 12 | Malformed & Oversized Packet Immunity | **PASS** | 0.0 | Zero crashes, malformed payloads and NaN rejected cleanly |
| 13 | Inbound Queue Bounds & Memory Overflow Protection | **PASS** | 0.1 | Queue clamped to: 128 <= 128 |
| 14 | Synthetic Network Impairments (40ms Jitter, 10% Drop, Reorder) | **PASS** | 1291.7 | Delayed: 224, Dropped: 21, Session converged |
| 15 | Input Scheduling - Jitter Burst Decoupling & Zero Speed Hack | **PASS** | 0.0 | Tick 1 dx=4.666668 (max 4.666667), queue remaining=4, total dx=23.333340 |
| 16 | Input Command Filtering - Monotonic Sequences & Generation Gate | **PASS** | 0.0 | Rejections verified: dup=PASS, stale=PASS, badGen=PASS |
| 17 | Predictable Action Buttons - Semi-Auto Edge & Starvation Reset | **PASS** | 0.0 | Edge fire verified, ammo=48, starvation buttons=0 |
| 18 | Crypto Reconnect Key - Ghost Recovery & Generation Invalidation | **PASS** | 0.0 | Slot: P2 (Gen 2), oldPeerEjected=YES |
| 19 | Graceful Teardown of Host & All 7 Clients | **PASS** | 963.2 | Zero resource leaks, all sockets closed |
| 20 | Concurrent Spawn of Multiple Objects | **PASS** | 302.4 | Spawned 23 entities across 5 types simultaneously, confirmed by Client P2 |
| 21 | Duplicate Spawn Protection & Idempotency | **PASS** | 0.0 | 5 duplicate spawn packets discarded, entity count unchanged (idempotency verified) |
| 22 | Despawn and Entity ID Reuse with New Generation | **PASS** | 100.8 | Entity 2001 despawned (gen 1), stale generation packet safely rejected |
| 23 | Snapshot Loss Resilience & Client Interpolation | **PASS** | 0.0 | 50% snapshot loss tolerated; smooth lerp x=30 yaw=45 verified |
| 24 | Packet Reordering & Monotonic Sequence Filtering | **PASS** | 0.0 | Stale snapshot seq=480 discarded after seq=500; monotonic order preserved |
| 25 | Late-Joining Client Baseline Sync | **PASS** | 496.3 | Client P3 authenticated and synced 27 entities via baseline |
| 26 | Mid-Battle Reconnect | **PASS** | 755.6 | Client P2 reconnected to slot P2 (Gen 2); baseline restored team keys=0x01 |
| 27 | Map Transition with Active Projectiles | **PASS** | 150.2 | Transition to epoch 2 purged 10 projectiles; epoch 1 stale events rejected |
| 28 | Preservation of Door/Switch State | **PASS** | 253.8 | Switch activation toggled Door 30001; state preserved across all peers |
| 29 | Non-Corruption of Player State Isolation | **PASS** | 0.0 | Damage to Slot P2 did not affect Slots P1 or P3; strict memory isolation verified |
| 30 | Wire Protocol Wire Sizes & Generational Fields (P0.1) | **PASS** | 0.0 | EntitySnapshot=55B (55B), MsgInteractionRequest=14B (14B), MsgWorldEventConfirm=30B (30B) |
| 31 | Multi-Batch Snapshot Assembly under 1200B MTU (P0.2/P0.3) | **PASS** | 0.1 | 128 entities -> 7 batches of <=21 entities, max packet=1182B <= 1200B, frame completed |
| 32 | Out-of-Order Snapshot Batch Reassembly (P0.3) | **PASS** | 0.0 | Batches delivered [3, 2, 1, 0]; out-of-order batches preserved and frame completed |
| 33 | Single Batch Loss & Partial Frame Tracking (P0.3) | **PASS** | 0.0 | Frame 300 missing batch 2 recorded as partial frame upon arrival of frame 301 |
| 34 | Duplicate Batch Rejection within Frame (P0.3) | **PASS** | 0.0 | Duplicate batch 0 dropped via bitmask (dupCount=1), frame completed on batch 1 |
| 35 | Generational Identity & ABA Protection in Adapter (P0.4) | **PASS** | 0.4 | Stale generation calls rejected, matching generation calls accepted (health=75, x=500) |
| 36 | Multi-Batch Baseline Sync under MTU Budget (P0.2/P0.3) | **PASS** | 0.1 | 128 entities baseline synced in 7 batches <= 1200B; all 128 entities reconstructed on client |
| 37 | Adapter generation = 0 Prohibition (All Methods) | **PASS** | 0.0 | Zero generation rejected on 7 adapter methods; valid matching generation accepted |
| 38 | Protocol Network Messages generation = 0 Rejection | **PASS** | 0.0 | Spawn, Despawn, Snapshot, and EventConfirm strictly rejected generation=0 over the wire |
| 39 | Generational ABA & Simultaneous Verification | **PASS** | 0.4 | Reused ID 2001 safely rejected stale gen=1, accepted gen=2 (health: 120 -> 80) |
| 40 | Frame Reassembly 64-bit Mask Boundaries (1, 32, 33, 63, 64) | **PASS** | 0.0 | Masks 1, 32, 33, 63, 64 verified UB-free; 64-batch frame completed (~0ULL); batch 65 rejected |
| 41 | Sequence Rollover (UINT32_MAX), Inconsistency & Drops | **PASS** | 0.0 | Rollover 0xFFFFFFFF -> 1 succeeded; mid-frame batch change rejected; partial frame & late drop verified |
| 42 | Continuous 128-Entity Client Reception Rate Measurement | **PASS** | 0.6 | Client received 3840 entities across 210 batches (30 complete frames/sec = 3840 entities/s) |

## System Resource & Traffic Metrics

| Metric | Measured Value |
|---|---|
| Participants Supported | 8 (1 Host + 7 Clients) |
| Peak RAM Working Set | 8 MB |
| CPU Utilization (8 peers) | 0.00 % |
| Command Queue Bound | 128 items (enforced) |
| Loopback RTT Average | < 1.0 ms |
| Synthetic Network Delay | 40ms + 15ms Jitter (tolerated, zero disconnects) |
| Synthetic Packet Drop | 10.0% loss rate (reliable events delivered, snapshots sequenced) |


## Client Scaling Benchmark (60Hz Tick, 30Hz Snapshots)

| Clients (Peers) | Host Total Egress (KB/s) | Per-Client Bandwidth (KB/s) | Packets/sec Host | Reconnect Time (ms) |
|---|---|---|---|---|
| 1 client(s) | 4.34 KB/s | 4.34 KB/s | 30.40 p/s | 192.5 ms |
| 2 client(s) | 12.28 KB/s | 6.14 KB/s | 60.80 p/s | 194.6 ms |
| 4 client(s) | 37.25 KB/s | 9.31 KB/s | 117.60 p/s | 191.0 ms |
| 7 client(s) | 93.06 KB/s | 13.29 KB/s | 189.60 p/s | 192.0 ms |

## Tick & Snapshot Frequency Comparison (7 Clients)

| Tick Rate | Snapshot Rate | Host Egress (KB/s) | CPU Usage (%) | Working Set RAM (MB) |
|---|---|---|---|---|
| 30 Hz | 20 Hz | 69.62 KB/s | 0.23 % | 7 MB |
| 60 Hz | 30 Hz | 93.06 KB/s | 0.36 % | 7 MB |
| 60 Hz | 60 Hz | 146.24 KB/s | 0.23 % | 7 MB |
| 120 Hz | 60 Hz | 144.75 KB/s | 0.23 % | 7 MB |

## World State Replication Scalability & Empirical Delivery (128 Entities Target)

| Scenario | Serialized Entities/Frame | Broadcast Rate | Host Egress (KB/s) | Per-Client Bandwidth | Client Ingest (Entities/sec) | Batches/Frame | Avg RTT | CPU (%) | RAM |
|---|---|---|---|---|---|---|---|---|---|
| Combat Decay Simulation | 91.4 ent | 23.7 Hz | 932.78 KB/s | 133.25 KB/s | 2206.7 ent/s | 5 | 26.6 ms | 0.35 % | 10 MB |
| Sustained 128 Entities (Clean) | 128.7 ent | 24.0 Hz | 1300.58 KB/s | 185.80 KB/s | 3133.7 ent/s | 7 | 26.9 ms | 0.57 % | 11 MB |
| Sustained 128 Entities (Impaired) | 128.3 ent | 23.7 Hz | 1160.62 KB/s | 165.80 KB/s | 2743.4 ent/s | 7 | 85.0 ms | 0.31 % | 11 MB |
