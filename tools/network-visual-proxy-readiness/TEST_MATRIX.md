# PowerSlave Exhumed Together — DOC-58B Preparation: Test Matrix

**Document ID:** `TEST-MATRIX-NETWORK-VISUAL-PROXY-READINESS`  
**Classification:** Autonomous QA Test Matrix & Verification Evidence  
**Author:** Gemini 3.8 High, Independent Networking & QA Engineer  
**Date:** 2026-11-03  
**Working Directory:** `tools/network-visual-proxy-readiness/`  
**Target Executable:** `tools/network-visual-proxy-readiness/build/Release/test_visual_proxy_readiness.exe`  
**Executable SHA-256:** `c9832239236eae3e35d90b28cb063cd6c8f7d838f2939505adc9e10d213a83c0`  
**Summary:** 17 Total Checks | **16 PASS** | **0 FAIL** | **1 DEFERRED**  

---

## 1. Классификация компонентов тестового стенда

1. **Реальный C++ существующей кодовой базы:**
   - `pse::MsgPlayerSnapshot` и `pse::PlayerStateSnapshot` (`tools/enet-prototype/include/net_protocol.h`)
   - `pse::BridgePlayerSnapshot` (`tools/enet-prototype/include/thread_bridge_dto.h`)
   - `snapshot55::Guard` (`experiments/m1/native_snapshot/src/snapshot_contract.hpp`)
   - `map56::ClientGate` (`experiments/m1/native_map_ready/src/map_identity.hpp`)
   - `pse::ThreadBridge` (`tools/enet-prototype/include/thread_bridge.h`)
   - `pse_core.lib` (ENet transport core, thread bridge, metrics)
2. **Автономные тесты будущей State Machine:**
   - `pse::proxy::VisualProxyStateMachine` (`tools/network-visual-proxy-readiness/include/visual_proxy_state_machine.hpp`)
   - Валидация жизненного цикла: `ABSENT -> SPAWN_PENDING -> ACTIVE -> REMOVE_PENDING -> ABSENT`
   - Защита потоковой аффинности (`assertSimulationThread()`)
   - Защита от ABA / повторного использования указателей (`localProxyGen`)
3. **Mock / Fixture компоненты:**
   - `MockVisualProxySeam` (`tests/test_visual_proxy_readiness.cpp`): имитирует вызовы нативной фабрики KEX `SpawnActor`, `updateVisualTransform`, `transitionSector` и `removeVisualActor`.
4. **Непроверенные гипотезы (DEFERRED):**
   - Нативное перемещение актора между секторами KEX через `LinkSector` на живом процессе игры (ожидает завершения DOC-58A).

---

## 2. Сводная матрица тестов (17 сценариев)

| ID | Сценарий | Ожидаемое поведение | Фактический статус реализации | Тестовая команда | Результат | Evidence / Лог | Статус |
|:---:|---|---|---|---|---|---|:---:|
| **1** | **Первый корректный Host P1 snapshot** | Переход `ABSENT -> SPAWN_PENDING`, затем на безопасном тике вызов `spawnVisualActor` и переход в `ACTIVE`. Указатель актора сохранен, `spawnCount == 1`. | Реализовано в `VisualProxyStateMachine` с интеграцией `snapshot55::Guard`. | `workflow.py run` (Test 1) | Состояние ACTIVE, указатель валиден, 1 спавн. | `[PASS] Test 1: First valid Host P1 snapshot - ABSENT -> SPAWN_PENDING -> ACTIVE transition successful` | **PASS** |
| **2** | **Duplicate snapshot** | Снапшот с тем же `sequence` и `tick` отклоняется Guard (`Verdict::Stale`). Позиция активного актора не меняется, повторный спавн не вызывается. | Реализовано через монотонную проверку Guard. | `workflow.py run` (Test 2) | Отклонено с `StaleSequence`, `transforms` пуст. | `[PASS] Test 2: Duplicate snapshot - Rejected duplicate sequence without modifying active transform` | **PASS** |
| **3** | **Out-of-order snapshot** | Снапшот с меньшим `sequence` отклоняется как устаревший. Состояние прокси не деградирует. | Реализовано через Guard по слотам. | `workflow.py run` (Test 3) | Отклонено с `StaleSequence`, позиция сохранена. | `[PASS] Test 3: Out-of-order snapshot - Out-of-order sequence dropped monotonically` | **PASS** |
| **4** | **Wrong epoch** | Снапшот с чужой `mapEpoch` отклоняется (`Verdict::Identity`). Прокси остается в `ABSENT`. | Реализовано проверкой `s.mapEpoch == m_activeEpoch`. | `workflow.py run` (Test 4) | Отклонено с `IdentityMismatch`, прокси ABSENT. | `[PASS] Test 4: Wrong epoch - Snapshot with mismatched mapEpoch rejected by Guard` | **PASS** |
| **5** | **Wrong connection generation** | Снапшот с устаревшей `connectionGeneration` отклоняется (`Verdict::Identity`). | Реализовано проверкой генерации сессии. | `workflow.py run` (Test 5) | Отклонено с `IdentityMismatch`, прокси ABSENT. | `[PASS] Test 5: Wrong connection generation - Mismatched connectionGeneration rejected` | **PASS** |
| **6** | **Wrong slot** | Снапшоты слотов 1 (Client P2) и 2+ фильтруются. Прокси Host P1 реагирует строго на `slotId == 0`. Предотвращен спавн дубликата локальной куклы. | Реализовано строгим фильтром `s.slotId == m_targetSlot`. | `workflow.py run` (Test 6) | Отклонено с `WrongSlot`, локальный прокси не создан. | `[PASS] Test 6: Wrong slot - Non-zero slots filtered out, preventing local puppet proxy duplication` | **PASS** |
| **7** | **Snapshot before MapReady** | Снапшот, пришедший до завершения рукопожатия карты (`mapReady == false`), отклоняется Guard (`Verdict::NotReady`). Спавн не инициируется. | Реализовано проверкой шлюза готовности карты. | `workflow.py run` (Test 7) | Отклонено с `NotReady`, прокси ABSENT. | `[PASS] Test 7: Snapshot before MapReady - Snapshot received before MapReady rejected by Guard` | **PASS** |
| **8** | **Disconnect while active** | При сетевом разрыве переход `ACTIVE -> REMOVE_PENDING`, вызов `removeVisualActor`, обнуление указателя, переход в `ABSENT`. | Реализовано в `onDisconnect()` и `executePendingRemove()`. | `workflow.py run` (Test 8) | Актор корректно удален через seam, прокси в ABSENT. | `[PASS] Test 8: Disconnect while active - ACTIVE -> REMOVE_PENDING -> ABSENT completed safely on disconnect` | **PASS** |
| **9** | **Stale queued DTO after disconnect** | Застрявший в очереди DTO, извлеченный после disconnect, отбрасывается сессионным флагом без воскрешения актора. | Реализовано проверкой `m_sessionActive`. | `workflow.py run` (Test 9) | Отклонено с `SessionInactive`, спавн не вызван. | `[PASS] Test 9: Stale queued DTO after disconnect - Post-disconnect DTO dropped without resurrecting proxy` | **PASS** |
| **10** | **Map invalidation** | При сбросе уровня (`LevelUnload` / `Transition`) немедленный перевод в `REMOVE_PENDING` и удаление актора до освобождения геометрии KEX. | Реализовано в `onMapInvalidated()`. | `workflow.py run` (Test 10) | Переход в `REMOVE_PENDING` $\to$ `ABSENT`, актор удален. | `[PASS] Test 10: Map invalidation - Map invalidation triggers immediate REMOVE_PENDING -> ABSENT` | **PASS** |
| **11** | **Shutdown with pending removal** | При экстренном закрытии приложения (`forceShutdown`) висящий запрос на удаление выполняется синхронно до отгрузки DLL. | Реализовано в `forceShutdown()`. | `workflow.py run` (Test 11) | Синхронная очистка, нулевые висячие акторы. | `[PASS] Test 11: Shutdown with pending removal - forceShutdown synchronously cleaned up pending removal` | **PASS** |
| **12** | **Repeated spawn request** | Повторные валидные пакеты при состояниях `SPAWN_PENDING` или `ACTIVE` обновляют координаты, но вызывают фабрику спавна ровно один раз. | Реализовано дедупликацией в автомате состояний. | `workflow.py run` (Test 12) | Ровно 1 вызов `spawnVisualActor`, координаты обновлены. | `[PASS] Test 12: Repeated spawn request - Multiple incoming snapshots resulted in exactly one native spawn` | **PASS** |
| **13** | **Double remove attempt** | Повторный или ошибочный вызов `executePendingRemove` при состоянии `ABSENT` является безопасным no-op, не разыменовывает `nullptr`. | Реализовано проверкой `m_state == REMOVE_PENDING`. | `workflow.py run` (Test 13) | Второй вызов вернул `false`, seam вызван 1 раз. | `[PASS] Test 13: Double remove attempt - Second remove attempt was a safe no-op on null actor` | **PASS** |
| **14** | **Actor generation mismatch** | При респавне хоста смена `entityGeneration` фиксируется Guard (`Verdict::EntityMismatch`), переводя прокси в `REMOVE_PENDING` для пересоздания. | Реализовано реакцией на `Verdict::EntityMismatch`. | `workflow.py run` (Test 14) | Зафиксировано расхождение, запрошено удаление старого актора. | `[PASS] Test 14: Actor generation mismatch - Entity generation change detected, scheduled old proxy removal` | **PASS** |
| **15** | **Invalid transform values** | Пакеты, содержащие `NaN` или `Infinity` в координатах XYZ/yaw/pitch, отклоняются Guard (`Verdict::Nonfinite`). | Реализовано проверкой `std::isfinite`. | `workflow.py run` (Test 15) | Отклонено с `NonfiniteTransform`, прокси ABSENT. | `[PASS] Test 15: Invalid transform values - NaN and Infinity transform payloads cleanly rejected` | **PASS** |
| **16** | **Worker/Simulation Thread ownership** | Попытка мутации состояния прокси из потока Worker пресекается guard-проверкой `isSimulationThread()`. | Реализовано потоковой аффинностью `assertSimulationThread()`. | `workflow.py run` (Test 16) | Зафиксировано `AffinityViolation`, прокси не изменен. | `[PASS] Test 16: Worker/Simulation Thread ownership - Direct worker thread mutation rejected by affinity guard` | **PASS** |
| **17** | **Live KEX Dynamic Sector Traversal** | Непрерывное перемещение актора в памяти KEX с перелинковкой списков секторов `LinkSector` на реальном бинарнике игры. | **Ожидает DOC-58A.** В текущем стенде не запускается во избежание конкуренции с Sol. | Не применимо до завершения DOC-58A. | N/A | `[DEFERRED] Test 17: Live KEX Dynamic Sector Traversal (DOC-58A Dependency)` | **DEFERRED** |

---

## 3. Инструкция по воспроизведению тестов

Тестовый набор полностью автономен и не требует запущенных процессов игры или Steam.

### 3.1. Команда полного запуска через Python
```powershell
& 'C:\Python314\python.exe' S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness\workflow.py run
```

### 3.2. Ручная пересборка через CMake
```powershell
& 'C:\Program Files\CMake\bin\cmake.exe' -S S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness -B S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness\build -G "Visual Studio 17 2022" -A x64
& 'C:\Program Files\CMake\bin\cmake.exe' --build S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod\tools\network-visual-proxy-readiness\build --config Release --target test_visual_proxy_readiness
```

### 3.3. Лог выполнения (`RESULT.json`)
Результат выполнения сохраняется в:
`tools/network-visual-proxy-readiness/RESULT.json`
- `exit_code: 0`
- `pass_count: 16`
- `fail_count: 0`
- `deferred_count: 1`
