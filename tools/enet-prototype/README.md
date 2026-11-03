# PowerSlave Exhumed Together — Standalone ENet Networking Prototype

Autonomous, host-authoritative cooperative networking layer for **PowerSlave Exhumed Together**.

- **Target Architecture:** Windows x64, C++20, MSVC.
- **Topology:** Host-Authoritative Listen Server (1 Host + up to 7 Remote Clients = 8 Players).
- **Transport:** Official [ENet v1.3.18](https://github.com/lsalzman/enet) over UDP.
- **Connectivity Modes:** Loopback (`127.0.0.1`), Physical LAN, Radmin VPN.
- **Dependencies:** WinSock2 (`ws2_32.lib`), Windows Multimedia (`winmm.lib`), Process Status (`psapi.lib`). Zero third-party runtime DLLs or cloud SDKs.
- **Game Isolation:** Completely standalone from `anubis_Shipping_Steam_x64.exe` (no game binary edits or hooks modified).

---

## 1. Quick Start & Build Instructions

### Prerequisites
- Windows 10/11 x64
- Visual Studio 2022 (MSVC v143 or BuildTools) with C++ Desktop Development
- CMake 3.20 or newer

### Building via Script
Run the automated build script:
```cmd
cd tools\enet-prototype\scripts
build.bat
```

### Manual CMake Build
```cmd
cmake -S tools\enet-prototype -B tools\enet-prototype\build -A x64
cmake --build tools\enet-prototype\build --config Release
```

Compiled binaries are placed in `tools\enet-prototype\build\Release\`:
- `pse_host.exe` — Standalone listen server
- `pse_client.exe` — Autonomous test client
- `pse_test_suite.exe` — 15-scenario automated verification test runner
- `pse_benchmark.exe` — Scalability, throughput, and latency benchmark runner

---

## 2. Running in the Three Supported Modes

### Mode A: Loopback (Localhost Development & Testing)
Used for automated verification and running all 8 participants on a single PC without internet or VPN:

#### Automated 8-Player Cluster:
```cmd
scripts\run_loopback.bat
```
*(Starts 1 Host and 7 autonomous client processes in separate console windows)*

#### Manual Commands:
```cmd
# Terminal 1: Host
pse_host.exe --bind 127.0.0.1 --port 27015 --max-clients 7 --tick-rate 60 --snapshot-rate 30

# Terminals 2..8: Clients (up to 7)
pse_client.exe --connect 127.0.0.1:27015 --id client_p2 --name Player_P2 --auto-input
pse_client.exe --connect 127.0.0.1:27015 --id client_p3 --name Player_P3 --auto-input
```

---

### Mode B: Physical LAN
Connect computers on the same local subnet (e.g. Wi-Fi or Ethernet router):

1. **Find Host LAN IPv4:**
   ```powershell
   (Get-NetIPAddress -InterfaceAlias 'WiFi' -AddressFamily IPv4).IPAddress
   # Example: 192.168.1.45
   ```
2. **On Host PC:**
   ```cmd
   pse_host.exe --bind 192.168.1.45 --port 27015 --max-clients 7
   ```
3. **On Client PCs:**
   ```cmd
   pse_client.exe --connect 192.168.1.45:27015 --id player_john --name John
   ```

---

### Mode C: Radmin VPN
Connect computers across the internet using the virtual LAN created by Radmin VPN.

> [!NOTE]
> On the current workstation, the Radmin VPN adapter was detected at IPv4 **`26.71.218.51`**.

#### Step-by-Step 2-Computer Radmin VPN Guide:

1. **Join the Same Radmin VPN Network:**
   - Both computers install [Radmin VPN](https://www.radmin-vpn.com/).
   - PC 1 creates a private network (e.g. `PowerSlave-Coop`, password `...`).
   - PC 2 joins that network.
   - Confirm both PCs see each other in the Radmin client window with a solid icon.

2. **Allow UDP Port 27015 in Windows Firewall (Host PC Only):**
   Run Command Prompt as Administrator on the Host PC:
   ```cmd
   netsh advfirewall firewall add rule name="PowerSlave Exhumed Together UDP" dir=in action=allow protocol=UDP localport=27015
   ```
   *(Alternatively, run `scripts\run_radmin.bat` and select Option 3).*

3. **Start Host on PC 1:**
   ```cmd
   pse_host.exe --bind 26.71.218.51 --port 27015 --max-clients 7
   ```

4. **Connect from Client on PC 2:**
   On PC 2, launch `pse_client.exe` targeting PC 1's Radmin IP:
   ```cmd
   pse_client.exe --connect 26.71.218.51:27015 --id my_laptop_client --name Teammate
   ```

---

## 3. Running Automated Tests & Benchmarks

Run the complete 15-scenario test suite and latency/bandwidth benchmarks:
```cmd
scripts\run_tests.bat
```

### Verified Test Scenarios (15 / 15 PASS):
1. **Connect All 7 Clients:** Host accepts 7 simultaneous connections (Total 8 participants).
2. **Player Slot Uniqueness:** Every participant receives an exclusive slot ($P_1 \dots P_8$).
3. **Input & Snapshot Transmission:** Client movement stream produces validated coordinate changes.
4. **Reliable Event Delivery:** `MAP_CHANGE` event broadcast and confirmed across all peers.
5. **Stale Snapshot Discard:** Out-of-order snapshots with older sequence numbers are discarded.
6. **Single Client Disconnect:** Graceful disconnect removes peer and frees slot state.
7. **Seamless Reconnect:** Rejoining with same `participantId` restores original slot with incremented generation.
8. **Death & Manual Respawn on 'E':** Player death, single-shot 'E' trigger, starter weapon reset (Machete + Pistol), and warp to start sector verified.
9. **Duplicate Respawn Rejection:** Spamming 'E' or requesting respawn while alive rejected cleanly.
10. **Map Epoch Advancement:** Synchronized level transition to new epoch across all 8 slots.
11. **Stale Epoch Rejection:** Packets from expired map epochs rejected by host.
12. **Malformed & Oversized Packet Immunity:** Truncated buffers, bad magic headers, and oversized payloads discarded safely without crash.
13. **Inbound Queue Bounds:** High-rate input flood clamped to maximum queue size (128 commands).
14. **Synthetic Network Impairments:** Session tolerates 40ms latency, 15ms jitter, 10% packet drop, and packet reordering without disconnects.
15. **Graceful Teardown:** Clean shutdown of all 8 nodes with zero resource leaks.

---

## 4. Measured Performance & Scalability Metrics

Actual measured metrics from local benchmark execution:

| Metric | 1 Client | 2 Clients | 4 Clients | 7 Clients (Max) |
|---|---|---|---|---|
| **Host Total Egress** | 4.40 KB/s | 12.39 KB/s | 36.91 KB/s | 93.42 KB/s |
| **Bandwidth Per Client** | 4.40 KB/s | 6.19 KB/s | 9.23 KB/s | 13.35 KB/s |
| **Host Packets/sec** | 30.8 p/s | 61.2 p/s | 116.4 p/s | 190.0 p/s |
| **Average Reconnect Time** | 85.7 ms | 83.1 ms | 84.6 ms | 84.0 ms |
| **Process RAM Working Set** | 7 MB | 7 MB | 8 MB | 8 MB |
| **Process CPU Utilization** | < 0.1% | < 0.1% | 0.1% | 0.1% |

*Under maximum load (8 players, 60Hz tick, 30Hz snapshots), total server egress is **< 100 KB/s** (< 1 Mbps), well within standard home broadband and VPN bandwidth limits.*

---

## 5. Directory Structure

```
tools/enet-prototype/
├── third_party/
│   ├── enet/             # Official ENet repository (tag v1.3.18, commit 2662c0de09e36f2a2030ccc2c528a3e4c9e8138a)
│   └── PROVENANCE.md     # Upstream source provenance & MIT license tracking
├── include/
│   ├── net_protocol.h    # Compact binary wire protocol, message schemas, and byte serialization
│   ├── player_state.h    # State machine, life cycle, and inventory loadout structures
│   ├── simulation.h      # Host-authoritative model world simulation
│   ├── enet_host.h       # Listen server session manager and client dispatch
│   ├── enet_client.h     # Client connection, input generation, and snapshot mirrors
│   ├── net_simulator.h   # In-app synthetic network condition impairment simulator
│   └── metrics.h         # RTT, bandwidth, queue depth, CPU, and RAM metrics collector
├── src/
│   ├── net_protocol.cpp  # String helpers and protocol validation
│   ├── simulation.cpp    # Model simulation logic, collision clamping, and manual respawn
│   ├── net_simulator.cpp # Latency, jitter, and packet drop implementations
│   ├── metrics.cpp       # Windows API performance calculations
│   ├── enet_host.cpp     # Host server loop, authentication, and snapshot broadcast
│   ├── enet_client.cpp   # Client networking loop, handshake, and commands
│   ├── host_main.cpp     # pse_host console application entry point
│   └── client_main.cpp   # pse_client console application entry point
├── tests/
│   ├── test_suite.cpp    # 15-scenario automated verification test runner
│   └── benchmark_metrics.cpp # Scalability and throughput benchmark runner
├── scripts/
│   ├── build.bat         # Reproducible CMake build script
│   ├── run_loopback.bat  # 1 Host + 7 Clients loopback launcher
│   ├── run_radmin.bat    # Radmin VPN automated launcher & firewall helper
│   └── run_tests.bat     # Test suite and benchmark batch runner
├── docs/
│   ├── WIRE_PROTOCOL.md  # Detailed binary packet specification
│   ├── INTEGRATION_INTERFACE.md # Native KEX simulation tick interface contract for Sol Max
│   ├── TEST_RESULTS.md   # Actual empirical benchmark outputs and scenario log
│   └── KNOWN_ISSUES.md   # Network caveats, MTU headroom, and relay fallback details
├── CMakeLists.txt        # Root CMake configuration (C++20, MSVC x64)
└── README.md             # This document
```
