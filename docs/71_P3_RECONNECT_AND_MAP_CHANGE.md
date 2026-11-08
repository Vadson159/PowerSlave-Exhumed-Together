# DOC-71 — П3 / S1.1–S1.2: реконнект, повторный Spawn и смена карт с живым прокси

2026-11-04. Исполнитель — Opus 5.5 (max, Claude Code). Статус на момент исполнения — **REVIEW / PARTIAL, режим В** (поднят с Б); проверка [DOC-78](78_P3_ACCEPTANCE_CHECK.md) **вернула пакет исполнителю**, исправления F1–F5, F7 и новый чистый прогон run 05 сделаны ([§13](#13-исправления-по-doc-78-и-run-05)); повторная проверка — **PASS (bounded)** ([DOC-78 §7](78_P3_ACCEPTANCE_CHECK.md#7-повторная-проверка-после-исправлений)). Разделы 1–12 — текст первого исполнения; изменённое после DOC-78 отмечено в §13. Проверяющий по ROADMAP — Sol extra. Зависимые пакеты (П5, П7, П17) ждут.

**Почему режим В.** Пакет добавил четыре новых нативных детура на путь выгрузки (`Level::Unload`, purge арены, dispatch блока, деструктор Actor) и изменил момент нативного Remove: теперь он выдаётся только на входе Sweep живого тика. Это время жизни объектов и выгрузка вне принятого шаблона (ROADMAP §3, правило эскалации). Ниже — результат исполнителя, а не приёмка.

**PARTIAL** относится только к S1.2. Не выполнены пути «смерть → рестарт» и «выход с уровня → следующая карта», не проверена смена карты на стороне хоста (§9).

Целевой EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Перед каждым прогоном сверены обе тестовые копии. Preferred/реальная база `0x140000000`.

## Результат

| Что | Итог | Метка | Evidence |
|---|---|---|---|
| S1.1 — реконнект в тот же слот | два реконнекта; Spawn #2 и #13 — тот же Actor из пула после собственного retirement | RUNTIME OBSERVED | run 03: B1, C1, C2 |
| S1.1 — повторный Spawn поверх ENGINE_RETIRED | 10 из 10 миров: новый экземпляр автомата, новый handle, новый Actor | RUNTIME OBSERVED | run 03: E1, C1 |
| S1.1 — teardown-witness в DLL | 10 из 10 полных упорядоченных цепочек `ArenaDestroyed` | RUNTIME OBSERVED | run 03: E1, E2 |
| S1.1 — anti-ABA (alloc seq блока) | 13 идентичностей, 0 `Anomaly`, alloc seq растёт строго (+110 за мир) | RUNTIME OBSERVED | run 03: B2 |
| S1.1 — «Remove в полёте» (`af3380/af3388`) | 3 из 3 Remove сняты Drain того же StateTick, затем retirement | RUNTIME OBSERVED | run 03: D1, D2 |
| S1.1 — QA DOC-58C (7 FAIL + 1 NVR) | к текущему автомату относятся TC-03, TC-09 и TC-30, все три закрыты; остальное — дефекты кандидата Gemini или ёмкость (П22) | VERIFIED (разбор файлов) + offline + native | [§8](#8-qa-doc-58c-разбор-7-fail--1-nvr) |
| S1.2 — 10 переходов подряд с живым прокси | 4× Exit To Title + Load Game, 3× Restart Level, 3× Reload Last Checkpoint — все PASS, дважды (run 02 и run 03) | RUNTIME OBSERVED | run 03: E3 |
| S1.2 — смерть → рестарт, следующая карта, смена карты хостом | не выполнены | — | [§9](#9-ограничения-и-что-не-сделано) |
| N1 DOC-64 (`block()` вне Sim TID) | исправлен (почтовый ящик ForeignFault); в runtime чужой поток не встретился | offline F01–F03; run 03 A4 | [§6](#6-n1-doc-64-ошибка-с-чужого-потока) |

Машинные проверки: offline 29/29 (`p3_rearm_contract` 53/0); инвариантность 9 существующих DLL-целей; pre-native gate 10/10; ревью сырых журналов run 03 — 19/19 ([P3_RUNTIME_RESULT_RUN03.json](evidence/m1/PSE_TOGETHER_P3/P3_RUNTIME_RESULT_RUN03.json)), run 02 — 18/18; свежая пересборка по коду равна загруженной DLL (7/7 секций); оригинал и копии 77/77; защищённые сохранения не изменились.

## 1. Код

Принятый автомат DOC-58B.2 ([`visual_proxy.hpp`](../experiments/pse_together/src/proxy/visual_proxy.hpp)) **не изменён**. Принятые цели П1 не тронуты. Код П3 — отдельные файлы и отдельная цель `pse_dll_client_p3` (флаг `PSE_FEATURE_P3`, только CLIENT + VISUAL_PROXY, без P4/P8/P14):

| Файл | Роль |
|---|---|
| [`proxy_rearm_p3.hpp`](../experiments/pse_together/src/proxy/proxy_rearm_p3.hpp) | правила rearm R1–R4, журнал закрытий, `RearmableProxy` и `RearmRouter<1>` поверх неизменённого автомата; KEX не трогает |
| [`teardown_witness_p3.hpp`](../experiments/pse_together/src/proxy/teardown_witness_p3.hpp) | разбор заголовка tagged-блока, классификация anti-ABA, правило вердикта по цепочке выгрузки |
| [`foreign_fault_p3.hpp`](../experiments/pse_together/src/proxy/foreign_fault_p3.hpp) | N1: ошибка с чужого потока только публикуется, Sim TID её применяет |
| [`proxy_profile_p3.hpp`](../experiments/pse_together/src/native/proxy_profile_p3.hpp) | 4 новые точки хуков с 16-байтными префиксами, кучи `af4490/af4600`, `d9980`, vtable `528608` |
| [`native_proxy_p3.cpp`](../experiments/pse_together/src/native/native_proxy_p3.cpp) / [`.hpp`](../experiments/pse_together/src/native/native_proxy_p3.hpp) | форк принятого адаптера П1 (`native_proxy.cpp` `1536882e…`, равен [FINAL_SOURCE_MANIFEST](evidence/m1/PSE_TOGETHER_P1/FINAL_SOURCE_MANIFEST.json) П1, то есть до рефакторинга П4 из DOC-67 F1): повторный вход в мир, свидетель выгрузки, Remove в тике, N1 |
| [`client_p3.cpp`](../experiments/pse_together/src/native/client_p3.cpp) | форк принятого `client.cpp` П1 (`0382eec4…`, тот же манифест): сессия на каждый мир с ключом реконнекта, барьер и вердикт на выходе из мира, управление реконнектом |
| [`p3_rearm_contract.cpp`](../experiments/pse_together/tests/proxy/p3_rearm_contract.cpp) | offline-контракт: R01–R33, Q28, Q31, W01–W15, F01–F03 = 53 |

Общие файлы изменены только дописыванием в конец: блок `#error` в `config.hpp` (P3 изолирован), цель и набор в `CMakeLists.txt`, строка в `tools/expected_suites.json`. [Инвариантность](evidence/m1/PSE_TOGETHER_P3/TARGET_INVARIANCE/RESULT.json) проверена так: поток токенов `cl /EP` девяти существующих DLL-целей (`pse_dll_host`, `_host_bootstrap`, `_client`, `_client_proxy`, `_host_p4`, `_client_p4`, `_host_p14`, `_host_p8`, `_client_p8`) одинаков с блоком П3 в `config.hpp` и без него — PASS. Ошибки типа DOC-67 F1 (машинный код чужой принятой цели изменён вне флага) здесь нет.

Новые exports: `PSE_P3_RequestReconnect` (атомарный запрос реконнекта для стенда) и `PSE_P3_Status` (`incarnation | closure<<8 | world<<16`). Frida по-прежнему не управляет прокси. Скрипты стенда публикуют только атомики. Wire v1.10 не менялся.

## 2. Реконнект и повторный Spawn: правила rearm

Каждая нативная жизнь Actor — отдельный экземпляр принятого `VisualProxyStateMachine` («инкарнация»). Внутри экземпляра latches (disconnect, teardown barrier, shutdown) не снимаются никогда. Новый экземпляр создаётся только при всех условиях:

- **R1** — текущий закрыт безопасно: `Retired` (ABSENT после нативного свидетеля retirement), `NeverSpawned` (барьер до Spawn), `EngineRetired` (ENGINE_RETIRED по принятому правилу DOC-58B.2) или `EngineRetiredQueueClear` (R4).
- **R2** — строго более новая сессия (generation хоста больше), сконфигурированная после последнего разрыва сессии. Она может быть настроена ещё до закрытия экземпляра (так шли оба реконнекта), но потеря сессии стирает ожидающую сессию вместе с её MapReady (DOC-78 F5, §13).
- **R3** — после конфигурации пришёл нативный MapReady именно этой сессии: та же epoch и тот же world serial клиента. После барьера world serial должен быть больше снесённого мира.
- **R4** — `ORPHANED(RemoveInFlightAtBarrier)` закрывается только вердиктом `ArenaDestroyed` вместе с queue witness `NotIssued` или `IssuedDrained`.

Мёртвые закрытия на всю жизнь процесса (fail-closed): `DeadQuarantined`, `DeadQueueHazard` (Remove в очереди на барьере), `DeadNoWitness` (вердикт неполон; а если адаптер вердикта не выдал вовсе, ORPHANED закрывается `DeadNoWitness` явно — DOC-78 F4), `DeadHandleReuse` (opaque handle повторился), `DeadShutdown`. Причины отказа rearm журналируются: `NotClosed`, `Dead`, `NoSession`, `SessionNotNewer`, `NoMapReady`, `WorldNotNewer`, `InSeamCall`, `Capacity`, `AllocationFailed`.

**«Новая эпоха».** Epoch DOC-56 — хеш идентичности карты, для TOMB всегда `3404130831`. Поэтому «новый native MapReady новой эпохи» из S1.1 реализован как строго новый ключ сессии `{generation, epoch, world}`: generation строго больше, после барьера строго больше и world. Это решение исполнителя (INFERRED); его стоит проверить при приёмке.

**Сессия на каждый мир.** На выходе из мира (`StateExit`, состояние 3 → не 3) клиент делает так:

1. Барьер: `router.onPreUnload(world)` и `exitBarrier` адаптера.
2. Остановка сетевой Session.
3. Исходный StateExit, внутри которого выполняется `Level::Unload` и purge.
4. Вердикт свидетеля → `onTeardownVerdict` → ENGINE_RETIRED, затем world serial + 1.

Следующий игровой мир создаёт новую Session с сохранённым **ключом реконнекта**: хост отклоняет тот же participantId без ключа. Хост выдаёт новую generation, затем идут ClientGate, нативный MapReady и rearm. Транспортный реконнект без смены мира стенд вызывает принятым управляющим сообщением RECONNECT (`PSE_P3_RequestReconnect`).

## 3. Teardown-witness в DLL

Четыре pass-through детура MinHook. Аргументы не меняются, исходная функция вызывается всегда. Детуры только читают списки и заголовки и журналируют.

| Точка | VA | RVA | File offset | Префикс 16 байт | Роль |
|---|---|---|---|---|---|
| `level_unload` | `0x1401c90e0` | `0x1c90e0` | `0x1c84e0` | `48 89 5c 24 10 48 89 6c 24 18 48 89 74 24 20 57` | Level::Unload |
| `arena_purge` | `0x1400c58b0` | `0xc58b0` | `0xc4cb0` | `48 89 5c 24 08 48 89 6c 24 20 44 89 44 24 18 48` | purge tagged-кучи (`af4490` Actor, `af4600` узлы) |
| `block_dispatch` | `0x1400c5310` | `0xc5310` | `0xc4710` | `48 89 5c 24 10 57 48 83 ec 20 48 83 79 40 00 48` | вызов dtor-thunk блока |
| `actor_dtor` | `0x14019f210` | `0x19f210` | `0x19e610` | `40 53 48 83 ec 20 48 8b d9 48 8d 05 e8 93 38 00` | деструктор kexSlaveActor |

Цепочка и адреса — VERIFIED static по сохранённым досье ([58B.2](58B2_NATIVE_ACTOR_OWNERSHIP_UNLOAD.md) §A и его `static/`: `c58b0`, `c5310`, `c5430`, `d9980`; `docs/evidence/ghidra_2022/G_FUNC_1401c90e0.json`, `G_FUNC_14019f210.json`) и RUNTIME OBSERVED в DOC-58B.2 §B (Frida, Exit to Title). Префиксы сверены по байтам EXE обеих копий (gate 03). Восстановленных имён нет, все имена — аналитические. `af4490/af4600` (`.data`, zero-initialised) file offset не имеют.

**Вердикт `ArenaDestroyed`** ставится только при полной упорядоченной цепочке. Барьер защёлкнут до Unload. Unload один раз (entry/return). Purge `af4490` один раз: наш блок в списке на входе, dispatch нашего блока с совпавшими адресом и alloc seq, вход и выход dtor, на выходе блока в списке нет. После этого purge `af4600` один раз: блок нашего узла в списке на входе, на выходе его нет. Нарушений 0, нативных вызовов прокси после барьера 0. Любое отклонение даёт `Unknown`/`NotObserved` и мёртвое закрытие (W09–W15). Purge с живым Actor без барьера — ошибка адаптера. Списки обходятся только по живым блокам, под SEH.

## 4. Anti-ABA

Идентичность Spawn: адрес Actor, узел, блок `actor−0x60` и **alloc seq** блока (`+8` заголовка, счётчик кучи из `c5430`, монотонный). Классы такие:

- `First`;
- `NewAddress`;
- `PoolReuseAfterRetire` — тот же адрес и тот же seq после нашего собственного PoolReturn в том же мире;
- `NewBlockSameAddress` — тот же адрес, новый seq;
- `Anomaly` — тот же seq без нашего retirement; это ошибка `P3_ABA_ANOMALY`.

Блок проверяется по magic `0x3151983`, куче `af4490`, dtor-thunk `d9980`, размеру `0x1b8` и счётчику ctor 1. Иначе — `P3_ACTOR_BLOCK_UNATTESTED`. Opaque handle — отдельный строго растущий счётчик адаптера.

RUNTIME OBSERVED (run 02 и run 03): первый мир каждого процесса — seq 1459, каждый новый мир +110. `NewBlockSameAddress` для Actor в прогонах не встретился (только offline W07). Узлы переиспользуют адреса между мирами: в run 03 узел `0x680D9630` (слот 17 блока `0x680D9100`) был и в мире 4, и в мире 7. Это разные Actor с разным seq; блок узла каждый раз проверен свидетелем.

## 5. «Remove в полёте»

VERIFIED static (досье DOC-57/58A/58B.2, ветка live в StateTick `1b6ec0`): Update `1dfe30` → тики Actor → Sweep `1b6360` → Drain `1b61d0`. Remove ставит ячейку в очередь `af3380` (хвост) / `af3388` (голова) через `1b5530`, снимает её только Drain. Remove вне живого тика оставлял ячейку в очереди на барьере — это hazard DOC-58B.2 §F.

Теперь автомат по-прежнему запрашивает Remove в своём тике, но адаптер его только **взводит** (`p3_remove_armed`). Нативный Remove выдаётся на входе Sweep следующего тика живого мира, и Drain того же StateTick снимает ячейку. Если ячейка не снята в этом же тике, `tickLeave` даёт ошибку `P3_REMOVE_NOT_DRAINED_IN_TICK`. Поэтому на барьере очередь по замыслу бывает только `NotIssued` или `IssuedDrained`. `IssuedQueued` означает `DeadQueueHazard`. Это вывод (INFERRED): ячейки `af3380/af3388` адаптер не читает, `IssuedDrained` определяется по refs узла 0 и 0 вхождений в сектор (DOC-78 F9).

RUNTIME OBSERVED (run 03, 3 из 3): handle 1 — тик 3713, handle 12 — тик 18176, handle 13 — тик 18907. Перед Drain refs 1 и одно вхождение в сектор; Drain того же тика оставляет refs 0, вхождений 0, `queue_cell_drained`. Через два тика — OnRemove, PoolReturn и `deferred_retired`. P1 при этом не меняется.

## 6. N1 DOC-64: ошибка с чужого потока

`block()` вне Sim TID больше не меняет `g` и журнал адаптера. Он вызывает только `ForeignFault::post(reason)`: побеждает первая причина, последующие считаются. Sim TID применяет ошибку при следующем входе: `tickEnter`, `worldReady`, Spawn, update, Remove, retirement, `tickLeave`, `worldExit`, detach, вердикт. PoolReturn нашего Actor с чужого потока тоже только публикуется, `g.life` не трогается. Оговорки (DOC-78 F6, F8): унаследованный `sweep()` сбрасывал `inSweep` с любого потока (исправлено в §13); purge `af4490/af4600` и деструктор нашего живого Actor на чужом потоке по-прежнему никак не публикуются (делает ли KEX такое — UNKNOWN).

Проверено offline (F01–F03). В run 03 чужой поток не встретился: 0 публикаций, `foreign_calls` 0 (A4). Ветка в runtime не исполнялась.

## 7. Прогоны в игре — RUNTIME OBSERVED

Стенд принятый: REA MCP (Frida spawn), DOC-51 loader, isolation guard, retained-handle watcher, ввод через ОС. Два процесса на одном ПК, тестовые копии и свежие профили. Хост — замороженная принятая DLL П1 `docs/evidence/m1/PSE_TOGETHER_P1/P1_HOST_03/native_artifact/pse_m11_bootstrap.dll` (`73b76e1f…3376`, по DOC-64 по коду равна хосту DOC-56). Хост всё время на TOMB, его P2 создаётся один раз на прогон.

| Прогон | Клиентская DLL | PID/TID хост, клиент | Итог |
|---|---|---|---|
| 01 | `15a4bf03…68bc` | 6608, 13752/35632 | **остановлен**: после первого выхода из мира адаптер больше не входил в мир (дефект ниже); Spawn #1, #2 и первый вердикт `ArenaDestroyed` получены; exit 0, 10/10 restores |
| 02 | `d3d7443d…0f64` | 33416/4532, 9080/36336 | 18/18 ([P3_RUNTIME_RESULT.json](evidence/m1/PSE_TOGETHER_P3/P3_RUNTIME_RESULT.json)); до исправления N1 |
| 03 | `3ab7982b…297c` | 11408/28776, 2116/37228 | **19/19** ([P3_RUNTIME_RESULT_RUN03.json](evidence/m1/PSE_TOGETHER_P3/P3_RUNTIME_RESULT_RUN03.json)); финальная DLL с N1 |

**Дефект run 01.** `observePuppet` пропускал тики P1, пока `g.stopping`, а `enterWorld` требует тик P1 в том же тике. После первого барьера адаптер навсегда оставался вне мира. Исправлено условием `(g.stopping && !g.worldPending)`. Заодно после вердикта очищается журнал свидетеля (`g.tlog={}`): без этого неармированные вердикты повторяли старые поля. Ошибок адаптера в run 01 не было; процессы завершились чисто.

**Сценарий run 03.**

1. Хост: gate, разрешение, короткий W → `(-7420.137, -6809.438)`, сектор 259.
2. Клиент: gate, разрешения, Spawn #1.
3. Реконнект → Remove в тике → Retired → Spawn #2. Поворот мышью на хосте применён к инкарнации 2 (revision 2, yaw −2.63760495).
4. 10 переходов.
5. Реконнект → Spawn #13.
6. Закрытие хоста → у клиента `LOCAL_SESSION_LOST` → Remove в тике → Retired.
7. Отсоединение (10/10 exact16, seam detached) → WM_CLOSE → exit 0 у обоих.

Хост принял MapReady generations 1…13 одного зарезервированного слота.

**Инкарнации run 03** (одна строка — одна нативная жизнь Actor, один экземпляр автомата):

| Handle | Мир | Gen | Actor | Узел | alloc seq | Класс | Закрытие |
|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | `61B06730` | `61AEDF10` | 1459 | First | Retired (реконнект) |
| 2 | 1 | 2 | `61B06730` | `61AEDF10` | 1459 | PoolReuseAfterRetire | EngineRetired |
| 3 | 2 | 3 | `61B4A3B0` | `61D550B0` | 1569 | NewAddress | EngineRetired |
| 4 | 3 | 4 | `61B1CFC0` | `68702840` | 1679 | NewAddress | EngineRetired |
| 5 | 4 | 5 | `04AB3C90` | `680D9630` | 1789 | NewAddress | EngineRetired |
| 6 | 5 | 6 | `68AD2E90` | `686E4BB0` | 1899 | NewAddress | EngineRetired |
| 7 | 6 | 7 | `04A935F0` | `68432170` | 2009 | NewAddress | EngineRetired |
| 8 | 7 | 8 | `04AB3A70` | `680D9630` | 2119 | NewAddress | EngineRetired |
| 9 | 8 | 9 | `61D343D0` | `689681F0` | 2229 | NewAddress | EngineRetired |
| 10 | 9 | 10 | `04A93A30` | `68A0C490` | 2339 | NewAddress | EngineRetired |
| 11 | 10 | 11 | `52CD6430` | `680D5D00` | 2449 | NewAddress | EngineRetired |
| 12 | 11 | 12 | `618988B0` | `687258B0` | 2559 | NewAddress | Retired (реконнект) |
| 13 | 11 | 13 | `618988B0` | `687258B0` | 2559 | PoolReuseAfterRetire | Retired (потеря хоста) |

Адреса — младшие 32 бита (старшие нули); полные значения есть в `p3_spawn_identity`. Мёртвых закрытий 0. Каждый rearm шёл после нативного MapReady строго более новой generation для того же мира клиента (C1).

**10 переходов run 03** ([CYCLES.jsonl](evidence/m1/PSE_TOGETHER_P3/P3_CLIENT_03/CYCLES.jsonl)). Каждый: барьер до Unload → полная цепочка `ArenaDestroyed` / `NotIssued` → ENGINE_RETIRED → новый мир, повторная аттестация, новая сессия, MapReady → новый Spawn.

| # | Путь | Мир | Снесён handle | Блок узла | Gen | Новый Spawn | с |
|---|---|---|---|---|---|---|---|
| 1 | Exit To Title → Load Game | 1 → 2 | 2 | `61AED9E0` | 3 | 3 (seq 1569) | 23.6 |
| 2 | Restart Level | 2 → 3 | 3 | `61D54B80` | 4 | 4 (seq 1679) | 9.6 |
| 3 | Reload Last Checkpoint | 3 → 4 | 4 | `68702310` | 5 | 5 (seq 1789) | 9.7 |
| 4 | Exit To Title → Load Game | 4 → 5 | 5 | `680D9100` | 6 | 6 (seq 1899) | 22.7 |
| 5 | Restart Level | 5 → 6 | 6 | `686E4680` | 7 | 7 (seq 2009) | 10.0 |
| 6 | Reload Last Checkpoint | 6 → 7 | 7 | `68431C40` | 8 | 8 (seq 2119) | 10.2 |
| 7 | Exit To Title → Load Game | 7 → 8 | 8 | `680D9100` | 9 | 9 (seq 2229) | 23.3 |
| 8 | Restart Level | 8 → 9 | 9 | `68967CC0` | 10 | 10 (seq 2339) | 10.5 |
| 9 | Reload Last Checkpoint | 9 → 10 | 10 | `68A0BF60` | 11 | 11 (seq 2449) | 10.8 |
| 10 | Exit To Title → Load Game | 10 → 11 | 11 | `680D57D0` | 12 | 12 (seq 2559) | 23.7 |

«Load Game» загружает слот 0 свежего тестового профиля: автосохранение TOMB, которое игра сама записала в `runtime_p3/profile/P3_CLIENT_03/saves`. Это путь «загрузка сохранения» из S1.2, но на той же карте. Помимо 10 мировых барьеров есть одиннадцатый — DirectQuit при выходе процесса (принятый путь C25: после него нет ни Unload, ни вызовов прокси; E2).

**Ревью сырых журналов** ([`review_p3.py`](evidence/m1/PSE_TOGETHER_P3/tools/review_p3.py), отдельно от собственных сводок стенда). Run 03 — 19/19:

- A1 — точный EXE, gate PASS, загруженные DLL = замороженные.
- A2 — все события P3/прокси на аттестованном Sim TID.
- A3 — 0 ошибок и блокировок.
- A4 — N1 заморожен в исходниках, 0 публикаций.
- B1 — 13 Spawn, handles 1..13.
- B2 — anti-ABA.
- C1 — rearm только после MapReady более новой сессии.
- C2 — закрытия: Retired ×3, EngineRetired ×10, мёртвых нет.
- D1, D2 — Remove в тике.
- E1 — 10 полных цепочек.
- E2 — барьер до каждого Unload, 0 вызовов до повторной аттестации, DirectQuit.
- E3 — смесь переходов.
- F1 — P1 клиента равен базе мира во всех снимках.
- F2 — rearm-инкарнация применяет поворот хоста.
- G1 — хост принял gen 1..13, P2 один.
- H1 — 10/10 exact16 restores, seam detached.
- H2 — exit 0, `failed:false`, `foreign_calls` 0, `worlds_exited` 10.
- H3 — сохранения не изменились.

Ревью run 02 тем же скриптом (`--run 02`) воспроизводит прежний результат байт в байт: 18/18, A4 только для run 03.

## 8. QA DOC-58C: разбор 7 FAIL + 1 NVR

Целью того QA был кандидат Gemini `remediated_visual_proxy_state_machine.hpp`, **не** принятый автомат. Корневой [`TEST_RESULTS.json`](../../TEST_RESULTS.json) (24/7/1, SHA `01258b28…`) расходится с выводом в папке QA `tools/network-visual-proxy-readiness/doc58c-integration-qa/TEST_RESULTS.json` (29/2/1, SHA `cb78c690…`, та же метка времени). В корневом файле 5 строк FAIL с actual = expected. TC-26 из них — намеренное воспроизведение (FAIL = опасность воспроизведена). Остальные четыре (09, 28, 29, 31) — несогласованные записи. Разбор — [QA_DOC58C_TRIAGE.json](evidence/m1/PSE_TOGETHER_P3/QA_DOC58C_TRIAGE.json).

| Случай | Корень / папка QA | К текущему автомату | Закрыто |
|---|---|---|---|
| TC-03 stale isLevelValid во время Unload | NVR / NVR | **да** (момент барьера у Level::Unload) | нативно: E2 run 02 и run 03 (барьер до каждого из 10 Unload, 0 вызовов до повторной аттестации) + R12 |
| TC-09 отложенная деструкция / гонка Sweep | FAIL (actual = expected) / PASS | **да, по смыслу** (Spawn при отложенном PoolReturn/Sweep) | автомат не спавнит до свидетеля retirement; rearm требует `Retired`: R05, R07; нативно D1 + C1 |
| TC-26 реентерабельный callback seam | FAIL / FAIL (repro) | нет: дефект кандидата | принятый guard `m_inSeamCall` (58B, 58B.2 T20); слой П3 отклоняет session/MapReady/verdict внутри вызова: R26 |
| TC-27 8 слотов, фильтрация | FAIL / PASS | нет: ёмкость 1 — граница DOC-58B | слот 3 `CapacityDeferred` по замыслу: R31; 8 прокси — П22 (S7.1) |
| TC-28 независимые поколения слотов | FAIL (actual = expected) / FAIL | нет: 2-слотовый guard кандидата | принятый `MultiSlotGuard8`, поколения слотов независимы: Q28 |
| TC-29 дисконнект одного из 6 | FAIL (actual = expected) / PASS | нет как дефект: ёмкость (П22) | изоляция экземпляров: Q28 |
| TC-30 быстрый реконнект в тот же слот | FAIL / PASS | **да**: disconnect-latch не снимался (блокер 1 DOC-58B.1) | rearm по инкарнациям: R01–R09; нативно 2 реконнекта в тот же слот, Spawn #2 и #13 (B1, C1) |
| TC-31 карантин при нескольких клиентах | FAIL (actual = expected) / PASS | нет как дефект: ёмкость (П22) | радиус карантина — один экземпляр: Q31 |

## 9. Ограничения и что не сделано

- **S1.2 — смерть → рестарт: не выполнено.** Унаследованный инвариант N3 DOC-64 не перепроектирован. Пока прокси жив, любое изменение 12 полей или графа P1 клиента (здоровье, патроны, ключи) — ошибка адаптера `P1_INVENTORY_CHANGED` / `P1_OWNERSHIP_CHANGED`. Урон до смерти остановит адаптер раньше, чем начнётся выгрузка. П3 изменил только реакцию на `WORLD_NOT_STABLE` после аттестации: вместо вечной блокировки — барьер, свидетель и world lost (`loseWorld`). В прогонах этот путь не срабатывал (0 событий `p3_world_lost`), он не проверен в runtime.
- **S1.2 — выход с уровня → следующая карта: не выполнено.** Адаптер аттестует только TOMB (270 секторов, сектор 259, плоская область). Другая карта не пройдёт повторную аттестацию, и адаптер заблокируется без Spawn. Нужны прокси на любых картах и секторах (П4 S1.4, П17); кроме того, уровень надо пройти до выхода.
- **Смена карты на стороне хоста** не проверялась: хост всё время на TOMB, переходы — только на клиенте. Выгрузка хоста с подключённым клиентом (host P2 через Unload) — отдельный путь (58B.1).
- Из списка UNKNOWN DOC-58B.2 §A теперь RUNTIME OBSERVED: Restart Level, Reload Last Checkpoint, Exit to Title + Load Game и хвост StateExit после purge (вердикт и новый мир). Остаются UNKNOWN: переход на следующую карту, смерть, DirectQuit **с живым** прокси (в run 03 к DirectQuit прокси уже был Retired), другие классы Actor.
- «Загрузка сохранения» проверена только для автосохранения той же карты (TOMB) в тестовом профиле. Сохранения пользователя не использовались, и использовать их нельзя.
- Rearm-инкарнации 3–13 получили по одному применённому update (позиция при Spawn), дальше шли `unchanged`. Изменение состояния в живой rearm-инкарнации показано только на инкарнации 2 (поворот, F2).
- `NewBlockSameAddress` и `IssuedDrained`/`EngineRetiredQueueClear` на барьере в runtime не встретились: барьер ни разу не попал на взведённый Remove. Эти ветки проверены offline (W07, R19–R22).
- Хост подтверждает MapReady по эху epoch клиента (`client_attested_epoch_echo_v110`, `world_synchronized:false`): ограничение wire v1.10, предложение v1.11 — в П4.
- Один прокси (ёмкость 1), один ПК, два процесса, TOMB, сектор 259 — границы DOC-58B. Восемь прокси — П22, два ПК — П5.
- N2 и N4 DOC-64 (отказ update в движении, teleport, выборочный ActorTick) — П4; П3 их не трогал.

## 10. Ошибки, прерванные попытки, целостность

- Run 01 — прерванная попытка с дефектом адаптера (§7). Run 02 принят ревью 18/18, но в нём ещё нет исправления N1. Поэтому финальный прогон — 03 с новой DLL, новым gate и новыми профилями. Принятые прогоны и исследования не повторялись: каждый прогон — новая DLL с изменёнными условиями.
- Ошибки стенда и оператора с последствиями — [HARNESS_ERRORS.md](evidence/m1/PSE_TOGETHER_P3/HARNESS_ERRORS.md). Среди них: меню паузы запоминает курсор (в run 01 один раз открылись Options); первый детектор подсветки ошибался; в run 02 параллельный `resume` с неверным id отклонён REA; первое ожидание E2 не учитывало барьер DirectQuit; `ui_start` иногда останавливается на экране карты.
- Пересборка: свежие деревья `b72` и `b73` с `/PDBALTPATH` PDB из `b71` → [REBUILD_02](evidence/m1/PSE_TOGETHER_P3/REBUILD_02/dll_compare.json) и [REBUILD_03](evidence/m1/PSE_TOGETHER_P3/REBUILD_03/dll_compare.json): 7/7 секций (5 мест хеша анонимного namespace замаскированы), exports равны, сырые байты различаются. Предупреждения сборки только в стороннем ENet, в файлах П3 диагностик 0.
- После каждого прогона проверялась целостность (итог после run 03 — [INTEGRITY_RUN03.json](evidence/m1/PSE_TOGETHER_P3/INTEGRITY_RUN03.json)): 77/77 файлов оригинала и обеих тестовых копий совпали с манифестом M0 (154 сверки копий, копии независимы). Хеши исходников и артефактов — [SOURCE_MANIFEST.json](evidence/m1/PSE_TOGETHER_P3/SOURCE_MANIFEST.json). Лишние файлы в `experiments/m0/game` датированы 2026-10-31…11-01, то есть старше П3. `protected_before == protected_after` для хоста и клиента. Процессов `anubis*`/Frida нет, все сессии REA закрыты.

## 11. Воспроизведение

- Offline: [`run_offline_p3.py`](evidence/m1/PSE_TOGETHER_P3/tools/run_offline_p3.py) → `OFFLINE_03/RESULT.json` (29/29). Gate: `pre_native_p3.py --offline … --out … --freeze …` → [PRE_NATIVE_GATE_P3_03.json](evidence/m1/PSE_TOGETHER_P3/PRE_NATIVE_GATE_P3_03.json), исходники заморожены в `run03_sources/`.
- Прогон: порядок — [RUNTIME_P3.md](evidence/m1/PSE_TOGETHER_P3/tools/RUNTIME_P3.md) ([`runtime_p3.py`](../experiments/pse_together/tools/runtime_p3.py) поверх принятого `runtime_p1.py`). Переходы — `p3cycle.py` / `p3menu.py`, журнал — `p3watch.py`. Завершённые `P3_*_01…03` повторно не использовать: для нового прогона нужны свежие имена, профили и gate.
- Ревью: `python -X utf8 -B docs/evidence/m1/PSE_TOGETHER_P3/tools/review_p3.py --run 03 --out <NEW.json>`.
- Пересборка: `cmake -S experiments/pse_together -B <новое дерево той же длины> -G "Visual Studio 17 2022" -A x64 "-DCMAKE_SHARED_LINKER_FLAGS=/PDBALTPATH:<путь PDB b71>"`, затем `cmake --build … --config Release --target pse_dll_client_p3` и `experiments/pse_together/tools/dll_compare.py`. В `b60` не собирать.

## 12. Что проверить при приёмке В (Sol extra)

1. Правила R1–R4 и мёртвые закрытия в `proxy_rearm_p3.hpp` против DOC-58B.2: latches внутри экземпляра действительно не снимаются; толкование «новой эпохи» как `{generation, epoch, world}` (§2) приемлемо.
2. Детуры свидетеля (`native_proxy_p3.cpp`): pass-through без изменения аргументов (4-аргументный проход `actor_dtor`), обход списков под SEH только по живым блокам, нет чтения нашего Actor после PoolReturn; правило вердикта W09–W15.
3. Remove только на входе Sweep живого тика: нет ли пути, где барьер приходит между выдачей Remove и Drain того же StateTick. Что StateExit не вызывается внутри live-ветки, — INFERRED: в прогонах барьер ни разу не совпал со взведённым Remove, статически все пути вызова не перебраны. Если такое случится, вердикт `IssuedQueued` даст `DeadQueueHazard`: слот умрёт навсегда. Переживёт ли сама очередь движка purge нашего Actor — UNKNOWN; именно этот hazard П3 исключает, выдавая Remove только перед Drain того же тика.
4. N1: все входы Sim TID вызывают `applyForeign()`; чужой поток не трогает `g.life`.
5. Сырые журналы `P3_CLIENT_03/native.jsonl` и `P3_HOST_03/native.jsonl` против таблиц §7; пересборка по `run03_sources`.
6. Что осталось для S1.2 (§9): решать ли N3 в П3 или передать (П7/П14), и где выполнять путь «следующая карта» (П4/П17).

## 13. Исправления по DOC-78 и run 05

[DOC-78](78_P3_ACCEPTANCE_CHECK.md) подтвердил прогоны run 02/03 (сырые журналы 14/14, пересборка 7/7) и вернул пакет из-за дефектов нового кода. Все исправления в тех же файлах П3, принятый автомат и цели П1 по-прежнему не тронуты.

| # | Что исправлено | Где | Проверка |
|---|---|---|---|
| F1 | `onPreUnload`/`onDisconnect`/`requestShutdown`/`requestRemove` ставят защёлку и пересылают её автомату и во время seam-вызова (нужна только affinity); закрытие и rearm ждут следующего обычного входа | `proxy_rearm_p3.hpp` (`intentEntry`, `intentDone`, `intentsDuringSeam`) | offline R34–R38; на старом поведении 4 из них падают ([NEGATIVE_CONTROL_04](evidence/m1/PSE_TOGETHER_P3/NEGATIVE_CONTROL_04/result.txt)) |
| F2 | запрос Remove по лимиту жизни (36000 шагов) принимается один раз **на инкарнацию**, а не один раз на процесс | `PerIncarnationOnce`, `client_p3.cpp` | offline R43. В игре не проверено (см. ниже) |
| F3 | `g.lost` сбрасывается вместе с вердиктом выгрузки, а не только в `enterWorld` | `native_proxy_p3.cpp` (`teardownVerdict`) | по коду; событий `p3_world_lost` в run 05 нет |
| F4 | если адаптер вердикта не выдал, ORPHANED закрывается `DeadNoWitness` явно (`p3_teardown_verdict_missing`) | `onTeardownMissing`, `finishWorldExit` | offline R41, R42 |
| F5 | потеря сессии стирает ожидающую сессию и MapReady слота и роутера | `onDisconnect` | offline R39, R40, R44; в игре оба реконнекта прошли с новой логикой |
| F6 | `sweep()` сбрасывает `inSweep` только на Sim TID | `native_proxy_p3.cpp` | по коду |
| F7 | `applyForeign()` перед нативным Remove | `issueArmedRemoveCpp` | по коду; чужих публикаций в run 05 нет |

Не менялось: F8 (чужой поток при purge/dtor нашего Actor — UNKNOWN), F9 (очередь удаления выводится косвенно — INFERRED), чтение заголовка блока в локальный буфер (вопрос пользователю в DOC-78), заметки DOC-78 §3. Исправлена формулировка R2 и заголовок `client_p3.cpp` (Mismatch не барьер).

**Offline.** `p3_rearm_contract` 64/0 (53 прежних + 11 новых), весь набор 30/30 в `b71` ([OFFLINE_05](evidence/m1/PSE_TOGETHER_P3/OFFLINE_05/RESULT.json), ctest без пересборки; сборка и первый ctest — `OFFLINE_04`, где `snapshot_contract` упал один раз: живой loopback ENet-тест нестабилен, 1 из 5 на одном и том же бинарнике, к П3 не относится). Закреплённая строка набора обновлена до `TOTAL 64`. Gate 05 ([PRE_NATIVE_GATE_P3_05.json](evidence/m1/PSE_TOGETHER_P3/PRE_NATIVE_GATE_P3_05.json)) заморозил DLL `af29298b…` и исходники (`run05_sources/`).

**Run 04 — прерванная попытка.** Та же DLL. После реконнекта и 10 переходов я ждал срабатывания лимита жизни инкарнации 12 (около 11 минут). Через ~17 минут простоя `P1_INVENTORY_CHANGED`: изменилось состояние игрока клиента (здоровье, патроны или ключи; причина не установлена — снимок за секунды до этого равен базе). Это унаследованное ограничение N3: адаптер навсегда блокируется, живой Actor остаётся до выхода. Прогон закрыт штатно (exit 0, 10/10 restores), но сценарий лимита жизни в игре не получен и пересчитывать его в приёмку run 05 нельзя. Попытка и её журналы сохранены. Дополнительно найдено: заблокированный адаптер пишет `native_proxy_fault` на каждый вызов (8276 записей за 80 секунд).

**Run 05 — чистый прогон на той же DLL** (хост 23112/41392, клиент 39880/28500; новые имена и профили; gate 05). Сценарий run 03: Spawn #1, реконнект → #2 (с поворотом хоста), 10 переходов, реконнект → #13, закрытие хоста. Ревью сырых журналов [P3_RUNTIME_RESULT_RUN05.json](evidence/m1/PSE_TOGETHER_P3/P3_RUNTIME_RESULT_RUN05.json) **19/19**. Первый вывод (18/19) сохранён: проверка F2 сравнивала поворот с константой run 03, а шаг мыши в run 05 чуть иной (−2.64206958 вместо −2.63760495); проверка заменена на сверку с рысканием, которое хост сам записал в `position.jsonl`, и пересчитана на всех прогонах (run 02 — 18/18, run 03 — 19/19, run 05 — 19/19). Оригинал и копии 77/77, сохранения не менялись, процессов нет ([INTEGRITY_RUN05.json](evidence/m1/PSE_TOGETHER_P3/INTEGRITY_RUN05.json)). Свежая пересборка в `b75` равна DLL run 05 (7/7 секций, [REBUILD_05](evidence/m1/PSE_TOGETHER_P3/REBUILD_05/dll_compare.json)).

**Инкарнации run 05** (одна строка — одна нативная жизнь Actor):

| Handle | Мир | Gen | Actor | Узел | alloc seq | Класс | Закрытие |
|---|---|---|---|---|---|---|---|
| 1 | 1 | 1 | `61B3FEA0` | `61B1ADC0` | 1459 | First | Retired (реконнект) |
| 2 | 1 | 2 | `61B3FEA0` | `61B1ADC0` | 1459 | PoolReuseAfterRetire | EngineRetired |
| 3 | 2 | 3 | `61B391A0` | `621700C0` | 1569 | NewAddress | EngineRetired |
| 4 | 3 | 4 | `6843B9E0` | `68650800` | 1679 | NewAddress | EngineRetired |
| 5 | 4 | 5 | `618B4990` | `61AA0120` | 1789 | NewAddress | EngineRetired |
| 6 | 5 | 6 | `676CB530` | `61DB13E0` | 1899 | NewAddress | EngineRetired |
| 7 | 6 | 7 | `618B6970` | `68BEF130` | 2009 | NewAddress | EngineRetired |
| 8 | 7 | 8 | `04A55990` | `68BEA4F0` | 2119 | NewAddress | EngineRetired |
| 9 | 8 | 9 | `04A5FA30` | `61DB13E0` | 2229 | NewAddress | EngineRetired |
| 10 | 9 | 10 | `04A56870` | `684E44E0` | 2339 | NewAddress | EngineRetired |
| 11 | 10 | 11 | `04A406B0` | `6891B800` | 2449 | NewAddress | EngineRetired |
| 12 | 11 | 12 | `676D2870` | `68A95AA0` | 2559 | NewAddress | Retired (реконнект) |
| 13 | 11 | 13 | `676D2870` | `68A95AA0` | 2559 | PoolReuseAfterRetire | Retired (потеря хоста) |

Remove: 3 из 3 на входе Sweep (тики 2363, 15120, 15774), Drain того же тика, затем retirement. 10 переходов — те же 4× Exit to Title + Load Game, 3× Restart Level, 3× Reload Last Checkpoint, все PASS, ArenaDestroyed / NotIssued.

**Что остаётся открытым** (границы PASS bounded, если он будет поставлен): лимит жизни у rearm-инкарнаций проверен только offline (R43); смерть → рестарт, следующая карта и смена карты хостом не выполнялись; N3; один прокси, TOMB, два процесса на одном ПК; F8, F9. Случайный простой игрока в уровне (как в run 04) ломает контроллер по N3 — для будущих долгих прогонов нужен перепроект N3.
