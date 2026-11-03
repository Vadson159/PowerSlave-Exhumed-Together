# PowerSlave Exhumed Together — M1.2a Test Client Report

**Target:** `127.0.0.1:28715`  
**Participant ID:** `m12a_test_p2`  
**Wire Protocol:** v1.10 (`0x0110`, Magic: `0x50534558`)  
**Execution Timestamp:** 2026-11-01 23:38:04 UTC  
**Total Elapsed Time:** 1610.9 ms  
**Overall Verifiable Result:** **PASS** (15/15 passed, 4 deferred to native KEX host)  

## 1. Verifiable Network Protocol Contracts

| ID | Check | Status | Elapsed | Observations |
|---|---|---|---|---|
| **NET-01** | ENet transport connection | **PASS** | 5.7 ms | Socket connected to 127.0.0.1:28715 |
| **NET-02** | Wire Protocol v1.10 Handshake | **PASS** | 5.7 ms | MsgHello -> MsgWelcome exchange successful (version=0x0110) |
| **NET-03** | Slot assignment & validation | **PASS** | 0.0 ms | Assigned slot P2 (id=1), map=KARNAK_SANCTUARY, sessionId=619488, reconnectKey=0x21b22467f33cae0 |
| **NET-04** | Initial connectionGeneration check | **PASS** | 0.0 ms | gen=1 as required for new session |
| **NET-06** | Forward movement stream (Hold W) [Local Dispatch] | **PASS** | 541.4 ms | Dispatched 30 commands with moveForward=1.0f at 60Hz into local ENet queue (proves local transmission; does not prove lossy UDP delivery or KEX motion) |
| **NET-07** | Explicit W-release packet [Local Dispatch] | **PASS** | 1.1 ms | Explicit transition moveForward=1.0f -> 0.0f dispatched into local ENet queue |
| **NET-08** | Neutral input stream [Local Dispatch] | **PASS** | 268.7 ms | Dispatched 16 neutral commands into local ENet queue |
| **NET-05** | Input sequence monotonicity | **PASS** | 0.0 ms | Strictly increasing sequence numbers 1..46 (zero gaps, zero reordering) |
| **NET-09** | Clean session disconnect | **PASS** | 1.7 ms | Dispatched LEAVE_USER_QUIT and destroyed peer cleanly |
| **NET-10** | Reconnect handshake | **PASS** | 5.1 ms | Reconnection authenticated using cryptographic key=0x1229772792238916946 |
| **NET-11** | Reconnection slot consistency | **PASS** | 0.0 ms | Slot preserved across reconnect: P3 == P3 |
| **NET-12** | Generational bump on reconnect | **PASS** | 0.0 ms | connectionGeneration incremented: gen 1 -> 2 |
| **NET-13** | Prevention of stale session state reuse | **PASS** | 0.0 ms | All 16 commands in reconnected session used new gen=2; zero commands carried stale gen=1 |
| **NET-14** | Anti-hijack defense gate | **PASS** | 5.2 ms | Forged reconnectKey rejected by host: code 8 (Session Hijack Attempt / Invalid) |
| **NET-15** | Robustness & socket stability | **PASS** | 0.0 ms | Completed all automated scenarios with zero unhandled exceptions, zero socket hangs, and zero crashes |

## 2. Conditions Unverifiable Without Native KEX DLL

> [!NOTE]
> Packet transmission and protocol conformance do not prove that P2 Puppet moved inside KEX.
> The conditions below can only be verified after GPT-6.1 Sol Max connects the native DLL adapter to ENet.

| ID | Condition | Status | Reason & Future Verification Gate |
|---|---|---|---|
| **KEX-01** | Native P2 Puppet Actor instantiation | **DEFERRED** | Requires native KEX Factory::SpawnActor (type 0) executed on host simulation thread. Deferred to Sol Max M1.2a. |
| **KEX-02** | Native P2 physics displacement in world | **DEFERRED** | Packet transmission does not equal engine displacement; verified native movement requires KEX physics tick. Deferred to Sol Max M1.2a. |
| **KEX-03** | Native P1 state isolation & non-interference | **DEFERRED** | Requires live host GameInstance P1 vs P2 memory comparison. Deferred to Sol Max M1.2a. |
| **KEX-04** | Safe native engine StateExit and process shutdown | **DEFERRED** | Requires observing host KEX platform shutdown sequence. Deferred to Sol Max M1.2a. |

## 3. Telemetry and Methodology

> [!IMPORTANT]
> **Local Transmission Dispatch vs Guaranteed Delivery & KEX Movement:** Checks NET-06, NET-07, and NET-08 verify that input packets were successfully formatted and transmitted into the local ENet socket queue (`enet_peer_send`). Over lossy UDP (`CHANNEL_UNRELIABLE_SEQUENCED`), network packet drop means outbound transmission count does not guarantee host reception count. Furthermore, host reception does not constitute proof of native KEX physics displacement or actor rendering, which remain deferred under KEX-01..KEX-04.

- Test client uses `pse::ENetClientNode` and Wire Protocol v1.10 directly (`net_protocol.h`).
- Handshake exchanges `MsgHello` -> `MsgWelcome`, authenticating the slot, sessionId, and cryptographic reconnectKey.
- Automated scenario: 60Hz input stream with `moveForward = 1.0f`, followed by an explicit `moveForward = 0.0f` release packet and neutral input.
- Reconnect scenario: verifies that disconnect clears session state, and reconnection increments `connectionGeneration` without reusing stale state.
