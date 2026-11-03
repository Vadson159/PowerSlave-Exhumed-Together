# PowerSlave Exhumed Together — ENet Prototype Test Execution Results

- **Date:** 2026-11-01
- **Platform:** Windows x64 (MSVC / C++20)
- **Transport:** ENet v1.3.18 over UDP
- **Target Architecture:** Host-Authoritative Listen Server (1 Host + 7 Clients)

## Automated Scenario Results

| ID | Scenario | Status | Duration (ms) | Details |
|---|---|---|---|---|
| 1 | Connect All 7 Clients (1 Host + 7 Clients) | **PASS** | 808.8 | Authenticated: 7/7 |
| 2 | Player Slot Uniqueness (P1-P8) | **PASS** | 0.0 | Distinct slots count: 8 |
| 3 | Input Command & Authoritative Snapshot Transmission | **PASS** | 934.9 | Delta X: 140.000031 |
| 4 | Reliable Event Delivery (Map Change Event) | **PASS** | 303.5 | Confirmed by Client P3 |
| 5 | Ignore Stale / Out-of-Order Snapshots | **PASS** | 0.0 | Monotonic sequence enforced |
| 6 | Disconnect Single Participant (Slot P8) | **PASS** | 514.2 | Slot marked disconnected |
| 7 | Reconnect to Same Slot & Anti-Hijack Gate | **PASS** | 907.1 | Hijack rejected, legitimate reassigned P8 (Gen 2) |
| 8 | Player Death & Manual Respawn on 'E' (Starter Loadout Reset) | **PASS** | 516.3 | HP: 200, Weapons reset verified |
| 9 | Duplicate / Invalid Respawn Protection | **PASS** | 0.0 | Host rejection code: 2 |
| 10 | Map Epoch Advancement & Synchronized Transition | **PASS** | 305.7 | Epoch 2 -> 3 |
| 11 | Stale MapEpoch Packet Rejection | **PASS** | 108.4 | Rejected with REJECT_EPOCH_MISMATCH (code 3) |
| 12 | Malformed & Oversized Packet Immunity | **PASS** | 0.0 | Zero crashes, malformed payloads and NaN rejected cleanly |
| 13 | Inbound Queue Bounds & Memory Overflow Protection | **PASS** | 0.0 | Queue clamped to: 128 <= 128 |
| 14 | Synthetic Network Impairments (40ms Jitter, 10% Drop, Reorder) | **PASS** | 1283.5 | Delayed: 224, Dropped: 21, Session converged |
| 15 | Input Scheduling - Jitter Burst Decoupling & Zero Speed Hack | **PASS** | 0.0 | Tick 1 dx=4.666668 (max 4.666667), queue remaining=4, total dx=23.333340 |
| 16 | Input Command Filtering - Monotonic Sequences & Generation Gate | **PASS** | 0.0 | Rejections verified: dup=PASS, stale=PASS, badGen=PASS |
| 17 | Predictable Action Buttons - Semi-Auto Edge & Starvation Reset | **PASS** | 0.0 | Edge fire verified, ammo=58, starvation buttons=0 |
| 18 | Crypto Reconnect Key - Ghost Recovery & Generation Invalidation | **PASS** | 0.0 | Slot: P2 (Gen 2), oldPeerEjected=YES |
| 19 | Graceful Teardown of Host & All 7 Clients | **PASS** | 932.5 | Zero resource leaks, all sockets closed |
| 20 | Concurrent Spawn of Multiple Objects | **PASS** | 305.4 | Spawned 23 entities across 5 types simultaneously, confirmed by Client P2 |
| 21 | Duplicate Spawn Protection & Idempotency | **PASS** | 0.0 | 5 duplicate spawn packets discarded, entity count unchanged (idempotency verified) |
| 22 | Despawn and Entity ID Reuse with New Generation | **PASS** | 103.1 | Entity 2001 despawned (gen 1), stale generation packet safely rejected |
| 23 | Snapshot Loss Resilience & Client Interpolation | **PASS** | 0.0 | 50% snapshot loss tolerated; smooth lerp x=30 yaw=45 verified |
| 24 | Packet Reordering & Monotonic Sequence Filtering | **PASS** | 0.0 | Stale snapshot seq=480 discarded after seq=500; monotonic order preserved |
| 25 | Late-Joining Client Baseline Sync | **PASS** | 486.3 | Client P3 authenticated and synced 27 entities via baseline |
| 26 | Mid-Battle Reconnect | **PASS** | 740.9 | Client P2 reconnected to slot P2 (Gen 2); baseline restored team keys=0x01 |
| 27 | Map Transition with Active Projectiles | **PASS** | 150.7 | Transition to epoch 2 purged 10 projectiles; epoch 1 stale events rejected |
| 28 | Preservation of Door/Switch State | **PASS** | 253.8 | Switch activation toggled Door 30001; state preserved across all peers |
| 29 | Non-Corruption of Player State Isolation | **PASS** | 0.0 | Damage to Slot P2 did not affect Slots P1 or P3; strict memory isolation verified |
| 30 | Wire Protocol Wire Sizes & Generational Fields (P0.1) | **PASS** | 0.0 | EntitySnapshot=55B (55B), MsgInteractionRequest=14B (14B), MsgWorldEventConfirm=30B (30B) |
| 31 | Multi-Batch Snapshot Assembly under 1200B MTU (P0.2/P0.3) | **PASS** | 0.1 | 128 entities -> 7 batches of <=21 entities, max packet=1182B <= 1200B, frame completed |
| 32 | Out-of-Order Snapshot Batch Reassembly (P0.3) | **PASS** | 0.0 | Batches delivered [3, 2, 1, 0]; out-of-order batches preserved and frame completed |
| 33 | Single Batch Loss & Partial Frame Tracking (P0.3) | **PASS** | 0.0 | Frame 300 missing batch 2 recorded as partial frame upon arrival of frame 301 |
| 34 | Duplicate Batch Rejection within Frame (P0.3) | **PASS** | 0.0 | Duplicate batch 0 dropped via bitmask (dupCount=1), frame completed on batch 1 |
| 35 | Generational Identity & ABA Protection in Adapter (P0.4) | **PASS** | 0.3 | Stale generation calls rejected, matching generation calls accepted (health=75, x=500) |
| 36 | Multi-Batch Baseline Sync under MTU Budget (P0.2/P0.3) | **PASS** | 0.1 | 128 entities baseline synced in 7 batches <= 1200B; all 128 entities reconstructed on client |
| 37 | Adapter generation = 0 Prohibition (All Methods) | **PASS** | 0.0 | Zero generation rejected on 7 adapter methods; valid matching generation accepted |
| 38 | Protocol Network Messages generation = 0 Rejection | **PASS** | 0.0 | Spawn, Despawn, Snapshot, and EventConfirm strictly rejected generation=0 over the wire |
| 39 | Generational ABA & Simultaneous Verification | **PASS** | 0.3 | Reused ID 2001 safely rejected stale gen=1, accepted gen=2 (health: 120 -> 80) |
| 40 | Frame Reassembly 64-bit Mask Boundaries (1, 32, 33, 63, 64) | **PASS** | 0.0 | Masks 1, 32, 33, 63, 64 verified UB-free; 64-batch frame completed (~0ULL); batch 65 rejected |
| 41 | Sequence Rollover (UINT32_MAX), Inconsistency & Drops | **PASS** | 0.0 | Rollover 0xFFFFFFFF -> 1 succeeded; mid-frame batch change rejected; partial frame & late drop verified |
| 42 | Continuous 128-Entity Client Reception Rate Measurement | **PASS** | 0.8 | Client received 3840 entities across 210 batches (30 complete frames/sec = 3840 entities/s) |
| 43 | KPF 8-Weapon Mapping, Bitmasks, and 0..7 Range Validation | **PASS** | 0.0 | 8 Weapons (0..7) verified, ALL_WEAPONS_MASK=0xFF, Machete..Manacles range enforced |
| 44 | Configurable Starter Loadout (Dynamic C19 Defaults & Custom Fixtures) | **PASS** | 0.2 | KEX C19 (200HP, 60 ammo) verified, dynamic custom fixtures (100HP, 50 ammo) validated |
| 45 | Angle Conversion Utilities & +/-180 deg / +/-pi Wraparound | **PASS** | 0.0 | Bidirectional rad/deg conversion verified, +/-180 deg seam crossing interpolates shortest arc |
| 46 | Protocol Version Bump (0x0110 / v1.10) & Legacy Client Rejection | **PASS** | 167.4 | NET_PROTOCOL_VERSION=0x0110 (v1.10), legacy 0x0100 rejected, modern v1.10 connected |

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

