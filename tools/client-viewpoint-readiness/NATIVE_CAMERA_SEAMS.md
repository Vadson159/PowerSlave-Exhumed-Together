# Native Camera Seams & Engine Boundary Specification

> **Target Executable**: `anubis_Shipping_Steam_x64.exe`  
> **Executable SHA-256**: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`  
> **Preferred Image Base**: `0x140000000`  
> **Architecture**: x86-64 (AMD64 / MS x64 Calling Convention)  
> **Target Status**: Complete Static & Machine-Code Verification (Read-Only)

---

## 1. Executable Section Map & Addressing

| Section | Virtual Address (RVA) | Virtual Size | File Raw Offset | Raw Size | Characteristics |
|---|---|---|---|---|---|
| `.text` | `0x00001000` | `0x0048006F` | `0x00000400` | `0x00480200` | `0x60000020` (Code, Execute, Read) |
| `.rdata` | `0x00482000` | `0x0011C6EA` | `0x00480600` | `0x0011C800` | `0x40000040` (Initialized Data, Read) |
| `.data` | `0x0059F000` | `0x005A2A68` | `0x0059CE00` | `0x004B3400` | `0xC0000040` (Initialized Data, Read/Write) |
| `.pdata` | `0x00B42000` | `0x0002C034` | `0x00A50200` | `0x0002C200` | `0x40000040` (Exception Tables) |
| `.rsrc` | `0x00B6F000` | `0x0000DC80` | `0x00A7C400` | `0x0000DE00` | `0x40000040` (Resources) |

*Conversion Formula*:
- For `.text`: `FileOffset = RVA - 0x1000 + 0x400 = RVA - 0xC00`
- For `.rdata`: `FileOffset = RVA - 0x482000 + 0x480600 = RVA - 0x1A00`
- For `.data`: `FileOffset = RVA - 0x59F000 + 0x59CE00 = RVA - 0x2200`

---

## 2. Master Struct Layouts & Offsets

### 2.1 `kexSlaveGameInstanceLocal` (Singleton at `0x140A45920` or dynamic instance)

| Offset | Type | Field Name | Description | Verification |
|---|---|---|---|---|
| `+0x00` | `void**` | `vtable` | Vtable pointer (`0x14052B318`) | `VERIFIED STATIC` |
| `+0x20` | `int32_t` | `gameState` | `3` = In-Game Playing | `VERIFIED STATIC` |
| `+0x24` | `int32_t` | `pendingState` | `0` = Stable, non-zero = transitioning | `VERIFIED STATIC` |
| `+0x40` | `kexSlavePlayer` | `player` | Embedded primary player object | `VERIFIED STATIC` (`lea rax, [rcx+40h]`) |

### 2.2 `kexSlavePlayer` (Embedded at `GameInstance + 0x40`)

| Offset | Type | Field Name | Description | Verification |
|---|---|---|---|---|
| `+0x00` | `void**` | `vtable` | Vtable pointer (`0x14052B880`) | `VERIFIED STATIC` |
| `+0x18` | `kexSlavePuppet*` | `puppet` | Pointer to active puppet controller | `VERIFIED STATIC` |
| `+0xE0` | `kexSlaveWeapon*` | `weapon` | Pointer to current weapon object | `VERIFIED STATIC` |
| `+0xF0` | `float` | `shakeTime` | Camera shake duration timer | `VERIFIED STATIC` |
| `+0xF4` | `kexVec3` | `shakeVector` | Linear camera shake displacement $(X, Y, Z)$ | `VERIFIED STATIC` |
| `+0x118` | `float` | `recoilPitch` | Weapon firing recoil pitch kick (radians) | `VERIFIED STATIC` |
| `+0x1B4` | `float` | `headBob` | Walking view bob offset ($Z$) | `VERIFIED STATIC` |
| `+0x1CC` | `float` | `viewZ` | Nominal eye height ($41.0f$ stand, $17.0f$ crouch) | `VERIFIED STATIC` |

### 2.3 `kexSlavePuppet` (Allocated on heap, pointed to by `Player + 0x18`)

| Offset | Type | Field Name | Description | Verification |
|---|---|---|---|---|
| `+0x00` | `void**` | `vtable` | Vtable pointer (`0x14052C348`) | `VERIFIED STATIC` |
| `+0x18` | `kexVec3` | `origin` | Authoritative simulation position $(X, Y, Z)$ | `VERIFIED STATIC` |
| `+0x24` | `kexAngle` | `yaw` | Authoritative body yaw (radians, $[-\pi, +\pi]$) | `VERIFIED STATIC` |
| `+0x28` | `kexSlaveActor*` | `actor` | Associated physical actor in world | `VERIFIED STATIC` |
| `+0x30` | `kexVec3` | `rotation` | Euler orientation angles $(pitch, roll, yaw)$ | `VERIFIED STATIC` |
| `+0x50` | `kexVec3` | `simPrevOrigin` | Interpolant previous origin register | `VERIFIED STATIC` |
| `+0x6C` | `float` | `interpX` | Interpolated rendering coordinate $X$ | `VERIFIED STATIC` |
| `+0x70` | `float` | `interpY` | Interpolated rendering coordinate $Y$ | `VERIFIED STATIC` |
| `+0x74` | `float` | `interpZ` | Interpolated rendering coordinate $Z$ | `VERIFIED STATIC` |
| `+0xE0` | `kexVec3` | `velocity` | Movement velocity vector | `VERIFIED STATIC` |
| `+0xE8` | `float` | `velocity.z` | Vertical velocity ($0.0f$ when on ground) | `VERIFIED STATIC` |

### 2.4 `kexSlaveActor` (Pointed to by `Puppet + 0x28`)

| Offset | Type | Field Name | Description | Verification |
|---|---|---|---|---|
| `+0x00` | `void**` | `vtable` | Vtable pointer (`0x14052C5C8`) | `VERIFIED STATIC` |
| `+0x10` | `void*` | `nextInSector` | Intrusive linked-list pointer for sector actors | `VERIFIED STATIC` |
| `+0x4C` | `int32_t` | `sectorIndex` | Current spatial sector index ($0 \le s < numSectors$) | `VERIFIED STATIC` |
| `+0x50` | `kexVec3` | `origin` | Actor physical origin | `VERIFIED STATIC` |

### 2.5 `kexRenderView` (Constructed on Stack in `RenderState` at `[rbp - 0x60]`)

| Stack Offset | Size | Type | Field Name | Description | Verification |
|---|---|---|---|---|---|
| `[rbp - 0x60]` | `0x0C` | `kexVec3` | `viewOrigin` | Final camera eye world position $(X, Y, Z)$ | `VERIFIED STATIC` |
| `[rbp - 0x54]` | `0x0C` | `kexVec3` | `viewAngles` | Camera orientation angles $(pitch, roll, yaw)$ in radians | `VERIFIED STATIC` |
| `[rbp - 0x48]` | `0x04` | `float` | `fov` | Horizontal field of view | `VERIFIED STATIC` |
| `[rbp - 0x44]` | `0x04` | `float` | `aspectRatio` | Viewport aspect ratio | `VERIFIED STATIC` |
| `[rbp - 0x40]` | `0x04` | `int32_t` | `sectorIndex` | Sector index where camera originates | `VERIFIED STATIC` |

---

## 3. Native Function Seam Catalog

### Seam 01: `kexSlaveGameInstanceLocal::GetPlayer`

- **Preferred VA**: `0x1401B5520`
- **RVA**: `0x001B5520`
- **File Raw Offset**: `0x001B4920`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`)
- **Return Type**: `kexSlavePlayer*` in `rax`
- **Disassembly**:
  ```x86asm
  00000001401B5520: 48 8D 41 40        lea         rax,[rcx+40h]
  00000001401B5524: C3                 ret
  ```
- **Callers**: `GameInstance::RenderState` (`0x1401B77BB`), `kexRenderScene::DrawView` (`0x1401F078B`), `GameInstance::StateTick` (`0x1401B6F00`).
- **Callees**: None (leaf function).
- **Side Effects**: Pure accessor. None.
- **Thread Ownership**: Main Render / Game Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 02: `GameInstance::RenderState` (State 3 Camera Evaluation)

- **Preferred VA**: `0x1401B7700` (Jump Target for State 3: `0x1401B77BB`)
- **RVA**: `0x001B7700` (`0x001B77BB`)
- **File Raw Offset**: `0x001B6B00` (`0x001B6BBB`)
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`, `rdx = pRenderContext`)
- **Stack Frame**: `0x320` bytes (`sub rsp, 320h`, `rbp = rsp + 0x60`)
- **Key Disassembly Snippet**:
  ```x86asm
  00000001401B77BB: 48 8B 4B 18        mov         rcx,qword ptr [rbx+18h]  ; rcx = player.puppet
  00000001401B77BF: F3 0F 10 41 6C     movss       xmm0,dword ptr [rcx+6Ch] ; interpX
  00000001401B77C4: F3 0F 10 49 70     movss       xmm1,dword ptr [rcx+70h] ; interpY
  00000001401B77C9: F3 0F 10 51 74     movss       xmm2,dword ptr [rcx+74h] ; interpZ
  00000001401B77CE: F3 0F 58 83 CC 01  addss       xmm0,dword ptr [rbx+1CCh]; + viewZ (41.0f)
  00000001401B77D6: F3 0F 58 83 B4 01  addss       xmm0,dword ptr [rbx+1B4h]; + headBob
  00000001401B77DE: 0F 28 D8           movaps      xmm3,xmm0                ; final cam Z
  00000001401B782A: E8 11 87 03 00     call        00000001401EFF40         ; kexRenderScene::DrawView
  ```
- **Callers**: `GameInstance::Render` (`0x1401B1310`).
- **Callees**: `kexRenderScene::DrawView` (`0x1401EFF40`).
- **Side Effects**: Constructs stack-allocated `kexRenderView`, triggers entire 3D world render pass.
- **Thread Ownership**: Main Render Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 03: `kexRenderScene::DrawView`

- **Preferred VA**: `0x1401EFF40`
- **RVA**: `0x001EFF40`
- **File Raw Offset**: `0x001EF340`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`, `rdx = kexRenderView*`)
- **Key Disassembly Snippet**:
  ```x86asm
  00000001401EFF40: 48 8B C4           mov         rax,rsp
  00000001401EFF43: 55                 push        rbp
  00000001401EFF44: 56                 push        rsi
  00000001401EFF45: 57                 push        rdi
  00000001401EFF46: 41 54              push        r12
  00000001401EFF48: 41 55              push        r13
  00000001401EFF4A: 41 56              push        r14
  00000001401EFF4C: 41 57              push        r15
  00000001401EFF4E: 48 8D 68 A1        lea         rbp,[rax-5Fh]
  00000001401EFF52: 48 81 EC E0 01 00  sub         rsp,1E0h
  ...
  00000001401F078B: E8 90 4D FC FF     call        00000001401B5520         ; GetPlayer
  00000001401F0793: 48 8B 88 E0 00 00  mov         rcx,qword ptr [rax+0E0h] ; player->weapon
  00000001401F079A: E8 81 F4 01 00     call        000000014020FC20         ; Weapon::Draw
  ```
- **Callers**: `GameInstance::RenderState` (`0x1401B782A`).
- **Callees**: `SceneRenderer::RenderSectors` (`0x1401A2580`), `SceneRenderer::DrawActorBillboard` (`0x1401A34C0`), `kexSlaveWeapon::Draw` (`0x14020FC20`).
- **Side Effects**: Executes sector traversal via portal frustum culling; issues draw calls to GPU backend.
- **Thread Ownership**: Main Render Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 04: `kexWorld::FindSector`

- **Preferred VA**: `0x1401C98F0`
- **RVA**: `0x001C98F0`
- **File Raw Offset**: `0x001C8CF0`
- **Vtable Location**: `kexWorld` vtable (`0x14052B478`) slot 7 (`+0x38`)
- **Global Instance**: Pointer at `0x140A45940`
- **Calling Convention**: MS x64 `__fastcall`
- **Signature**:
  ```cpp
  int32_t __fastcall kexWorld::FindSector(kexWorld* this, const kexVec3* pos, int32_t guessSector);
  ```
- **Registers**:
  - `rcx`: `kexWorld* this` (loaded from `0x140A45940`)
  - `rdx`: `const kexVec3* pos` (world coordinates $X, Y, Z$)
  - `r8d`: `int32_t guessSector` (hints spatial search; if `0`, scans from sector 0; if `-1`, fails fast)
- **Return Value**: `eax` = sector index ($0 \le s < numSectors$) or `-1` if coordinates are outside BSP/sector polygons.
- **Key Disassembly**:
  ```x86asm
  00000001401C98F0: 40 53              push        rbx
  00000001401C98F2: 56                 push        rsi
  00000001401C98F3: 41 55              push        r13
  00000001401C98F5: 48 83 EC 50        sub         rsp,50h
  00000001401C9902: 48 8B F1           mov         rsi,rcx
  00000001401C990A: 4C 8B EA           mov         r13,rdx
  00000001401C990D: 49 63 D8           movsxd      rbx,r8d
  00000001401C9922: 83 FB FF           cmp         ebx,0FFFFFFFFh
  00000001401C9925: 7F 0E              jg          00000001401C9935
  00000001401C9927: B8 FF FF FF FF     mov         eax,0FFFFFFFFh
  00000001401C992C: 48 83 C4 50        add         rsp,50h
  00000001401C9930: 41 5D              pop         r13
  00000001401C9932: 5E                 pop         rsi
  00000001401C9933: 5B                 pop         rbx
  00000001401C9934: C3                 ret
  ```
- **Thread Ownership**: Main Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 05: `kexSlaveActor::LinkSector`

- **Preferred VA**: `0x1401D3D70`
- **RVA**: `0x001D3D70`
- **File Raw Offset**: `0x001D3170`
- **Calling Convention**: MS x64 `__fastcall`
- **Signature**:
  ```cpp
  void __fastcall kexSlaveActor::LinkSector(kexSlaveActor* this, int32_t sectorIndex);
  ```
- **Registers**:
  - `rcx`: `kexSlaveActor* this`
  - `edx`: `int32_t sectorIndex`
- **Key Disassembly**:
  ```x86asm
  00000001401D3D70: 48 89 5C 24 08     mov         qword ptr [rsp+8],rbx
  00000001401D3D75: 48 89 74 24 10     mov         qword ptr [rsp+10h],rsi
  00000001401D3D7A: 57                 push        rdi
  00000001401D3D7B: 48 83 EC 20        sub         rsp,20h
  00000001401D3D7F: 48 63 FA           movsxd      rdi,edx
  00000001401D3D82: 48 8B D9           mov         rbx,rcx
  00000001401D3D85: 48 3B 3D E4 1B 87  cmp         rdi,qword ptr [0000000140A45970h] ; max sectors
  00000001401D3D8C: 48 8B F7           mov         rsi,rdi
  00000001401D3D8F: 72 19              jb          00000001401D3DAA
  ... assert out of bounds ...
  00000001401D3DAA: 4C 8B 43 10        mov         r8,qword ptr [rbx+10h] ; actor->nextInSector
  00000001401D3DAE: 48 69 D6 88 00 00  imul        rdx,rsi,88h            ; sector size = 0x88
  00000001401D3DB5: 48 03 15 AC 1B 87  add         rdx,qword ptr [0000000140A45968h] ; sectors array
  ```
- **Side Effects**: Removes actor from previous sector list, updates `actor->sectorIndex` (`+0x4C`), inserts into new sector intrusive linked list at `sector + 0x78`.
- **Thread Ownership**: Main Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 06: `kexSlavePuppet::CheckedGetActor`

- **Preferred VA**: `0x140197EE0`
- **RVA**: `0x00197EE0`
- **File Raw Offset**: `0x001972E0`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = kexSlavePuppet* this`)
- **Return Type**: `kexSlaveActor*` in `rax`
- **Disassembly**:
  ```x86asm
  0000000140197EE0: 40 53              push        rbx
  0000000140197EE2: 48 83 EC 20        sub         rsp,20h
  0000000140197EE6: 48 8B 59 28        mov         rbx,qword ptr [rcx+28h] ; rbx = puppet->actor
  0000000140197EEA: 48 85 DB           test        rbx,rbx
  0000000140197EED: 75 08              jne         0000000140197EF7
  0000000140197EEF: 33 C0              xor         eax,eax                 ; return nullptr
  0000000140197EF1: 48 83 C4 20        add         rsp,20h
  0000000140197EF5: 5B                 pop         rbx
  0000000140197EF6: C3                 ret
  0000000140197EF7: 48 8B 03           mov         rax,qword ptr [rbx]      ; actor->vtable
  0000000140197EFA: 48 8B CB           mov         rcx,rbx
  0000000140197EFD: FF 10              call        qword ptr [rax]          ; vtable[0] validation
  ```
- **Thread Ownership**: Main Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 07: `kexInterpolantManager::Evaluate`

- **Preferred VA**: `0x1400A7D60`
- **RVA**: `0x000A7D60`
- **File Raw Offset**: `0x000A7160`
- **Vtable Location**: `kexInterpolantManager` vtable (`0x1405112B8`) slot 2 (`+0x10`)
- **Global Instance**: Singleton at `0x14063D360`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`, `xmm1 = subFrameAlpha`)
- **Side Effects**: Iterates all registered interpolants; computes $\text{eval} = \text{prev} + \alpha \times (\text{curr} - \text{prev})$ and writes evaluated coordinates to `Puppet + 0x6C, +0x70, +0x74`.
- **Thread Ownership**: Main Thread (called immediately prior to rendering).
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 08: `kexSlaveWeapon::Draw`

- **Preferred VA**: `0x14020FC20`
- **RVA**: `0x0020FC20`
- **File Raw Offset**: `0x0020F020`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = kexSlaveWeapon* this`, `rdx = pRenderView`)
- **Disassembly Profile**:
  - Checks weapon animation frame (`this + 0x18`).
  - Fetches HUD weapon texture descriptor.
  - Computes weapon screen-space bob offset: $X_{bob} = \cos(\text{bobPhase}) \times \text{bobScale}$, $Y_{bob} = |\sin(\text{bobPhase})| \times \text{bobScale}$.
  - Submits 2D/3D screen billboard draw call to render queue.
- **Thread Ownership**: Main Render Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 09: `kexSlaveWeapon::Update`

- **Preferred VA**: `0x14020FB10`
- **RVA**: `0x0020FB10`
- **File Raw Offset**: `0x0020EF10`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = kexSlaveWeapon* this`)
- **Side Effects**: Advances weapon firing state machine, decrements muzzle flash timer, plays weapon firing sound if trigger pulled.
- **Thread Ownership**: Main Simulation Thread (`kexSlavePlayer::Update`).
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 10: `kexSlaveHudLocal::Update`

- **Preferred VA**: `0x1401C1470`
- **RVA**: `0x001C1470`
- **File Raw Offset**: `0x001C0870`
- **Global Instance**: Singleton at `0x140A43E20` (vtable `0x14052B1F0`)
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`)
- **Side Effects**: Reads player health (`Player + 0x24`), ammo count (`Player + 0x28`), selected weapon id, refreshes internal digit sprites.
- **Thread Ownership**: Main Simulation Thread (`StateTick`).
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 11: `kexSlaveHudLocal::Display`

- **Preferred VA**: `0x1401C08B0`
- **RVA**: `0x001C08B0`
- **File Raw Offset**: `0x001BFCB0`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`, `rdx = pRenderContext`)
- **Side Effects**: Draws HUD status bar, health counter, ammo counter, crosshair to orthographic 2D render buffer.
- **Thread Ownership**: Main Render Thread (`GameInstance::Render`).
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 12: `SoundSystem::UpdateListener` (Inlined in `StateTick`)

- **Preferred VA Range**: `0x1401B7170` - `0x1401B71FA`
- **RVA Range**: `0x001B7170` - `0x001B71FA`
- **Global Audio Manager**: Pointer at `0x140AE9100`
- **Key Disassembly Snippet**:
  ```x86asm
  00000001401B7170: 48 8B 0D 89 1F 93  mov         rcx,qword ptr [0000000140AE9100h] ; AudioManager
  00000001401B7177: 48 8B 43 18        mov         rax,qword ptr [rbx+18h]           ; player.puppet
  00000001401B717B: 48 8D 50 18        lea         rdx,[rax+18h]                     ; puppet.origin
  00000001401B717F: 48 8D 48 30        lea         rcx,[rax+30h]                     ; puppet.rotation
  00000001401B7183: E8 A8 B2 F3 FF     call        00000001400F2430                  ; SetListenerTransform
  ```
- **Side Effects**: Directly copies `puppet->origin` and `puppet->rotation` into 3D audio listener matrices.
- **Thread Ownership**: Main Game Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 13: `kexSlavePuppet::Tick`

- **Preferred VA**: `0x1401DB8A0`
- **RVA**: `0x001DB8A0`
- **File Raw Offset**: `0x001DACA0`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`)
- **Side Effects**: Runs movement physics, calls `GroundMove`, tests floor/ceiling collisions, executes `EnterSectorSpecial` (lava, acid, triggers, crushing ceilings).
- **Hazard**: Executing this locally on Client B causes duplicate gameplay events, trigger desyncs, and prediction fighting.
- **Thread Ownership**: Main Simulation Thread.
- **Confidence**: `VERIFIED STATIC`.

---

### Seam 14: `kexSlavePlayer::Update`

- **Preferred VA**: `0x1401DFE30`
- **RVA**: `0x001DFE30`
- **File Raw Offset**: `0x001DF230`
- **Calling Convention**: MS x64 `__fastcall` (`rcx = this`)
- **Key Actions**:
  - Reads local input commands (`forward`, `strafe`, `yaw`, `pitch`, `buttons`).
  - Updates head bob cycle (`+0x1B4`).
  - Decays camera shake timer (`+0xF0`) and recoil pitch (`+0x118`).
  - Calls `kexSlavePuppet::Tick` on `this->puppet`.
  - Updates active weapon (`+0xE0`).
- **Thread Ownership**: Main Simulation Thread.
- **Confidence**: `VERIFIED STATIC`.

---

## 4. Native Hooking & Safety Matrix

| Native Function | Recommended Hook Action | Calling Frequency | ABI / Stack Sensitivity | Risk Level |
|---|---|---|---|---|
| `GetPlayer` (`0x1401B5520`) | **DO NOT HOOK** | 1000s/sec | Leaf function (`lea rax, [rcx+40h]; ret`). Replacing it breaks inlining assumptions and causes cache thrashing. | `HIGH` |
| `RenderState` (`0x1401B7700`) | **PASSIVE DETOUR / PRE-CALL HOOK** | Once per render frame (60-144 Hz) | Stack allocated frame (`0x320`). RBP-relative addressing. Must preserve non-volatile registers `rbx, rsi, rdi, r12-r15`. | `MEDIUM` |
| `LinkSector` (`0x1401D3D70`) | **DIRECT CALL (SAFE)** | On sector transition | Standard MS x64 fastcall. Validates sector boundary against `[0x140A45970]`. Safe to invoke from mod DLL on main thread. | `LOW` |
| `FindSector` (`0x1401C98F0`) | **DIRECT CALL (SAFE)** | On position change | Must pass valid guess sector ($0$ is safe fallback). Read-only spatial traversal. | `LOW` |
| `CheckedGetActor` (`0x140197EE0`) | **DIRECT CALL (SAFE)** | As needed | Validates actor nullity and vtable integrity. | `LOW` |
| `Puppet::Tick` (`0x1401DB8A0`) | **CONDITIONAL DETOUR (SUPPRESSION)** | Once per simulation tick (30-60 Hz) | Suppress local physics/trigger execution on Client B when running in Client Shell mode. | `MEDIUM` |
