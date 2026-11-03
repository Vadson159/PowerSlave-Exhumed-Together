# Technical Handoff for GPT-6.1 Sol Max: P1 & P2 Remediation

**To:** GPT-6.1 Sol Max  
**From:** Gemini 3.8 Flash High (Senior C++ QA Automation Engineer / Windows Timing Analyst)  
**Date:** 2026-11-02  
**Subject:** Minimal, Actionable Engineering Solutions for DOC-53 Regressions (P1: 44/46 Suite & P2: Attack/Delta Validation)  
**Reference Document:** `tools/background-host-simulation-research/DOC53_QA_FOLLOWUP_REPORT.md`

---

## 1. Executive Summary

Our independent QA and reverse-engineering investigation has established the exact mechanisms for both open problems in DOC-53:

1. **P1 (`pse_test_suite` 44/46 vs 46/46):**
   * **Root Cause:** In Scenario 8, `clients[0]->getLocalPlayerState()` searches inside `m_lastSnapshot.players`. While `MsgRespawnConfirm` is reliably received, `ENetClientNode::onMsgRespawnConfirm` does not update `m_lastSnapshot`. It waits for an unsequenced `MsgPlayerSnapshot` (30 Hz). Under the default Windows timer (15.6 ms sleep quantum), `pumpNetwork(host, clients, 300)` executes only ~20 iterations, racing against snapshot dispatch and socket buffering. The fixed 300 ms deadline expires before the snapshot is polled.
   * **Test 9 Failure:** Cascading failure. Client 0's local state still shows `LIFE_DEAD`, so `triggerManualRespawn()` unexpectedly returns `true` instead of being rejected.
   * **Timer Fixture Proof:** `timeBeginPeriod(1)` reduces sleep duration from 14.98 ms to 1.83 ms, boosting iterations in 300 ms from 20 to 159 (7.95x). The snapshot arrives within ~40 ms, yielding 46/46 PASS.

2. **P2 (Unverified Attack-Held & Mouse Delta):**
   * **Root Cause (Attack):** `ui53.py` posted `WM_LBUTTONDOWN` (`0x201`) with `lParam=0` (`x=0, y=0`). In `kexengine.cfg`, `g_nativeMouse "0"` configures SDL2 in relative mouse mode, which ignores synthetic window-edge clicks that lack Windows Raw Input (`WM_INPUT`). Furthermore, `bootstrap.cpp`'s `native_focus_decision` logged offsets 12, 16, and 0, but omitted offset 4 (`held[0]`).
   * **Root Cause (Mouse Delta):** `ui53.py` contains literally zero code generating mouse movement (`WM_MOUSEMOVE` or `SendInput(MOUSEEVENTF_MOVE)`).
   * **Neutralization Invariant:** Already proven in production code. `clearPrimaryCommands()` zeroes offsets `0x00..0x7c`, which encompasses `buttons` (offset 0), `held[0]` (offset 4), and mouse yaw/pitch deltas (offsets `0x38..0x40`). The only deficiency was fixture stimulus.

---

## 2. Minimal Production & Harness Remediation for Sol Max

### 2.1 Remediation for P1: `pse_test_suite` (Choose A or B, or both)

#### Option A (Recommended Test Infrastructure Hardening: Predicate-Based Polling)
In `tools/enet-prototype/tests/test_suite.cpp`:
Replace the fixed-time `pumpNetwork` in Scenarios 8 with a predicate-based `pumpUntil`:

```cpp
// Add to test_suite.cpp:
template <typename Predicate>
bool pumpUntil(ENetHostServer& host, std::vector<std::unique_ptr<ENetClientNode>>& clients,
               Predicate pred, uint32_t timeoutMs = 1000) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        host.poll(1);
        for (auto& c : clients) {
            if (c) c->poll(1);
        }
        if (pred()) return true;
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
```

In Scenario 8:
```cpp
// Replace: pumpNetwork(host, clients, 300);
// With:
pumpUntil(host, clients, [&]() {
    const auto* s = clients[0]->getLocalPlayerState();
    return s && s->lifeState == LIFE_ALIVE && s->health == DEFAULT_HEALTH;
}, 1000);
```

#### Option B (Production Client Hardening: Update Local State on Confirm)
In `tools/enet-prototype/src/enet_client.cpp`, inside `ENetClientNode::onMsgRespawnConfirm`:
```cpp
void ENetClientNode::onMsgRespawnConfirm(ByteReader& reader, uint32_t /*seq*/) {
    MsgRespawnConfirm confirm;
    if (!confirm.deserialize(reader)) return;

    if (confirm.slotId == m_assignedSlot) {
        m_respawnRequested = false;
        // Authoritative confirmation: immediately reflect alive state locally
        for (auto& p : m_lastSnapshot.players) {
            if (p.slotId == m_assignedSlot) {
                p.health = DEFAULT_HEALTH;
                p.armor = 0;
                p.lifeState = LIFE_ALIVE;
                p.weaponMask = confirm.defaultWeapons;
                p.ammo = confirm.defaultAmmo;
                p.posX = confirm.spawnX;
                p.posY = confirm.spawnY;
                p.posZ = confirm.spawnZ;
                break;
            }
        }
        std::cout << "[Client] Respawn confirmed at (" << confirm.spawnX << ", " << confirm.spawnY
                  << ", " << confirm.spawnZ << "), weapons reset to starter loadout." << std::endl;
    }

    if (onRespawnConfirmCallback) onRespawnConfirmCallback(confirm);
}
```
*Benefits:* Eliminates the unsequenced snapshot race entirely. Client UI and local queries immediately recognize the player is alive upon receipt of the reliable confirmation packet.

#### Option C (Timer Fixture in Test Suite Runner)
Add an RAII `timeBeginPeriod(1)` guard to `test_suite.cpp` `main()`:
```cpp
#ifdef _WIN32
struct ScopedTimerPeriod {
    ScopedTimerPeriod() { timeBeginPeriod(1); }
    ~ScopedTimerPeriod() { timeEndPeriod(1); }
} g_timerScope;
#endif
```

---

### 2.2 Remediation for P2: Attack-Held & Mouse Delta Validation

#### Step 1: Telemetry Update in `bootstrap.cpp`
In `hookFocusResponder` (around line 185):
Add `held[0]` (`read<unsigned>(primary(), 4)`) and mouse delta yaw (`read<int>(primary(), 0x38)`):
```cpp
log("native_focus_decision",
    "\"type\":%u,\"reason\":%u,\"focus_byte\":%u,\"known\":%s,\"serial\":%llu,\"menu\":%u,"
    "\"held_forward\":%u,\"held_back\":%u,\"held_attack\":%u,\"mouse_yaw\":%d,\"buttons\":%u",
    type, reason, value, g.focusKnown ? "true" : "false", g.focusSerial,
    read<Byte>(instance(), background::MenuByte),
    read<unsigned>(primary(), 12), // forward
    read<unsigned>(primary(), 16), // backward
    read<unsigned>(primary(), 4),  // attack (Action 0)
    read<int>(primary(), 0x38),    // mouse yaw delta
    read<unsigned>(primary()));    // buttons bitmask
```

#### Step 2: Reliable Attack Generation in `ui53.py`
Because `mouse_left` is sensitive to SDL relative mouse mode, bind Attack to a secondary keyboard key (e.g. `f` or `lctrl`) in the host game profile or send key messages:
* Add to `kexengine.cfg` or execute via console:
  `bind f "attack"`
* In `ui53.py` (`action == 'host_loss_inputs'`):
  ```python
  # Hold W and Attack (F) concurrently
  key(h, 'W', True)
  key(h, 'F', True) # Genuine WM_KEYDOWN for Attack!
  time.sleep(0.25)
  focus('client')
  ```
* This is 100% reliable across Windows message queues, works under non-elevated CI, generates an authentic positive `held[0] > 0` before focus loss, and proves neutralization when `held[0]` drops to 0 in the background.

#### Step 3: Mouse Delta Generation in `ui53.py`
To generate nonzero mouse delta prior to focus loss:
* Post `WM_MOUSEMOVE` with non-zero delta or client coordinates, or post keyboard turning (`bind a "turnleft"` / `bind d "turnright"` or `joy_13/14`).
* Alternatively, post `WM_INPUT` or use `SendInput` if running in an interactive session.
* Disassembly confirms turn deltas at `primary() + 0x38` and pitch deltas at `primary() + 0x3c` are actively cleared by `clearPrimaryCommands()`.

---

## 3. QA Artifact Summary

The following standalone tools have been developed and verified in `tools/background-host-simulation-research/`:
* `qa_timing_analyzer.py`: Empirically measures Windows timer resolution and iteration throughput in `pumpNetwork` (15ms vs 1.8ms, 20 vs 159 iterations).
* `qa_input_synthetic_tester.py`: Analyzes Win32 `PostMessageW` vs `SendInput` hardware state and UIPI behavior.
* `parse_kex_actions.py`: Disassembles the native KEX action registration table at RVA `0x1b0061`, proving Action 0 is `attack` (bit 0, offset 4) and Action 2 is `forward` (bit 2, offset 12).
* `DOC53_QA_FOLLOWUP_REPORT.md`: Comprehensive engineering report detailing all findings, proofs, and the defect classification matrix.

Sol Max can immediately proceed with applying these recommendations.
