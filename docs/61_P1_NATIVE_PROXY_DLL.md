# DOC-61 — П1 / S0.4: управление визуальным прокси внутри DLL

> **Приёмка (2026-11-04):** PASS (bounded), Opus extra — [DOC-64](64_P1_ACCEPTANCE_REVIEW.md). Ниже текст исполнителя без изменений.

2026-11-04. Исполнение П1; статус **REVIEW, режим В**. Б повышен до В из-за переноса lifecycle-наблюдателей в нативные детуры. Следующие зависимые пакеты ждут отдельной приёмки Opus extra по ROADMAP. Машинный результат ниже — кандидат, не приёмка пакета другой моделью.

**Результат:** `pse_dll_client_proxy` создаёт, перемещает и удаляет один Actor1007 посредством C++ внутри единого `experiments/pse_together/`. Сценарий DOC-58B run 02 воспроизведён двумя процессами на одном ПК без JS-адаптера прокси: network → native Spawn/update → sprite renderer/native lerp → disconnect → Remove/Drain/PoolReturn/retirement → штатные exit. Проверка сохранённых исследований на входе S0.3 выполнена без их повторного запуска.

Целевой EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Проверены обе тестовые копии перед свежими запусками. Реальная база в принятых наблюдениях этого прогона — `0x140000000`.

## Код и контракт

- Канонический рабочий код — [`experiments/pse_together/`](../experiments/pse_together/README.md). Лаборатории `experiments/m1/*`, их замороженные DLL и принятые evidence сохранены.
- [`native_proxy.cpp`](../experiments/pse_together/src/native/native_proxy.cpp) переносит принятый контракт DOC-58A/58B: фабрика `1d4780`, три origin float `+18/+1c/+20`, нормализованный yaw `+38`, reset интерполянтов при teleport/yaw; обычный шаг до 160 единиц использует нативный lerp. Нет memcpy игровых структур, записи ссылок/флагов/sector index или принудительного free.
- [`bounded_proxy.hpp`](../experiments/pse_together/src/proxy/bounded_proxy.hpp) — реально используемые геометрия, mapping и одноразовые бюджеты. Unknown/null результата фабрики исчерпывает Spawn; Remove не повторяется после неизвестного исхода. После PoolReturn Actor/node сразу недоступны, ObjectNode проверяется отдельно на возврате Sweep.
- Шесть детуров: PuppetTick, ActorTick, OnRemove, PoolReturn, Sweep, Drain. Они сверяют TID и simulation scope. Mutations выполняются только в фазе DLL StateTick после исходного KEX tick и consume; Worker получает DTO. Интерфейс seam/opaque handle и автомат DOC-58B.2 сохранены; внешний `PSE58B_RegisterSeam` отказывает кодом 50. Таблица callbacks скомпилирована в закреплённой DLL.
- `PSE_P1_ReadObservation(out,128)` копирует диагностический DTO только на Sim/render TID. Чужой поток отклоняется до чтения изменяемого состояния; TID опубликован атомарно. `PSE_P1_IsPositionInterpolant(candidate)` сравнивает кандидата без разыменования. Из этих API адреса Actor/ObjectNode наружу не передаются.
- [`observer_p1.js`](../experiments/pse_together/tools/observer_p1.js) вызывает только read-only exports. Для renderer identity собственная пара берётся из журнала native Spawn; собственный Actor не разыменовывается. После свежего Live DTO на том же Sim TID census проверяет собственный node и остальных Actor текущего сектора. UNKNOWN/AMBIGUOUS не превращаются в OURS. После invalidation census не выполняется.
- Frida не управляет gameplay-прокси. Принятые DOC-51 loader, isolation guard и атомарные разрешения стенда остаются; новый загрузчик/инжектор не создавался. OS path callbacks isolation guard не являются JS-адаптером прокси. Wire v1.10 и транспорт не менялись.

Адреса и ABI переиспользованы из сохранённых DOC-57/58A досье. [Координаты 12 функций](evidence/m1/PSE_TOGETHER_P1/RUNTIME_COORDINATES_P1.json) разделяют SHA бинарника, preferred VA, RVA и file offset; префиксы сверены по байтам EXE. Имена ролей аналитические, восстановленные C++ символы не заявляются. Factory: Win64 `(factory*,int,position*,float,int) -> Actor*`; Remove/reset: один pointer; point query: `(sector*,point*,float) -> uint8`; floor/ceiling: `(point*,sector*,uint8) -> float`. Нативные detours сохраняют RAX исходной функции; это не восстановление её исходного return type. Подробности статической проверки и исправления PN1–PN4 — [pre-native review](evidence/m1/PSE_TOGETHER_P1/PRE_NATIVE_CODE_REVIEW.md). Последняя правка порядка отказа read-only API проверена сборкой/preflight и исполнена в run 03.

## Входная S0.3 и машинные проверки

[S0.3 review](evidence/m1/PSE_TOGETHER_P1/S03_ENTRY_REVIEW.md): PASS в offline-границах. Сверены 55/55 source hashes, 31/31 пары UNIFICATION_MAP, MinHook 16/16, сохранённые журналы 23/23 наборов и DLL-сравнения. Новых KEX-прогонов для принятия S0.3 не было.

| Проверка | Результат | Evidence |
|---|---|---|
| Единый configure/build + CTest, все исходные наборы и новый C++ bounded contract | 24/24 | [восстановленная сверка исходного XML](evidence/m1/PSE_TOGETHER_P1/OFFLINE_01/RECOVERED_RESULT.json) |
| Новый C++ контракт geometry/mapping/lifetime | 118 checks, 0 failed | [результат](evidence/m1/PSE_TOGETHER_P1/BOUNDED_CONTRACT_RESULT.json) |
| Текущая клиентская DLL, `/W4 /WX /permissive-` | build PASS | [сборка run 03](evidence/m1/PSE_TOGETHER_P1/BUILD_RUN03_THREAD_GUARD.log) |
| Exact EXE/sites, wrong-process refusal, внешняя seam запрещена, DTO128, Python syntax | 11/11 | [PRE_NATIVE_GATE_RUN03](evidence/m1/PSE_TOGETHER_P1/PRE_NATIVE_GATE_RUN03.json) |
| Реальная функция observer census на mock-memory: валидный/двойной объект, stale/invalid scope, разрыв/cycle links, unreadable fields, alias; owned Actor reads всегда 0 | 32 checks, 0 failed | [лог](evidence/m1/PSE_TOGETHER_P1/OBSERVER_CENSUS_TEST.log), [проверка](../experiments/pse_together/tools/check_observer_census.js) |

Это отдельные offline/preflight результаты, не доказательства работы в KEX. Полный набор повторно не запускался после правок только DLL/стенда, которые не меняют источники контрактных тестов.

Ошибка `run_offline.py --out <relative>` сначала дала ложный MISSING: CTest сохранил XML относительно `b60`. Исправлено разрешение абсолютного пути; первоначальный FAIL сохранён. Исходный XML скопирован и сверён без повторения тестов ([recovery](evidence/m1/PSE_TOGETHER_P1/OFFLINE_01/RECOVERED_RESULT.json)).

## Нативный прогон 03 — RUNTIME OBSERVED

Свежие профили `experiments/pse_together/runtime/profile/P1_HOST_03` и `P1_CLIENT_03`. Хост PID29964/Sim38120; клиент PID31168/Sim40152. Два процесса на одном ПК. Запуск через принятый suspended launcher, isolation PASS обоих процессов, загрузчик DOC-51 и retained/duplicate handle watcher DOC-53.

| Артефакт единой сборки | SHA-256 |
|---|---|
| Host `pse_m11_bootstrap.dll` | `73b76e1f0698d314905f5991a478237557f50b5c7db4daeac2536a66a9303376` |
| Client `pse_native_client58b.dll` | `2366326a99ee05c4a9e4ec388431af901cbc15d99d9d427041a2e38c81758e97` |

Копии, реально загруженные в процессы, лежат в `P1_HOST_03/native_artifact/` и `P1_CLIENT_03/native_artifact/`; манифесты в тех же run-каталогах. Это новые кандидаты, не замена принятых DLL DOC-56/58B.

| Наблюдение | Результат |
|---|---|
| Native MapReady | epoch1245199520, TOMB; отдельно от Welcome; геометрия 49 samples/270 секторов, floor1152/ceiling1664 |
| Фабрика | 1 call/return/Spawn; opaque handle1, Actor `0x61bed860`, node `0x61bc52c0`, отдельно от Local P1 |
| Snapshot → native current transform | 217 updates, 0 missing source, 0 bit mismatches; position z+88 и нормализованный yaw |
| Host capture → OS send → OS receive → Client Sim | 10 283 строки в общем окне socket-наблюдения, 0 missing stage, 0 bit mismatches |
| Slot demux | slot0 Routed7640; slot1 LocalExcluded7639; нарушений нет |
| Реальный sprite renderer | 19 202 OURS submissions по свежему DTO/census; ambiguous0, unknown0 |
| Интерполяция | 640 native lerp samples; 490 intermediate sprite submissions, все совпали с собственным lerp; t восстановлен, max formula error0.007081 |
| Шаги/ActorTick | 168 выборочных counters: во всех `steps=ticks+1`, включая Spawn tick до первого ActorTick; последние 9900/9899. Полного per-step журнала не заявлено |
| P1 клиента | 119 полных observer snapshots ×12 полей и 9 C++ snapshots по54 байта; все поля/identity совпали, позиция не изменилась. Обычный mouse-look клиента использован только для диагностического кадра |
| Disconnect и retirement | LOCAL_SESSION_LOST01:43:02.677 → один Remove → pending(refs1, membership1) → Drain/OnRemove/PoolReturn → retirement01:43:02.727: payload null, refs0, in_use0, membership0, все weak pointers null |
| Выход | шесть native hooks exact16 восстановлены, seam Detached3 до закрытия; client Update и четыре host controls восстановлены; оба retained+duplicate wait0/exit0; emergency cleanup не было |

[Runtime candidate](evidence/m1/PSE_TOGETHER_P1/P1_RUNTIME_ACCEPTANCE_CANDIDATE.json): **34/34 PASS**, с конкретными ограничениями каждого утверждения. Native faults/foreign calls/observer errors0; реальные TID собственных операций совпали с Sim TID. Литерал `foreign_calls:0` в sampled stable-событии не использовался как независимый счётчик. [19 REA sessions закрыты](evidence/m1/PSE_TOGETHER_P1/RUN03_SESSIONS_CLOSED.json).

Raw: [host native](evidence/m1/PSE_TOGETHER_P1/P1_HOST_03/native.jsonl), [client native](evidence/m1/PSE_TOGETHER_P1/P1_CLIENT_03/native.jsonl), [renderer observer](evidence/m1/PSE_TOGETHER_P1/P1_CLIENT_03/observer_LIVE.jsonl), BEFORE/AFTER_RETIREMENT рядом. Полные retained REA records: [movement bundle](evidence/m1/PSE_TOGETHER_P1/BUNDLE_RUN03_MOVEMENT.json), [exit bundle](evidence/m1/PSE_TOGETHER_P1/BUNDLE_RUN03_EXIT.json). Корреляция — соседний `P1_RUNTIME_ACCEPTANCE_CANDIDATE_CORRELATION.jsonl`.

## Ограничения и незавершённые пакеты

Только один пассивный Actor1007, одна карта TOMB, сектор259, фиксированный z+88, foreground-применение клиента; без LinkSector и world replication. Reconnect/anti-ABA, engine teardown witness, «Remove в полёте» и map transitions — П3; spawn placement/порталы/jitter/background клиента — П4. Их gates не менялись. Два ПК, 3–8 игроков, UI, Steam achievements и загрузчик без Frida не проверялись. Внешняя проверка режима В ещё не выполнена.

Критерий S0 воспроизведён в этих же bounded-границах DOC-58B; принятие П1 остаётся за другой моделью. Снимок окна после mouse-look — вспомогательная диагностика; доказательство renderer/interpolation основано на отдельных native submissions/lerp/census, а не на выдаче скриншота за трассу.

## Ошибки, прерванные попытки, целостность

- Run01: обычный ввод вывел Host P1 в сектор258; адаптер корректно отказал OUT_OF_ATTESTED_AREA без единой фабрики. Обе игры штатно закрыты exit0. Затем изменена подготовка: одна короткая W до join с проверкой нативной позиции, далее strafe/mouse.
- Run02: Native Spawn и стабильная работа наблюдались; продолжение прервано ограничением сессии. После возобновления REA session отсутствовала, процессы отсутствовали, exit.json не было; handle watcher исчерпал бюджет. [Сводка](evidence/m1/PSE_TOGETHER_P1/RUN02_INTERRUPTION.json) — **INTERRUPTED_NOT_ACCEPTED**, не evidence штатного завершения. Сырые журналы сохранены. Run03 свежий, с исправлением thread guard API; результаты не смешивались.
- Foreground временами удерживал SearchHost. Отказы принятых helper сохранены; fallback делает один реальный click только в проверенный HTCAPTION собственного HWND/PID, с повторной проверкой. Synthetic activation сообщений нет. Проверки стандовых DTO/focus лежат в том же evidence-каталоге.
- [Целостность в области П1](evidence/m1/PSE_TOGETHER_P1/P1_SCOPED_INTEGRITY.json): исходные77 файлов игры, обе EXE и защищённые профили совпали с baseline; все52 компилируемых файла совпали с pre-native freeze run03. Строгая общая сверка помечает параллельную работу:43 добавления П2 [зафиксированы до run03](evidence/m1/PSE_TOGETHER_P1/CONCURRENT_WORKSPACE_ADDITIONS.json) и не менялись. Позже П6 создал research script и evidence в `P6_ADR_AUTHORITY_WORLD`, изменил два cache-файла Ghidra; [первое позднее наблюдение](evidence/m1/PSE_TOGETHER_P1/CONCURRENT_WORKSPACE_LATE.json) и полный список есть в итоговой сверке. П1 эти файлы не редактировал и не принимает. Raw strict-result сохранён с FAIL общей рабочей папки; scoped PASS относится к защищённым данным, исходникам П1 и остальной исторической базе вне явно перечисленных конкурентных cache-изменений.

## Воспроизведение и передача

Сборка/offline: `python -X utf8 -B experiments/pse_together/tools/run_offline.py`. Текущие24 набора, CMake tree `b60`. Стенд и порядок — [`RUNTIME_P1.md`](../experiments/pse_together/tools/RUNTIME_P1.md). Перед новым runtime — новое имя/profiles, новый pre-native freeze и isolation gate; не переиспользовать завершённые run03.

Машинная повторная обработка сохранённых данных: `review_runtime_p1.py --host …/P1_HOST_03 --client …/P1_CLIENT_03 --bundle …/BUNDLE_RUN03_MOVEMENT.json --bundle …/BUNDLE_RUN03_EXIT.json --integrity …/P1_SCOPED_INTEGRITY.json --out <новый-файл.json>`. Сохранённые function dossiers и принятые KEX-прогоны повторять не требуется.

Проверяющему режима В: текущие native source hashes/реально загруженные DLL, PN1–PN4 и отказ API до foreign-state reads, полная цепочка retirement и exact restores; затем принять bounded-критерий П1 либо вернуть конкретное замечание. П3/П4 не начинать до PASS П1.
