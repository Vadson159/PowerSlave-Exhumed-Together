# Wire Protocol v1.10 Implementation & Verification Matrix

**Document ID:** `AUDIT-DOC54-PROTOCOL-MATRIX`  
**Classification:** Network Systems Engineering & Protocol Audit  
**Author:** Senior Multiplayer Protocol Engineer / C++ Network QA Engineer  
**Target Specification:** PowerSlave Exhumed Together Wire Protocol v1.10 (`tools/enet-prototype/include/net_protocol.h`)  
**Audit Date:** 2026-11-02  
**Status:** COMPLETE / VERIFIED

---

## 1. Сводная статусная матрица сообщений протокола

В таблице ниже приведён статус каждого сообщения Wire Protocol v1.10 по уровням зрелости:
- **L1 — DEFINED IN PROTOCOL:** Сообщение имеет константу в `enum NetMessageType` и спецификацию в коде.
- **L2 — SERIALIZED IN C++:** Реализованы `serialize()` / `deserialize()` в `net_protocol.h`.
- **L3 — SUPPORTED IN ENET_WORKER:** `ENetWorker` (`enet_worker.cpp`) имеет обработчик приёма/отправки.
- **L4 — USED IN NATIVE KEX RUNTIME:** Сообщение генерируется или потребляется реальной DLL мода в процессе `anubis_Shipping_Steam_x64.exe`.
- **L5 — FULL INTEGRATION PROVEN:** Сквозное двустороннее взаимодействие хост-клиент подтверждено в реальных тестах KEX с доказанным OS Exit 0.

| Opcode | Имя сообщения | Канал ENet по коду | Направление | L1 (Def) | L2 (Ser) | L3 (Worker) | L4 (Native KEX) | L5 (Proven MP) | Фактический размер (байт) |
|---|---|---|---|:---:|:---:|:---:|:---:|:---:|---|
| `0x00` | `MSG_NONE` | — | — | YES | NO | NO | NO | NO | 0 |
| `0x01` | `MSG_HELLO` | `0` (Control) | Client $\to$ Host | YES | YES | YES | NO* | NO* | 11 + (18..194) = 29..205 B |
| `0x02` | `MSG_WELCOME` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO* | NO* | 11 + (27..59) = 38..70 B |
| `0x03` | `MSG_REJECT` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO* | NO* | 11 + (3..35) = 14..46 B |
| `0x04` | `MSG_PLAYER_JOIN` | `0` (Control) | Broadcast | YES | YES | NO** | NO | NO | 11 + (9..89) = 20..100 B |
| `0x05` | `MSG_PLAYER_LEAVE` | `0` (Control) | Broadcast | YES | YES | NO** | NO | NO | 11 + (4..52) = 15..63 B |
| `0x10` | `MSG_INPUT_COMMAND` | `1` (Unreliable) | Client $\to$ Host | YES | YES | YES | **YES** | **PARTIAL** (DOC-51/53) | 11 + 30 = **41 B** (фиксированный) |
| `0x20` | `MSG_PLAYER_SNAPSHOT` | `1` (Unreliable) | Host $\to$ Client | YES | YES | **PARTIAL** | **NO** | **NO** | 11 + 13 + $N \times 65$ B (см. ниже) |
| `0x21` | `MSG_ENTITY_SPAWN` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 59 = 70 B |
| `0x22` | `MSG_ENTITY_DESPAWN` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 13 = 24 B |
| `0x23` | `MSG_WORLD_SNAPSHOT` | `3` (WorldSnap) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 16 + $N \times 55$ B |
| `0x24` | `MSG_WORLD_BASELINE` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 16 + $N \times 59$ B |
| `0x30` | `MSG_WORLD_EVENT` | `0` (Control) | Bidirectional | YES | YES | NO*** | NO | NO | 11 + (10..42) = 21..53 B |
| `0x31` | `MSG_INTERACTION_REQUEST` | `0` (Control) | Client $\to$ Host | YES | YES | YES | NO | NO | 11 + 14 = 25 B |
| `0x32` | `MSG_WORLD_EVENT_CONFIRM` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 30 = 41 B |
| `0x35` | `MSG_MAP_CHANGE` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + (6..38) = 17..49 B |
| `0x36` | `MSG_MAP_READY` | `0` (Control) | Client $\to$ Host | YES | YES | YES | NO | NO | 11 + 5 = 16 B |
| `0x40` | `MSG_RESPAWN_REQUEST` | `0` (Control) | Client $\to$ Host | YES | YES | YES | NO | NO | 11 + 9 = 20 B |
| `0x41` | `MSG_RESPAWN_CONFIRM` | `0` (Control) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 13 = 24 B |
| `0x50` | `MSG_PING` | `2` (Diagnostic) | Client $\to$ Host | YES | YES | YES | NO | NO | 11 + 12 = 23 B |
| `0x51` | `MSG_PONG` | `2` (Diagnostic) | Host $\to$ Client | YES | YES | YES | NO | NO | 11 + 12 = 23 B |

*\*Примечание к L4/L5:* В тестах DOC-51 и DOC-53 использовался нативный ENet handshake (сообщения `MSG_HELLO`, `MSG_WELCOME` и внутренний `M12A_INPUT_ONLY` domain) между DLL хоста и DLL клиента, однако это был изолированный входной стенд управления $W/S$, без синхронизации мира.  
*\*\*Примечание:* Вместо `MSG_PLAYER_JOIN` / `LEAVE` текущий `ENetWorker` транслирует события подключения/отключения через внутренний DTO `BridgeSessionEvent` (`CLIENT_JOINED`, `CLIENT_LEFT`, `RECONNECTED`).  
*\*\*\*Примечание:* `MSG_WORLD_EVENT` замещён парой `MSG_INTERACTION_REQUEST` / `MSG_WORLD_EVENT_CONFIRM` по результатам аудита DOC-28.

---

## 2. Детальный разбор сообщений, критических для MVP репликации

### 2.1. `MsgHeader` (Заголовок кадра Wire Protocol)
- **Opcode:** Присутствует в каждом пакете в поле `msgType`.
- **Размер сериализации:** Строго **11 байт** (`static_assert(MsgHeader::SERIALIZED_SIZE == 11)`).
- **Порядок байтов (Endianness):** Little-Endian (стандарт x86/x64 Windows).

| Смещение (байт) | Поле | Тип | Размер | Диапазон / Значение |
|---|---|---|---|---|
| `0 .. 3` | `magic` | `uint32_t` | 4 B | `0x50534558` ('P','S','E','X' $\to$ на проводе `58 45 53 50`) |
| `4 .. 5` | `version` | `uint16_t` | 2 B | `0x0110` (v1.10 $\to$ на проводе `10 01`) |
| `6` | `msgType` | `uint8_t` | 1 B | Opcode сообщения (`0x01 .. 0x51`) |
| `7 .. 10` | `sequence` | `uint32_t` | 4 B | Порядковый номер пакета отправителя |

---

### 2.2. `MsgPlayerSnapshot` (Opcode `0x20`)
- **Назначение:** Высокочастотная передача авторитетного кинематического состояния игроков от Host к Client.
- **Заголовок сообщения (без MsgHeader):** 13 байт.
- **Размер записи одного игрока (`PlayerStateSnapshot`):** **65 байт** (доказано тестом `T01_PlayerStateSnapshot_ExactSize`).
- **Формула размера пакета:**
  $$\text{WireSize}(N) = 11_{\text{MsgHeader}} + 13_{\text{SnapHeader}} + (N \times 65_{\text{PlayerState}}) = \mathbf{24 + 65 \cdot N \text{ байт}}$$

#### Детальная таблица смещений заголовка `MsgPlayerSnapshot` (смещение от начала payload):
| Смещение | Поле | Тип | Размер | Назначение |
|---|---|---|---|---|
| `+0x00 .. +0x03` | `snapshotSequence` | `uint32_t` | 4 B | Монотонный номер снапшота хоста |
| `+0x04 .. +0x07` | `serverTick` | `uint32_t` | 4 B | Тик симуляции сервера (60 Гц) |
| `+0x08 .. +0x0B` | `mapEpoch` | `uint32_t` | 4 B | Эпоха текущей карты |
| `+0x0C` | `playerCount` | `uint8_t` | 1 B | Количество игроков в массиве ($0 \dots 8$) |

#### Детальная таблица смещений одной записи `PlayerStateSnapshot` (смещение от начала записи):
| Смещение | Поле | Тип | Размер | Ограничения и валидация |
|---|---|---|---|---|
| `+0x00` | `slotId` | `uint8_t` | 1 B | $0 \dots 7$ |
| `+0x01 .. +0x04` | `entityId` | `uint32_t` | 4 B | $1001 \dots 1008$ |
| `+0x05 .. +0x08` | `entityGeneration` | `uint32_t` | 4 B | Поколение сущности в мире |
| `+0x09 .. +0x0C` | `posX` | `float` | 4 B | IEEE 754, finite only (NaN/Inf reject) |
| `+0x0D .. +0x10` | `posY` | `float` | 4 B | IEEE 754, finite only |
| `+0x11 .. +0x14` | `posZ` | `float` | 4 B | IEEE 754, finite only |
| `+0x15 .. +0x18` | `velX` | `float` | 4 B | IEEE 754, finite only |
| `+0x19 .. +0x1C` | `velY` | `float` | 4 B | IEEE 754, finite only |
| `+0x1D .. +0x20` | `velZ` | `float` | 4 B | IEEE 754, finite only |
| `+0x21 .. +0x24` | `yaw` | `float` | 4 B | В градусах $[-180.0 \dots +180.0]$ |
| `+0x25 .. +0x28` | `pitch` | `float` | 4 B | В градусах $[-90.0 \dots +90.0]$ |
| `+0x29 .. +0x2C` | `health` | `int32_t` | 4 B | Текущее здоровье ($0 \dots 200$) |
| `+0x2D .. +0x30` | `armor` | `int32_t` | 4 B | Текущая броня ($0 \dots 200$) |
| `+0x31` | `activeWeapon` | `uint8_t` | 1 B | Слот оружия ($0 \dots 7$) |
| `+0x32 .. +0x35` | `weaponMask` | `uint32_t` | 4 B | Битовая маска имеющегося оружия |
| `+0x36 .. +0x37` | `ammo` | `uint16_t` | 2 B | Боезапас активного оружия |
| `+0x38` | `lifeState` | `uint8_t` | 1 B | $0=\text{ALIVE}, 1=\text{DYING}, 2=\text{DEAD}, 3=\text{RESPAWN}$ |
| `+0x39 .. +0x3C` | `buttons` | `uint32_t` | 4 B | Битовые флаги клавиш (Attack, Jump и др.) |
| `+0x3D .. +0x40` | `deathSequence` | `uint32_t` | 4 B | Счётчик смертей для идемпотентного респавна |
| **ИТОГО** | — | — | **65 B** | **Точный размер сериализации** |

---

### 2.3. `MsgInputCommand` (Opcode `0x10`)
- **Назначение:** Высокочастотная передача управления Client $\to$ Host (60 Гц).
- **Размер:** 11 байт `MsgHeader` + 30 байт payload = **41 байт** (фиксированный).
- **Канал:** `CHANNEL_UNRELIABLE_SEQUENCED = 1`.
- **Поля payload:** `slotId` (1B), `connectionGeneration` (4B), `commandSequence` (4B), `clientTick` (4B), `pitch` (4B), `yaw` (4B), `moveForward` (4B), `moveRight` (4B), `buttons` (4B), `selectedWeapon` (1B).
- **Статус:** **VERIFIED RUNTIME** в тестах DOC-51 и DOC-53. Единственное сообщение, полностью интегрированное в нативный KEX simulation loop на текущий момент.

---

### 2.4. `MsgMapChange` (Opcode `0x35`) и `MsgMapReady` (Opcode `0x36`)
- **Назначение:** Двухфазный барьер смены карты.
- **MsgMapChange:** `newMapEpoch` (4B) + `mapName` (2B length + N bytes string, max 32B). Канал `0` (Reliable). Размер: $17 \dots 49$ байт.
- **MsgMapReady:** `slotId` (1B) + `mapEpoch` (4B). Канал `0` (Reliable). Размер: **16 байт**.
- **Статус:** Проверены в mock-симуляциях DOC-40 (A4/F3), но **не подключены к нативному загрузчику KEX** (`LoadMap`).

---

## 3. Сравнение фактического Wire-протокола с утверждениями DOC-54

| Положение в DOC-54 | Фактическое состояние в коде | Статус аудита | Влияние на разработку |
|---|---|---|---|
| Размер `PlayerStateSnapshot` = **61 байт** | Размер `PlayerStateSnapshot` = **65 байт** | **REFUTED** (Арифметическая ошибка в DOC-54) | Пакет на 2 игроков равен **154 байтам**, а не 146. На 8 игроков — **544 байта**, а не 512. Оба размера безопасны для MTU. |
| Пакет отправляется по **Channel 3** (`CHANNEL_WORLD_SNAPSHOT`) | `enet_worker.cpp:610` вызывает отправку по **Channel 1** (`CHANNEL_UNRELIABLE_SEQUENCED`) | **REFUTED** (Расхождение реализации с DOC-54) | В коде Channel 3 зарезервирован под `MsgWorldSnapshot`. Для отправки `MsgPlayerSnapshot` по Channel 3 требуется правка строки 610. |
| В `BridgePlayerSnapshot` передаётся **`mapEpoch`** | В `BridgePlayerSnapshot` (`thread_bridge_dto.h:93`) поле `mapEpoch` **отсутствует** | **REFUTED** (Пробел в DTO) | `ps.mapEpoch` на хосте уходит как `0`, а клиентский Worker игнорирует поле при приёме. Требуется добавить `mapEpoch` в DTO. |
| Клиент сразу батчит всех игроков в единый снапшот | `enet_worker.cpp:605` создаёт `MsgPlayerSnapshot` с `players.size() == 1` на каждый DTO | **REFUTED** (Поточечная отправка) | Текущий код шлёт по UDP-пакету на каждого игрока (2 пакета по 89 байт вместо одного на 154 байта). |
| Репликация игроков готова к запуску в KEX | В `experiments/m1/` код отправки и приёма снапшотов **полностью отсутствует** | **REFUTED** (Отсутствие нативного кода) | Готов только транспорт прототипа; в KEX DLL снапшоты ещё не интегрированы. |
