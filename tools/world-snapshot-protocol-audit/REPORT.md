# Независимый критический аудит сетевого протокола DOC-54 (World Replication MVP Readiness)

**Document ID:** `AUDIT-DOC54-NETWORK-REPORT`  
**Classification:** Critical Network Protocol Audit & QA Engineering Report  
**Author:** Gemini 3.8 Flash High (Senior Multiplayer Protocol Engineer / C++ Network QA Engineer)  
**Date:** 2026-11-02  
**Target Specification:** `docs/54_WORLD_REPLICATION_MVP_READINESS.md`  
**Production Codebase:** `tools/enet-prototype/` (`include/net_protocol.h`, `include/thread_bridge_dto.h`, `src/enet_worker.cpp`)  
**Autonomous Verification Suite:** `tools/world-snapshot-protocol-audit/test_snapshot_wire.exe` (14/14 PASS, 100%)  
**Verdict:** **PARTIAL (Ready for Minimal PoC after 2 targeted DTO/Worker fixes; Full MVP blocked on Client Proxy & Actor implementation)**

---

## 1. Executive Summary

По поручению пользователя проведён независимый критический аудит сетевой части документа `DOC-54-WORLD-REPLICATION-MVP-READINESS`.

Главный исследовательский вопрос:  
**«Можно ли действительно переиспользовать существующий `MsgPlayerSnapshot` из Wire Protocol v1.10 для передачи авторитетного состояния игроков из настоящего KEX Host A в настоящий KEX Client B без изменения формата протокола?»**

### Ключевой вердикт аудита:
1. **По проводу (Wire Format):** **ДА, ПОЛНОСТЬЮ ГОТОВ (READY)**. Сообщение `MsgPlayerSnapshot` (Opcode `0x20`) и структура `PlayerStateSnapshot` в файле `tools/enet-prototype/include/net_protocol.h` полностью самодостаточны, содержат все необходимые поля (позиция, скорость, углы, здоровье, броня, оружие, флаги кнопок, `deathSequence`), защищены от некорректных данных (NaN/Inf) и строго укладываются в бюджет MTU (154 байта на 2 игроков, 544 байта на 8 игроков). Изменять wire-формат протокола **не требуется**.
2. **В коде прототипа (C++ Glue & Thread Bridge):** **PARTIAL (Требуются 2 узких исправления)**.
   - **Дефект DTO:** Во внутреннем транспортном классе `BridgePlayerSnapshot` (`thread_bridge_dto.h`) **отсутствует поле `mapEpoch`**, в результате чего на хосте в пакет пишется `0`, а на клиенте эпоха игнорируется.
   - **Дефект диспетчера Worker:** В `enet_worker.cpp` снапшоты игроков отправляются по `CHANNEL_UNRELIABLE_SEQUENCED` (Channel 1), а не Channel 3, как утверждалось в DOC-54. Кроме того, хост отправляет по отдельному UDP-пакету на каждого игрока (размером 89 байт), а не объединяет их в единый пакет кадра (154 байта).
3. **В нативном KEX (DLL Integration):** **NOT IMPLEMENTED**. В актуальных исходниках нативного адаптера (`experiments/m1/`) код генерации или приёма `MsgPlayerSnapshot` полностью отсутствует. Тесты DOC-51 и DOC-53 передавали исключительно ввод $W/S$ через `MsgInputCommand` (Opcode `0x10`).

---

## 2. Разрешение расхождения размеров: 61 байт vs 65 байт

В документе DOC-54 было допущено арифметическое расхождение: утверждалось, что размер `PlayerStateSnapshot` составляет 61 байт, а общий пакет на 2 игроков — 146 байт.

### 2.1. Точный побайтовый расчёт (Verified Source & Offline Tested)

Анализ исходного кода `PlayerStateSnapshot::serialize()` в `net_protocol.h`:

```cpp
w.writeUint8(slotId);             // +1 B  (offset 0)
w.writeUint32(entityId);          // +4 B  (offset 1..4)
w.writeUint32(entityGeneration);  // +4 B  (offset 5..8)
w.writeFloat(posX);               // +4 B  (offset 9..12)
w.writeFloat(posY);               // +4 B  (offset 13..16)
w.writeFloat(posZ);               // +4 B  (offset 17..20)
w.writeFloat(velX);               // +4 B  (offset 21..24)
w.writeFloat(velY);               // +4 B  (offset 25..28)
w.writeFloat(velZ);               // +4 B  (offset 29..32)
w.writeFloat(yaw);                // +4 B  (offset 33..36)
w.writeFloat(pitch);              // +4 B  (offset 37..40)
w.writeInt32(health);             // +4 B  (offset 41..44)
w.writeInt32(armor);              // +4 B  (offset 45..48)
w.writeUint8(activeWeapon);       // +1 B  (offset 49)
w.writeUint32(weaponMask);        // +4 B  (offset 50..53)
w.writeUint16(ammo);              // +2 B  (offset 54..55)
w.writeUint8(lifeState);          // +1 B  (offset 56)
w.writeUint32(buttons);           // +4 B  (offset 57..60)
w.writeUint32(deathSequence);     // +4 B  (offset 61..64)
// Конец записи: offset 65 байт.
```

**Группировка по типам данных:**
- 3 поля `uint8_t` (`slotId`, `activeWeapon`, `lifeState`): $3 \times 1 = 3 \text{ байта}$.
- 1 поле `uint16_t` (`ammo`): $1 \times 2 = 2 \text{ байта}$.
- 15 полей по 4 байта (`entityId`, `entityGeneration`, 6 координат/скоростей, 2 угла, 2 здоровья/брони, `weaponMask`, `buttons`, `deathSequence`): $15 \times 4 = 60 \text{ байт}$.
- **ИТОГО:** $3 + 2 + 60 = \mathbf{65 \text{ байт}}$.

Автономный тест `T01_PlayerStateSnapshot_ExactSize` в скомпилированном бинарнике `test_snapshot_wire.exe` подтвердил: **размер сериализации составляет ровно 65 байт**.

> [!NOTE]
> **Причина ошибки в DOC-54:** Предыдущий отчёт DOC-54 сложил $3 + 2 + 60$, но механически записал «61 байт», пропустив либо 4 байта `buttons`, либо 4 байта `deathSequence`. Это привело к неверной цепочке вычислений для $N$ игроков.
>
> Также важно отметить, что оператор `sizeof(PlayerStateSnapshot)` в памяти MSVC x64 возвращает **72 байта** из-за компиляторного паддинга выравнивания (3 байта после `slotId`, 3 байта после `activeWeapon`, 1 байт после `lifeState`). Сериализатор же пакует данные без паддинга в 65 байт.

### 2.2. Исправленная таблица размеров сетевых пакетов

| Число игроков ($N$) | Заголовок `MsgHeader` | Заголовок `Snapshot` | Полезная нагрузка ($N \times 65$) | Общий Application Payload | С учётом ENet оверхеда (~10B) | С учётом UDP/IP (~28B) | Оценка для Radmin VPN (~40B оверхед) |
|---|---|---|---|---|---|---|---|
| **$N = 0$** (пустой) | 11 B | 13 B | 0 B | **24 B** | 34 B | 62 B | 102 B |
| **$N = 1$** (1 игрок) | 11 B | 13 B | 65 B | **89 B** | 99 B | 127 B | 167 B |
| **$N = 2$** (Host + Client) | 11 B | 13 B | 130 B | **154 B** *(в DOC-54 было 146)* | 164 B | 192 B | 232 B |
| **$N = 4$** (4 игрока) | 11 B | 13 B | 260 B | **284 B** | 294 B | 322 B | 362 B |
| **$N = 8$** (Полный сервер) | 11 B | 13 B | 520 B | **544 B** *(в DOC-54 было 512)* | 554 B | 582 B | 622 B |

**Анализ MTU и фрагментации:**  
Даже при максимальной нагрузке 8 игроков размер кадра с учётом всех возможных сетевых оверхедов (IP + UDP + ENet + VPN туннель) составляет **622 байта**, что более чем в 2 раза меньше минимального безопасного MTU Интернета (1280 байт для IPv6 / 1472 байта для Ethernet).  
**Риск IP-фрагментации отсутствует полностью.**

---

## 3. Критический аудит ENet-каналов (P3)

В DOC-54 утверждалось, что снапшоты игроков отправляются по каналу `CHANNEL_WORLD_SNAPSHOT = 3`.

### 3.1. Фактическое состояние в коде
Проверка реализаций в каталоге `tools/enet-prototype/`:
1. **Количество каналов:**  
   В `net_protocol.h` объявлено:
   ```cpp
   enum NetChannel : uint8_t {
       CHANNEL_RELIABLE_CONTROL = 0,
       CHANNEL_UNRELIABLE_SEQUENCED = 1,
       CHANNEL_DIAGNOSTIC = 2,
       CHANNEL_WORLD_SNAPSHOT = 3,
       CHANNEL_COUNT = 4
   };
   ```
   И в `enet_host.cpp:52`, и в `enet_client.cpp:31, 44` вызовы `enet_host_create` и `enet_host_connect` явно передают `CHANNEL_COUNT` (4).  
   **Канал 3 действительно существует и аллоцирован.**
2. **Фактическое использование в `enet_worker.cpp`:**
   - Строка 610 (`BridgePlayerSnapshot`):
     ```cpp
     m_hostServer->broadcastPacket(CHANNEL_UNRELIABLE_SEQUENCED, encoded, false);
     ```
     **Снапшоты игроков отправляются по Каналу 1, а НЕ по Каналу 3!**
   - Строка 637 (`BridgeWorldSnapshot`):
     ```cpp
     m_hostServer->broadcastPacket(CHANNEL_WORLD_SNAPSHOT, encoded, false);
     ```
     Канал 3 в текущем коде зарезервирован исключительно под монстров и объекты мира (`MsgWorldSnapshot`).

### 3.2. Анализ влияния на задержку ввода (Input Latency)
- Если и ввод (`MsgInputCommand`), и снапшоты игроков (`MsgPlayerSnapshot`) идут по Каналу 1 (`CHANNEL_UNRELIABLE_SEQUENCED`), возникает ли интерференция?
  - В ENet каждый канал поддерживает **независимые порядковые номера в каждом направлении** (Client $\to$ Host и Host $\to$ Client).
  - Потеря пакета снапшота от хоста **не** задерживает и не блокирует обработку команд ввода от клиента.
  - Однако пакеты снапшотов делят очередь отправки сокета хоста. При объёме 154 байта при 30 Гц это не создаёт очередей в winsock буфере.
- **Рекомендация для PoC:**
  - Оставить отправку по Каналу 1 для минимального PoC (работает «из коробки»).
  - Либо при желании строгой изоляции переключить строку 610 на `CHANNEL_WORLD_SNAPSHOT` (Канал 3). Никаких изменений handshake не потребуется, так как обе стороны уже создают 4 канала.

---

## 4. Аудит последовательностей, поколений и mapEpoch (P4)

### 4.1. Разделение Sequence
- `commandSequence` (Client $\to$ Host): строго валидируется хостом по DOC-50 (`advance <= 1800`, монотонный рост).
- `snapshotSequence` (Host $\to$ Client): генерируется хостом, принимается клиентом.
- Ни в одном месте кода эти два счётчика не сравниваются. Принцип разделения соблюдён.

### 4.2. Опровержение тождества `connectionGeneration == entityGeneration`
В DOC-26 и DOC-54 было высказано предположение, что поколение сущности совпадает с поколением подключения.  
**Аудит доказал, что эти понятия ортогональны:**
1. `connectionGeneration` — это атрибут сетевой сессии пира на хосте (`HostSession`). При реконнекте пира поколение увеличивается ($1 \to 2$).
2. В тестах M1.2a (DOC-44) было доказано: при реконнекте клиента на хосте сохраняется тот же нативный актор `P2` (`Spawn` происходит ровно один раз). В KEX `entityId` и физический актор не пересоздаются.
3. Следовательно, `connectionGeneration` относится к валидации сетевых пакетов, а `entityGeneration` — к жизненному циклу актора в KEX. Их отождествление ошибочно.

### 4.3. Выявленный пробел `mapEpoch` в `BridgePlayerSnapshot`
- В wire-структуре `MsgPlayerSnapshot` поле `mapEpoch` присутствует (смещение `+0x08`).
- Однако в классе `BridgePlayerSnapshot` (`thread_bridge_dto.h:93`) поле `mapEpoch` **было забыто**.
- При сериализации на хосте `ps.mapEpoch` остаётся равным `0`.
- При десериализации на клиенте (`enet_worker.cpp:849`) значение `ps.mapEpoch` считывается из пакета, но выбрасывается, так как фабричный метод `BridgePlayerSnapshot::fromNet` не принимает этот параметр.
- **Статус:** **CRITICAL IMPLEMENTATION GAP**. Перед запуском двух KEX процессов поле `mapEpoch` должно быть добавлено в `BridgePlayerSnapshot` и проброшено в Worker.

---

## 5. Аудит архитектуры ThreadBridge (P5)

Путь движения данных:
$$\text{Host KEX Sim} \xrightarrow{\text{Outbound SPSC}} \text{Host Worker} \xrightarrow{\text{ENet UDP}} \text{Client Worker} \xrightarrow{\text{Inbound SPSC}} \text{Client KEX Sim}$$

1. **Очереди и емкости:**
   - `Host Outbound Queue`: емкость **256** элементов `BridgeMessage`.
   - `Client Inbound Queue`: емкость **128** элементов `BridgeMessage`.
   - При 30 Гц снапшотов (33 мс на кадр) даже при задержке тика симуляции на 100 мс в очереди скопится не более 3–4 сообщений. Переполнение очередей невозможно.
2. **Обработка переполнения (Overflow):**
   - Для `BridgePlayerSnapshot` функция `isCriticalMessage()` возвращает `false` (`BEST_EFFORT`).
   - Если очередь заполнена, воркер или симуляция просто отбрасывают устаревший снапшот (`m_bestEffortDropped++`) без паники и без вызова `requestResync`. Это абсолютно корректное поведение для снапшотов состояния.
3. **Где должен выполняться сериализатор:**
   - Сериализация в байты UDP (`encodePacket`) выполняется **строго на потоке воркера** (`NETWORK` TID).
   - Поток симуляции KEX формирует только легковесный C++ DTO со скалярами. Никаких сетевых вызовов на потоке симуляции нет.

---

## 6. Результаты автономных тестов (P6)

В рамках аудита создан и скомпилирован автономный тестовый комплекс `test_snapshot_wire.exe`, напрямую использующий оригинальный заголовочный файл `net_protocol.h`.

**Результаты запуска (14 проверок из 14 пройдены успешно, 100% PASS):**

```text
=================================================================
PowerSlave Exhumed Together — Wire Protocol v1.10 Autonomous Test
Target: MsgPlayerSnapshot (0x20) & PlayerStateSnapshot
=================================================================

[PASS] T01_PlayerStateSnapshot_ExactSize - Serialized size: 65 bytes (expected exactly 65 bytes)
[PASS] T02_PacketSize_Count_0 - Count 0 -> Wire bytes: 24 (expected 24)
[PASS] T02_PacketSize_Count_1 - Count 1 -> Wire bytes: 89 (expected 89)
[PASS] T02_PacketSize_Count_2 - Count 2 -> Wire bytes: 154 (expected 154)
[PASS] T02_PacketSize_Count_8 - Count 8 -> Wire bytes: 544 (expected 544)
[PASS] T03_RoundTrip_2Players - Round-trip 2 players verified byte-perfect
[PASS] T04_TruncatedPacketRejection - Truncated by 1 byte rejected cleanly: 1, Header only rejected: 1
[PASS] T05_OversizedPlayerCountRejection - playerCount = 9 cleanly rejected by MAX_PLAYERS bound check
[PASS] T06_NaN_Infinity_Handling - Sanitizes to 0.0f on serialize: 1, Rejects raw wire NaN: 1, Rejects raw wire Inf: 1
[PASS] T07_TrailingBytesBehavior - Deserializes payload successfully, leaves remaining trailing bytes: 4
[PASS] T08_HexDumpGeneration - Hex samples generated for 1, 2, 8 players
[PASS] T09_SPEC_StaleSequencePolicy - SPEC ONLY: Sequence monotonic policy check (stale=drop, dup=drop, advance=accept)
[PASS] T10_SPEC_MapEpochGatePolicy - SPEC ONLY: MapEpoch gate strictly enforces current epoch equality
[PASS] T11_SPEC_ConnectionGenerationPolicy - SPEC ONLY: Old connectionGeneration packets dropped upon peer reconnect

=================================================================
Test Summary: 14 PASS, 0 FAIL
=================================================================
```

### Шестнадцатеричный дамп реального пакета на 2 игроков (154 байта):
```text
58 45 53 50 10 01 20 14 00 00 00 14 00 00 00 b0 
04 00 00 01 00 00 00 02 00 e9 03 00 00 01 00 00 
00 00 00 20 41 00 00 a0 41 00 00 f0 41 00 00 00 
3f 00 00 00 3f 00 00 00 00 00 00 00 00 00 00 00 
00 c8 00 00 00 32 00 00 00 01 03 00 00 00 3c 00 
00 00 00 00 00 00 00 00 00 01 ea 03 00 00 01 00 
00 00 00 00 a0 41 00 00 20 42 00 00 70 42 00 00 
00 3f 00 00 00 3f 00 00 00 00 00 00 34 42 00 00 
00 00 c8 00 00 00 32 00 00 00 01 03 00 00 00 3c 
00 00 00 00 00 00 00 00 00 00
```
- Байты `00..10`: `MsgHeader` (Magic `58 45 53 50`, Version `10 01`, Opcode `20`, Seq `14 00 00 00`).
- Байты `11..23`: Заголовок `MsgPlayerSnapshot` (SnapshotSeq 20, ServerTick 1200, MapEpoch 1, Count 2).
- Байты `24..88`: `PlayerStateSnapshot` слота 0 (Хост).
- Байты `89..153`: `PlayerStateSnapshot` слота 1 (Клиент).

---

## 7. Доказанные ошибки и неподтверждённые предположения DOC-54

1. **Ошибка размера:** В DOC-54 заявлен размер `PlayerStateSnapshot` 61 байт. Фактический размер сериализации — **65 байт**. Пакет на 2 игроков равен **154 байтам** (не 146).
2. **Ошибочное утверждение о канале:** В DOC-54 утверждалось, что снапшоты игроков передаются по Каналу 3 (`CHANNEL_WORLD_SNAPSHOT`). В коде `enet_worker.cpp:610` они транслируются по **Каналу 1** (`CHANNEL_UNRELIABLE_SEQUENCED`).
3. **Не замеченный пробел `mapEpoch`:** DOC-54 утверждал, что снапшоты полностью валидируются по `mapEpoch`. В коде DTO `BridgePlayerSnapshot` поле `mapEpoch` **отсутствует**, а Worker отправляет `0`.
4. **Предположение о готовности к запуску:** DOC-54 рассматривал репликацию игроков как готовую к визуализации. В действительности ни на стороне хоста (сбор координат), ни на стороне клиента (приём в KEX DLL) нативный код ещё не написан.

---

## 8. Итоговый вердикт и рекомендация для Sol Max

**Вердикт аудита: PARTIAL.**

Сетевой протокол v1.10 полностью готов и пригоден к использованию без изменения бинарного формата wire-пакетов.  
Для запуска первого нативного PoC (*Host A $\to$ Client B: передача нативных координат P1/P2 и логирование на клиенте*) требуется:
1. Внести 2 узких исправления в транспортный слой:
   - Добавить поле `uint32_t mapEpoch` в `BridgePlayerSnapshot` (`thread_bridge_dto.h`).
   - Пробросить `mapEpoch` при создании и десериализации пакета в `enet_worker.cpp`.
2. На стороне хоста в нативной DLL снимать координаты P1/P2 на Simulation Thread и пушить в Outbound Bridge.
3. На стороне клиента в нативной DLL вычитывать снапшоты на Simulation Thread и писать в лог.
4. Отложить создание визуальных акторов и интерполяцию до успешного прохождения числового PoC.
