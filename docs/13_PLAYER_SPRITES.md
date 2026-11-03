# PowerSlave Exhumed Together — Player Sprites & Visual Differentiation

**Document ID:** `DOC-13-PLAYER-SPRITES`  
**Classification:** Art Assets, Engine Integration & Shader Research  
**Target Binary:** `anubis_Shipping_Steam_x64.exe` (SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`)  
**Target Package:** `PowerslaveEX.kpf` (SHA-256 `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`)  
**Extracted Assets Directory:** [`docs/presentation/player_sprites/`](presentation/player_sprites/)  
**Visual Catalog:** [`sprite_catalog_overview.png`](presentation/player_sprites/sprite_catalog_overview.png)  
**Scope:** Asset inventory, lack of 3rd-person player in stock 2022 release, analysis of 2015 `john.kpf`, color palette ramp analysis, 8-player visual differentiation strategy, generated demonstration variants, engine billboard integration, legal provenance.

---

## 1. Executive Summary

In *PowerSlave Exhumed* (2022), the single-player campaign is exclusively first-person. An exhaustive static audit of all 2,194 assets packaged inside `PowerslaveEX.kpf` establishes that **no third-person player character model, mesh, or billboard sprite sheet exists anywhere in the stock game**.

To enable cooperative multiplayer where 2 to 8 teammates visually recognize one another while maintaining the authentic 1996 aesthetic, external 3rd-person billboard sprite assets must be introduced. The community-standard solution—first implemented in the 2015 *PowerSlave EX Co-Op Edition* via `mods/john.kpf`—utilizes the classic PC *Exhumed* player sprite ("John") ripped by Dunkelschwamm for The Spriters Resource.

This document presents:
1. An exhaustive technical analysis of the sprite geometry, animation definitions, and texture dimensions.
2. A quantitative decomposition of the 181 unique RGBA colors in the sprite sheet.
3. A comparative evaluation of separate sprite sheets vs. palette swaps vs. fragment shader runtime tinting.
4. A complete visual catalog of all 40 frames and generated demonstration sheets for 8 player slots in Egyptian-themed color palettes.
5. Legal provenance and copyright guidelines for distribution.

---

## 2. Asset Audit: PowerSlave Exhumed 2022 vs 2015 Co-Op

### 2.1 Audit of Stock 2022 Assets (`PowerslaveEX.kpf`)

A complete scan of `PowerslaveEX.kpf` (file count: 2,194 members) yielded:
- `sprites/weapons/`: Contains viewmodels for all 8 first-person weapons (`machete`, `pistol`, `m60`, `flamethrower`, `grenade`, `cobrastaff`, `ring`, `manacle`).
- `sprites/monsters/`: Contains 8-way directional billboard sprites for all enemy classes (`anubis`, `bastet`, `mummy`, `magmantis`, `kilmaat`, `set`, `ramses`, `wasp`, etc.).
- `sprites/actors/`: World interactive objects (torches, switches, key amulets, power-up vessels, transmitter parts).
- **Finding:** Zero 3rd-person player assets exist. The single-player engine exclusively renders the local player's floating hands and active weapon viewmodel.

*Status:* `VERIFIED static` (archive member hash scan in `E007_KPF_INVENTORY.json`).

### 2.2 Analysis of 2015 Co-Op Asset Package (`mods/john.kpf`)

The reference co-op mod `PowerSlave_EX_-_CO-OP_Edition` includes `mods/john.kpf` (156,436 bytes), which has been extracted and archived in [`docs/presentation/player_sprites/`](presentation/player_sprites/):

| File Name | Format | Dimensions / Size | Technical Role |
|---|---|---|---|
| `john.png` | 32-bit RGBA PNG | $1024 \times 315$ px (155,010 bytes) | Master sprite sheet containing 40 poses of mercenary protagonist "John". |
| `john_sprite.txt` | KEX Sprite Script | 40 sprite rects (1,694 bytes) | Maps sprite IDs `0` through `39` to pixel bounding boxes $[x, y, w, h]$ on `john.png`. |
| `john_anim.txt` | KEX Anim Script | 4 animation sequences (17,079 bytes) | Frame timings, 8-way directional rotations, origin offsets, and horizontal flipping. |
| `metadata.json` | JSON Specification | 612 bytes | Provenance metadata, dimensions, checksums, and reference mappings. |

---

## 3. Visual Catalog & Frame Inventory

A high-resolution visual catalog displaying all 40 frames arranged in a structured grid with bounding box dimensions and animation classification has been generated:
- Catalog Image: [`sprite_catalog_overview.png`](presentation/player_sprites/sprite_catalog_overview.png)

### 3.1 Frame Classification & Bounding Box Table

| Sprite ID | Animation Set | Direction / View | Bounding Box $[x, y, w, h]$ | Visual Description |
|---|---|---|---|---|
| **00** | `john_walk` | North (Away) | `[0, 0, 42, 100]` | Walk Cycle 1: Back view left leg forward |
| **01** | `john_walk` | North (Away) | `[43, 0, 44, 101]` | Walk Cycle 1: Back view right leg forward |
| **02** | `john_walk` | North (Away) | `[88, 0, 43, 98]` | Walk Cycle 2: Back view stride |
| **03** | `john_walk` | North (Away) | `[132, 0, 45, 100]` | Walk Cycle 2: Back view follow-through |
| **04** | `john_walk` | North-East / North-West | `[178, 0, 52, 98]` | Diagonal back walk step 1 |
| **05** | `john_walk` | North-East / North-West | `[231, 0, 54, 100]` | Diagonal back walk step 2 |
| **06** | `john_walk` | North-East / North-West | `[286, 0, 75, 97]` | Diagonal back walk stride |
| **07** | `john_walk` | North-East / North-West | `[362, 0, 56, 100]` | Diagonal back walk follow-through |
| **08** | `john_walk` | East / West Profile | `[419, 0, 90, 95]` | Side profile walk stride 1 |
| **09** | `john_walk` | East / West Profile | `[510, 0, 77, 97]` | Side profile walk passing position |
| **10** | `john_walk` | East / West Profile | `[588, 0, 94, 92]` | Side profile walk stride 2 |
| **11** | `john_walk` | East / West Profile | `[683, 0, 68, 100]` | Side profile walk recovery |
| **12** | `john_walk` | South-East / South-West | `[752, 0, 87, 92]` | Diagonal front walk step 1 |
| **13** | `john_walk` | South-East / South-West | `[840, 0, 70, 94]` | Diagonal front walk step 2 |
| **14** | `john_walk` | South-East / South-West | `[911, 0, 85, 90]` | Diagonal front walk stride |
| **15** | `john_walk` | South-East / South-West | `[0, 102, 69, 97]` | Diagonal front walk recovery |
| **16** | `john_walk` | South (Towards Camera) | `[70, 102, 42, 95]` | Front walk left foot forward |
| **17** | `john_walk` | South (Towards Camera) | `[113, 102, 41, 97]` | Front walk right foot forward |
| **18** | `john_walk` | South (Towards Camera) | `[155, 102, 46, 94]` | Front walk passing |
| **19** | `john_walk` | South (Towards Camera) | `[202, 102, 43, 96]` | Front walk recovery |
| **20** | `john_idle` | North (Away) | `[246, 102, 44, 100]` | Idle standing breathing, back view |
| **21** | `john_idle` | North-East / North-West | `[291, 102, 60, 100]` | Idle standing diagonal back view |
| **22** | `john_idle` | East / West Profile | `[352, 102, 63, 99]` | Idle standing side profile |
| **23** | `john_idle` | South-East / South-West | `[416, 102, 54, 100]` | Idle standing diagonal front view |
| **24** | `john_idle` | South (Towards Camera) | `[471, 102, 59, 99]` | Idle standing front view |
| **25** | `john_shoot` | North (Away) | `[531, 102, 43, 101]` | Firing pose back view |
| **26** | `john_shoot` | North-East / North-West | `[575, 102, 42, 100]` | Firing pose diagonal back view |
| **27** | `john_shoot` | East Profile | `[618, 102, 56, 96]` | Firing pose side view frame 1 |
| **28** | `john_shoot` | East Profile | `[675, 102, 57, 97]` | Firing pose side view frame 2 (flash) |
| **29** | `john_shoot` | South-East / South-West | `[733, 102, 60, 97]` | Firing pose diagonal front frame 1 |
| **30** | `john_shoot` | South-East / South-West | `[794, 102, 60, 98]` | Firing pose diagonal front frame 2 |
| **31** | `john_shoot` | South (Towards Camera) | `[855, 102, 45, 99]` | Firing pose front view frame 1 |
| **32** | `john_shoot` | South (Towards Camera) | `[901, 102, 45, 99]` | Firing pose front view frame 2 (muzzle flash) |
| **33** | `john_death` | Non-directional | `[947, 102, 47, 97]` | Death frame 1: Hit reaction / stumble |
| **34** | `john_death` | Non-directional | `[0, 204, 60, 104]` | Death frame 2: Kneeling collapse |
| **35** | `john_death` | Non-directional | `[61, 204, 89, 100]` | Death frame 3: Falling torso |
| **36** | `john_death` | Non-directional | `[151, 204, 101, 111]`| Death frame 4: Gib explosion / gore blast |
| **37** | `john_death` | Non-directional | `[253, 204, 109, 96]` | Death frame 5: Disintegrating torso |
| **38** | `john_death` | Non-directional | `[363, 204, 108, 75]` | Death frame 6: Blood pool spreading |
| **39** | `john_death` | Non-directional | `[472, 204, 98, 31]`  | Death frame 7: Resting skeletal remains |

### 3.2 Directional Flipping Optimization

In `john_anim.txt`, West-facing rotations (`rotation_W`, `rotation_SW`, `rotation_WN`) do not require separate sprite graphics. The engine sets `flip_horizontal_flag = 1`:
```c
rotation_SW {
    sprites {
        // Syntax: { texture_name, sprite_id, origin_offset_x, origin_offset_y, flip_flag }
        { "player/john", 23, -27, -100, 0 } // Normal (East/South-East)
    }
}
rotation_ES {
    sprites {
        { "player/john", 23, -26, -100, 1 } // Flipped horizontally
    }
}
```
This saves ~40% of texture VRAM and guarantees identical silhouette geometry across symmetrical angles.

---

## 4. Quantitative Color Audit & Palette Ramp Extraction

Analysis of the 1024x315 RGBA image revealed **181 unique RGBA colors** (180 opaque, 1 fully transparent background). Because these sprites originate from the 1996 8-bit paletted PC release, colors are organized in deterministic brightness ramps:

```
Total Unique Colors: 181
├── Transparent Alpha (0): 1 color (RGBA 0, 0, 0, 0)
└── Opaque Colors (255): 180 colors
    ├── Black Outlines & Combat Boots: (0,0,0), (8,8,8), (20,20,20)
    ├── Gun Metal & Buckles: Slate Greys (64,68,72) .. (208,216,224)
    ├── Skin Tones (Face, Arms, Neck): Warm Peaches (96,56,52) .. (216,160,116)
    ├── Death Gore & Blood: Deep Reds (64,4,4) .. (212,24,4)
    └── Military Camouflage & Pants: Olive Drab Ramp (11 Colors)
```

### 4.1 The Olive Camouflage Ramp

The player's military combat pants and vest shading form an 11-step luminance ramp:

| Step | RGB Color | Perceived Luminance ($L$) | Pixel Count | Shading Purpose |
|---|---|---|---|---|
| **0** | `(0, 16, 0)` | 9.4 | 120 | Deepest seam shadow |
| **1** | `(20, 24, 4)` | 20.5 | 4,677 | Primary shadow & folds (dominant tone) |
| **2** | `(20, 32, 0)` | 24.8 | 73 | Deep crease accent |
| **3** | `(48, 32, 0)` | 33.1 | 2,587 | Mid-tone shadow |
| **4** | `(68, 28, 8)` | 37.7 | 756 | Warm fold shadow |
| **5** | `(40, 52, 8)` | 43.4 | 114 | Medium olive base |
| **6** | `(52, 68, 16)` | 57.3 | 7 | Olive highlight transition |
| **7** | `(76, 68, 48)` | 68.1 | 275 | Khaki mid-tone |
| **8** | `(60, 88, 24)` | 72.3 | 1 | Bright olive crease |
| **9** | `(96, 80, 52)` | 81.6 | 483 | Light khaki highlight |
| **10**| `(112, 84, 52)`| 88.7 | 581 | Direct light specular highlight |

*Critical Finding:* Remapping exclusively this 11-color ramp cleanly differentiates the player character across all poses while completely protecting skin tones, weapon barrels, muzzle flashes, and death gibs from color distortion.

---

## 5. Visual Differentiation Strategy for 8 Players

### 5.1 Evaluation of Technical Approaches

| Criterion | Approach A: 8 Distinct Sprite Sheets | Approach B: Palette Translation Table | Approach C: Fragment Shader Uniform Tint |
|---|---|---|---|
| **Engine Compatibility** | 100% native (uses standard KEX texture loader) | Poor (requires 8-bit paletted textures, obsolete in Vulkan/D3D11) | High (requires modifying `actor.frag`) |
| **Memory Footprint** | $8 \times 160\text{ KB} \approx 1.28\text{ MB}$ VRAM (negligible) | $1 \times 160\text{ KB} + 8 \times 256\text{ B}$ LUT | $1 \times 160\text{ KB}$ |
| **Artifact / Bleed Risk** | **Zero** (hand-tuned luminance per slot) | Medium (color quantization edge artifacts) | Medium (requires strict color distance thresholds) |
| **Implementation Effort** | Very Low (pure asset package, zero engine code changes) | High (custom shader and texture decoder) | Medium (shader hook & uniform injection) |
| **Selected Verdict** | **RECOMMENDED PRIMARY** | Rejected | Recommended Secondary / Polish |

### 5.2 8-Player Color Harmonization & Generated Demonstration Assets

To guarantee high visibility across dark tombs, sunny courtyards, and underground canals, 8 distinct character themes have been generated:

| Slot | Player | Theme Name | Primary Hue | Demonstration Sheet |
|---|---|---|---|---|
| **P1** | Host | Original Khaki / Camo | Olive Drab (Original) | [`demo_john_p1.png`](presentation/player_sprites/demo_john_p1.png) |
| **P2** | Client 1 | Egyptian Cobalt Blue | Sky / Deep Blue | [`demo_john_p2.png`](presentation/player_sprites/demo_john_p2.png) |
| **P3** | Client 2 | Emerald Nile Green | Vibrant Jungle Green | [`demo_john_p3.png`](presentation/player_sprites/demo_john_p3.png) |
| **P4** | Client 3 | Desert Sand / Amber Gold | Golden Ochre | [`demo_john_p4.png`](presentation/player_sprites/demo_john_p4.png) |
| **P5** | Client 4 | Crimson Red | Ruby Combat Red | [`demo_john_p5.png`](presentation/player_sprites/demo_john_p5.png) |
| **P6** | Client 5 | Royal Amethyst Purple | Deep Egyptian Violet | [`demo_john_p6.png`](presentation/player_sprites/demo_john_p6.png) |
| **P7** | Client 6 | Flame Orange | Sunset / Terracotta | [`demo_john_p7.png`](presentation/player_sprites/demo_john_p7.png) |
| **P8** | Client 7 | Charcoal Slate | Spec-Ops Carbon Grey | [`demo_john_p8.png`](presentation/player_sprites/demo_john_p8.png) |

### 5.3 Generated Multi-Angle Comparison Visuals

Visual comparisons across all 8 players in various stances have been created and archived in `docs/presentation/player_sprites/`:

1. **Front View (Idle, South Facing):**  
   [`players_1_to_8_idle_comparison.png`](presentation/player_sprites/players_1_to_8_idle_comparison.png)  
   *Evaluates front silhouette, face preservation, and pants recoloring.*

2. **Back View (Idle, North Facing):**  
   [`players_1_to_8_back_comparison.png`](presentation/player_sprites/players_1_to_8_back_comparison.png)  
   *Evaluates backpack, field straps, and rear trouser visibility.*

3. **Side Profile View (Idle, East Facing):**  
   [`players_1_to_8_side_comparison.png`](presentation/player_sprites/players_1_to_8_side_comparison.png)  
   *Evaluates weapon holding and leg stride balance.*

4. **Walk Cycle (Stride Frame):**  
   [`players_1_to_8_walk_comparison.png`](presentation/player_sprites/players_1_to_8_walk_comparison.png)  
   *Demonstrates dynamic movement leg shading consistency.*

5. **Combat Firing (Muzzle Flash):**  
   [`players_1_to_8_shoot_comparison.png`](presentation/player_sprites/players_1_to_8_shoot_comparison.png)  
   *Confirms yellow/white muzzle flash remains untouched across all color variants.*

---

## 6. KEX Engine Billboard Pipeline & Puppet Binding

### 6.1 Actor Class & Spawn Seam

Secondary co-op players are represented in the world via `kexSlavePuppet` instances (derived from `kexSlaveActor`):
- **Actor Class Name:** `Obj_Player_Puppet`
- **Bounding Box Dimensions:** Radius $= 16.0f$, Height $= 56.0f$, View Height $= 48.0f$.
- **Billboard Matrix Calculation:** Managed natively in `kexRenderView::DrawActorBillboard()`. The sprite quad automatically aligns with the active camera plane while rotating around the $Z$-axis according to the player's network yaw.

### 6.2 Animation State Machine

During each engine tick, the secondary player puppet evaluates its movement velocity and attack state:

```cpp
void UpdatePuppetAnimation(kexSlavePuppet* puppet, const PlayerNetState& state)
{
    if (state.health <= 0)
    {
        if (puppet->AnimID() != ANIM_JOHN_DEATH) {
            puppet->ChangeAnim("player/john_death");
        }
        return;
    }

    if (state.buttons & BC_ATTACK)
    {
        puppet->ChangeAnim("player/john_shoot");
        return;
    }

    float speedSqr = state.velocity.x * state.velocity.x + state.velocity.y * state.velocity.y;
    if (speedSqr > 1.0f)
    {
        puppet->ChangeAnim("player/john_walk");
    }
    else
    {
        puppet->ChangeAnim("player/john_idle");
    }
}
```

### 6.3 Per-Slot Texture Binding

To assign `demo_john_p{N}.png` to Puppet $N$ without code modifications:
1. Define 8 animation namespaces in `john_anim.txt`: `john_p1_walk`, `john_p2_walk`, etc., pointing to `player/john_p1`, `player/john_p2`.
2. When creating Puppet $N$, assign animation prefix `sprintf(animBuf, "player/john_p%d_%s", slotId + 1, animName)`.
3. The engine texture cache (`kexTextureManager`) loads all 8 sheets at level initialization, consuming $< 1.3\text{ MB}$ of VRAM.

---

## 7. Legal Analysis & Redistribution Provenance

1. **Origin of Raw Sprites:**
   - The "John" mercenary sprites were created in 1996 by Lobotomy Software for the PC MS-DOS version of *PowerSlave* / *Exhumed*.
   - The sprites were ripped from PC game data by Dunkelschwamm and published on *The Spriters Resource* (TSR) for fan research and preservation.
2. **Prior Mod Use:**
   - In 2015, Samuel "Kaiser" Villarreal (lead developer of *PowerSlave EX* and *PowerSlave Exhumed* at Nightdive Studios) licensed the KEX engine source under the GNU General Public License (GPL).
   - The 2015 Co-Op Edition kit packaged `mods/john.kpf` alongside the GPL binary with explicit credit:  
     `CREDITS: John player sprites: PC Exhumed sprites ripped by Dunkelschwamm for The Spriters Resource.`
3. **Distribution Guidance for PowerSlave Exhumed Together:**
   - **Do Not Embed in DLL:** Never embed sprite PNG byte arrays into `Together.dll` or the game executable.
   - **Companion Mod Package:** Distribute the recolored sprite sheets as a separate community add-on package: `mods/coop_sprites.kpf`.
   - **Read-Only Engine Rule:** Ensure the mod loads `mods/coop_sprites.kpf` through the standard KEX virtual filesystem mount points, preserving the bit-exact integrity of all 77 original game files in Steam.

---

## 8. Implementation Checklist for Next Developer

- [ ] In M1 presentation milestone:
  - Package the 8 generated sheets into `mods/coop_sprites.kpf` along with updated `john_anim.txt` definitions.
  - Mount `mods/coop_sprites.kpf` via `kexFileSystem::MountPackage()`.
- [ ] In M1 puppet test harness:
  - Spawn `Obj_Player_Puppet` for slot 2 and cycle animations (`idle`, `walk`, `shoot`, `death`).
  - Verify horizontal flip mirroring across 8 directional camera angles.
- [ ] Confirm no distortion on non-camo pixels (gun metal, muzzle flash, skin, and blood).
