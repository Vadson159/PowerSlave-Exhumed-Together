# PowerSlave Exhumed Together — Cutscenes in Co-Op Architecture

**Document ID:** `DOC-11-CUTSCENES`  
**Classification:** Research, Engine Architecture & Co-Op Design  
**Target Binary:** `anubis_Shipping_Steam_x64.exe` (SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)  
**Target Package:** `PowerslaveEX.kpf` (SHA-256 `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`)  
**Scope:** Inventory of cutscenes, engine playback mechanisms, game-state transitions, AngelScript in-game cinematics, host authority, client synchronization, skip handling, edge cases, and future test designs.

---

## 1. Executive Summary

In *PowerSlave Exhumed* (2022), narrative sequences fall into two fundamentally different technical categories:
1. **Pre-rendered Full-Motion Video (FMV):** Ogg Theora (`.ogv`) streams decoded via hardware/software video pipeline (`kexMoviePlayerTheora`), invoked during engine state transitions in `FUN_1401b6ec0` (boot splashes, campaign intro crash, good ending, bad ending).
2. **In-Engine Scripted Sequences:** Interactive/cinematic sequences driven by AngelScript level scripts (`scripts/map/TOMB_script.txt`, `scripts/map/TOMBEND_script.txt`), controlling camera orientation, player input locks, weapon holstering, speech audio (`ramses/SP_*.wav`), phoneme mouth animations (`scripts/ramses/SP_*.txt`), and map sector geometry.

In a 2–8 player host-authoritative cooperative architecture (LAN / Radmin VPN), cutscenes must preserve individual player immersion without causing state desynchronization, duplicate actor spawns, or locking secondary players into unrecoverable camera or weapon-lock states.

---

## 2. Inventory of Narrative Assets & Cutscenes

### 2.1 Pre-rendered Video Files (FMV)

Located in the game root under `movies/`. All video files use the Ogg Theora container/codec format with Vorbis audio:

| File Name | Size (Bytes) | Audio Language | Narrative Purpose / Trigger Point |
|---|---|---|---|
| `NightDive.ogv` | 28,467,759 | Multi / FX | Publisher splash screen on boot. |
| `Kex.ogv` | 6,512,795 | Multi / FX | Engine intro logo on boot. |
| `throwback.ogv` | 2,460,032 | Multi / FX | Retro developer bumper. |
| `intro-en.ogv` | 25,013,810 | English | Campaign prologue / helicopter crash. |
| `intro-de.ogv` | 22,986,541 | German | Campaign prologue (localized). |
| `intro-fr.ogv` | 22,548,037 | French | Campaign prologue (localized). |
| `intro-es.ogv` | 22,604,219 | Spanish | Campaign prologue (localized). |
| `goodend-en.ogv` | 6,312,118 | English | Epilogue (All 23 Team Dolls collected, Quest Items complete). |
| `goodend-de.ogv` | 6,179,019 | German | Epilogue (German). |
| `goodend-fr.ogv` | 6,443,569 | French | Epilogue (French). |
| `goodend-es.ogv` | 6,128,516 | Spanish | Epilogue (Spanish). |
| `badend-en.ogv` | 6,977,092 | English | Standard Epilogue (Game completed without full secret artifacts). |
| `badend-de.ogv` | 6,522,437 | German | Standard Epilogue (German). |
| `badend-fr.ogv` | 6,567,629 | French | Standard Epilogue (French). |
| `badend-es.ogv` | 6,530,034 | Spanish | Standard Epilogue (Spanish). |

*Status:* `VERIFIED static` (directory inspection and binary string table xrefs).

### 2.2 In-Engine Scripted Sequences (King Ramses & Boss Transitions)

Defined inside `PowerslaveEX.kpf` via AngelScript scripts in `scripts/map/`:

| Script File | Size (Bytes) | Key Functions / Entities | Cinematic Behavior |
|---|---|---|---|
| `scripts/map/TOMB_script.txt` | 13,227 | `Obj_Ramses`, `$script 1`..`$script 8`, `EndRamsesEvent` | Ramses sarcophagus opens; camera locks and interpolates; mouth phoneme animation; audio dialogue; exit door opens. |
| `scripts/map/TOMBEND_script.txt` | 8,064 | `Obj_Ramses_Body_Rest`, `EndRamsesEvent`, `EndGame` | Final tomb scene: checking `Player().GetQuestItems() == 0xFF`. Screen quakes (`SetShakeTime`); transitions to good/bad ending. |
| `scripts/map/SETARENA_script.txt` | 1,408 | `SetArenaBoss` trigger | Boss encounter intro sequence, lock-in sectors. |
| `scripts/map/SELBUROW_script.txt` | 1,412 | `SelburowBoss` trigger | Selkis lair cutscene / gate closing. |
| `scripts/map/KILARENA_script.txt` | 761 | Kilmaat mother arena | Boss arena trigger, artifact verification (`PA_ALL`). |

---

## 3. Engine Playback Architecture

### 3.1 Video Playback Pipeline (`kexMoviePlayerTheora`)

*Binary Evidence:*
- Class Vtable: `0x1405154A0` (referenced at `PTR_vftable_140a40520`)
- `Play` Method: Virtual method at offset `+0x10` (`0x1400D69B0`)
- Texture decoders: 3 planar textures (`m_pTexture[0]` = Y, `m_pTexture[1]` = U, `m_pTexture[2]` = V) decoded into GPU shader sampler.
- Game Loop Caller: Inside `FUN_1401b6ec0` at cases 6 and 7:
  - Case 6: Invokes `Play` with formatted `"movies/badend-{}.avi"`.
  - Case 7: Invokes `Play` with formatted `"movies/goodend-{}.avi"`.
  - Filename formatter: Engine substitutes `{}` with user language code (`en`, `de`, `fr`, `es`) and resolves `.avi` to `.ogv`.
- State Transition: Case 6 and 7 transition the engine state variable to State 8 (`*(int*)(param_1 + 0x20) = 8`). State 8 manages frame-by-frame decoding, audio sync, and video fade.

### 3.2 In-Engine Scripted Sequence Pipeline (AngelScript)

Examining [`scripts/map/TOMB_script.txt`](S:/Games/SteamLibrary/steamapps/common/PowerSlave%20Exhumed/PowerslaveEX.kpf#scripts/map/TOMB_script.txt):

1. **Triggering Point:**
   ```c
   $script 1 {
       if (bScript1Triggered) return;
       bScript1Triggered = true;
       kexSlaveEvents::StopMusic();
       instigator.GetVelocity().Clear();
       if (bCasketOpened == false) {
           kexSlaveEvents::MoveScriptedSector(666, 320, 4, true);
           bCasketOpened = true;
       }
       Player().HoldsterWeapon();
       kexSlaveLevelScript::CallDelayed(2, instigator, 0);
       delay(0.5f);
       @ramses = ObjectFactory().SpawnActor("Obj_Ramses", ramsesOrigin.x, ramsesOrigin.y, ramsesOrigin.z, 0, 56);
   }
   ```
2. **Camera Lock & Orientation Interpolation:**
   ```c
   $script 2 {
       kVec3 dir;
       Player().SetLockTime(8); // Repeatedly locks movement & firing
       dir = (ramsesOrigin - instigator.GetOrigin()).Normalize();
       instigator.GetRotation().z = (dir.ToYaw() - instigator.GetRotation().z) * 0.0625f + instigator.GetRotation().z;
       instigator.GetRotation().x = (-dir.ToPitch() - instigator.GetRotation().x) * 0.0625f + instigator.GetRotation().x;
       instigator.GetOrigin().Lerp(kVec3(0.0f, 2816.0f, -864.0f), 0.0625f);
       $restart;
   }
   ```
3. **Phoneme Lip-Syncing:**
   `$script 7` executes at 60 Hz (`delay(1.0f / 60.0f); $restart;`), parsing bitmasks `(frame & 0x1, 0x2, 0x4, 0x8)` from `g_SP_ENGxx` / `g_SP_FRExx` / `g_SP_GERxx` / `g_SP_SPAxx` and invoking `ramses.SetFrameID(talkFrame);`.
4. **Interactive Skip Detection:**
   ```c
   $script 4 {
       if ((Player().GetButtons() & (BC_USE|BC_ATTACK|BC_JUMP)) != 0) {
           kexSlaveLevelScript::Halt(2);
           kexSlaveLevelScript::Halt(5);
           EndRamsesEvent(instigator);
           return;
       }
       $restart;
   }
   ```
5. **Clean Exit:**
   `EndRamsesEvent` makes Ramses disappear (`ramses.ChangeAnim("misc/ramses_vanish")`), stops voice lines, restarts background ambient music, and fires remote trigger tag `1020` to open the exit chamber.

---

## 4. Dependencies on Player & Global State

The native implementation relies heavily on singleton globals:
1. `Player()` singleton: calls `Player().HoldsterWeapon()`, `Player().SetLockTime(8)`, `Player().GetArtifacts()`, and `Player().GetButtons()`.
2. `instigator` actor handle: represents the specific player puppet that stepped across the sector trigger line.
3. Script-level state booleans: `bScript1Triggered`, `bCasketOpened`, `bPlayAlternateDialog`.

### Identified Co-Op Hazards:
- **Hazard 1 (Desynced Camera Lock):** If only the triggering player (`instigator`) gets rotated, secondary players (P2–P8) would remain free to walk around, shoot the casket, jump on King Ramses, or leave the sector.
- **Hazard 2 (Multiple Script Invocations):** If P1 and P2 cross the boundary simultaneously before `bScript1Triggered` is committed, duplicate instances of `Obj_Ramses` could spawn.
- **Hazard 3 (Singleton `Player()` Inconsistency):** In stock AngelScript bindings, `Player()` always returns Player 0 (Host). If P2 triggers the event, `Player().HoldsterWeapon()` holsters Host's weapon instead of P2's!

---

## 5. Cooperative Synchronization Architecture (Host-Authoritative)

### 5.1 Pre-rendered Videos (FMV) Protocol

Pre-rendered movies occur at known deterministic points:
1. Campaign Start (Intro movie)
2. Game Completion (Good / Bad Ending)

```
        Host (Server)                             Client (Peer)
              │                                         │
              │──── NET_MSG_PLAY_MOVIE(movie_id) ──────>│
              │                                         │ (Client loads & plays
              │ (Host loads & plays movie locally)      │  movie in local language)
              │                                         │
              │                                  [Client presses SKIP]
              │                                         │
              │                                   (Shows semi-transparent
              │                                    "Waiting for host..."
              │                                    overlay, keeps audio muted)
              │                                         │
        [Host presses SKIP or Movie Finishes]           │
              │                                         │
              │──── NET_MSG_STOP_MOVIE ────────────────>│
              │                                         │ (Client terminates video,
              │ (Transitions to Level 1 / Credits)      │  transitions to synced state)
              ▼                                         ▼
```

- **Language Autonomy:** Each player watches the movie in their local UI language (`intro-ru` / `intro-en` / `intro-de`), while timestamps and transitions are locked to the host.
- **Skip Policy:** Only the host has the authority to skip FMVs for everyone. If a client skips, their local playback pauses or shows a waiting spinner without interrupting others.

### 5.2 In-Engine Scripted Sequences Protocol

For AngelScript sequences (`TOMB.MAP`, `TOMBEND.MAP`, boss intros):
1. **Host-Only Trigger Execution:**
   - Map triggers only execute on the Host. Triggers are disabled on client instances.
   - When any puppet crosses a trigger sector, the host intercepts it and binds `instigator = triggererPuppet`.
2. **Broadcast Cinematic State:**
   The host sends a lightweight network message:
   `NET_EVENT_CINEMATIC_START`:
   - `sequence_id`: e.g. `CINEMATIC_RAMSES_TOMB`
   - `duration_sec`: float
   - `focus_origin`: `[x, y, z]` (`ramsesOrigin`)
   - `lock_inputs`: bool (`true`)
   - `holster_weapon`: bool (`true`)
3. **Client Local Reaction:**
   Upon receipt, each client:
   - Sets local `SetLockTime()` on their local controller.
   - Holsters weapon locally.
   - Smoothly lerps their local camera yaw/pitch towards `focus_origin`.
   - Ramses actor and lid animations are replicated via regular actor replication packets; client does not run duplicate AngelScript logic.
4. **Skip Consensus / Termination:**
   - When Host presses Skip, host triggers `EndRamsesEvent()`, broadcasting `NET_EVENT_CINEMATIC_END`.
   - Clients instantly restore weapon state and release input lock.

---

## 6. Edge Cases & Resilience

| Scenario | Risk | Mitigation |
|---|---|---|
| **Player dies during cutscene** | Dead player awaiting respawn [E] could cause lerp glitch. | Cutscene sectors in Tomb/Boss intros are strictly non-combat zones. Additionally, players inside a cinematic have invulnerability (`god` flag) set by host until `EndRamsesEvent`. Respawn requests are blocked until cinematic finishes. |
| **Client disconnects during cutscene** | Unreleased locks or missing packet could freeze remaining clients. | Client locks use a timeout countdown (`SetLockTime(8)` decrements each frame). If host packets cease for 2 seconds, client automatically breaks lock. |
| **Late joiner connects during cutscene** | Late joiner spawns inside sarcophagus or desynced. | Late joiners remain in a connected lobby state until the next level transition or until the current cinematic concludes. |
| **Host skips immediately** | Clients might glitch if packet arrives during initialization. | Host enforces a minimum 1.0s grace period before Skip input is registered (`CallDelayed(4, instigator, 1.0f)`). |
| **Varying client load times** | Slower PC begins playback late. | Host buffers state: level transition waits for `CLIENT_READY` confirmation before firing FMV or level start sequence. |

---

## 7. Implementation Checklist for Future Developers

- [ ] In M1/M2 network protocol, allocate packet IDs:
  - `0x30`: `NET_MSG_PLAY_MOVIE`
  - `0x31`: `NET_MSG_STOP_MOVIE`
  - `0x32`: `NET_EVENT_CINEMATIC_START`
  - `0x33`: `NET_EVENT_CINEMATIC_END`
- [ ] In `AngelScript` engine binding:
  - Expose `GetPlayerByPuppet(instigator)` or modify `Player()` to return the correct local player on clients and triggering player on host.
- [ ] Hook `kexMoviePlayerTheora::Play` (`0x1400D69B0`) to dispatch movie broadcast when host.
- [ ] Hook `kexMoviePlayerTheora::Stop` to dispatch completion broadcast.
