# PowerSlave Exhumed Together — M1.2a Test Client Report

**Target:** `127.0.0.1:28809`  
**Participant ID:** `m12a_test_p2`  
**Wire Protocol:** v1.10 (`0x0110`, Magic: `0x50534558`)  
**Execution Timestamp:** 2026-11-01 23:38:14 UTC  
**Total Elapsed Time:** 0.0 ms  
**Overall Verifiable Result:** **FAIL** (0/1 passed, 0 deferred to native KEX host)  

## 1. Verifiable Network Protocol Contracts

| ID | Check | Status | Elapsed | Observations |
|---|---|---|---|---|
| **NET-14** | Anti-hijack defense gate | **FAIL** | 7.0 ms | CRITICAL HIJACK DEFECT: Host allowed session hijack with forged key! Echoed forged key (0xDEADBEEFCAFE). Resumed generation=2 (> 1). Assigned existing slot P2. |

## 2. Conditions Unverifiable Without Native KEX DLL

> [!NOTE]
> Packet transmission and protocol conformance do not prove that P2 Puppet moved inside KEX.
> The conditions below can only be verified after GPT-6.1 Sol Max connects the native DLL adapter to ENet.

| ID | Condition | Status | Reason & Future Verification Gate |
|---|---|---|---|

## 3. Telemetry and Methodology

> [!IMPORTANT]
> **Local Transmission Dispatch vs Guaranteed Delivery & KEX Movement:** Checks NET-06, NET-07, and NET-08 verify that input packets were successfully formatted and transmitted into the local ENet socket queue (`enet_peer_send`). Over lossy UDP (`CHANNEL_UNRELIABLE_SEQUENCED`), network packet drop means outbound transmission count does not guarantee host reception count. Furthermore, host reception does not constitute proof of native KEX physics displacement or actor rendering, which remain deferred under KEX-01..KEX-04.

- Test client uses `pse::ENetClientNode` and Wire Protocol v1.10 directly (`net_protocol.h`).
- Handshake exchanges `MsgHello` -> `MsgWelcome`, authenticating the slot, sessionId, and cryptographic reconnectKey.
- Automated scenario: 60Hz input stream with `moveForward = 1.0f`, followed by an explicit `moveForward = 0.0f` release packet and neutral input.
- Reconnect scenario: verifies that disconnect clears session state, and reconnection increments `connectionGeneration` without reusing stale state.
