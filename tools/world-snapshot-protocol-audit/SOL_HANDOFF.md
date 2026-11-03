# Sol Max Handoff: Минимальный Host → Client Snapshot PoC

**Document ID:** `SOL-HANDOFF-DOC54-SNAPSHOT-POC`  
**Target:** Sol Max (Native Hooking & Integration Lead)  
**Author:** Senior Multiplayer Protocol Engineer / C++ Network QA Engineer  
**Scope:** Подготовка ограниченного этапа **Host A $\to$ Client B: передача нативных позиций P1/P2, приём и логирование на клиенте** (без акторов, без визуального прокси, без интерполяции).

---

## 1. Главный вывод аудита для Sol Max

**Да, Wire Protocol v1.10 (`MsgPlayerSnapshot` 0x20) можно переиспользовать БЕЗ изменения бинарного формата пакета на проводе.**

Формат сообщения на проводе полностью сформирован, протестирован в автономном комплексе (`14/14 PASS`) и обладает достаточной вместимостью и защитой (bounds check, NaN/Inf reject, MTU-safety).

Однако **внутримодульные C++ стыки (ThreadBridge DTO и ENetWorker)** содержат конкретные пробелы, которые необходимо устранить перед натурным запуском в двух KEX-процессах.

---

## 2. Что можно безопасно переиспользовать БЕЗ изменений

1. **Бинарный формат `MsgPlayerSnapshot` (Opcode `0x20`):**
   - Файл: `tools/enet-prototype/include/net_protocol.h`.
   - Сериализация и десериализация проверены байт-в-байт. Менять поля, типы или порядок байтов на проводе **запрещено**.
2. **Параметры безопасности ByteReader / ByteWriter:**
   - Автоматическая санитизация `NaN` и `Infinity` в `0.0f` при сериализации.
   - Строгий отказ при приёме невалидных чисел с плавающей точкой из сети.
   - Ограничение `playerCount <= MAX_PLAYERS (8)`.
3. **Механизм распределения очередей SPSC в `ThreadBridge`:**
   - Очередь `m_outboundQueue` (Capacity 256) на хосте.
   - Очередь `m_inboundQueue` (Capacity 128) на клиенте.
   - Дисциплина ролей `SIMULATION` и `NETWORK` остаётся неизменной (DOC-40).

---

## 3. Что НЕ нужно делать на первом этапе (Strictly Out of Scope)

Первый этап нативной интеграции репликации **НЕ ДОЛЖЕН**:
- Спавнить `kexActor` или `kexSlavePuppet` на клиенте.
- Создавать Visual Player Proxy в мире KEX.
- Вызывать `kexActor::LinkSector` или мутировать граф сцены клиента.
- Подключать 3rd-person спрайты (`john.kpf`).
- Реализовывать клиентскую интерполяцию или задержку отображения.
- Трогать камеру локального игрока Client B.
- Переключать карты или менять жизненный цикл уровня.
- Реализовывать World Replication (врагов, предметы, двери, урон).

> **Цель PoC:** Только сквозная передача чисел — Simulation Host читает $(X, Y, Z, \text{yaw}, \text{pitch})$ для P1 и P2, передаёт через ENet, а Simulation Client B принимает их и записывает в диагностический журнал.

---

## 4. Конкретные изменения, необходимые перед запуском PoC

### 4.1. Добавить `mapEpoch` в `BridgePlayerSnapshot` DTO
**Файл:** `tools/enet-prototype/include/thread_bridge_dto.h` (строка 93).
- **Проблема:** В структуре `BridgePlayerSnapshot` есть `slotId`, `entityId`, `entityGeneration`, `sequence`, `serverTick`, но **нет `mapEpoch`**!
- **Следствие:** На хосте `enet_worker.cpp:605` оставляет `ps.mapEpoch = 0`. При смене карты или рассинхроне клиент не сможет отсечь устаревший снапшот.
- **Действие:**
  ```cpp
  struct BridgePlayerSnapshot {
      uint8_t  slotId = 0;
      uint32_t entityId = 0;
      uint32_t entityGeneration = 1;
      uint32_t mapEpoch = 1; // <--- ДОБАВИТЬ ЭТО ПОЛЕ
      uint32_t sequence = 0;
      uint32_t serverTick = 0;
      // ... остальные поля без изменений ...
  };
  ```

### 4.2. Исправить привязку `mapEpoch` при отправке и приёме в `ENetWorker`
**Файл:** `tools/enet-prototype/src/enet_worker.cpp`.
- **При отправке (строка 605):**
  ```cpp
  ps.snapshotSequence = val.sequence;
  ps.serverTick = val.serverTick;
  ps.mapEpoch = val.mapEpoch; // <--- ДОБАВИТЬ ПРИВЯЗКУ
  ```
- **При приёме (строка 851):**
  ```cpp
  // Передавать mapEpoch из ps в DTO:
  deliverInbound(BridgePlayerSnapshot::fromNet(p, ps.snapshotSequence, ps.serverTick, ps.mapEpoch));
  ```

### 4.3. Выбор канала ENet: Channel 1 vs Channel 3
- **Текущее состояние в коде:** В `enet_worker.cpp:610` вызов `m_hostServer->broadcastPacket(CHANNEL_UNRELIABLE_SEQUENCED, encoded, false)`. То есть снапшоты игроков идут по **Channel 1**.
- **Канал 3 (`CHANNEL_WORLD_SNAPSHOT`):** Используется строкой 637 для `MsgWorldSnapshot`.
- **Решение для PoC:**
  - Для минимального PoC 2 игроков передача по **Channel 1** (`CHANNEL_UNRELIABLE_SEQUENCED`) **допустима и безопасна**: объём снапшота на 2 игроков составляет всего 154 байта (при 30 Гц это ~4.6 KB/s). В ENet очереди каналов независимы по направлениям (Host $\to$ Client и Client $\to$ Host).
  - При желании полностью изолировать поток репликации от команд управления на уровне ENet каналов, достаточно переключить строку 610 на `CHANNEL_WORLD_SNAPSHOT`. Оба хоста создаются с `CHANNEL_COUNT = 4`, поэтому Channel 3 полностью готов и существует в транспортном пуле.

---

## 5. Что Sol Max обязан проверить в нативном коде KEX (Simulation Thread)

1. **Чтение Transform на хосте:**
   - P1: `primaryPlayer = GameInstance + 0x40`. Позиция: `P1->Actor() + 0x18`, углы: `P1 + 0x38` (pitch, yaw).
   - P2: Контекст `P2` в статической памяти DLL мода. Позиция: `P2->Actor() + 0x18`.
   - **Важно:** Чтение производить **строго после завершения оригинального `Update` и нативного движения P2**, до сброса тика.
2. **Не путать `connectionGeneration` и `entityGeneration`:**
   - `connectionGeneration` берётся из реестра сессий ENet (`HostSession`).
   - Для P1 (хост) `connectionGeneration` всегда равен 1.
   - Для P2 (клиент) `connectionGeneration` равен поколению подключённого пира.
3. **Безопасная передача в ThreadBridge:**
   - Вызов `m_bridge.pushOutbound(std::move(ps))` выполнять **только на потоке симуляции** (`StateTick`).
   - Если очередь заполнена (`pushOutbound` вернул отказ) — для best-effort снапшота это штатная ситуация: пакет просто пропускается, паниковать и вызывать `requestResync` не нужно.

---

## 6. Критерии приёмки (PASS) для будущего PoC

| № | Критерий | Способ доказательства |
|---|---|---|
| 1 | **Host Simulation Dispatch** | Хост формирует и успешно пушит `BridgePlayerSnapshot` для P1 и P2 в `pushOutbound` без аффинити-ошибок. |
| 2 | **Host ENet Worker Send** | Worker хоста отправляет UDP дейтаграммы `MsgPlayerSnapshot` (Opcode 0x20) размером 89 или 154 байта. |
| 3 | **Client ENet Worker Recv** | Worker клиента принимает UDP пакет, десериализует `MsgPlayerSnapshot` без ошибок (`deserialize == true`). |
| 4 | **Client Inbound Delivery** | Worker клиента успешно доставляет DTO в `m_bridge.pushInbound()`. |
| 5 | **Client Simulation Drain** | Поток симуляции клиента (`StateTick` TID) вычитывает DTO из `drainInbound()`. |
| 6 | **Numeric Coordinate Match** | Координаты $(X, Y, Z)$ удалённого игрока в логе клиента с точностью до плавающей точки совпадают с координатами в логе хоста. |
| 7 | **Zero KEX Access from Worker** | Ни один сетевой воркер не вызывает KEX-функций и не читает память KEX. |
| 8 | **Clean OS Exit** | Оба KEX-процесса завершаются штатно (Exit Code 0), как в DOC-52/53. |

---

## 7. Чего категорически нельзя делать

1. **Не memcpy структуру `PlayerStateSnapshot` в память KEX.** Внутренний layout актора `kexSlavePuppet` (`0x1e0` байт) и игрока (`0x198` байт) абсолютно не совпадает со структурой сетевого пакета.
2. **Не слать снапшоты до получения `MsgMapReady` / подтверждения устойчивого геймплея.**
3. **Не создавать второй `kexSlavePlayer` на клиенте.**
4. **Не делать блокирующих вызовов сокетов на Simulation Thread.**
