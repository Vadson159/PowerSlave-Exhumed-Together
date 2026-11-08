# DOC-64 — П1 / S0.4: приёмка режима В

2026-11-04. Проверяющий: Opus 5.5 (Claude Code), extra. Исполнитель П1: GPT 6.1 Sol max ([DOC-61](61_P1_NATIVE_PROXY_DLL.md)).

**Вердикт: PASS (bounded).** Критерий S0 «сценарий DOC-58B (run 02) воспроизводится на единой сборке без Frida-адаптера с тем же результатом» выполнен в границах DOC-58B: один Actor1007, TOMB, сектор 259, смещение z +88, два процесса на одном ПК. Блокирующих дефектов нет. Четыре неблокирующих замечания перечислены в §4 и передаются П3/П4.

Целевой EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Во время проверки игра, Frida и REA не запускались, новых прогонов в KEX не было, `b60/` не трогался. Принятые исследования и прогоны не повторялись. Сырые журналы run 03 исполнителя прочитаны заново, отдельными скриптами. Machine-сводка: [REVIEW_CHECKS.json](evidence/m1/P1_ACCEPTANCE_REVIEW_64/REVIEW_CHECKS.json) (13 проверок, все PASS). Скрипты лежат в [tools/](evidence/m1/P1_ACCEPTANCE_REVIEW_64/tools/).

## 1. Исходники и реально загруженные DLL

| Проверка | Результат |
|---|---|
| EXE обеих тестовых копий | `b8f131…a76d` |
| Копии DLL в `P1_HOST_03/` и `P1_CLIENT_03/native_artifact/` | равны замороженным `b60` и `FINAL_SOURCE_MANIFEST`: host `73b76e1f…3376`, client `2366326a…8e97` |
| Текущие исходники против pre-native freeze run 03 (72 файла) | 0 расхождений в компилируемых файлах. После прогона изменились 4 некомпилируемых файла (README, RUNTIME_P1.md, `review_runtime_p1.py`, `integrity_p1.py`); их состояние совпадает с `FINAL_SOURCE_MANIFEST` (82/82) |
| **Независимая пересборка** текущих исходников (VS 2022, MSVC 19.44.35225, SDK 10.0.26100, отдельное дерево, не `b60`) | client и host — **7/7 секций равны** загруженным DLL после маски `dll_compare.py`, экспорты равны |
| Хост run 03 против принятой замороженной хост-DLL DOC-56 `2b66f075…` | **7/7 секций равны**: хост run 03 по коду совпадает с принятым хостом DOC-56 |
| Offline-наборы в отдельном дереве сборки | CTest **24/24**; bounded_proxy_contract 118/0, proxy58b_contract 32/0, lifecycle58b2_contract 21/0 |

Методическая деталь для будущих сверок. `dll_compare.py` маскирует запись CodeView, но если путь к PDB другой длины, следующие элементы `.rdata` сдвигаются, и сравнение ложно падает. Тогда в `.text` различаются только disp32 с постоянной дельтой (+16 у client, +8 у host). Это диагностирует [pe_diff.py](evidence/m1/P1_ACCEPTANCE_REVIEW_64/tools/pe_diff.py). Для точной сверки DLL перелинковывается с `/PDBALTPATH:<точный путь эталонного PDB>`. Макрос `%_PDB%` записывает `%имя.pdb%`, а не путь, поэтому для этой цели не подходит.

## 2. Код (PN1–PN4, API, детуры, lifetime)

Проверены по коду, который фактически собран: [native_proxy.cpp](../experiments/pse_together/src/native/native_proxy.cpp), [native_proxy.hpp](../experiments/pse_together/src/native/native_proxy.hpp), [bounded_proxy.hpp](../experiments/pse_together/src/proxy/bounded_proxy.hpp), интеграция seam/exit в [client.cpp](../experiments/pse_together/src/native/client.cpp) и [seam.hpp](../experiments/pse_together/src/native/seam.hpp).

- **PN1:** `sweep()` безусловно сбрасывает `inSweep` после защищённого endSweep (`native_proxy.cpp:353`).
- **PN2:** `exit58` при неуспешном `detach()` блокирует клиент и публикует Faulted (`client.cpp:396-398`).
- **PN3:** `exit58` всегда вызывает идемпотентные `exitBarrier()` и `detach()`, даже если seam уже отцеплен (`client.cpp:393-398`).
- **PN4:** `update()` переписывает `g.command` только для Applied и Unchanged. `spawn()` пишет `g.command` до проверки цели, но Observation отдаётся только в фазе Live, а до неё последняя запись — от успешного Spawn. Дефекта нет.
- **Отказ API до чтения чужого состояния.** `observeImpl` сначала атомарно читает TID и сравнивает его, и только потом читает phase/handle. `isPositionInterpolant` проверяет `thread()` первым и выполняет только сравнение. `retirement()` и `permitSpawn()` работают только с проверкой TID или с атомиком. `PSE58B_RegisterSeam` возвращает `ERROR_NOT_SUPPORTED` (50).
- **Lifetime.** Бюджеты Spawn и Remove расходуются до нативного вызова. Указатель, возвращённый фабрикой, сохраняется до первой диагностики. PoolReturn обнуляет слабые токены до trampoline. После PoolReturn читается только ObjectNode, на возврате того же Sweep. Это шаблон DOC-58B.2. OnRemove-after и Drain-after не читают Actor, если PoolReturn успел пройти внутри них. MinHook снимается через `MH_DisableHook`: trampoline остаются, модуль закреплён.
- **Отображение кодов seam** на `SpawnOutcome`, `UpdateOutcome`, `RemoveOutcome` и `RetirementStatus` (`visual_proxy.hpp:41-44`) корректно. При неизвестном исходе возвращается 0 и повторного вызова нет. Исключение до вызова фабрики даёт Refused и блокирует адаптер без нативного вызова.
- **ABI** фабрики, высоты пола/потолка и point-in-sector соответствуют досье DOC-57/58A, на которые ссылается DOC-61. Нативные original вызываются вне SEH наблюдателя.

## 3. Runtime run 03 (сохранённые данные, независимое чтение)

- **Без JS-адаптера.** JS, загруженный в клиент, не управляет игрой. `NativeCallback` и `Interceptor.replace` используются только принятой изоляцией (`SHGetKnownFolderPath`, write-guard `NtCreateFile`/`NtOpenFile`). Наблюдатель ставит `Interceptor.attach` на scene, sprite и lerp — ни один из них не совпадает с 12 сайтами адаптера — и вызывает только read-only экспорты. Скрипты permit, detach и shutdown лишь публикуют атомики DLL.
- **Сеть → Actor.** Все 217 C++ update бит-в-бит совпадают с маршрутизированным снапшотом slot0: x, y, z+88 с одним округлением float, нормализованный yaw ([update_chain_check.json](evidence/m1/P1_ACCEPTANCE_REVIEW_64/update_chain_check.json)). Повторный прогон `review_runtime_p1.py` исполнителя дал те же 34/34 PASS. Отличие от сохранённого кандидата только в записи путей к bundle; 10 283 строки корреляции равны по slot, sequence, bits и bit_exact.
- **Цепочка retirement** ([independent_p1_check.json](evidence/m1/P1_ACCEPTANCE_REVIEW_64/independent_p1_check.json)):
  1. `LOCAL_SESSION_LOST` в 01:43:02.677.
  2. Один Remove: refs 1, вхождений в сектор 1, flags `0x2044`.
  3. Drain ×2: refs 1 → 0, вхождения 1 → 0.
  4. OnRemove entry/return.
  5. PoolReturn с `valid_scope`.
  6. `deferred_retired` в 01:43:02.727: payload null, refs 0, in_use 0, вхождений 0.

  После этого 121 запись `post_retire`: слабые указатели null, `actor_reads:false`. Все собственные события — на Sim TID 40152. Fault-событий 0.
- **P1 клиента.** 9 C++-снимков, у всех один набор полей, позиция и identity. Плюс 119 полных снимков наблюдателя (повторный прогон).
- **Точные восстановления и выход.** Клиент: 6/6 хуков адаптера exact16, затем seam Detached, затем Update exact16. Хост: 4 control + getter exact16. Оба процесса завершились с exit 0, `failed:false`, `foreign_calls:0`.
- **Целостность.** Оригинал прочитан сейчас, только для чтения: 77/77 файлов равны снимку M0, лишних нет. Защищённые сохранения равны baseline П1.
- **Run 02** помечен INTERRUPTED_NOT_ACCEPTED и в приёмку не входит. В его журналах 0 fault-событий; оба журнала обрываются в 01:30:37 — внешняя потеря сессии (INFERRED).

**Сравнение с DOC-58B run 02.** Совпадают все категории приёмки: NETWORK, NATIVE_SPAWN, SAME_SECTOR_MOVEMENT, RENDER, INTERPOLATION, LIFECYCLE, LOCAL_PLAYER_INTEGRITY, HOOK_RESTORATION, OS_EXIT. Позиция в обоих прогонах менялась только через native_step, yaw — двумя сбросами. Ограничения прогона перечислены в N2 и N4.

## 4. Неблокирующие замечания (передаются дальше)

- **N1 — `block()` вне Sim TID.** `block()` может выполниться не на Sim TID: через `__except`/catch в `observe()`, через guard-пути детуров и через `beforePool`, если наш Actor возвращается в пул с чужого потока. Тогда `g` и журнал меняются без синхронизации. В run 03 этого не было (`foreign_calls` 0). Исправить до или внутри П3, где открываются пути выгрузки и реконнекта: на чужом потоке ставить только атомарный флаг, а инвалидацию делать на Sim TID.
- **N2 — непроверенные в runtime ветки.** В run 03 не сработали отказ update в движении (refused 0; в DOC-58B run 02 их было 2 035) и путь teleport позиции больше 160 единиц с `reset(+0x50)` (его не было и в DOC-58B run 02). Отказ проверен статически, offline-контрактами и отказом Spawn `OUT_OF_ATTESTED_AREA` в C++ в run 01. Runtime-покрытие этих веток — в П4 (порталы, любые сектора).
- **N3 — унаследованные жёсткие инварианты.** Пока прокси жив, любое изменение графа P1 клиента или 12 его полей (здоровье, патроны, ключи) останавливает контроллер. После аттестации любая ошибка «мир не стабилен / не TOMB» блокирует адаптер навсегда, а живой Actor остаётся без Remove; его забирает engine-owned purge (DOC-58B.2). Так же работал `adapter58b.js:164`. Перед геймплейными пакетами (П3, П7, П14) это нужно перепроектировать.
- **N4 — выборочное свидетельство ActorTick.** Это 168 выборок кумулятивных счётчиков (steps = ticks + 1), а не пошаговый журнал, как в DOC-58B (12 454/12 454). Последняя выборка ограничивает итог.

Косметика: в DOC-61 местами пропущены пробелы между словом и числом («PID29964», «exit0»). На выводы это не влияет.

## 5. Статусы и что дальше

- П1 и S0.4 — **PASS** (bounded). Сегмент S0 закрыт: S0.1–S0.5 — PASS.
- DLL run 03 приняты как артефакты П1: host `73b76e1f…3376` (по коду совпадает с DOC-56), client `2366326a…8e97`. Замороженные DLL DOC-56/58B и их gates не меняются.
- Можно начинать П3 и П4, П8, П14 и П24 (П24 ещё ждёт D5). П9 ждёт также П6.
- Gates DOC-58B (G1 bounded, 58C-04/06 DEFERRED, 58D-08 PARTIAL) не расширялись. Реконнект, anti-ABA, переходы карт — П3. Порталы, место спавна, jitter, фоновый режим клиента — П4.
