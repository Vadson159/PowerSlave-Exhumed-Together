# PowerSlave Exhumed Together — DOC-58B Adversarial Lifecycle QA Report

**Document ID:** `REPORT-ADVERSARIAL-LIFECYCLE-QA`  
**Classification:** Adversarial Verification, Fault Injection & False-PASS Audit  
**Author:** Gemini 3.8 High, Independent QA & Multiplayer Networking Specialist  
**Date:** 2026-11-03  
**Status:** COMPLETE (Critical Audit Completed; DOC-58B NOT started, NOT declared ready)  
**Working Directory:** `tools/network-visual-proxy-readiness/adversarial/`  
**Executable:** `tools/network-visual-proxy-readiness/adversarial/build/Release/test_adversarial_lifecycle.exe`  
**Executable SHA-256:** `42a27ff6ea657f22be0c68686ffb041cfb1c3c92bb888b1397dc2095f32eb424`  

---

## 1. Общие результаты стресс-тестирования

В рамках проверки собственной реализации `VisualProxyStateMachine` на ложные PASS, нарушения времени жизни (lifetime) и некорректную обработку сетевых сбоев выполнен стресс-тест из **12 сценариев с инжекцией неисправностей**.

### Сводная статистика (Accounting):
- **Всего сценариев:** 12
- **Истинных обнаружений дефектов (`TRUE_DETECTION`):** 10
- **Ложных срабатываний / ложных допущений (`FALSE_PASS`):** 1 (ADV-11)
- **Прямых нарушений собственного контракта (`FAIL`):** 1 (ADV-02)
- **Отложенных проверок (`DEFERRED`):** 0 (все 12 автономных сценариев выполнены)

Финальный JSON-отчёт сохранён в:  
[`tools/network-visual-proxy-readiness/adversarial/TEST_RESULTS.json`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/adversarial/TEST_RESULTS.json).

---

## 2. Детальные результаты по 12 adversarial-сценариям

| ID | Сценарий | Внедренная неисправность | Ожидаемое поведение | Фактическое поведение | Классификация |
|:---:|---|---|---|---|:---:|
| **ADV-01** | **Failed Spawn** | Нативная фабрика вернула `nullptr` (OOM / невалидный сектор). | Возврат в `ABSENT`, `m_nativeActorPtr == nullptr`, возможность повторного спавна при следующем валидном пакете. | Ошибка обработана чисто, указатель `nullptr`, состояние `ABSENT`, следующий пакет успешно инициировал спавн. | `TRUE_DETECTION` |
| **ADV-02** | **Failed Remove** | `removeVisualActor()` вернул `false` (KEX не смог освободить актор). | Сохранение состояния `REMOVE_PENDING` и указателя для повторной попытки / блокировки утечки. | **ДЕФЕКТ:** автомат сбросил указатель в `nullptr` и выставил `ABSENT`, бросив живой нативный актор в KEX. | **`FAIL`** |
| **ADV-03** | **Shutdown During Pending Spawn** | Экстренное завершение работы во время `SPAWN_PENDING`. | Отмена запроса на спавн, переход в `ABSENT`, 0 вызовов фабрики. | `forceShutdown` корректно сбросил pending spawn, вызов фабрики заблокирован. | `TRUE_DETECTION` |
| **ADV-04** | **Shutdown During Pending Remove** | Экстренное завершение во время `REMOVE_PENDING`. | Однократный вызов `removeVisualActor`, отсутствие повторных вызовов и обращения к null/stale pointer. | Вызван ровно 1 раз через `forceShutdown`, последующий вызов вернул `false`. | `TRUE_DETECTION` |
| **ADV-05** | **Recycled Pointer / ABA** | Аллокатор KEX выделил тот же числовой адрес памяти под новый объект другого поколения. | События старого поколения не могут обновить или удалить новый объект. | C++ wrapper отфильтровал старый пакет по `connectionGen`/`entityGen`. KEX naked pointers риска подтверждены. | `TRUE_DETECTION` |
| **ADV-06** | **Stale Snapshot After Reconnect** | В очередь поступил пакет со старой `connectionGeneration` (1 вместо 2). | Отклонение Guard (`Verdict::Identity`), состояние `ABSENT`. | Отклонено с `IdentityMismatch`, прокси не активирован. | `TRUE_DETECTION` |
| **ADV-07** | **Epoch Invalidation** | Пакет со старой `mapEpoch` поступил после смены уровня/эпохи. | Отклонение Guard, состояние `ABSENT`. | Отклонено с `IdentityMismatch`, прокси не активирован. | `TRUE_DETECTION` |
| **ADV-08** | **Wrong Slot / Local Player Protection** | Поток снапшотов с чередованием слота 0 и слота 1. | Слот 1 (локальный игрок) строго отбрасывается, спавн разрешен только для слота 0 (Host P1). | Слот 1 отклонен с `WrongSlot`, ровно 1 спавн для слота 0, локальный игрок не дублирован. | `TRUE_DETECTION` |
| **ADV-09** | **Unsafe Thread (Thread Affinity)** | Вызовы Spawn/Update/Remove/forceShutdown с чужого TID (Worker). | Отказ в вызове, отсутствие обращений к native seam. | Заблокированы все операции с чужого TID. Выявлен риск `std::runtime_error` для KEX. | `TRUE_DETECTION` |
| **ADV-10** | **Transform After Removal** | Запоздавший пакет пришел после подтвержденного удаления актора. | Запрет вызова `updateVisualTransform` на освобожденном акторе. | Вызовы обновления заблокированы, счетчик `transforms` заморожен на момент удаления. | `TRUE_DETECTION` |
| **ADV-11** | **Invalid Native Sector** | Координаты игрока находятся в Секторе 176, а коду передан сектор 259. | Отказ или валидация сектора перед спавном. | **ЛОЖНЫЙ PASS:** автомат слепо привязал актор к Сектору 259, создав пространственную коллизию в KEX. | **`FALSE_PASS`** |
| **ADV-12** | **Real Source Contract Mismatch** | Сверка структур `BridgePlayerSnapshot`, `Guard`, `ClientGate`. | Выявление расхождений между спекой v1.10 и реальным кодом. | Подтверждено: отсутствие `sectorId`, нули в скорости, `entityGen == 1`, разделение MapReady и геймплея. | `TRUE_DETECTION` |

---

## 3. Обнаруженные критические проблемы и дефекты

### Проблема 1: Утечка владения при сбое нативного удаления (ADV-02 — FAIL) `[VERIFIED TEST]`
В файле [`include/visual_proxy_state_machine.hpp:252-257`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/include/visual_proxy_state_machine.hpp#L252-L257) метод `executePendingRemove` имел фатальную ошибку:
```cpp
const bool ok = seam->removeVisualActor(m_nativeActorPtr);
m_nativeActorPtr = nullptr;
m_localProxyGen++;
m_state = VisualProxyState::ABSENT;
m_removeCount++;
return ok;
```
Если нативное удаление не удалось (`ok == false`), автомат состояний всё равно обнулял `m_nativeActorPtr` и переводил состояние в `ABSENT`. Это приводит к потере контроля над нативным объектом: актор остаётся в списках секторов KEX, а обёртка считает, что его нет. Создан минимальный воспроизводящий пример [`repro_adv02_failed_remove.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/adversarial/repro_adv02_failed_remove.cpp).

### Проблема 2: Фатальная иллюзия Сектора 259 (ADV-11 — FALSE_PASS) `[VERIFIED TEST]`
Предыдущие рекомендации Gemini строились на допущении, что в Tomb of Ramses начальный сектор всегда равен 259.  
**Это допущение опровергнуто реальным опытом DOC-58A:** Host P1 может уйти в коридор (Сектор 176 или 123) до того, как Client B завершит подключение и получит первый снапшот.  
Спавн актора в Сектор 259 с координатами Сектора 176 приводит к тому, что актор привязывается к списку `sectors[259].actorList`, а геометрически находится в Секторе 176. В KEX это вызывает:
1. **Невидимость:** при отрисовке Сектора 176 движок итерирует только список 176 — прокси не рисуется.
2. **Сбой отсечения по порталам:** при отрисовке Сектора 259 движок видит узел за пределами сектора, что ломает frustum/portal culling.
Создан минимальный воспроизводящий пример [`repro_adv11_sector_fallacy.cpp`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/adversarial/repro_adv11_sector_fallacy.cpp).

### Проблема 3: Выброс `std::runtime_error` на границе KEX (ADV-09) `[VERIFIED SOURCE]`
Метод `assertSimulationThread()` выбрасывал `std::runtime_error`. В продакшене вызовы KEX происходят через нативные C-хуки MinHook. Непойманное C++ исключение приведёт к немедленному `std::terminate()` и аварийному закрытию игры без сохранения логов. Требуется мягкий fail-closed отказ без исключений.

---

## 4. Критический статический аудит процедуры Shutdown (§4 ТЗ) `[VERIFIED SOURCE]`

В предыдущем документе `SOL_HANDOFF.md` рекомендовался синхронный вызов `g.visualProxy.forceShutdown(g.nativeSeam)` в `Session::stop()`.

### Результаты аудита:
1. **Потоковый контекст:** `Session::stop()` вызывается из `stop(reason)`, который защищён проверкой `if (!thread()) return;`. То есть вызов происходит на потоке симуляции KEX.
2. **Инвалидация мира (Level Invalidation):** `Session::stop()` вызывается в том числе при событии `StateExit` (переход в меню или смена уровня). В этот момент движок KEX вызывает `kexSlaveLevelLocal::Unload` (`0x1401c90e0`), который освобождает массивы секторов.
3. **Опасность `Drain`:** Нативный `Remove` вызывает `Drain` (`0x1401b61d0`), обращающийся к `sectors[actor->sectorId]`. Если уровень уже выгружен, вызов `removeVisualActor()` выполнит Use-After-Free запись в освобожденную память кучи KEX!
4. **Вывод:** Синхронный `forceShutdown` ДОПУСТИМ только если проверено, что мир KEX всё ещё активен (`state == 3 && next == 3 && pending == 0` и level counts > 0). Если мир уже инвалидирован, нативный `Remove` вызывать **категорически запрещено** — KEX уже освободил память уровня.

---

## 5. Ответы на 8 обязательных вопросов ТЗ

### 1. Сколько тестов выполнено?
**12 тестов** (ADV-01 по ADV-12).

### 2. Сколько настоящих fault detections?
**10 сценариев** (`TRUE_DETECTION`).

### 3. Сколько false-PASS?
**1 сценарий** (`FALSE_PASS`): ADV-11 (хардкод Сектора 259).

### 4. Какие проблемы обнаружены?
- Утечка владения при возврате `false` из `removeVisualActor()` (ADV-02).
- Слепой спавн в неверный сектор при движении хоста до подключения (ADV-11).
- Небезопасность выброса `std::runtime_error` в нативном коде KEX (ADV-09).
- Опасность Use-After-Free при вызове `forceShutdown` после `LevelUnload` (§4).

### 5. Какие предыдущие рекомендации Gemini неверны или не доказаны?
- **НЕВЕРНО:** Предположение о постоянстве Сектора 259 для Tomb of Ramses.
- **НЕВЕРНО:** Рекомендация безусловного вызова `forceShutdown()` в `Session::stop()` без проверки статуса `LevelUnload`.
- **НЕВЕРНО:** Допущение, что счетчик `localProxyGen` сам по себе защищает нативную память KEX от ABA-эффекта (он защищает только C++ обёртку).

### 6. Какие изменения понадобятся перед DOC-58B?
1. Внести патч в `executePendingRemove()`: запретить сброс указателя и переход в `ABSENT` при ошибке нативного удаления.
2. Заменить выброс исключений в `assertSimulationThread()` на возврат кодов ошибок.
3. Добавить в `forceShutdown()` проверку валидности уровня KEX (`isLevelValid`).
4. Ввести контракт получения корректного сектора (через `PointInSector` или проверку стартового witness).

### 7. Какие вопросы могут быть решены только после завершения DOC-58A?
- Реализация нативного шва смены секторов `transitionSector()` через `LinkSector` (`0x1401d3d70`).
- Нативная интерполяция позиции через subobject `+0x50` и reset `0x1401d3e60`.
- Определение нативного сектора по мировым координатам (`PointInSector`).

### 8. Какие native lifecycle контракты остаются UNKNOWN?
- `[UNKNOWN]` Поведение `LinkSector` при быстром пересечении нескольких секторальных порталов за один сетевой тик.
- `[UNKNOWN]` Корректная процедура отложенной сборки (`Sweep` `0x1401b6360`) при выходе из игры через `DirectQuit`.
- `[UNKNOWN]` Способ безопасного спавна прокси, если Host P1 загрузился в произвольной точке карты вне зоны видимости стартового сектора.

---

## 6. Заключение

Автономный стресс-тест успешно выявил слабые места и ложные допущения в архитектурном плане DOC-58B до того, как они попали в production C++ или привели к сбоям на живом KEX.

Все замечания, исправления и предупреждения оформлены в документе [`HANDOFF_CORRECTIONS.md`](file:///S:/_Vibe_Coding/PowerSlave_Exhumed_Together/PowerSlave_Exhumed_Together_mod/tools/network-visual-proxy-readiness/adversarial/HANDOFF_CORRECTIONS.md) и готовы для использования Claude Opus 5.5 High и Sol Extra.

**DOC-58B НЕ НАЧАТ И НЕ ОБЪЯВЛЯЕТСЯ ПРИНЯТЫМ. РАБОТА ОСТАНОВЛЕНА.**
