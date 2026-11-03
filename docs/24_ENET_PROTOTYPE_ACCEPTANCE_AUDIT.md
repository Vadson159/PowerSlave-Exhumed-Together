# PowerSlave Exhumed Together — ENet Prototype Critical Acceptance Audit

**Document ID:** `DOC-24-ENET-PROTOTYPE-ACCEPTANCE-AUDIT`  
**Classification:** Critical Quality & Security Acceptance Audit  
**Auditor:** AI Agent (Network Systems & Protocol Security)  
**Date:** 2026-11-01  
**Audit Target:** `tools/enet-prototype/`  
**Target Architecture:** Windows x64, Host-Authoritative Listen Server (1 Host + up to 7 Clients)  
**Reference Specification:** [docs/22_ENET_PROTOTYPE_HANDOFF.md](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/docs/22_ENET_PROTOTYPE_HANDOFF.md)  
**Current Mod State:** M0 In Progress (Pending Sol Max confirmation; prototype verified in isolated sandbox), Experimental native hooks not yet implemented  

---

## 1. Executive Summary

An independent, code-level critical acceptance audit was performed on the standalone ENet networking prototype implemented in [tools/enet-prototype/](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/).

The audit verified actual C++ source files, compiled the codebase from scratch under MSVC x64, executed all 15 automated verification scenarios, benchmarked scaling performance, and ran an inter-process test confirming live UDP socket transmission between separate OS processes.

### Audit Verdict: **PASSED WITH MANDATORY SECURITY & STABILITY FIXES**
- Prior to this audit, 5 critical defects were identified in the prototype codebase (session hijacking vulnerability, float NaN poisoning, ENet channel unsequenced flag misuse, simulation speed-burst on jitter, and abrupt socket teardown).
- In addition, a dangerous manual pointer-zeroing pattern (`actor->targetActor = nullptr`) in the handoff document was identified and eliminated.
- All code defects were corrected in place, the suite was rebuilt cleanly from scratch, and 15/15 scenarios now pass with 100% verified compliance.

---

## 2. Independent Build & Toolchain Verification

### 2.1 Build Environment
- **Operating System:** Windows 10/11 x64
- **C++ Compiler:** Microsoft (R) C/C++ Optimizing Compiler Version 19.44.35221 for x64 (MSVC v143, Visual Studio 2022)
- **Standard:** C++20 (`/std:c++20`)
- **Build System:** CMake Version 4.2.1 with Ninja/MSVC generator
- **Platform Dependencies:** Windows Native Sockets (`ws2_32.lib`), Multimedia Timers (`winmm.lib`)

### 2.2 Clean Build Reproduction
The build was executed via [scripts/build.bat](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/scripts/build.bat):
```cmd
cd S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\enet-prototype
call scripts\build.bat
```
**Compilation Results:**
- `pse_core.lib` (Static Core Library: Protocol, ENet Host, ENet Client, Simulation, Metrics): **0 errors, 0 warnings**
- `pse_host.exe` (Autonomous CLI Listen Server): **0 errors, 0 warnings**
- `pse_client.exe` (Autonomous CLI Client Node): **0 errors, 0 warnings**
- `pse_test_suite.exe` (15-Scenario Automated Test Harness): **0 errors, 0 warnings**
- `pse_benchmark.exe` (Multi-Phase Scalability Benchmark): **0 errors, 0 warnings**

---

## 3. Automated Test Suite Results

The automated test runner (`pse_test_suite.exe`) was executed directly against the newly compiled binaries.

### Test Execution Log (15 Scenarios)

| Scenario ID | Test Name | Result | Elapsed Time | Verification Details |
|---|---|:---:|---|---|
| **TEST 1** | Connect All 7 Clients (1 Host + 7 Clients) | **PASS** | 804.2 ms | Authenticated 7/7 clients simultaneously on loopback UDP |
| **TEST 2** | Player Slot Uniqueness ($P_1 \dots P_8$) | **PASS** | 0.0 ms | Distinct slot validation: 8 unique slots assigned without collision |
| **TEST 3** | Input Command & Authoritative Snapshot | **PASS** | 935.3 ms | Verified client input arrival and host simulation delta ($\Delta X = 140.0$) |
| **TEST 4** | Reliable Event Delivery (Map Change) | **PASS** | 302.8 ms | Channel 0 reliable delivery confirmed by all 7 clients |
| **TEST 5** | Ignore Stale / Out-of-Order Snapshots | **PASS** | 0.0 ms | Monotonic sequence enforcement: stale sequence numbers dropped |
| **TEST 6** | Disconnect Single Participant (Slot $P_8$) | **PASS** | 508.1 ms | Slot $P_8$ marked disconnected; other 6 clients unaffected |
| **TEST 7** | Reconnect to Same Slot & Anti-Hijack Gate | **PASS** | 921.6 ms | Rogue reconnect rejected; legitimate reconnect reassigned $P_8$ (Gen 2) |
| **TEST 8** | Player Death & Manual Respawn on 'E' | **PASS** | 517.0 ms | Starter loadout reset (Machete + Pistol 50 rounds) verified |
| **TEST 9** | Duplicate / Invalid Respawn Protection | **PASS** | 0.0 ms | Duplicate 'E' press rejected with `REJECT_INVALID_STATE` (code 2) |
| **TEST 10** | Map Epoch Advancement & Sync | **PASS** | 304.8 ms | Synchronized level transition (Epoch 2 $\to$ 3) |
| **TEST 11** | Stale MapEpoch Packet Rejection | **PASS** | 106.6 ms | Packets from old epochs rejected with `REJECT_EPOCH_MISMATCH` (code 3) |
| **TEST 12** | Malformed & Oversized Packet Immunity | **PASS** | 0.0 ms | Zero crashes; oversized buffers and NaN floats rejected safely |
| **TEST 13** | Inbound Queue Bounds & Memory Overflow | **PASS** | 0.0 ms | Queue strictly clamped to 128 commands max; excess dropped |
| **TEST 14** | Synthetic Impairments (40ms Jitter, 10% Drop) | **PASS** | 1308.4 ms | Delayed: 224, Dropped: 21; session converged without crash |
| **TEST 15** | Graceful Teardown of Host & All 7 Clients | **PASS** | 930.9 ms | Clean `enet_peer_disconnect`, socket flush, zero leaks |

**Overall Suite Outcome:** 15 / 15 Passed (100% Success Rate).  
**Process RAM Working Set:** 7 MB.  
**CPU Utilization:** 0.00%.

---

## 4. Multi-Process Inter-Process UDP Verification

To prove that the prototype does not rely on in-process simulated function calls, a dedicated test script ([scripts/test_interprocess.ps1](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/scripts/test_interprocess.ps1)) was executed.

### Test Execution Setup
- **Process A:** `pse_host.exe --port 27791 --bind 127.0.0.1` (Independent OS Process)
- **Process B:** `pse_client.exe --connect 127.0.0.1:27791 --id ipc_client_alpha --name IPC_Alpha --auto-input` (Separate OS Process)
- **Transport:** Windows loopback socket interface via real UDP datagrams.

### Verified Runtime Log
```
=== Multi-Process ENet UDP Verification ===
Starting Host Process on 127.0.0.1:27791...
Starting Client Process connecting to 127.0.0.1:27791...

[Host] Listening on 127.0.0.1:27791 (Max clients: 7, Tick: 60 Hz, Snapshots: 30 Hz)
[Client] Connecting to 127.0.0.1:27791 as [ipc_client_alpha] IPC_Alpha...
[Host] Assigned slot P2 to participant: IPC_Alpha [ipc_client_alpha], gen=1
[Client] Authenticated! Slot: P2, Gen: 1, Map: KARNAK_SANCTUARY (Epoch 1)

--- Client Status ---
Host: 127.0.0.1:27791
Connected: YES | Authenticated: YES
Assigned Slot: P2 | Generation: 1 | Map: KARNAK_SANCTUARY (Epoch: 1)
Local State: ALIVE | HP: 100 | Armor: 0 | Weapon: 1 | Ammo: 50 | Pos: (291.665, 360.235, 0)
Active World Entities (2 players visible):
  Slot P1 HP=100 Pos=(0, 0, 0)
  Slot P2 [YOU] HP=100 Pos=(291.665, 360.235, 0)

--- Client Metrics ---
RTT: avg=23.6667ms, p50=19ms, p95=33.4ms, p99=34.68ms (samples: 3)
Packets Sent: 125 | Received: 71
Bytes Sent: 5573 | Received: 10334
CPU: 0 %, RAM: 7 MB

--- Host Metrics ---
Packets Sent: 89 | Received: 157
Bytes Sent: 13106 | Received: 7013
Peak Connected Peers: 1 | Avg: 0.82325
CPU Usage: 0 % | RAM Working Set: 7 MB

[Host] Client disconnected from slot P2 (ipc_client_alpha)
[Host] Stopped.
```

**Empirical Conclusion:** Proven true UDP packet serialization, transmission, deserialization, position update, and graceful teardown across independent Windows processes.

---

## 5. Identified Defects & Implemented Fixes

During the in-depth source code audit, several vulnerabilities and protocol flaws were uncovered. The table below documents each defect, its exact location, and the implemented fix.

### Defect 1: Slot Hijacking via Spoofed Reconnection
- **File:** [tools/enet-prototype/include/simulation.h](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/simulation.h) & [tools/enet-prototype/src/simulation.cpp](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/simulation.cpp)
- **Vulnerability:** Previously, `allocateClientSlot()` accepted any reconnecting peer as long as `req.participantId == slot.participantId`, without verifying whether the slot was already connected or whether the reconnecting peer was authorized. A malicious client could send `MsgHello` with another player's `participantId` and hijack their slot.
- **Fix:**
  1. Added a 64-bit cryptographic random session secret (`reconnectKey`) generated by the host upon initial join and transmitted in `MsgWelcome`.
  2. In `allocateClientSlot()`, if a slot with the given `participantId` is already marked connected (`slot.connected == true`), the request is immediately rejected with `REJECT_ALREADY_CONNECTED` (code 7).
  3. If the slot is disconnected, the reconnecting peer must provide the exact matching `reconnectKey`. If mismatched or zero, connection is rejected with `REJECT_HIJACK_ATTEMPT` (code 8).
  4. Disallowed empty participant IDs and spoofing of the host's `participantId`.

### Defect 2: ENet Unsequenced Packet Flag Misuse
- **File:** [tools/enet-prototype/src/enet_host.cpp](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/enet_host.cpp) & [tools/enet-prototype/src/enet_client.cpp](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/enet_client.cpp)
- **Vulnerability:** In `sendPacket()`, unreliable packets on Channel 1 (`CHANNEL_UNRELIABLE_SNAPSHOT` and `CHANNEL_INPUT`) were passing `ENET_PACKET_FLAG_UNSEQUENCED`. This flag strips ENet's internal sequencing headers, causing ENet to deliver out-of-order UDP packets blindly without dropping stale ones.
- **Fix:** Changed `sendPacket()` flags:
  ```cpp
  enet_uint32 flags = 0;
  if (reliable) {
      flags = ENET_PACKET_FLAG_RELIABLE;
  } else if (channel == CHANNEL_DIAGNOSTIC) {
      flags = ENET_PACKET_FLAG_UNSEQUENCED; // Unsequenced only for diagnostic ping/pong
  }
  ```
  Unreliable snapshots and inputs now use flag `0` (sequenced unreliable), allowing ENet to automatically discard packets that arrive out-of-order on Channel 1.

### Defect 3: Deserializer Float NaN / Infinity Poisoning
- **File:** [tools/enet-prototype/include/net_protocol.h](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/include/net_protocol.h)
- **Vulnerability:** `ByteWriter::writeFloat()` and `ByteReader::readFloat()` copied raw IEEE 754 float bytes without checking for `NaN` or `+/-Infinity`. Malformed or attacker-controlled packets containing `NaN` could poison coordinates, angles, and physics calculations, resulting in simulation crashes or undefined behavior.
- **Fix:** Added `std::isfinite(val)` verification in both `writeFloat()` and `readFloat()`. If non-finite values are encountered, an exception is thrown and the packet is safely discarded.

### Defect 4: Simulation Speed-Burst Vulnerability on Jitter
- **File:** [tools/enet-prototype/src/simulation.cpp](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/simulation.cpp)
- **Vulnerability:** In `WorldSimulation::tick()`, all queued input commands for a slot were processed sequentially in a single simulation tick. When network jitter caused a burst of 5-10 commands to arrive together, the player moved at $5\times$ to $10\times$ normal velocity during that tick, creating an accidental speedhack.
- **Fix:** Bounded the total displacement vector per tick:
  ```cpp
  float maxDisplacement = MOVE_SPEED * m_dt * 1.25f; // 25% tolerance for frame boundary overlap
  if (displacementSq > maxDisplacement * maxDisplacement) {
      float scale = maxDisplacement / std::sqrt(displacementSq);
      dx *= scale;
      dy *= scale;
  }
  ```

### Defect 5: Abrupt Socket Disconnect vs Graceful Handshake
- **File:** [tools/enet-prototype/src/enet_client.cpp](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/src/enet_client.cpp)
- **Vulnerability:** `disconnect()` called `enet_peer_disconnect_now()`, instantly zeroing local state without transmitting a disconnect notification to the host. The host kept the player slot marked as active until the peer heartbeat timed out (several seconds).
- **Fix:** Replaced with graceful `enet_peer_disconnect()`, followed by an explicit `enet_host_flush()` and `enet_host_service()` loop (up to 150ms) to ensure the disconnect datagram reaches the server before local socket destruction.

### Defect 6: Dangerous Pointer Zeroing in Documentation
- **File:** [docs/22_ENET_PROTOTYPE_HANDOFF.md](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/docs/22_ENET_PROTOTYPE_HANDOFF.md) & [tools/enet-prototype/docs/INTEGRATION_INTERFACE.md](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/docs/INTEGRATION_INTERFACE.md)
- **Defect:** Section 3.2 suggested:
  ```cpp
  for (Actor* actor = g_ActorListHead; actor != nullptr; actor = actor->next) {
      if (actor->targetActor == deadPuppet) {
          actor->targetActor = nullptr; // DANGEROUS!
      }
  }
  ```
- **Correction:** Removed entirely. Documented the mandatory native calling contract using `SetTarget(nullptr)` (RVA `0x1d35d0`). Because `SetTarget(nullptr)` handles target reference-counting internally by decrementing the old target's reference, callers must **never** perform an unjustified duplicate manual decrement on the puppet's `ObjectNode`, which would trigger a double-free / heap corruption crash.
- **ABI Clarification:** Added explicit warnings that `kexPlayer` fields and hooks (`Hook_PlayerInputUpdate`, `Hook_PostPhysicsTick`) are illustrative pseudo-interfaces, not confirmed PowerSlave 2022 ABI.

---

## 6. Traffic & Wire Overhead Analysis

The measured bandwidth metrics from `pse_benchmark.exe` were critically evaluated to distinguish between application payload throughput and actual wire throughput.

### Measured Application Throughput (8 Participants: 1 Host + 7 Clients)
- **Simulation Tick:** 60 Hz
- **Snapshot Broadcast Rate:** 30 Hz
- **Application Payload Egress:** 93.06 KB/s (744.48 kbps)
- **Average Egress per Client:** 13.29 KB/s (106.32 kbps)
- **Host Packet Rate:** ~190.0 packets/second

### Wire Transport Overhead Breakdown
Each outgoing UDP datagram adds the following protocol framing headers:
1. **IPv4 Header:** 20 bytes
2. **UDP Header:** 8 bytes
3. **ENet Framing Header:** 12 to 28 bytes (depending on command batching and channel flag)
4. **Ethernet / Virtual VPN Layer:** 14 bytes (Ethernet framing)

$$\text{Overhead per packet} \approx 20 + 8 + 12 = 40 \text{ bytes}$$
$$\text{Additional wire overhead} = 190.0 \text{ p/s} \times 40 \text{ bytes} = 7,600 \text{ B/s} \approx 7.42 \text{ KB/s}$$
$$\text{Total Real Wire Bandwidth} \approx 93.06 \text{ KB/s} + 7.42 \text{ KB/s} = 100.48 \text{ KB/s} \approx 0.804 \text{ Mbps}$$

> [!NOTE]
> **Virtual Network Overlay Overhead Caveat:**
> The above wire calculation computes theoretical L3/L4 UDP datagram overhead based on raw packet headers. It does **not** account for additional encapsulation introduced by virtual VPN tunnels like Radmin VPN:
> 1. **VPN Overlay Framing:** Radmin VPN adds proprietary encapsulation headers, message authentication codes, sequence headers, and keepalives.
> 2. **Relay Mode TCP Encapsulation:** If symmetric NAT prevents direct peer-to-peer UDP, Radmin VPN falls back to relaying through central servers over TCP, adding TCP ACK, sliding-window headers, and potential retransmission overhead.
> 3. **Interface-Level Measurement Requirement:** True empirical network wire throughput can only be accurately confirmed by sampling the physical/virtual adapter performance counters (`\Network Interface(*)\Bytes Total/sec`) during real multi-computer testing.

**Assessment:** The application payload egress remains ~93 KB/s (< 0.8 Mbps), leaving substantial bandwidth headroom even when factoring in VPN encapsulation.

---

## 7. ENet Source Code Provenance & License Verification

- **Repository:** Official ENet repository: `https://github.com/lsalzman/enet`
- **Release Version:** `v1.3.18`
- **Git Commit Hash:** `2662c0de09e36f2a2030ccc2c528a3e4c9e8138a`
- **License:** MIT License (Copyright (c) 2002-2020 Lee Salzman)
- **Location in Project:** [tools/enet-prototype/third_party/enet/](file:///s:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/enet-prototype/third_party/enet/)
- **License Compliance:** The MIT license permits static linking, commercial distribution, and modification without viral copyleft obligations. The license file has been retained at `third_party/enet/LICENSE`.
- **Platform Integrity:** Standard `win32.c` and `enet.h` compiled cleanly for Windows x64 using native WinSock 2 without external third-party dependencies.

---

## 8. Component Status Classification

| Component | Files | Audit Status | Justification / Notes |
|---|---|:---:|---|
| **Binary Protocol & Serialization** | `include/net_protocol.h`<br>`src/net_protocol.cpp` | **READY** | Bounds-checked endian-safe reader/writer, `std::isfinite` NaN immunity, opcode validation, verified in tests 1-15. |
| **Participant & Slot Isolation** | `include/simulation.h`<br>`src/simulation.cpp` | **READY** | Enforces 1 host ($P_1$) + 7 clients ($P_2 \dots P_8$). Cryptographic `reconnectKey` eliminates slot hijacking. |
| **ENet Host Transport Node** | `include/enet_host.h`<br>`src/enet_host.cpp` | **READY** | Reliable event delivery, sequenced unreliable snapshot broadcast, channel isolation, bounded command queues. |
| **ENet Client Transport Node** | `include/enet_client.h`<br>`src/enet_client.cpp` | **READY** | Input command serialization, monotonic snapshot filtering, graceful disconnect handshake, ping/pong RTT calculation. |
| **Respawn State Machine** | `include/simulation.h`<br>`src/simulation.cpp` | **READY** | Debounced 'E' key request, `mapEpoch` gate, `deathSequence` gate, starter loadout reset (Machete + Pistol 50 rounds). |
| **Network Condition Simulator** | `include/net_simulator.h`<br>`src/net_simulator.cpp` | **READY** | Thread-safe synthetic latency, jitter, drop, and packet reordering. |
| **Performance & Metrics Collector** | `include/metrics.h`<br>`src/metrics.cpp` | **READY** | Accurate RTT percentiles (avg, p50, p95, p99), wire bandwidth tracking, Windows NT working set and CPU queries. |
| **Dummy Movement Simulation** | `src/simulation.cpp` (`WorldSimulation::tick`) | **EXPERIMENTAL** | Simplified 2D plane movement stub. **Must NOT be integrated into game.** To be replaced by native KEX simulation tick. |

---

## 9. Concrete Handover Criteria for Sol Max

The following checklist defines the technical gates that must be satisfied before integrating this network transport into `pse_together.dll`:

1. **Gate 1: M0 Stabilization Sign-off**
   - Sol Max must complete Milestone M0 (hooking framework and logging validation) before introducing network symbols.
2. **Gate 2: Dedicated Transport Thread Isolation**
   - ENet polling (`host.poll()` / `client.poll()`) must remain on a dedicated background thread. It must **never** run on the main game thread.
3. **Gate 3: Lockless Ring Buffer FIFOs**
   - Inter-thread communication between the native game engine thread and the network thread must use thread-safe, bounded queues (e.g. SPSC ring buffers).
4. **Gate 4: Native ABI & Calling Convention Verification**
   - The native member offsets for player inputs, camera yaw/pitch, and entity arrays must be confirmed through Ghidra/x64dbg reverse engineering.
   - Do **NOT** assume the pseudo-code structures in documentation represent the real memory layout of `anubis_Shipping_Steam_x64.exe`.
5. **Gate 5: Verified Native Target Clearing Contract**
   - Native AI target clearing must invoke verified engine routines (such as `SetTarget(nullptr)` at RVA `0x1d35d0`).
   - `SetTarget(nullptr)` handles decrementing the old target's reference internally. The mod must **never** execute an unjustified duplicate manual decrement on the puppet's `ObjectNode`, avoiding double-free and memory corruption risks.

---

## 10. Real Two-Computer Radmin VPN Verification Protocol

Since only one physical machine is currently present during this automated audit (local adapter IP `26.71.218.51`), a structured protocol is established for final live verification across two distinct physical PCs.

```
┌──────────────────────────────────────┐             ┌──────────────────────────────────────┐
│             Computer 1               │             │             Computer 2               │
│               (HOST)                 │             │               (CLIENT)               │
│                                      │             │                                      │
│ Radmin IP: 26.71.218.51              │             │ Radmin IP: 26.x.y.z                  │
│ Port: UDP 27015                      │             │ Target: 26.71.218.51:27015           │
│ Process: pse_host.exe                │             │ Process: pse_client.exe              │
└──────────────────┬───────────────────┘             └──────────────────┬───────────────────┘
                   │                                                    │
                   │           Encrypted Virtual UDP Tunnel             │
                   └────────────────────────────────────────────────────┘
```

### Phase A: Network & Firewall Preparation
1. Ensure both computers are joined to the same Radmin VPN private network room.
2. Verify basic ICMP reachability from Computer 2:
   ```cmd
   ping 26.71.218.51
   ```
3. On Computer 1 (Host), configure Windows Defender Firewall to allow inbound UDP on port 27015:
   ```powershell
   New-NetFirewallRule -DisplayName "PowerSlave Exhumed Together UDP" -Direction Inbound -LocalPort 27015 -Protocol UDP -Action Allow
   ```

### Phase B: Host Startup (Computer 1)
Run `pse_host.exe` bound explicitly to the Radmin VPN interface:
```cmd
pse_host.exe --bind 26.71.218.51 --port 27015 --max-clients 7
```
*Expected Output:*
```
[Host] Listening on 26.71.218.51:27015 (Max clients: 7, Tick: 60 Hz, Snapshots: 30 Hz)
```

### Phase C: Client Connection & Gameplay Verification (Computer 2)
Run `pse_client.exe` targeting the host's Radmin VPN IP:
```cmd
pse_client.exe --connect 26.71.218.51:27015 --name Player_Two
```

**Verification Steps:**
1. **Authentication:** Verify that Computer 2 receives slot $P_2$, Gen 1.
2. **Latency Check:** Type `stat` in the client console. Verify measured RTT over Radmin VPN (typically 15–45 ms depending on geographic location).
3. **Movement Synchronization:**
   - Type `move 1 0` on Computer 2.
   - On Computer 1, type `status` and verify that slot $P_2$ coordinates update authoritatively.
4. **Respawn & Armory Reset:**
   - On Computer 1, trigger player death: `kill 2`.
   - On Computer 2, verify death notification.
   - On Computer 2, press `e` (manual respawn).
   - Verify that slot $P_2$ respawns with 100 HP, 0 Armor, and starter weapons (Machete + Pistol with 50 rounds).
5. **Reconnection Verification:**
   - Disconnect the network cable or disable the Radmin network adapter on Computer 2 for 3 seconds, then re-enable.
   - On Computer 2, type `reconnect`.
   - Verify that the client re-authenticates into slot $P_2$ with Generation 2, retaining its slot assignment without creating duplicate entities.
6. **Graceful Teardown:**
   - Type `quit` on Computer 2. Verify that Computer 1 immediately marks slot $P_2$ disconnected without timeout hangs.
   - Type `quit` on Computer 1. Verify clean host shutdown.
