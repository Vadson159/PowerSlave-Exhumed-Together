# DOC-58B.2 — Native Actor Ownership & Level Unload Safety

2026-11-03 UTC. **Overall: BOUNDED_PASS. G1 (GATE-PSE-58C-01): VERIFIED (bounded) — LIVE_PROXY_UNLOAD, engine-owned.** Один живой Remote Visual Proxy (stock Actor1007, flags 0x40, сектор 259 Tomb of Ramses) пережил штатный Exit to Title Screen на клиенте: barrier до `Level::Unload`, ноль native-вызовов proxy после barrier, KEX сам уничтожил Actor деструктором из tagged-heap purge и освободил его ObjectNode вместе с node-блоком — после free секторов, без обращения к ним. Исторические DOC-58B (BOUNDED PASS) и DOC-58B.1 (PARTIAL) не изменялись. Исполнитель: Claude Opus 5.5.

Лаборатория: `experiments/m1/native_actor_ownership_58b2/`. Evidence: [`evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/`](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/RESULT_SUMMARY.json). Build `b582/`. TEMP/TMP — `…_58b2/scratch`. Все файлы на S:.

## A. Ownership

| Объект | Владелец | Уничтожение на Exit to Title | Метка |
|---|---|---|---|
| Sector array (`level+0x28`/count `+0x30`) | Level, CRT heap | `free` внутри `Level::Unload 1c90e0` (return `1c92e2`) | VERIFIED static + native |
| Actor1007 `0x615f0140` | **tagged heap `af4490`** (header: magic `0x3151983`, heap `0x140af4490`, dtor-thunk `0x1400d9980`, size 0x1b8, alloc seq 1459) | `c58b0(af4490)` → `c5310` → `d9980` → vt+8 `19f1d0` → `19f210` → `1d32f0`, затем `free(block)` | VERIFIED static + native |
| ObjectNode `0x61ab6230` | 0x1210-node-блок `0x61ab5d00` (слот 17) в **heap `af4600`** | `c58b0(af4600)` освобождает блок целиком; node до конца in_use 1, payload = старый Actor | VERIFIED static + native |
| Sentinels списка сектора (`*(sector+0x78)`) | node-блоки `af4600` | там же | VERIFIED static |
| Proxy | только opaque handle 1 | ничего не освобождает | VERIFIED source + native |

Это **вариант D** (ближе всего к C): engine-owned purge tagged-heap'ов с деструктор-колбэками для объектов фабрики и bulk-free без деструкторов для node-блоков. Ни Remove, ни Drain/OnRemove/PoolReturn/Sweep-retirement на этом пути не выполняются, unlink не делается. Подробно: [ACTOR_OBJECTNODE_LIFETIME_AUDIT](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/ACTOR_OBJECTNODE_LIFETIME_AUDIT.md), [STATIC_OWNERSHIP_GRAPH](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/STATIC_OWNERSHIP_GRAPH.json) (9 рёбер с указанием strong/borrowed, создателя, unlink, free, фазы и exact-build evidence).

Новая static-работа ограничена функциями teardown (Ghidra readOnly/noanalysis, [static/](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/static/)): `c58b0`, `c5310`, `c59e0`, `c5430`, `19ee40`, `1d5360`, `1d3460`, `1d4170`, `1b6360`, `1b6090`, `1ca930`, thunk `d9980`, факторная vtable `52c078`, xrefs `af4490/af4600/af3380/af3388/aec868/aec7d8/aeafa8/af45f0`. RVA проверены по exact EXE, runtime base `0x140000000`, 16-байтные префиксы — по байтам файлов обеих копий и в памяти перед attach.

**UNKNOWN:** другие пути выгрузки (переход на следующую карту, Restart, Reload Checkpoint, смерть, DirectQuit с живым proxy), другие классы Actor, хвост StateExit после purge natively.

## B. Lifecycle (наблюдённый порядок, Sim TID 35060)

| # | UTC | Событие |
|---|---|---|
| 1 | 21:03:39.663 | DOC-56 ClientGate `invalidated` (pending transition) → `barrier58` → ORPHANED (spawn 1 / update 2029 / remove 0) |
| 2 | 21:03:40.663 | StateExit-детур: второй barrier, adapter `worldExit` (weak pointers null, KEX-вызовов нет), seam ExitDetached(5), session stop |
| 3 | 21:03:40.709 | `Level::Unload` entry: sectors `0x615aa040`/270, **removal queue пуста**, identity Actor/Node/блока = Spawn |
| 4 | 21:03:40.728 | `free(0x615aa040)`; Unload return: pointer/count 0 |
| 5 | 21:03:40.728 | `c58b0(af4490)`: dispatch нашего блока → `19f210` (vt `528608` → `505408`, own node не тронут) → `free(0x615f00e0)` (alloc seq совпал); всего 110 объектов (80 kexSlaveActor, 22 `527610`, локальный Puppet `52d198`, …) |
| 6 | 21:03:40.729 | `c58b0(af4600)`: free node-блока `0x61ab5d00` (наш node: payload старый Actor, in_use 1, refs 0); 11 node-блоков |
| 7 | 21:03:43 | Title Screen; 21:05:07 WM_CLOSE клиента → exit 0; 21:05:34 WM_CLOSE хоста → exit 0 |

Opaque handle: на шаге 1 handle = 0, orphaned_handle 1 только диагностический. Поздних разыменований: наблюдатель читал Actor только до `free` его блока и node только до `free` node-блока; нарушений (повторный dispatch/dtor/free) 0. Деструктор и purge статически не обращаются к массиву секторов; proxy не вызывает KEX после barrier. Полный порядок: [UNLOAD_CLEANUP_CALL_ORDER](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/UNLOAD_CLEANUP_CALL_ORDER.json), [SECTOR_FREE_ORDER](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/SECTOR_FREE_ORDER.json), [NATIVE_OWNERSHIP_WITNESSES](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/NATIVE_OWNERSHIP_WITNESSES.jsonl), сырые `N58B2_CLIENT_01/{ownership,native,adapter}.jsonl`.

**Найденная static-опасность (не воспроизводилась):** Remove ставит node в removal queue (`af3380/af3388`, +1 ref), которую опустошает только Drain в live-world ветке StateTick; StateExit её не сбрасывает. Proxy в RETIRING на момент выхода оставил бы ячейку со ссылкой на node, освобождённый purge `af4600`, — UAF в первом Drain следующей карты. Поэтому «Remove перед выгрузкой» на этом пути **опаснее**, чем engine-owned unload.

## C. G1

Канонический контракт DOC-58D.1 registry: barrier до деаллокации секторов; ноль Destroy/Remove proxy после неё. Выполнено и засвидетельствовано: barrier дважды до Unload; сектора живы на входе Unload; remove_calls 0, счётчики update/spawn заморожены; уничтожение Actor — только штатный деструктор KEX в purge после free секторов; handle инвалидирован, exit 0. Применение правила `EngineTeardownWitness` к журналу: все 11 критериев → `ArenaDestroyed` ([G1_NATIVE_ACCEPTANCE](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/G1_NATIVE_ACCEPTANCE.json)).

Это **LIVE_PROXY_UNLOAD**, а не SAFE_PRE_UNLOAD_RETIREMENT: Remove не вызывался.

**Не выполнено (поэтому «bounded»):** sign-off DOC-58D review «10 последовательных переходов карт»; проверка `rcx+0x140` (неверный offset этой сборки, сохранён как расхождение DOC-58B.1); другие пути выгрузки; другие типы Actor; > 1 proxy. Остаточный риск: Remove-in-flight (static hazard), неизученные пути. [NATIVE_GATE_RESULTS](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/NATIVE_GATE_RESULTS.json): 58C-01 VERIFIED (bounded); 58C-05 расширен на title-exit unload; остальные статусы DOC-58B сохранены.

## D. Code

Изменены только новые файлы лаборатории; DOC-58B header, DLL, адаптер, ENet, Wire v1.10 не тронуты. Patch ([решение](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/TEARDOWN_STRATEGY_DECISION.md)): `OrphanCause` в момент barrier; `onEngineTeardownWitness()` → терминальный **ENGINE_RETIRED** только для LiveAtBarrier/RemovePendingNoCall, того же handle, после barrier, на Sim TID, без реентерабельности, однократно; RETIRING на barrier → ORPHANED + `nativeQueueHazard()`, witness всегда отклоняется; ENGINE_RETIRED не снимает barrier и не разрешает Spawn. Восемь исправлений D1–D8 DOC-58B сохранены. **Живой прогон использовал замороженный DOC-58B DLL** (терминал ORPHANED); новый переход проверен offline и replay-правилом на нативном журнале.

| Проверка (MSVC C++20 `/W4 /WX`) | Результат |
|---|---|
| DOC-58B proxy contract (без изменений) | 32/32 |
| DOC-58B.1 lifecycle | 18/18 |
| **DOC-58B.2 lifecycle (новые)** | **21/21** (attempt 01: 20/21 из-за ошибки ожидания в тесте T08, сохранён) |
| DOC-58B adapter JS | 11/11 |
| DOC-58A offline controller | 39/39 |
| Pre-native gate | PASS 16/16 |

| Файл | SHA-256 |
|---|---|
| `src/visual_proxy_58b2.hpp` | `86846b756bf50eefda988a76510b28379a3d5c32dfddb6978d5835a593b5a330` |
| `tests/lifecycle58b2_contract.cpp` | `859713ab68094fe010aa06d2316fce6f6b65f86b2ef975c9b9525b16c159d5d7` |
| `ownership58b2.js` (пассивный наблюдатель) | `b7782fc6a1543b6ec864cef1988958609d49240784c1d970032605ea7a26d694` |
| `runner58b2.py` / `input58b2.py` (копии DOC-58B, новые run/OUT) | `32c6b099…` / `2b8e1f80…` |
| `precheck58b2.py` / `report58b2.py` / `static_graph58b2.py` | `4ca7ce3c…` / `fd3e0b66…` / `b1984de9…` |
| `workflow.py` / `build58b2.py` / `CMakeLists.txt` / `rea_extract58b2.py` | `89a3940a…` / `b047ad1c…` / `e4e77070…` / `7cb78f13…` |

Полный манифест: [SOURCE_MANIFEST](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/SOURCE_MANIFEST.json), [LIFECYCLE_CONTRACT_RESULTS](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/LIFECYCLE_CONTRACT_RESULTS.json), [PRE_NATIVE_GATE](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/PRE_NATIVE_GATE.json).

## E. Integrity

- Exact EXE `b8f131…a76d` (оригинал и обе копии). Host PID 24484 / Sim 3992 (frozen DOC-56 DLL `2b66f075…`), Client PID 36684 / Sim 35060 (frozen DOC-58B DLL `3249b735…`, adapter source `808297ba…` как в DOC-58B). Isolation gates PASS; guard errors 0, overflow 0; protected saves before/after равны в обоих процессах.
- Spawn: Welcome slot 1, gen 1, epoch 1254939278, native MapReady; Actor создан одним вызовом фабрики; один нативный yaw-update (−3.1376 → −2.5126), sprite submissions; 4060 Actor Tick callbacks.
- Player клиента: BEFORE, AFTER_SPAWN, LIVE_1800, LIVE_3600 × 12 полей — EQUAL; после StateExit мир (включая Puppet) уничтожен KEX, записей DLL/адаптера в Player/камеру 0 ([PLAYER_INTEGRITY](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/PLAYER_INTEGRITY.json)).
- Hooks: 24/24 Frida-сайтов (адаптер + наблюдатель) и CRT `free` — exact16 после выгрузки скриптов; DLL вернула Update exact16 при StateExit, Tick/StateExit остаются pinned до выхода процесса (дизайн DOC-58B).
- OS exit: оба WM_CLOSE → exit 0 (retained handle + OS_SIGNALED), без forced termination; REA-сессии закрыты, игровых процессов 0.
- Historical evidence/исходники/DOC-55…58B.1/Gemini/оригинал игры: [INTEGRITY_FINAL](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/INTEGRITY_FINAL.json). Отклонения и ошибки стенда: [HARNESS_ERRORS](evidence/m1/NATIVE_ACTOR_OWNERSHIP_58B2/HARNESS_ERRORS.md).

## F. Next Step

Единственное рекомендуемое продолжение — **Proxy Reconnect / rearm одного proxy** поверх ENGINE_RETIRED: перенести engine-teardown witness в DLL/адаптер (переживающий seam detach), после него снимать disconnect-latch и barrier только по новому native MapReady новой map epoch, затем второй Spawn в той же сессии с native anti-ABA (alloc seq блока как generation). Отдельно в том же этапе надо закрыть Remove-in-flight hazard (запрет Remove вне live-world шага или ожидание Drain до разрешения выхода). Этап не начат.
