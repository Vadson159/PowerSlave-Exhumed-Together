# PowerSlave Exhumed Together — Visual Player Proxy Native Seams Specification

**Document ID:** `SEAMS-VISUAL-PLAYER-PROXY`  
**Classification:** Low-Level Reverse Engineering Reference  
**Target Binary:** `anubis_Shipping_Steam_x64.exe`  
**Target Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`  
**Target Image Base:** `0x140000000` (PE Preferred Base)  
**File Mapping Rules:**
- `.text`: Virtual Base `0x140001000` $\to$ File Offset = RVA − `0xc00`.
- `.rdata`: Virtual Base `0x140482000` $\to$ File Offset = RVA − `0x1a00`.
- `.data`: Virtual Base `0x14059f000` $\to$ File Offset = RVA − `0x2200`.

---

## 1. Сводная таблица исследованных нативных функций

| Символ / Описание | Preferred VA | RVA | File Offset | ABI / Calling Convention | Confidence Level |
|---|---|---|---|---|---|
| `kexSlaveObjectFactoryLocal::SpawnActor` | `0x1401d4780` | `0x1d4780` | `0x1d3b80` | Win64 `__fastcall` (thiscall) | **VERIFIED STATIC** / **RUNTIME OBSERVED** |
| `kexSlaveActor::LinkSector` | `0x1401d3d70` | `0x1d3d70` | `0x1d3170` | Win64 `__fastcall` | **VERIFIED STATIC** |
| `kexActor::Remove` | `0x1401a02b0` | `0x1a02b0` | `0x19f6b0` | Win64 `__fastcall` (virtual `+0x30`) | **VERIFIED STATIC** / **RUNTIME OBSERVED** |
| `kexSlaveActor::Spawn` | `0x1401a02f0` | `0x1a02f0` | `0x19f6f0` | Win64 `__fastcall` (virtual `+0x38`) | **VERIFIED STATIC** |
| `kexSlavePuppet::Spawn` | `0x1401dbc60` | `0x1dbc60` | `0x1db060` | Win64 `__fastcall` (virtual `+0x38`) | **VERIFIED STATIC** / **RUNTIME OBSERVED** |
| `kexSlaveActor::Tick` | `0x14019f2d0` | `0x19f2d0` | `0x19e6d0` | Win64 `__fastcall` (virtual `+0x20`) | **VERIFIED STATIC** |
| `kexSlaveActor::ChangeAnim` | `0x1401a1240` | `0x1a1240` | `0x1a0640` | Win64 `__fastcall` | **VERIFIED STATIC** |
| `kexSlaveObjectNode::Retire` / `QueueRetirement` | `0x1401b5530` | `0x1b5530` | `0x1b4930` | Win64 `__fastcall` (`GameInstance + 0xb0`)| **VERIFIED STATIC** |
| `kexSlaveObjectNode::DeferredSweep` | `0x1401b6360` | `0x1b6360` | `0x1b5760` | Win64 `__fastcall` (`void()`) | **VERIFIED STATIC** |
| `kexSlaveObjectNode::DrainQueue` | `0x1401b61d0` | `0x1b61d0` | `0x1b55d0` | Win64 `__fastcall` (`void()`) | **VERIFIED STATIC** |
| `SceneRenderer::RenderSectors` | `0x1401a2580` | `0x1a2580` | `0x1a1980` | Win64 `__fastcall` | **VERIFIED STATIC** |
| `SceneRenderer::DrawActorBillboard` | `0x1401a34c0` | `0x1a34c0` | `0x1a28c0` | Win64 `__fastcall` | **VERIFIED STATIC** |
| `kexBatchedVertices::AddQuad` | `0x140055fb0` | `0x55fb0` | `0x553b0` | Win64 `__fastcall` | **VERIFIED STATIC** |
| `kexActor::InflictDamage` | `0x1401a1370` | `0x1a1370` | `0x1a0770` | Win64 `__fastcall` | **VERIFIED STATIC** |

---

## 2. Детальные спецификации нативных швов

### 2.1. `kexSlaveObjectFactoryLocal::SpawnActor`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401d4780`
- **RVA:** `0x1d4780`
- **File Offset:** `0x1d3b80`
- **ABI / Сигнатура:**
  ```cpp
  kexSlaveActor* __fastcall SpawnActor(
      kexSlaveObjectFactoryLocal* factory, // RCX
      int actorType,                       // EDX
      const kVec3* pos,                    // R8
      float yaw,                           // XMM3
      int sectorId                         // [RSP+0x28] (5-й аргумент на стеке)
  );
  ```
- **Known Callers:**
  - `FUN_1401bfcc0` (RVA `0x1bfcc0`): спавн триггерных и скриптовых сущностей.
  - `FUN_1401e7860` (RVA `0x1e7860`): генераторы снарядов и спецэффектов.
  - `FUN_14019f2d0` (RVA `0x19f2d0`): выброс обломков (`Obj_Debris`) при повреждении.
  - AngelScript binding `0x1401f9f18`: обёртка функции `SpawnActor(type, pos, yaw, sector)`.
- **Known Callees:**
  - `FUN_1401bb140` (RVA `0x1bb140`): поиск дефиниции актора `ActorDef` в `DAT_140af3350`.
  - `FUN_1400becf0` (RVA `0xbecf0`): хеширование имени класса и поиск RTTI дескриптора.
  - `(*pRTTI->pfnAlloc)()`: виртуальный вызов аллокатора/конструктора C++.
  - `FUN_1401d3d70` (RVA `0x1d3d70`): `LinkSector(actor, sectorId)`.
  - `actor->vtable[0x38 / 8](actor)`: вызов виртуального метода `Spawn()`.
- **Verified Preconditions:**
  - Вызов разрешён **исключительно из потока симуляции** (`StateTick` TID).
  - Сектор мира инициализирован: `sectorId >= 0 && sectorId < DAT_140a45970`.
  - Указатель `pos` валиден (доступен на чтение 12 байт).
  - Фабрика `factory` проинициализирована (глобальный указатель фабрики валиден).
- **Verified Side Effects:**
  - Извлекает или аллоцирует `kexSlaveActor` (440 байт) или `kexSlavePuppet` (480 байт).
  - Аллоцирует `ObjectNode` из пула узлов и помещает в кольцевой двусвязный список сектора.
  - Устанавливает координаты `actor + 0x18`, углы `actor + 0x30` / `0x38`, индекс сектора `actor + 0x3c`.
  - Вызывает виртуальный `Spawn()`, который регистрирует интерполянты в `DAT_14063d368`.
- **Known Ownership:**
  - Возвращаемый указатель принадлежит графу сектора мира (`sector->actorList`).
  - Узел `ObjectNode` создаётся с `refcount = 0`.
- **Evidence Path:**
  - `docs/evidence/ghidra_2022/G_FUNC_1401d4780.json`
  - `docs/evidence/m0/C01/RESULT.md` (наблюдение runtime-вызова фабрики в M0).
- **Confidence Level:** **VERIFIED STATIC** + **RUNTIME OBSERVED**.

---

### 2.2. `kexSlaveActor::LinkSector`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401d3d70`
- **RVA:** `0x1d3d70`
- **File Offset:** `0x1d3170`
- **ABI / Сигнатура:**
  ```cpp
  void __fastcall LinkSector(
      kexSlaveActor* actor, // RCX
      int newSectorId       // EDX
  );
  ```
- **Known Callers:**
  - `SpawnActor` (`0x1401d4780`, `0x1401d41f0`).
  - `kexSlaveActor::Tick` (`0x14019f2d0`): при перемещении актора между секторами.
  - `GameInstance::ResetLevel` (`0x1401b5df0`): при восстановлении состояния мира из сохранения.
- **Verified Preconditions:**
  - `actor != nullptr`.
  - `newSectorId >= 0 && (uint)newSectorId < DAT_140a45970` (`m_dwLength`).
- **Verified Side Effects:**
  - Если `actor->node == nullptr` (первоначальный линк): аллоцирует узел `ObjectNode` (`FUN_1401d5820`) и вставляет в `sectors[newSectorId].actorList`.
  - Если `actor->node != nullptr` и `actor->sectorIndex != newSectorId`:
    1. Исключает узел из старого списка (`node->prev->next = node->next`, `node->next->prev = node->prev`).
    2. Вставляет узел в голову списка нового сектора (`sectors[newSectorId].actorList`).
  - Перезаписывает `actor->sectorIndex = newSectorId` (`*(int*)(actor + 0x3c)`).
- **Критический архитектурный вывод:**
  **Функция выполняет атомарный Unlink и Relink самостоятельно.** Любой предварительный вызов вымышленного `UnlinkSector()` или предварительная запись `actor->sectorIndex = newSectorId` разрушает логику функции.
- **Evidence Path:** `docs/evidence/ghidra_2022/G_FUNC_1401d3d70.json`.
- **Confidence Level:** **VERIFIED STATIC**.

---

### 2.3. `kexActor::Remove`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401a02b0`
- **RVA:** `0x1a02b0`
- **File Offset:** `0x19f6b0`
- **ABI / Сигнатура:**
  ```cpp
  void __fastcall Remove(
      kexSlaveActor* actor // RCX (виртуальный метод vtable + 0x30)
  );
  ```
- **Known Callers:**
  - Виртуальный метод через интерфейс `kexActor`.
  - Скриптовый метод `kexActor::Remove()` (`0x140172f10`).
  - Ветка смерти сущностей и уничтожения снарядов.
- **Known Callees:**
  - `FUN_1401a0f20` (RVA `0x1a0f20`): отцепление от структур коллизий и следов.
  - `FUN_1401d3460` (RVA `0x1d3460`): деактивация интерполянтов и постановка узла в очередь.
  - `GameInstance::vtable[0xb0]` (`0x1401b5530`): атомарная постановка `ObjectNode` в очередь `DAT_140af3380`.
- **Verified Preconditions:**
  - `actor != nullptr`.
  - Вызов разрешён **только из потока симуляции**.
- **Verified Side Effects:**
  - Модифицирует флаги: `*(uint*)(actor + 0x16c) = (*(uint*)(actor + 0x16c) & 0xfffffc5d) | 0x2004`. Сбрасывает флаги твёрдости и уязвимости, устанавливает флаг удаления.
  - Удаляет связку интерполянтов из массива `DAT_14063d368` через `0x1400a7f10`.
  - Помещает узел `ObjectNode` в отложенную очередь удаления движка.
  - **НЕ освобождает память немедленно.** Физическое освобождение отложено до `FUN_1401b6360` в конце тика.
- **Evidence Path:**
  - `docs/evidence/ghidra_2022/G_FUNC_1401a02b0.json`
  - `docs/evidence/ghidra_2022/G_INSPECT_1401d3460.json`
  - `docs/evidence/ghidra_2022/G_INSPECT_1401d34aa.json`
- **Confidence Level:** **VERIFIED STATIC** + **RUNTIME OBSERVED**.

---

### 2.4. `kexSlaveActor::Spawn`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401a02f0`
- **RVA:** `0x1a02f0`
- **File Offset:** `0x19f6f0`
- **ABI / Сигнатура:**
  ```cpp
  void __fastcall Spawn(
      kexSlaveActor* actor // RCX (виртуальный метод vtable + 0x38)
  );
  ```
- **Known Callers:**
  - `kexSlaveObjectFactoryLocal::SpawnActor` (`0x1401d4780`) для всех акторов, не являющихся `kexSlavePuppet`.
  - Базовый вызов из начала `kexSlavePuppet::Spawn` (`0x1401dbc60`).
  - Базовый вызов из начала `kexSlaveAI::Spawn` (`0x1401953d0`).
- **Verified Preconditions:**
  - `actor->node != nullptr` (узел уже создан в `LinkSector`).
  - `actor->sectorIndex` валиден.
  - `actor + 0x170` содержит валидный `ActorDef*` (или `nullptr`).
- **Verified Side Effects:**
  - Вызывает `FUN_1401d34d0`: регистрирует адреса `origin` (`+0x18`) и `rotation` (`+0x30`) как интерполянты в `DAT_14063d368`.
  - Устанавливает локальный bounding box: от `[-32, -32, -32]` до `[+32, +32, +32]`.
  - Очищает дескрипторы целей и владельцев (`+0x140`, `+0x148`, `+0x190`...`+0x1a8`).
  - Копирует флаги сущности из `ActorDef` в `actor + 0x16c`.
  - Запрашивает высоты пола и потолка через трассировку геометрии сектора (`FUN_1401a4060`, `FUN_1401a3eb0`).
  - **ПОЛНОЕ ОТСУТСТВИЕ СВЯЗЕЙ С ИГРОКОМ:** Функция не обращается к `GameInstance->GetPlayer()` и не модифицирует синглтон игрока.
- **Evidence Path:** `docs/evidence/ghidra_2022/G_FUNC_1401a02f0.json`.
- **Confidence Level:** **VERIFIED STATIC**.

---

### 2.5. `kexSlavePuppet::Spawn`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401dbc60`
- **RVA:** `0x1dbc60`
- **File Offset:** `0x1db060`
- **ABI / Сигнатура:**
  ```cpp
  ulonglong __fastcall Spawn(
      kexSlavePuppet* puppet // RCX (виртуальный метод kexSlavePuppet::vftable + 0x38)
  );
  ```
- **Known Callers:**
  - `kexSlaveObjectFactoryLocal::SpawnActor` (`0x1401d4780`) при `actorType == 0` (`AT_PLAYER`).
- **Verified Preconditions:**
  - `puppet != nullptr`.
  - `GameInstance` проинициализирован.
- **Verified Side Effects (Singleton Theft):**
  - Вызывает базовый `kexSlaveActor::Spawn(puppet)` (`0x1401a02f0`).
  - Вызывает `GameInstance->GetPlayer()` (`0x1401b5520`).
  - **Безусловно заменяет узел игрока:**
    `Player + 0xd8 = Puppet + 0x10` с инкрементом refcount нового узла (`LOCK INC [node + 0x40]`) и декрементом старого узла игрока.
  - Устанавливает обратную связь: `Puppet + 0x1b8 = Player*`.
- **Критический архитектурный вывод:**
  **НЕ ДОПУСКАЕТСЯ К ИСПОЛЬЗОВАНИЮ НА КЛИЕНТЕ.** Попытка спавна `kexSlavePuppet` на клиенте украдёт управление локальным персонажем.
- **Evidence Path:**
  - `docs/evidence/ghidra_2022/G_FUNC_1401dbc60.json`
  - `docs/evidence/m0/C01/RESULT.md`
- **Confidence Level:** **VERIFIED STATIC** + **RUNTIME OBSERVED**.

---

### 2.6. `SceneRenderer::RenderSectors` & `DrawActorBillboard`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401a2580` (`RenderSectors`), `0x1401a34c0` (`DrawActorBillboard`)
- **RVA:** `0x1a2580`, `0x1a34c0`
- **File Offset:** `0x1a1980`, `0x1a28c0`
- **ABI / Сигнатура:**
  ```cpp
  void __fastcall RenderSectors(void* renderContext, char renderPass);
  void __fastcall DrawActorBillboard(
      void* renderContext,     // RCX
      char renderPass,         // DL
      uint yawAngle,           // R8D
      const float* origin,     // R9
      float scale,             // [RSP+0x28]
      uint64_t colorMask       // [RSP+0x30]
  );
  ```
- **Known Callers:**
  - `GameInstance::RenderState` (`0x1401b7700`) при `state == 3` (активный геймплей).
- **Known Callees:**
  - `FUN_140070920` (PVS / Portal culling видимости сектора).
  - `kexBatchedVertices::AddQuad` (`0x140055fb0`): передача 4 вершин спрайта в GPU-буфер.
- **Verified Filter Logic:**
  - Итерирует секторы в пирамиде видимости камеры.
  - В каждом секторе обходит `sector->actorList` (`node + 0x28` $\to$ `actor`).
  - Проверяет `actor->InstanceOf<kexSlaveActor>()` (`0x140a432e0`).
  - **Проверяет равенство локальному игроку:** `if (actor != localPlayerPuppet)` (строка 543). Локальный игрок первого лица не рисуется.
  - **Проверяет флаг скрытия:** `if ((actor->flags & 0x1000000) == 0)` (`AF_HIDDEN`).
  - Для всех остальных акторов вызывает `DrawActorBillboard`.
- **Evidence Path:** `docs/evidence/ghidra_2022/G_FUNC_1401a2580.json`.
- **Confidence Level:** **VERIFIED STATIC**.

---

### 2.7. `kexSlaveObjectNode::DeferredSweep`

- **Exact Binary SHA-256:** `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`
- **Preferred VA:** `0x1401b6360`
- **RVA:** `0x1b6360`
- **File Offset:** `0x1b5760`
- **ABI / Сигнатура:** `void __fastcall DeferredSweep(void)`.
- **Known Callers:**
  - Вызывается в конце каждого кадра симуляции в `GameInstance::StateTick` (`0x1401b6ec0`).
- **Verified Behavior:**
  - Обходит список узлов, ожидающих уничтожения (`DAT_140aec7d8`).
  - Проверяет атомарный счетчик ссылок `node->refcount` (`*(int*)(node + 0x40)`).
  - Если `refcount == 0`:
    1. Проверяет `node->payload->InstanceOf<kexSlaveObject>()`.
    2. Вызывает виртуальный деструктор `payload->vtable[0x28]()`.
    3. Возвращает память актора в пул через аллокатор `0x1405a10a0`.
    4. Исключает узел `ObjectNode` из списка сектора.
    5. Обнуляет поля узла и возвращает узел в пул узлов.
  - Если `refcount > 0`: узел остаётся в списке до следующего кадра (pending destruction).
- **Evidence Path:** `docs/evidence/ghidra_2022/G_FUNC_1401b6360.json`.
- **Confidence Level:** **VERIFIED STATIC** + **RUNTIME OBSERVED**.
