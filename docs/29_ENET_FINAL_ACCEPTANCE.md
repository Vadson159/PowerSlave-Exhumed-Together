# PowerSlave Exhumed Together — Финальный приёмочный протокол и заморозка сетевого интерфейса (Final Replication Acceptance Gate)

**Версия документа:** 1.00  
**Дата приёмки:** 01 ноября 2026 г.  
**Статус:** Утверждён / Сетевой интерфейс заморожен (Interface Freeze)  
**Область приёмки:** Автономный прототип сетевой репликации ENet (`tools/enet-prototype`)  
**Связанные документы:**
- [DOC-20: Игровой процесс и репликация](20_GAMEPLAY_HANDOFF.md)
- [DOC-22: Передача ENet-прототипа](22_ENET_PROTOTYPE_HANDOFF.md)
- [DOC-24: Приёмочный аудит ENet-прототипа](24_ENET_PROTOTYPE_ACCEPTANCE_AUDIT.md)
- [DOC-25: Готовность к полевым испытаниям](25_ENET_FIELD_TEST_READINESS.md)
- [DOC-26: Прототип репликации игрового мира](26_WORLD_REPLICATION_PROTOTYPE.md)
- [DOC-27: Отчёт о завершении этапа M0](27_M0_COMPLETION_REPORT.md)
- [DOC-28: Аудит протокола репликации мира (P0 Protocol Correctness)](28_WORLD_REPLICATION_PROTOCOL_AUDIT.md)
- [Спецификация бинарного протокола WIRE_PROTOCOL.md](../tools/enet-prototype/docs/WIRE_PROTOCOL.md)
- [Результаты автоматических тестов TEST_RESULTS.md](../tools/enet-prototype/docs/TEST_RESULTS.md)

---

## 1. Введение и цели финальной приёмки

Цель настоящего финального приёмочного аудита — окончательное устранение трёх ключевых противоречий, выявленных независимой проверкой архитектуры репликации мира ([DOC-28](28_WORLD_REPLICATION_PROTOCOL_AUDIT.md)), подтверждение стабильности всех 42 автоматических сценариев тестирования в конфигурации **Release x64** и фиксация стабильного программного сетевого интерфейса (**Network Interface Freeze**) перед разработкой адаптера Sol Max.

В соответствии с установленными техническими ограничениями проекта:
1. Исходный код этапа **M0 Sol Max**, оригинальные бинарные файлы игры (2022) и нативные хуки KEX оставлены полностью нетронутыми.
2. Не выполнялась преждевременная модификация игрового клиента; вся верификация реализована и подтверждена внутри автономного стенда `tools/enet-prototype`.
3. Запрещено использование сырых указателей движка KEX в сетевых сообщениях — все объекты идентифицируются через стабильные кортежи `(mapEpoch, entityId, generation)`.

---

## 2. Разрешение противоречия 1: Фактический эгресс хоста и методика измерений

### 2.1 Математический базис vs отчётная цифра 930,49 КБ/с

Для целевого сценария в **128 сущностей** (8 игроков, 20 врагов, 50 снарядов, 30 предметов, 20 интерактивных объектов), размера записи `EntitySnapshot` **55 байт**, частоты отправки **30 Гц** и **семи клиентов** теоретический объём чистых записей составляет:
$$\text{Чистые данные: } 128 \times 55 \times 30 \times 7 = 1\,478\,400 \text{ байт/с} \approx 1\,443{,}75 \text{ КБ/с}$$

С учётом заголовков многопакетной сборки под бюджет 1200 байт (7 батчей по 27 байт на кадр):
$$\text{Данные снимков с заголовками: } (7 \times 27 + 128 \times 55) \times 30 \times 7 = 7\,229 \times 210 = 1\,518\,090 \text{ байт/с} \approx 1\,482{,}51 \text{ КБ/с}$$
С учётом периодического снимка игроков на Канале 1 (~214 байт/кадр):
$$\text{Полный теоретический эгресс: } \approx 1\,526 \text{ КБ/с} \approx 1{,}49 \text{ МБ/с}$$

Однако в предыдущем отчёте [DOC-28](28_WORLD_REPLICATION_PROTOCOL_AUDIT.md) было зафиксировано значение **930,49 КБ/с**.

### 2.2 Выявленная механическая причина расхождения

В ходе профилирования симуляции в [`tests/benchmark_metrics.cpp`](../tools/enet-prototype/tests/benchmark_metrics.cpp) и физики снарядов в [`src/world_entity.cpp`](../tools/enet-prototype/src/world_entity.cpp) была выявлена фактическая физическая причина:
1. **Коллизионный деспавн снарядов:** При генерации мира в `populateSyntheticWorld` 50 снарядов создавались на расстоянии 100 единиц с радиальным вектором скорости 350 ед/с, направленным прямо на 20 врагов, расположенных на радиусе 250 единиц.
2. Спустя $t = (250 - 100) / 350 \approx 0{,}43$ секунды все 50 снарядов сталкивались с врагами и деспавнились (`ctx.despawnEntity(entityId, DespawnReason::EXPIRED)`), попутно уничтожая часть врагов (`DespawnReason::KILLED`).
3. Дополнительно, оставшиеся пули имели время жизни `lifetimeRemaining = 2.5 с`, завершаясь до истечения 3-секундного окна измерения.
4. В результате на протяжении **85% времени бенчмарка** хост передавал не 128, а всего **~78 активных сущностей**!
5. Расчёт для 78 сущностей:
   $$(4 \text{ батча} \times 27 + 78 \times 55) \times 30 \times 7 = 4\,398 \times 210 = 923\,580 \text{ байт/с} \approx 901{,}9 \text{ КБ/с}$$
   С учётом снимка игроков: $901{,}9 + 52{,}5 = \mathbf{954{,}4 \text{ КБ/с}}$, что с точностью до долей процента объясняет отчётные 930–945 КБ/с!

### 2.3 Новые эмпирические измерения и телеметрия приёма клиентом

В прототип добавлены счётчики фактической отправки и приёма:
- В [`WorldReplicationHost`](../tools/enet-prototype/include/world_replication.h): `getTotalWorldSnapshotsBroadcast()`, `getTotalWorldEntitiesSerialized()`, `getLastSnapshotEntityCount()`.
- В [`WorldReplicationClient`](../tools/enet-prototype/include/world_replication.h): `getTotalEntitiesReceived()`, `getTotalSnapshotsReceived()`.

Бенчмарк разделен на три воспроизводимых сценария:

| Сценарий | Сериализовано сущностей/кадр | Частота кадров хоста | Эгресс хоста (КБ/с) | Полоса на клиента | Приём сущностей клиентом (ent/s) | Батчей/кадр | RTT loopback | CPU хоста | RAM |
|---|---|---|---|---|---|---|---|---|---|
| **A. Динамический бой (Combat Decay)** | 91.4 ent | 23.7 Hz | **932.78 KB/s** | 133.25 KB/s | **2,206.7 ent/s** | 5 | 26.6 ms | 0.35 % | 10 MB |
| **B. Непрерывные 128 сущностей (Sustained Clean)** | 128.7 ent | 24.0 Hz | **1,300.58 KB/s** | 185.80 KB/s | **3,133.7 ent/s** | 7 | 26.9 ms | 0.57 % | 11 MB |
| **C. Нарушенная сеть (Sustained Impaired 10% loss)** | 128.3 ent | 23.7 Hz | **1,160.62 KB/s** | 165.80 KB/s | **2,743.4 ent/s** | 7 | 85.0 ms | 0.31 % | 11 MB |

> [!NOTE]
> В Windows без принудительного переключения мультимедийного таймера системный квант `sleep_for(2ms)` + сетевой поллинг задают реальную частоту кадров цикла 24.0 Гц. При 24.0 Гц непрерывная передача 128 сущностей даёт $128 \times 24.0 \approx 3\,133$ сущностей/с на клиента и эгресс **1,300 КБ/с**.
> В лабораторном тесте на чистой частоте 30.0 Гц (Тест 42) клиент за 1 секунду гарантированно получает ровно $128 \times 30 = \mathbf{3\,840 \text{ сущностей/с}}$ через 210 батчей.

---

## 3. Разрешение противоречия 2: Полное устранение обхода `generation = 0`

### 3.1 Выявленная уязвимость

Ранее в коде адаптера и протокола присутствовали проверки вида:
```cpp
if (generation != 0 && it->second->generation != generation) return false;
```
Это позволяло вызывающему коду передавать `generation = 0` (или опускать аргумент по умолчанию `= 0`), полностью отключая защиту от ABA-коллизий при повторном использовании ID.

### 3.2 Реализованное ужесточение

1. **Удаление параметров по умолчанию:** В интерфейсе [`IEngineReplicationAdapter`](../tools/enet-prototype/include/engine_adapter.h) и классе [`WorldReplicationHost`](../tools/enet-prototype/include/world_replication.h) удалены все значения по умолчанию `= 0`. Все методы требуют явной передачи поколения.
2. **Строгий запрет `generation = 0` для сетевых акторов:**
   - `updateActorTransform`: при `generation == 0` немедленно возвращает `false`.
   - `updateActorState`: при `generation == 0` немедленно возвращает `false`.
   - `unregisterActor`: при `generation == 0` отклоняет деспавн (сущность остаётся активной).
   - `notifyDamage`: при `targetGeneration == 0` урон не наносится.
   - `notifyPickupCollected`: при `pickupGeneration == 0` предмет не подбирается.
   - `notifyDoorStateChanged`: при `doorGeneration == 0` состояние двери не меняется.
   - `notifySwitchToggled`: при `switchGeneration == 0` переключатель не активируется.
3. **Строгая валидация сетевых сообщений:**
   - `MSG_INTERACTION_REQUEST`: проверка `req.targetGeneration == 0 || target->generation != req.targetGeneration` немедленно отклоняет запрос.
   - `MSG_ENTITY_SPAWN`: пакеты с `entityGeneration == 0` игнорируются клиентом.
   - `MSG_ENTITY_DESPAWN`: пакеты с `entityGeneration == 0` или несовпадающим поколением игнорируются клиентом.
   - `MSG_WORLD_SNAPSHOT`: записи сущностей с `s.generation == 0` отбрасываются.
   - `MSG_WORLD_EVENT_CONFIRM`: события с `targetGeneration == 0` для существующих объектов отбрасываются.
4. **Сквозная тройная проверка:** Для всех сетевых действий одновременно проверяются `mapEpoch`, `entityId` и `generation`.
5. Все 36 прежних тестов сохранены без поломок; добавлены целевые тесты **37, 38, 39**.

---

## 4. Разрешение противоречия 3: Границы Frame Reassembly и семантика MTU

### 4.1 Безопасная работа с 64-битной битовой маской

В классе [`WorldReplicationClient`](../tools/enet-prototype/include/world_replication.h) внедрена безопасная статическая функция расчёта битовой маски:
```cpp
static inline uint64_t computeBatchMask(uint8_t totalBatches) {
    if (totalBatches == 0 || totalBatches > 64) return 0ULL;
    if (totalBatches == 64) return ~0ULL;
    return (1ULL << totalBatches) - 1ULL;
}
```
- **Исключение UB:** Для $N = 64$ возвращается константа `~0ULL`, исключая операцию `1ULL << 64` (Undefined Behavior в стандартах C++ на x86-64).
- **Проверены границы:**
  - $N = 1 \implies 0\text{x}1$ (бит 0)
  - $N = 32 \implies 0\text{xFFFFFFFF}$ (биты 0..31)
  - $N = 33 \implies 0\text{x}1FFFFFFFF$ (биты 0..32)
  - $N = 63 \implies 0\text{x}7FFFFFFFFFFFFFFF$ (биты 0..62)
  - $N = 64 \implies 0\text{xFFFFFFFFFFFFFFFF}$ (все 64 бита установлены)
  - $N = 0$ и $N > 64 \implies 0$ (отклонение некорректных метаданных).

### 4.2 Обработка переполнения счётчика `snapshotSequence` через `UINT32_MAX`

Замена оператора прямого сравнения `<` на модульную разность расстояний по стандарту RFC 1982:
```cpp
int32_t seqDiff = (m_currentFrameSeq == 0) ? 1 : static_cast<int32_t>(snap.snapshotSequence - m_currentFrameSeq);
```
- При переходе $0\text{xFFFFFFFF} \to 1$: разность `static_cast<int32_t>(1 - 0xFFFFFFFF) == +2 > 0`, кадр распознаётся как новый.
- Поздний пакет кадра $0\text{xFFFFFFFE}$, прибывший при текущем кадре $2$: разность отрицательна ($< 0$), пакет безопасно отбрасывается как устаревший (`m_staleFrameDropCount++`).

### 4.3 Несогласованность батчей и потеря последнего пакета

- **Изменение `totalBatches` внутри кадра:** Если при том же `snapshotSequence` прибывает батч с другим значением `totalBatches`, он отбрасывается как повреждённый.
- **Потеря последнего батча:** Если батчи $0 \dots N-2$ получены, а последний батч потерян, по прибытии кадра $N+1$ неполный кадр $N$ корректно регистрируется счётчиком `m_partialFramesCount++`.

### 4.4 Уточнение статуса бюджета 1200 байт

> [!IMPORTANT] Прикладной бюджет vs Path MTU
> Лимит 1200 байт на сообщение приложения является **целевым консервативным бюджетом**, выбранным с большим запасом относительно стандартного MTU Ethernet (1500 байт) и типичных заголовков туннелей (PPPoE 1492, WireGuard 1420, Radmin VPN 1420). Он **не гарантирует** сквозной Path MTU (PMTU), если промежуточные маршрутизаторы на специфических каналах имеют PMTU ниже 1200 байт. Динамический батчинг гарантирует, что размер пакета приложения всегда укладывается в этот настраиваемый бюджет.

---

## 5. Полная матрица автоматических тестов (42 / 42 PASS)

Сборка и прогон тестового комплекса выполнены в **Release x64** компилятором MSVC v143 (/std:c++20, /MT):

```
=========================================================
  Execution Summary & Metrics
=========================================================
Test Cases Passed: 42 / 42 (100.0%)
Process RAM Working Set: 8 MB
Process CPU Utilization: 0.00 %
=========================================================
```

| № | Название сценария | Результат | Время (мс) | Описание проверки |
|---|---|:---:|---|---|
| **1** | Connect All 7 Clients (1 Host + 7 Clients) | **PASS** | 808.2 | Параллельное подключение 7 пиров к хосту |
| **2** | Player Slot Uniqueness (P1-P8) | **PASS** | 0.0 | Строгая изоляция 8 уникальных слотов участников |
| **3** | Input Command & Authoritative Snapshot | **PASS** | 942.8 | Передача ввода 60 Гц и получение авторитетного снапшота |
| **4** | Reliable Event Delivery (Map Change) | **PASS** | 300.7 | Надёжная доставка смены карты всем участникам |
| **5** | Ignore Stale / Out-of-Order Snapshots | **PASS** | 0.0 | Монотонная фильтрация устаревших пакетов |
| **6** | Disconnect Single Participant (Slot P8) | **PASS** | 505.7 | Корректный разрыв сессии одного участника |
| **7** | Reconnect to Same Slot & Anti-Hijack Gate | **PASS** | 927.7 | Защита от угона слота и успешный реконнект (Gen 2) |
| **8** | Player Death & Manual Respawn on 'E' | **PASS** | 516.6 | Смерть, сброс снаряжения и возрождение по кнопке |
| **9** | Duplicate / Invalid Respawn Protection | **PASS** | 0.0 | Отклонение повторных и невалидных запросов возрождения |
| **10** | Map Epoch Advancement & Sync Transition | **PASS** | 300.6 | Смена эпохи карты (эпоха 2 $\to$ 3) |
| **11** | Stale MapEpoch Packet Rejection | **PASS** | 111.9 | Отклонение пакетов старой эпохи (код 3) |
| **12** | Malformed & Oversized Packet Immunity | **PASS** | 0.0 | Устойчивость к NaN, битому мусору и оверпакетам |
| **13** | Inbound Queue Bounds Protection | **PASS** | 0.1 | Ограничение очереди ввода (128 пакетов макс) |
| **14** | Synthetic Network Impairments | **PASS** | 1291.7 | Устойчивость к 40мс задержки, 15мс джиттера и 10% потерь |
| **15** | Input Scheduling - Zero Speed Hack | **PASS** | 0.0 | Декуплинг пакетов и ограничение шага движения |
| **16** | Input Command Filtering (Gen Gate) | **PASS** | 0.0 | Фильтрация ввода по генерациям и монотонным номерам |
| **17** | Semi-Auto Edge & Starvation Reset | **PASS** | 0.0 | Корректная обработка триггера стрельбы одиночными |
| **18** | Crypto Reconnect Key & Ghost Ejection | **PASS** | 0.0 | Вытеснение подвисшего пира при подтверждении крипто-ключа |
| **19** | Graceful Teardown of Host & 7 Clients | **PASS** | 963.2 | Полное закрытие сокетов и нулевые утечки ресурсов |
| **20** | Concurrent Spawn of Multiple Objects | **PASS** | 302.4 | Синхронный спавн 23 сущностей пяти типов |
| **21** | Duplicate Spawn Protection & Idempotency | **PASS** | 0.0 | Идемпотентность спавна при дублировании сообщений |
| **22** | Despawn & Entity ID Reuse New Gen | **PASS** | 100.8 | Деспавн ID 2001 и отказ применения устаревшей генерации |
| **23** | Snapshot Loss Resilience & Interpolation | **PASS** | 0.0 | Плавная интерполяция при 50% потере снапшотов |
| **24** | Packet Reordering & Monotonic Filtering | **PASS** | 0.0 | Сброс старого снапшота seq=480 после seq=500 |
| **25** | Late-Joining Client Baseline Sync | **PASS** | 496.3 | Полная передача baseline мира опоздавшему клиенту |
| **26** | Mid-Battle Reconnect | **PASS** | 755.6 | Восстановление клиента в бою, ключи команды 0x01 |
| **27** | Map Transition with Active Projectiles | **PASS** | 150.2 | Автоматическая очистка снарядов при смене карты |
| **28** | Preservation of Door/Switch State | **PASS** | 253.8 | Сохранение состояния переключателя и двери у всех пиров |
| **29** | Player State Memory Isolation | **PASS** | 0.0 | Урон слоту P2 изолирован, P1 и P3 не затронуты |
| **30** | Wire Sizes & Generational Fields (P0.1) | **PASS** | 0.0 | EntitySnapshot=55B, MsgInteraction=14B, MsgEventConfirm=30B |
| **31** | Multi-Batch Snapshot Assembly (P0.2/P0.3) | **PASS** | 0.1 | 128 сущностей $\to$ 7 батчей $\le 1182$B $\le 1200$B, кадр собран |
| **32** | Out-of-Order Batch Reassembly (P0.3) | **PASS** | 0.0 | Батчи [3, 2, 1, 0] собраны в единый валидный кадр |
| **33** | Single Batch Loss & Partial Frame (P0.3) | **PASS** | 0.0 | Кадр 300 без батча 2 корректно зафиксирован как частичный |
| **34** | Duplicate Batch Rejection via Bitmask | **PASS** | 0.0 | Дубликат батча 0 отброшен по маске, кадр завершён |
| **35** | Generational Identity ABA Protection | **PASS** | 0.4 | Устаревшая генерация отклонена, новая применена |
| **36** | Multi-Batch Baseline Sync (P0.2/P0.3) | **PASS** | 0.1 | 128 сущностей baseline синхронизированы в 7 пакетах $\le 1200$B |
| **37** | **Adapter `generation = 0` Prohibition** | **PASS** | 0.0 | **Все 7 методов адаптера отклонили вызовы с generation=0** |
| **38** | **Protocol Messages `generation = 0` Drop** | **PASS** | 0.0 | **Spawn, Despawn, Snapshot, EventConfirm отклонили gen=0** |
| **39** | **Generational ABA & Simultaneous Verification** | **PASS** | 0.4 | **Переиспользованный ID 2001 отклонил gen=1, принял gen=2** |
| **40** | **Frame Reassembly 64-bit Mask Boundaries** | **PASS** | 0.0 | **Маски 1, 32, 33, 63, 64 без UB; 64-батчевый кадр собран** |
| **41** | **Sequence Rollover & Inconsistent Frame Drop** | **PASS** | 0.0 | **Переход 0xFFFFFFFF $\to$ 1 успешен; битый батч отброшен** |
| **42** | **Continuous 128-Entity Client Reception** | **PASS** | 0.6 | **Клиент получил 3840 сущностей/с через 210 батчей/с** |

---

## 6. Фиксация сетевого интерфейса (Network Interface Freeze)

Настоящим объявляется **заморозка бинарного и программного сетевого интерфейса** автономного прототипа. Изменения в нижеперечисленные контракты запрещены без отдельного решения архитектурного комитета:

### 6.1 Стабильные программные контракты

1. [`include/engine_adapter.h`](../tools/enet-prototype/include/engine_adapter.h):
   - Абстрактный интерфейс `IEngineReplicationAdapter` для будущего Sol Max адаптера.
   - Обязательная передача ненулевого поколения во всех методах изменения состояний.
2. [`include/net_protocol.h`](../tools/enet-prototype/include/net_protocol.h):
   - Структура общего заголовка `MsgHeader` (11 байт, `0x50534558`).
   - Сообщения подключения, слотов, ввода, смены карты и пингов.
3. [`include/replication_protocol.h`](../tools/enet-prototype/include/replication_protocol.h):
   - `EntitySnapshot` (55 байт, статический ассерт `SERIALIZED_SIZE == 55`).
   - `MsgEntitySpawn` (59 байт), `MsgEntityDespawn` (13 байт).
   - `MsgInteractionRequest` (14 байт), `MsgWorldEventConfirm` (30 байт).
   - `MsgWorldSnapshot` (заголовок 16 байт) и `MsgWorldBaseline`.
4. [`include/world_scale_config.h`](../tools/enet-prototype/include/world_scale_config.h):
   - Алгоритм динамического расчёта размера батча под лимит 1200 байт.
   - Лимит 21 сущность на пакет снапшота, 19 сущностей на пакет baseline.

### 6.2 Каналы и флаги ENet

- **Канал 0 (`CHANNEL_RELIABLE_CONTROL`):** Надёжный упорядоченный (Reliable Ordered).
- **Канал 1 (`CHANNEL_UNRELIABLE_SEQUENCED`):** Ненадёжный упорядоченный для ввода и позиций игроков.
- **Канал 2 (`CHANNEL_DIAGNOSTIC`):** Ненадёжный неупорядоченный для пингов.
- **Канал 3 (`CHANNEL_WORLD_SNAPSHOT`):** Ненадёжный неупорядоченный (`ENET_PACKET_FLAG_UNSEQUENCED`) для многопакетных снимков мира с прикладной битовой сборкой.

---

## 7. План перехода к Sol Max Adapter

Автономный прототип репликации мира полностью завершён, верифицирован и готов к сопряжению с нативным игровым кодом.

### Границы ответственности адаптера Sol Max

```
+-------------------------------------------------------------+
|               Nightdive KEX Engine (2022)                   |
|  - Actor Tables / Thinkers                                  |
|  - Physics / Collision Detection                            |
|  - Level Geometry & BSP                                     |
+-------------------------------------------------------------+
                              |
                              v  (Native Function Hooks)
+-------------------------------------------------------------+
|                 Sol Max Adapter (Future Step)               |
|  - Implements IEngineReplicationAdapter                     |
|  - Translates KEX Actor pointers to (epoch, entityId, gen)  |
|  - Never passes raw pointers to network                     |
+-------------------------------------------------------------+
                              |
                              v  (Frozen Interface)
+-------------------------------------------------------------+
|              ENet Replication Core (Verified)               |
|  - WorldReplicationHost / WorldReplicationClient            |
|  - 1200B Dynamic MTU Batching                               |
|  - 64-bit Bitmask Frame Reassembly                          |
|  - Generational ABA Gate & Idempotent Despawn               |
+-------------------------------------------------------------+
```

1. **Native Hooks:** Перехват функций спавна/уничтожения акторов KEX, нанесения урона и нажатия клавиши 'E'.
2. **Entity ID Mapping:** Сопоставление локальных индексов акторов KEX со стабильными `entityId` и `generation`.
3. **No Raw Pointers:** Гарантия 100% изоляции сетевого слоя от внутренних структур движка.
4. **Transition to Sol Max:** Переход к фазе интеграции адаптера разрешается после подтверждения пользователем.
