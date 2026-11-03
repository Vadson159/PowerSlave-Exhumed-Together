# PowerSlave Exhumed Together — Presentation & Subsystems Handoff

**Document ID:** `DOC-14-PRESENTATION-HANDOFF`  
**Classification:** Master Handoff & Subsystems Integration Guide  
**Target Binary:** `anubis_Shipping_Steam_x64.exe` (SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)  
**Target Package:** `PowerslaveEX.kpf` (SHA-256 `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`)  
**Extracted Presentation Assets:** [`presentation/player_sprites/`](presentation/player_sprites/)  
**Scope:** Master synthesis of Cutscenes (`DOC-11`), Automap Allies (`DOC-12`), Player Sprites (`DOC-13`), 2015 Co-Op Systems (`DOC-15`), and Manual Respawn on [E] (`DOC-16`) into an actionable, prioritized roadmap for upcoming milestones (M1–M3).

---

## 1. Executive Summary & Separation of Tracks

Development for *PowerSlave Exhumed Together* proceeds along two strictly decoupled tracks:
1. **Track 1 — Core Gameplay & Native Lifecycle (M0):**
   - Active under Sol Max. Focuses on safe enemy-target node unlinking, bounded player retirement, and native Steam overlay isolation.
   - Preserves all 77 original game files and Steam achievements in a read-only state.
2. **Track 2 — Presentation, Visual Differentiation & Subsystems (This Investigation):**
   - Focuses on 3rd-person player visualization, automap ally tracking, cinematic synchronization, manual respawn architecture, campaign persistence (`.coop`), and HUD.
   - Per definitive user requirements:
     - **Voice Chat is completely excluded** (external Discord used).
     - **Spectator Mode is completely excluded** (no camera hopping or waiting for level transitions).
     - **Manual Respawn on [E] is adopted** with weapons reset to standard starting loadout upon death.

---

## 2. Master Feature Comparison Matrix

The following table evaluates each researched co-op subsystem by complexity, stability risk, milestone dependencies, and implementation priority:

| Subsystem / Feature | Complexity | Stability Risk | Engine Seam / Key Anchors | Milestone Dependencies | Implementation Priority | Recommended Owner |
|---|---|---|---|---|---|---|
| **Player Sprites & Recoloring** ([`DOC-13`](13_PLAYER_SPRITES.md)) | **Low** | **Very Low** | `Obj_Player_Puppet`, `ChangeAnim()`, `mods/coop_sprites.kpf` | **M0-C** (Puppet spawn) | **Priority 1 (M1-A)** | Claude Opus / Sol Max |
| **Automap Ally Arrows** ([`DOC-12`](12_AUTOMAP_ALLIES.md)) | **Low** | **Very Low** | `kexSlaveAutomapLocal::Draw` (`0x1401a2580`), `FUN_1401a34c0` | **M0-C** (Puppet positions) | **Priority 2 (M1-B)** | Claude Opus / Sol Max |
| **Manual Respawn on [E]** ([`DOC-16`](16_RESPAWN_DESIGN.md)) | **Medium** | **Low** | `RespawnRequest`/`Confirm`, Start sector spawn, Weapon reset | **M0-C** (Puppet lifecycle) | **Priority 3 (M1-C)** | Gameplay / Net Engineer |
| **Pre-rendered FMV Sync** ([`DOC-11`](11_CUTSCENES_COOP.md)) | **Low–Med** | **Low** | `kexMoviePlayerTheora::Play` (`0x1400D69B0`), `FUN_1401b6ec0` | **M2** (Network packets) | **Priority 4 (M2-A)** | Network Engineer |
| **In-Engine Cinematics** ([`DOC-11`](11_CUTSCENES_COOP.md)) | **Medium** | **Medium** | AngelScript `TOMB_script.txt`, Host trigger authority | **M2** (State broadcast) | **Priority 5 (M2-B)** | Gameplay Engineer |
| **Co-Op Sidecar Saves (`.coop`)** ([`DOC-15`](15_COOP_2015_FEATURES.md)) | **Medium** | **Very Low** | `profiles/coop_saves/save_NNN.coop`, individual loadouts | **M3** (Campaign persistence) | **Priority 6 (M3-A)** | Systems Engineer |
| **Late Join on Map Transition** ([`DOC-15`](15_COOP_2015_FEATURES.md)) | **Low–Med** | **Low** | `kexLevelManager::LoadMap`, Lobby waiting state | **M2** (Lobby sync) | **Priority 7 (M2-D)** | Network Engineer |
| **Scoreboard & Text Chat** ([`DOC-15`](15_COOP_2015_FEATURES.md)) | **Low** | **Very Low** | `kexFont::DrawString()`, Keys `\` and `T` | **M1-D** (HUD injection) | **Priority 8 (M1-E)** | UI Engineer |
| **Spectator Mode** ([`DOC-15`](15_COOP_2015_FEATURES.md)) | — | — | **EXCLUDED / REJECTED** (Superseded by Manual Respawn on [E]) | — | **None (Excluded)** | — |
| **Spatial Voice Chat** ([`DOC-15`](15_COOP_2015_FEATURES.md)) | — | — | **EXCLUDED COMPLETELY** (Players use external Discord) | — | **None (Excluded)** | — |

---

## 3. Detailed Subsystem Synthesis

### 3.1 Subsystem 1: 3rd-Person Player Sprites ([`DOC-13`](13_PLAYER_SPRITES.md))

- **Finding:** Stock 2022 release has zero 3rd-person player assets. Single-player is 100% first-person.
- **Asset Solution:** 2015 Co-Op Edition "John" sprite set (PC Exhumed rip by Dunkelschwamm).
- **Asset Breakdown:**
  - `john.png`: $1024 \times 315$ RGBA PNG, 40 sprite rects, 181 unique colors.
  - Animations: `john_idle` (sprites 20–24), `john_walk` (sprites 0–19), `john_shoot` (sprites 25–32), `john_death` (sprites 33–39).
  - 8-way directional rotation with horizontal flip optimization (`flip_horizontal_flag = 1`).
- **Recoloring Architecture:**
  - An 11-step olive camouflage luminance ramp: `(0, 16, 0)` up to `(112, 84, 52)`.
  - Remapping this ramp generates clean 8-player color variants while strictly protecting face/skin, weapon barrels, muzzle flashes, and death blood.
- **Delivered Artifacts in `docs/presentation/player_sprites/`:**
  - [`sprite_catalog_overview.png`](presentation/player_sprites/sprite_catalog_overview.png): Complete 40-frame visual catalog with pixel sizes.
  - 8 full demonstration sheets: [`demo_john_p1.png`](presentation/player_sprites/demo_john_p1.png) through [`demo_john_p8.png`](presentation/player_sprites/demo_john_p8.png).
  - Multi-pose comparisons: [`players_1_to_8_idle_comparison.png`](presentation/player_sprites/players_1_to_8_idle_comparison.png), [`players_1_to_8_back_comparison.png`](presentation/player_sprites/players_1_to_8_back_comparison.png), [`players_1_to_8_side_comparison.png`](presentation/player_sprites/players_1_to_8_side_comparison.png), [`players_1_to_8_walk_comparison.png`](presentation/player_sprites/players_1_to_8_walk_comparison.png), [`players_1_to_8_shoot_comparison.png`](presentation/player_sprites/players_1_to_8_shoot_comparison.png).
- **Redistribution Policy:** Package as companion mod `mods/coop_sprites.kpf`. Never embed proprietary bytes inside binaries.

### 3.2 Subsystem 2: Automap Teammate Markers ([`DOC-12`](12_AUTOMAP_ALLIES.md))

- **Finding:** Automap is encapsulated in singleton `kexSlaveAutomapLocal` (`DAT_140a43308`), vtable `0x140528820`.
- **Native Arrow Renderer:** `FUN_1401a34c0(this, mode, yaw, pos2D, radius, rgbaColor)`.
  - Template constant: `.rdata` at VA `0x1405287d0` (file offset `0x526dd0`, 18 floats, 3 line segments).
  - Default white buffer: `.rdata` at VA `0x1404d2a80` (file offset `0x4d1080`, `[255, 255, 255, 255]`).
- **Hook Seam:** Inside `kexSlaveAutomapLocal::Draw` at VA `0x1401a2580` immediately following line 521 (after the local player arrow finishes drawing).
- **8-Player Visual Rules:**
  - High-contrast colors: P1 White, P2 Cyan, P3 Green, P4 Yellow, P5 Magenta, P6 Orange, P7 Purple, P8 Mint.
  - Fallen teammates awaiting respawn [E]: Slate Grey `[128, 128, 128, 255]`.
  - Disconnected players: Skipped immediately (`connected == false`).
  - Relative height: 2D projection guarantees visibility; $|\Delta Z| > 256$ units scales radius ($1.25\times$ above, $0.75\times$ below).

### 3.3 Subsystem 3: Cooperative Cutscene Synchronization ([`DOC-11`](11_CUTSCENES_COOP.md))

- **Finding:**
  - Pre-rendered FMVs: 15 Ogg Theora `.ogv` files in `movies/`. Decoded via `kexMoviePlayerTheora::Play` (`0x1400D69B0`). Driven by engine states 6 and 7 in `FUN_1401b6ec0`.
  - In-engine cinematics: AngelScript level scripts (`TOMB_script.txt`, `TOMBEND_script.txt`). King Ramses sarcophagus opening, camera interpolation via `SetLockTime(8)` and `Lerp()`.
- **Hazards of Stock Single-Player:**
  - Singleton `Player()` binding manipulates Host when triggered by Client.
  - Unsynchronized triggers cause duplicate Ramses actor spawns.
- **Host-Authoritative Design:**
  - Triggers execute exclusively on the Host.
  - FMV playback: Host broadcasts `NET_MSG_PLAY_MOVIE`. Clients decode local language file (`intro-en` vs. `intro-de`). Client skip displays semi-transparent "Waiting for Host..." overlay; Host skip broadcasts `NET_MSG_STOP_MOVIE` to advance campaign state.
  - Scripted scenes: Host broadcasts `NET_EVENT_CINEMATIC_START` with target focus coordinate. Clients lock local input and smoothly rotate camera without executing redundant AngelScript threads. Respawn requests are delayed until cinematics complete.

### 3.4 Subsystem 4: Manual Respawn on [E] & Auxiliary Systems ([`DOC-16`](16_RESPAWN_DESIGN.md), [`DOC-15`](15_COOP_2015_FEATURES.md))

- **Manual Respawn on [E]:** Replaces 2015 spectator mode. On death, player enters `DEAD` state, movement/weapons are locked, and screen prompts «Нажмите E, чтобы возродиться».
- **No Mid-Level Waiting:** Player presses E $\implies$ host validates $\implies$ respawns player at current level's start spawn sector without reloading world state or waiting for level exit.
- **Weapons Reset on Death:** Acquired weapons are NOT preserved across death; loadout resets to standard starting baseline.
- **Save Sidecar Architecture (`.coop`):** Host writes individual player campaign progression to `profiles/coop_saves/save_NNN.coop`, preserving original campaign saves (`save_NNN.dat`) in a bit-exact read-only state.
- **Scoreboard & Chat:** HUD overlays bound to `\` and `T`. Voice chat is completely removed.

---

## 4. Minimum Future Runtime Experiments (Design Only — Do Not Execute Now)

When the development track transitions from M0 to M1, the following minimal, isolated runtime experiments should be conducted:

### Experiment PRE-01: Automap Arrow Draw Test
- **Objective:** Verify that calling `FUN_1401a34c0` with custom RGBA colors renders cleanly on the automap without graphical corruption or shader state leaks.
- **Procedure:**
  1. Set a hook on `kexSlaveAutomapLocal::Draw` (`0x1401a2580`).
  2. Immediately after original call, invoke `FUN_1401a34c0(this, mode, 0, dummyPos, 16.0f, cyanColor)`.
  3. Open TAB automap in single-player; verify dummy cyan arrow renders at $(X+100, Y+100)$.
- **Pass Criteria:** Second colored arrow appears on automap, rotates with dummy angle, and causes zero memory leaks or crashes upon closing automap.

### Experiment PRE-02: Third-Person Billboard Animation Cycling
- **Objective:** Verify that `Obj_Player_Puppet` correctly displays 8-way directional billboard rotations using the extracted John sprite sheets.
- **Procedure:**
  1. Mount `mods/coop_sprites.kpf`.
  2. Spawn dummy puppet at $(X+128, Y, Z)$ using `SpawnActor("Obj_Player_Puppet")`.
  3. Cycle animations: `john_idle` $\to$ `john_walk` $\to$ `john_shoot` $\to$ `john_death`.
  4. Walk around dummy puppet to inspect all 8 cardinal/diagonal angles and horizontal flip mirroring.
- **Pass Criteria:** Sprite renders seamlessly, rotates towards player camera, and transitions between animation states without texture tearing.

### Experiment PRE-03: Movie Player Remote Skip Seam
- **Objective:** Verify that calling `kexMoviePlayerTheora::Stop` transitions engine state 8 to game reset cleanly without leaving audio channels looping.
- **Procedure:**
  1. Trigger intro movie playback.
  2. Inject programmatic stop call via native vtable slot.
  3. Observe engine state transition in `FUN_1401b6ec0`.
- **Pass Criteria:** Movie stops cleanly, audio terminates without clicks, engine enters main level loop.

### Experiment PRE-04: Manual Respawn Handshake & Puppet Cleanup
- **Objective:** Verify that tearing down a dead puppet and re-spawning at start coordinates does not leak enemy target pointers or corrupt Player 0 state.
- **Procedure:**
  1. Simulate client death (`health = 0`).
  2. Intercept key 'E' $\implies$ dispatch local `RespawnRequest`.
  3. Host unlinks dead puppet from enemy target nodes, frees old puppet instance, and invokes `SpawnActor("Obj_Player_Puppet")` at start sector.
- **Pass Criteria:** Player regains control at spawn with standard starter weapons, no dangling pointer crashes on enemies targeting previous position.

---

## 5. Concrete Instructions for Next Developer (Sol Max / Claude Opus)

1. **Do Not Touch M0 Core Lifecycle:**  
   The presentation features documented here must remain completely dormant until Sol Max completes M0 acceptance (enemy-target node unlinking and Steam overlay isolation).
2. **Immediate M1 Steps (Presentation Milestone):**
   - Place the recolored sprite sheets (`demo_john_p1.png` .. `demo_john_p8.png`) into `mods/coop_sprites.kpf`.
   - Implement the `Hooked_kexSlaveAutomapLocal_Draw` wrapper defined in [`DOC-12`](12_AUTOMAP_ALLIES.md) to draw teammate arrows.
   - Bind `Obj_Player_Puppet` animations to secondary player puppet instances.
   - Implement local death screen prompt and E-key capture according to [`DOC-16`](16_RESPAWN_DESIGN.md).
3. **M2 Steps (Networking Milestone):**
   - Implement `RespawnRequest` / `RespawnConfirm` packets with `mapEpoch` validation.
   - Implement movie sync packets `0x30`–`0x33` defined in [`DOC-11`](11_CUTSCENES_COOP.md).
   - Wire late-join lobby waiting state and level transition synchronization.
4. **Preserve Workspace Integrity:**  
   Always run `python experiments/m0/validate_results.py` and `python docs/research/validate_handoff.py` before and after any changes. All 77 original game files and user saves must remain bit-exact.
