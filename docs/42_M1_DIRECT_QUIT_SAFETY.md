# M1.1b — Direct Quit Safety

Дата: 2026-11-01. **Результат: PARTIAL / PENDING.** Выполнен ровно один ограниченный runtime-test существующей DLL с живым P2 и штатным Quit из pause menu уровня. Native Direct-Quit unbind, exact restore hooks и путь до native final exit подтверждены. Не получен запланированный OS-boundary snapshot у фактического `ExitProcess`; сохранение всех необходимых buffers именно на этой границе остаётся UNKNOWN. **PASS не ставится по exit0.**

M0 и M1.1 остаются принятыми в прежних пределах. Обнаруженного ABI/lifetime дефекта DLL нет; текущий блокер — неполнота диагностического подтверждения финальной границы. Повторный runtime не проводился. ENet/Thread Bridge и M1.2 не затрагивались; после проверки работа остановлена.

## Evidence и неизменённая сборка

[Native journal](evidence/m1/M11B_DIRECT_QUIT_01/native.jsonl), [RESULT_SUMMARY](evidence/m1/M11B_DIRECT_QUIT_01/RESULT_SUMMARY.json), [выборка lifecycle](evidence/m1/M11B_DIRECT_QUIT_01/LIFECYCLE.json), [final capture](evidence/m1/M11B_DIRECT_QUIT_01/status_final_after_direct_quit.json), [post-exit drain](evidence/m1/M11B_DIRECT_QUIT_01/status_post_exit_drain.json). Оба завершённых capture содержат107 messages, not truncated; native journal55 последовательных records. Script errors0, recorded native faults0.

- EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
- Существующая DLL SHA-256 **`0cb84521c47dddd20bdebf3a2f42dda79f2f50ef1cfc3c280c14878812648629`**. DLL не редактировалась и не пересобиралась; [frozen artifact](evidence/m1/M11B_DIRECT_QUIT_01/NATIVE_ARTIFACT.json) совпадает с принятой M1.1.
- Новый run `M11B_DIRECT_QUIT_01`, PID37648; primary/APC/наблюдаемый simulation **TID8264**. DLL base `0x7ffa2cb20000`.
- Проверенная отдельная копия — `experiments/m0/game`; новый профиль/CWD — `experiments/m1/native_bootstrap/profile/M11B_DIRECT_QUIT_01`. [Launch](evidence/m1/M11B_DIRECT_QUIT_01/launch.json), [load](evidence/m1/M11B_DIRECT_QUIT_01/load_native.json), [fresh isolation PASS](evidence/m1/M11B_DIRECT_QUIT_01/ISOLATION_GATE.json).
- [Baseline118 файлов](evidence/m1/M11B_PREPARATION_20261101/BASELINE.json) сохранён до запуска; [integrity после выхода](evidence/m1/M11B_DIRECT_QUIT_01/INTEGRITY.json) подтверждает неизменность DLL, её исходников/harness, проверенных M0/transport файлов, DOC-40 и DOC-41.

Использованы существующие [DOC-41](41_M1_NATIVE_BOOTSTRAP.md) и [C25 Direct Quit contract](evidence/m0/C25_NATIVE_DIRECT_QUIT_BOUNDED/RESULT.md). Native GetModuleHandleEx(PIN), storage ownership и hook lifecycle DLL оставлены без изменений. Новых Ghidra/decompile, constructor или прежних M0 gameplay исследований не было.

## Выполненный сценарий — RUNTIME OBSERVED

После реального native P1 tick и fresh isolation gate передан только atomic permit существующей DLL. Её фиксированная последовательность завершила120 P2 controller/command/native Puppet updates;120 сравнений состояния P1 прошли, foreign_calls0. Отдельного повторения M1.1 movement experiment или новых игровых функций не добавлялось: выполнен единственный предусмотренный существующей DLL permit.

| Живой graph | P1 | P2 |
|---|---|---|
| Player |0x140af33e0|0x7ffa2cb470e0|
| Puppet |0x6bc3cfa0|0x6bc3dfd0|
| Node |0x13722078|0x135f8230|
| Owner |свой Player|свой Player|
| Health непосредственно перед unbind |200|200|

В `before_unbind` P2 node refs1, guards=true, native Puppet ticks3584, controller/commands120. P1 сохранил identity/health. Ранее journal фиксирует реальные P1 updates1/26/146, затем120 P2 updates после штатных P1 updates; это native simulation, а не наличие двух объектов или только рендер.

Меню использовано адресованными проверенному HWND сообщениями: **ESC → Down×5 → Quit → Return/Left/Return (Yes)**. Это pause menu внутри уровня, не Exit to Title Screen. [Quit выбран в уровне](evidence/m1/M11B_DIRECT_QUIT_01/window_20261101_210334_635614.png), [подтверждение Yes](evidence/m1/M11B_DIRECT_QUIT_01/window_20261101_210408_558749.png), key/helper journals сохранены в run directory. Перехода в title/main menu после создания P2 не было; StateExit records0.

## Native lifecycle

Все времена UTC, native records44–55 на TID8264.

| Время | Наблюдение |
|---|---|
|21:04:51.170|`shutdown_entry`: `state_exit_preceded=false`, живой внешний handle P2 |
|.208/.246/.284/.286|Update/Turning/Commands/Getter восстановлены exact16 |
|.286|`before_unbind`: оба Player/Puppet live, health200, guards=true |
|.287|Единственный `exit_reset`, reason=`DirectQuit`; native Reset вернулся, external+d8=0, P1 unchanged, guards=true |
|.289|`shutdown_quiesced`: handle0, pin-until-process-exit=true, forced_teardown=false |
|.290|Original engine shutdown entry **после** unbind |
|.407|Engine shutdown return; Actor/Node больше не читаются |
|.429|`native_final_exit`: handle0, guards=true, code/storage pinned, foreign_calls0, ticks_after_shutdown0, failed=false |
|.471|Launcher exit0; отдельное подтверждение завершения, не lifecycle PASS |

StateExit entry/return0, owned Puppet Actor destructor entry/return0. Adapter не вызывал Remove, arena cleanup, refcount/target repair или destructor. DLL/скрипты не выгружались преждевременно; Frida sessions закрыты только после native termination.

Координаты относятся к указанному EXE SHA; названия ролей аналитические. Win64 this-in-RCX / Reset(Player*) contracts прежние. Preferred VA, RVA и file offset разделены:

| Роль | Preferred VA | RVA | File offset |
|---|---|---|---|
|Platform shutdown|0x1400fcc20|0xfcc20|0xfc020|
|Player Reset|0x1401dfa70|0x1dfa70|0x1dee70|
|Engine shutdown|0x1400a4c90|0xa4c90|0xa4090|
|Native final exit|0x1400fc580|0xfc580|0xfb980|
|StateExit, не наблюдался|0x1401b6670|0x1b6670|0x1b5a70|
|Actor destructor observer, не наблюдался|0x14019f210|0x19f210|0x19e610|

Exact prefixes и конкретный исходный M0 Evidence — [принятый координатный manifest](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json); контракт final exit/engine shutdown — [C25 static result](evidence/m0_static_final_acceptance/RESULT.md). Статический `_exit`→ExitProcess route не подменяет отсутствующее текущее OS-наблюдение.

## Что проверил дополнительный observer и чего не подтвердил

Новый [пассивный probe](../experiments/m1/direct_quit_safety/probe.js) не вызывает KEX, не пишет game objects, не ставит hooks на KEX/DLL code и не ищет symbols/PDB. Он читает установленные MinHook relays, проверяет их targets внутри DLL и наблюдает только OS APIs. [Bound source/config](evidence/m1/M11B_DIRECT_QUIT_01/DIRECT_QUIT_PROBE.json), [load/readiness raw](evidence/m1/M11B_DIRECT_QUIT_01/load_direct_quit_observer.json).

Перед Quit `VirtualQuery` подтвердил:

- статический P2 storage в mapped MEM_IMAGE DLL, handle0x135f8230 и целые16-byte guards;
- десять native DLL callbacks в committed executable image memory;
- один committed executable allocation с MinHook relay/original-trampoline buffers; полные bounded bytes до Quit сохранены;
- control entries ещё установлены, scoped GetPlayer уже восстановлен.

Установлены наблюдатели `kernel32!ExitProcess`, `ntdll!RtlExitUserProcess`, целевых FreeLibrary/LdrUnloadDll/VirtualFree calls. В полном сохранённом capture есть `m11b_exit_observer_ready`, но **нет `m11b_os_exit` ни для одного OS boundary**. Post-exit drain новых сообщений не добавил. Capture не truncated, guard ledger не overflow, script error не записан. Причина отсутствия этих событий **UNKNOWN**: нельзя утверждать, что OS функция не вызывалась, либо назначить неподтверждённую причину потери наблюдения.

Не записано target DLL unload/pool-release events, однако этот отрицательный результат не заменяет отсутствующий финальный snapshot. Source-verified PIN и положительный native final-exit marker поддерживают принятый дизайн C25; они не превращаются в заявленную здесь runtime-проверку всех code/storage/trampoline buffers на фактическом ExitProcess. Именно этот checkpoint оставлен PENDING. Это не доказательство premature free или ошибочного lifecycle DLL.

## Итог по требованиям

| Требование | Оценка |
|---|---|
|Существующая DLL/изолированная копия/живой P2|Подтверждено |
|Реальные native P1/P2 updates|Подтверждено |
|Direct Quit из уровня без предварительного StateExit/title|Подтверждено |
|Своевременный native unbind и exact control restore до services|Подтверждено |
|Отсутствие force-destructor/hot unload|Подтверждено для adapter/наблюдаемого пути |
|Код и storage до native final exit|Есть native execution/pin witnesses; дополнительные mapped-page checks только до Quit |
|Все необходимые buffers / отсутствие freed-code callbacks на границе ExitProcess|**PENDING / UNKNOWN**, OS snapshot не получен |
|Оригинальная игра/защищённые saves|Неизменны |

Офлайн review14 checkpoints оставил2 непрошедших, относящихся к одному отсутствующему OS checkpoint; **общая оценка PARTIAL**. Recorded native fault logger покрывает AV/illegal instruction/stack overflow, не все возможные исключения. Нельзя объявлять отсутствие любых lifetime ошибок только по отсутствию crash и exit0.

## Целостность, файлы и остановка

[INTEGRITY](evidence/m1/M11B_DIRECT_QUIT_01/INTEGRITY.json): оригинал77/copy77 соответствуют прежним SHA и не alias друг другу; два protected profile snapshots before/after равны;118 immutable files не изменились. Успешных внешних write opens в наблюдаемом API scope нет. Guard — диагностический API layer, не полная OS sandbox; protected/original hashes проверены отдельно.

[PROCESS_CHECK](evidence/m1/M11B_DIRECT_QUIT_01/PROCESS_CHECK.json):0 игровых процессов. [SESSIONS_CLOSED](evidence/m1/M11B_DIRECT_QUIT_01/SESSIONS_CLOSED.json): все8 созданных sessions закрыты после termination. Все native/OS readiness messages, ошибки/отсутствующие checkpoints, screenshots, input journals, stdout/stderr и frozen DLL/source сохранены; raw Evidence не перезаписывалось.

Добавлены только:

- `experiments/m1/direct_quit_safety/probe.js` — passive observer;
- `experiments/m1/direct_quit_safety/evidence.py` — file-only baseline/binder/review;
- `docs/evidence/m1/M11B_PREPARATION_20261101/`, `M11B_DIRECT_QUIT_01/` — baseline/raw/frozen artifact/result;
- этот DOC-42; обновлены PROGRESS и EVIDENCE_INDEX. [M1.1b artifact manifest](evidence/m1/M11B_PREPARATION_20261101/M11B_FILES.json).

Для проведённого run использован неизменённый `native_bootstrap/runner.py`: provider launch→bind→OS isolation+DLL load→resume→gameplay gate→permit; затем file-only `direct_quit_safety/evidence.py bind M11B_DIRECT_QUIT_01`, загрузка bounded probe и штатный UI Quit. Офлайн review — `evidence.py review`; он не запускает/инструментирует игру и сохраняет результат в новый файл, не перезаписывая прошлый.

**Один runtime выполнен; проверка остановлена. Для закрытия PENDING нужен новый отдельно заданный диагностический шаг. DLL, ENet/Thread Bridge не менялись, M1.2 не начинался.**
