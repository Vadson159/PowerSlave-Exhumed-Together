# PowerSlave Exhumed Together — ENet Field Test Kit

**Package ID:** `ENET-FIELD-TEST-KIT-v1.1`  
**Architecture:** Windows x64 (MSVC v143, statically linked C/C++ runtime `/MT`)  
**Network Engine:** Official ENet v1.3.18  
**Scope:** Two-computer live connectivity testing via Radmin VPN or physical LAN.

---

## 1. Package Contents

| File / Folder | Role | Description |
|---|---|---|
| `bin/pse_host.exe` | Host Server | Authoritative listen server CLI with discrete fixed-tick input scheduling. |
| `bin/pse_client.exe` | Client Node | Client transport node with monotonic snapshot receiver and input stream. |
| `start_host.bat` | Launcher | One-click host launcher. Auto-detects Radmin VPN IP, checks firewall, logs session. |
| `start_client.bat` | Launcher | One-click client launcher. Prompts for Host 26.x.y.z IP, verifies ICMP ping, logs session. |
| `diagnose_network.bat` | Diagnostic | Read-only network diagnostics (Radmin adapter, IP, routing, firewall rules). |
| `collect_results.bat` | Telemetry Collector | Sanitizes session logs (strips usernames, MACs) and creates clean ZIP archive. |
| `scripts/*.ps1` | Automation | Underlying PowerShell engine powering the launchers and diagnostics. |
| `ИНСТРУКЦИЯ_ДЛЯ_ДРУГА.txt` | Guide | Simple non-technical step-by-step instructions in Russian for the remote participant. |
| `logs/` | Log Directory | Stores timestamped session logs (`host_*.log`, `client_*.log`). |

---

## 2. System Prerequisites

1. **Operating System:** Windows 10 or Windows 11 (x64).
2. **Runtime Dependencies:** Zero. Executables are statically compiled with `/MT` (no Visual C++ Redistributable required).
3. **Network Transport:** Radmin VPN (v1.4+ recommended, free from [radmin-vpn.com](https://www.radmin-vpn.com/)).
4. **Port:** UDP 27015 (inbound on Host only; clients use outbound ephemeral ports).

---

## 3. Host Step-by-Step Procedure

1. Launch Radmin VPN and ensure it is turned **ON** (green indicator).
2. Create or join a private Radmin network room (e.g. `PowerSlave_Coop_Room`).
3. Note your virtual IPv4 address in Radmin VPN (starts with `26.`, e.g. `26.71.218.51`).
4. Double-click `start_host.bat`.
5. The launcher will automatically bind to your Radmin VPN interface and print:
   ```
   ---------------------------------------------------------
     HOST IP ADDRESS : 26.71.218.51
     UDP PORT        : 27015
     --> GIVE THIS IP TO YOUR FRIEND: 26.71.218.51
   ---------------------------------------------------------
   ```
6. If Windows Defender Firewall does not have a rule for UDP 27015, the script will ask for your confirmation before adding one.
7. Send your Radmin IP (`26.71.218.51`) to your friend.
8. Interactive host console commands:
   - `status` — View all connected participants, slots, generation counters, and coordinates.
   - `map <name>` — Trigger a synchronized level transition (e.g. `map TEMPLE_OF_RAMSES`).
   - `kill <slot>` — Inflict lethal damage on a slot to test manual respawn (e.g. `kill 2`).
   - `stat` — Inspect ingress/egress bytes, packet counts, CPU utilization, and RAM working set.
   - `quit` — Gracefully disconnect all clients and shut down host.

---

## 4. Remote Client Step-by-Step Procedure

1. Join the same Radmin VPN room as the host.
2. Verify that the host computer displays a green circle in the Radmin VPN peer list.
3. Double-click `start_client.bat`.
4. Enter the Host IP provided by your friend (e.g. `26.71.218.51`).
5. Enter your player name (e.g. `Denis`).
6. Upon connection, the console confirms:
   ```
   [Client] Authenticated! Slot: P2, Gen: 1, Map: KARNAK_SANCTUARY (Epoch 1)
   ```
7. Interactive client commands:
   - `status` — Check local player state (HP, Armor, Weapon, Coordinates) and active entities.
   - `move <fwd> <strafe>` — Inject movement command (e.g. `move 1 0` for forward).
   - `shoot` — Fire active weapon (decrements pistol ammo from 50).
   - `stat` — Print real-time RTT metrics (avg, p50, p95, p99), packets sent/received, bandwidth.
   - `e` or `respawn` — When dead, request manual respawn (resets to starter loadout).
   - `reconnect` — Re-authenticate into the same slot (advances generation, verifies secret key).
   - `quit` — Gracefully disconnect.

---

## 5. Result Collection & Post-Test Reporting

When testing concludes:
1. Double-click `collect_results.bat`.
2. The utility scans `logs/`, redacts any private user paths and network MAC addresses, and bundles all session logs into a compressed archive:
   `field_test_results_YYYYMMDD_HHMMSS.zip`
3. Share this archive with the development team for telemetry analysis.
