# PowerSlave Exhumed Together — Prototype Limitations & Known Issues

**Document Version:** 1.00  
**Scope:** Operating constraints, network edge cases, and known limitations of the standalone ENet prototype.

---

## 1. Network & Transport Caveats

### 1.1 Radmin VPN: Direct UDP vs Relay/TCP Mode
- **Direct P2P UDP (Preferred):** Under standard conditions, Radmin VPN establishes direct peer-to-peer virtual UDP encapsulation between participants. In our benchmarks, direct connectivity yields minimal latency overhead ($\approx 10\dots 25\text{ ms}$ over physical ping) with negligible packet drop.
- **Relay / Proxy Fallback (Degraded):** If both peers are behind strict enterprise symmetric NATs or restrictive cellular firewalls, Radmin VPN falls back to routing traffic through its central relay servers over TCP.
  - *Symptom:* Ping spikes to $120\dots 250\text{ ms}$, snapshot jitter increases.
  - *Recommendation:* If high latency is observed between two Radmin machines, verify that both peers show "Direct Connection" (green indicator with direct connection icon in the Radmin VPN client).

### 1.2 Windows Firewall Inbound Rules
- Windows Firewall on Windows 10/11 defaults to dropping inbound UDP datagrams on untrusted or public network interfaces.
- **Requirement:** The host machine must allow inbound UDP traffic on port `27015`. A helper command is provided in `scripts/run_radmin.bat` or run:
  ```cmd
  netsh advfirewall firewall add rule name="PowerSlave Exhumed Together UDP" dir=in action=allow protocol=UDP localport=27015
  ```
- Clients connecting to a remote host do not require inbound firewall rules (outbound NAT state is created automatically).

### 1.3 Unencrypted Payloads
- ENet operates over standard UDP without DTLS or transport-layer encryption.
- Join tokens and player coordinates are transmitted in binary plaintext.
- This is intentional: for private LAN and virtual VPN gaming sessions, avoiding DTLS handshake delays and cryptographic CPU overhead preserves sub-millisecond packet processing times.
- Host authenticity and slot integrity are protected at application level via 64-bit cryptographic random tokens (`reconnectKey`) generated via Windows CNG `BCryptGenRandom`.

### 1.4 Loopback / Localhost vs Real Network Operational Divergence
Automated test suites and IPC benchmarks on `127.0.0.1` execute via the Windows Loopback Pseudo-Interface, which bypasses physical network hardware, NIC drivers, and OS packet framing queues. In real two-computer testing across Radmin VPN:
1. **Latency & Jitter:** RTT increases from sub-millisecond loopback (~0–1 ms) to real-world latency (~15–45 ms direct P2P, or 100–250 ms in relay mode).
2. **Jitter Queue Bursts:** Packets delayed by physical Wi-Fi or routing hops arrive in bursts. The prototype's hardened input scheduler decouples network arrivals from fixed simulation steps (consuming 1 step per simulation tick), strictly preventing accelerated movement or speed-bursts.
3. **Tunnel Overhead:** Real wire throughput includes Radmin VPN tunneling headers, TAP adapter framing, and TCP encapsulation (in relay mode), which are omitted on loopback sockets.
4. **Packet Loss:** Loopback packet drop is strictly 0% (unless synthetically induced by `NetworkSimulator`), whereas real virtual tunnels encounter real-world burst packet loss and jitter.

---

## 2. Protocol & State Machine Boundaries

### 2.1 Unreliable Snapshot MTU Headroom
- For 8 active participants (Host P1 + 7 Clients), a complete `MsgPlayerSnapshot` measures:
  $$11\text{ (header)} + 12\text{ (fields)} + 8 \times 32\text{ (player snapshots)} \approx 279\text{ bytes}$$
- This is far below the typical internet MTU threshold ($1400\text{ bytes}$), preventing IP packet fragmentation.
- *Future Consideration:* If monster/entity snapshots are added in Milestone M3, differential delta compression or spatial entity culling should be used to keep snapshots under $1200\text{ bytes}$.

### 2.2 Client-Side Prediction Clamping
- The standalone client prototype currently visualizes confirmed authoritative snapshots from the host.
- Full local client movement prediction and hermite spline interpolation between snapshots will be hooked into the KEX rendering loop during Milestone M2.

---

## 3. Disconnect & Heartbeat Timings

- **Graceful Quit (`LEAVE_USER_QUIT`):** Instantaneous slot retirement and generation advancement ($< 5\text{ ms}$).
- **Abrupt Cable Pull / Crash (`LEAVE_TIMEOUT`):** ENet peer timeout detects connection loss after $3000\text{ ms}$ of unacknowledged heartbeats. During this 3-second window, the host holds the participant's slot in disconnected state, allowing instant seamless reconnection.
