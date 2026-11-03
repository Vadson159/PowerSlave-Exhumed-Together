# DOC-58B.1 — Native Level Unload & Single-Proxy Reconnect Safety

2026-11-03 UTC. **Overall: PARTIAL. G1: OPEN. Reconnect: BLOCKED на уровне Proxy integration.** В одном новом изолированном процессе доказан настоящий порядок выхода из Tomb of Ramses в титульное меню и освобождения массива секторов. Живой Remote Proxy на границе Unload не создавался: ownership Actor1007/ObjectNode при последующей bulk cleanup не доказан. Два native-поколения Proxy, native ABA и network reconnect в живом KEX Client не выполнялись. Исторический DOC-58B BOUNDED PASS сохранён и не считается новым результатом.

Лаборатория: `experiments/m1/native_proxy_lifecycle_58b1/`. Evidence: [NATIVE_PROXY_LIFECYCLE_58B1](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/RESULT_SUMMARY.json). Все новые исходники, profiles, scratch, build и журналы — внутри корня мода на S:. TEMP/TMP перенаправлены; build `b581/`, C++20 `/W4 /WX`. Исполнитель этой сессии — Codex; назначение другой модели в задании не является утверждением о фактически исполнявшей модели.

## A. G1 — что действительно установлено

Exact EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Использована принятая независимая test copy `experiments/m0/game/`; новые profiles и Evidence принадлежат DOC-58B.1. Runtime base `0x140000000`. **PID 30336 / Sim TID 39756**, 1,245 наблюдённых local Player update callbacks. Это процесс разведки; Host/Client-пара не запускалась. Девять точных префиксов проверены перед attach. Game data writes и NativeFunction/KEX calls из trace отсутствуют; установка Frida observers меняет исполняемые hook-sites.

Новые live hooks: StateTick RVA `1b6ec0`, local Player Update `1dfe30`, StateExit `1b6670`, Level::Unload `1c90e0`, DirectQuit `fcc20`, arena cleanup `c58b0`, factory `1d4780`, Remove `1a02b0`, Drain `1b61d0`; дополнительно наблюдался CRT `free` из IAT RVA `482ae8`, с фильтром точного sector-array pointer. Gameplay DLL/MinHook не загружены, дублирующихся hooks не было. В observer нет native Spawn/Remove calls. Статические функции из сохранённых exact-build Ghidra records повторно не декомпилировались; ограничения непрямых ссылок сохранены в [STATIC_LIFECYCLE_AUDIT](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/STATIC_LIFECYCLE_AUDIT.json).

Штатный сценарий: fresh profile → обычный старт → Tomb of Ramses → Esc → **Exit to Title Screen** → **Yes**. Скриншоты меню, подтверждения, input JSON и первичный `N58B1_TRACE_01/lifecycle.jsonl` сохранены. Консольная `map`, cheats и исходные saves не использовались. `SetForegroundWindow` вернул false, однако measured Sim ticks и дальнейший native transition состоялись; тождество desktop с прежними REA-spawned runs не заявляется.

| Порядок | Native witness (UTC) | Секторы |
|---|---|---|
| 1 | tick 6347, 20:28:26.197: state3/next3, pending1 | pointer `0x61aa3a70`, count270 |
| 2 | StateExit entry, tick6405, 20:28:27.197: state3/next1, pending0 | ещё живы |
| 3 | Unload entry, 20:28:27.199; `this=base+a45940`, return RVA `1b6935` | тот же pointer/count |
| 4 | sector `free` entry/return, 20:28:27.220, return RVA `1c92e2` | аргумент `0x61aa3a70`; освобождённая память не разыменовывается |
| 5 | Unload return, 20:28:27.220 | pointer0/count0 |
| 6 | arena `base+af4490`, затем `base+af4600` cleanup | уже pointer0/count0 |
| 7 | StateExit return, затем tick6406, state1/next1 | pointer0/count0 |

Причинный порядок здесь задаётся вложенными callbacks и serial `seq`, не одним округлённым временем. См. [LEVEL_UNLOAD_CALL_ORDER](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/LEVEL_UNLOAD_CALL_ORDER.json). StateTick entry/exit сохранялись выборочно. Точное время последнего из 1,245 local Update не сохранялось; на границе transition счётчик уже не рос. Последний «безопасный для Proxy Remove» тик **не аттестован** одним этим счётчиком.

**Расхождение канонического offset.** G1 в Gemini DOC-58D.1 указывает sectors по `rcx+0x140`. Канонический текст и ID не изменялись. Принятый DOC-58A adapter использует `level+0x28`, count `+0x30`, stride `0x88`. Сохранённые инструкции Unload подтверждают `CALL [140482ae8]` по VA `1401c92dc`, затем обнуление pointer/count; новый runtime независимо подтвердил free этого массива. На `+0x140` оставались байты `c0079e61000000004000000010000000` даже после Unload. Этот offset нельзя использовать как witness валидности sector structures данной сборки. Более того, в момент **возврата free** глобальный pointer/count ещё оставались ненулевыми до последующих инструкций zeroing: проверка только на nonnull недостаточна.

**MapReady / session cleanup.** В recon нет ClientGate и сетевой DLL, поэтому порядок Client MapReady invalidation и Session::stop не наблюдался. По текущему source DOC-58B StateExit detour сначала вызывает `exit58`: latch barrier → adapter worldExit → seam detach; затем `stop`: worker.stop/join → inbound retirement → bridge reset; только после этого original StateExit. При stopped consume больше не вызывается. `ClientGate` здесь не следует считать автоматически Invalidated: `Session::stop` не вызывает отдельный invalidate. Это source conclusion, не runtime witness DOC-58B. На других путях Unload такое покрытие не доказано.

**Actor ownership.** В наблюдавшемся StateExit/Unload не было диагностического Proxy; индивидуальные native Remove/Drain в этом teardown-окне не наблюдались. После sector free идут arena cleanups и дальнейший factory cleanup по статическому графу. Ни вызов arena cleanup, ни отсутствие AV не подтверждают retirement конкретного Actor1007 или ObjectNode. Actor identity, pool return, node payload/refs/in_use и отсутствие ghost для Proxy здесь не измерены. Альтернативные destructor/unload paths и возможная реентерабельность bulk cleanup полностью не покрыты.

**Решение A2/A3:** native preconditions для следующего Spawn/Remove с последующим Unload не удовлетворены. Изменений gameplay seam/state machine нет; исправленный baseline DOC-58B сохранён. Не выполнялись forced destructor/free, поздний Remove, linked-list repair или попытка «очистить» ORPHANED. Новый pre-unload mutation barrier и безопасный rearm не внедрены без ownership witness. `SAFE_PRE_UNLOAD_RETIREMENT = NOT_EXECUTED`; `LIVE_PROXY_UNLOAD = NOT_EXECUTED`; **G1 = OPEN**, не PARTIAL по более слабому retirement-сценарию. [G1_NATIVE_WITNESS](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/G1_NATIVE_WITNESS.json).

## B. Reconnect и generations

Production ENet уже поддерживает reconnect без изменения Wire v1.10: `ENetWorker::reconnectClient` отключает старый node, использует reconnect key и создаёт новый client node; Welcome выдаёт CLIENT_JOINED/RECONNECTED DTO и authority generation. Client DLL ставит RECONNECT через ThreadBridge; Worker не вызывает KEX. Свежие Client/ENet contracts проходят, однако это не B3 native reconnect.

Конкретные blockers текущей Proxy integration:

1. `onDisconnect` навсегда латчит `m_disconnected`. `configureSession` сбрасывает identity guard и Spawn budget, но этот latch не снимает. Новый тест после Remove → Retired → ABSENT → configureSession(gen2) → NativeMapReady воспроизводит `SessionInactive`, factory counter остаётся 1. Это подтверждение ограничения, не successful second generation.
2. `adapter58b.js` содержит один `OneActor57C` lifecycle. После retired не реконструируются life/witness state, live handle и per-generation renderer bookkeeping. Простой сброс флага в state machine не является корректным reconnect patch.
3. Map transition отдельно терминален: ClientGate Invalidated, g.stopped, teardown barrier и seam detach не rearm-ятся. Транспортный reconnect не доказывает новую map epoch/MapReady.

Первой/второй **новой native session generation нет**, первой/второй native Actor generation нет. Host PID/Client PID/TID для B3 отсутствуют. Новых Proxy Spawn/Remove/Renderer submissions — **0/0/0**. Нет утверждения, что старый Actor удалён при ExitProcess: старого диагностического Actor вообще не создавали. Native anti-ABA с физическим повторным адресом **BLOCKED/NOT_EXECUTED**. [RECONNECT_CONTRACT_AUDIT](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/RECONNECT_CONTRACT_AUDIT.json).

Offline проверены rejection до seam: old session, old map epoch, предыдущая entity generation, duplicate sequence, older tick, post-disconnect, teardown, до нового NativeMapReady. Native write-call counter на intake не меняется. Entity mismatch по принятой политике может запросить последующий lifecycle Remove; тест не выдаёт это за native runtime stale-packet gate. Реальная повторная entity incarnation не выполнена. [STALE_SNAPSHOT_REJECTION](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/STALE_SNAPSHOT_REJECTION.json).

**Reconnect = BLOCKED на Proxy integration**, transport reconnect поддержан. Транспорт не расширялся. Нет ни runtime BOUNDED_PASS, ни второго Actor даже в mock test: offline characterization локализует необходимость безопасного rearm после native retirement.

## C. Local Player и Spawn overlap

Для новой Proxy-проверки полные pairwise comparisons **NOT_EXECUTED**. Recon не выдаётся за повторение 10×12 исторического DOC-58B. Принятый 12-field контракт сохранён:

| Поле | Владелец + hex offset | Bytes |
|---|---|---|
| ankh_count | Player+84 | 2 |
| ankh_mask | Player+86 | 2 |
| artifacts | Player+90 | 4 |
| abilities | Player+94 | 4 |
| keys | Player+98 | 2 |
| quest_items | Player+9a | 2 |
| team_dolls | Player+a4 | 4 |
| current_weapon | Player+88 | 4 |
| pending_weapon | Player+8c | 4 |
| owned_weapons | Player+cc | 8 |
| ammo | Player+bc | 16 |
| health | Puppet+17a | 2 |

Новых полных Player/Puppet graph pairs и camera measurements нет. Source audit: trace не пишет Player/Puppet/camera и не вызывает KEX; обычные UI input и штатная очистка мира разрешены. Это не доказательство runtime неизменности всех полей. [PLAYER_INTEGRITY](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/PLAYER_INTEGRITY.json) содержит явные null/NOT_EXECUTED, не фиктивные значения.

Причина overlap установлена по неизменённому host source `native_bootstrap/src/bootstrap.cpp`: P2 factory получает `position(g.p1.actor)`, yaw и sector P1 (строки449–452), под штатным native Player getter scope. Отдельный spawn-position параметр там не аттестован. Прямых writes в P1/P2 и geometry patch не делалось. Исторический Run02 обходил проблему обычным вводом P1 **до admission peer**; этот сценарий заново не выполнялся. **Spawn overlap = DEFERRED**; новая система spawn points не начата.

## D. Проверки и канонические Gates

Свежая сборка contract executables выполнена C++20 `/W4 /WX`. Исходные assertions DOC-58B не менялись:

| Проверка | Новый результат |
|---|---|
| DOC-58B proxy contract | 32/32 PASS |
| DOC-58B adapter JS | 11/11 PASS |
| DOC-58A offline controller | 39/39 PASS |
| DOC-58B.1 characterization | 18/18 PASS |
| Snapshot contracts | 42 checks, failures0 |
| MapReady contracts | 210 checks, failures0 |
| Client/ENet contracts | 136 checks, 3 scenarios, failures0 |
| Native no-Proxy title exit | фактический Unload/free и continuation state1 |
| Gameplay pre-native gate | BLOCKED_FOR_PROXY_MUTATION |

Новые tests покрывают pending cancel, in-flight Spawn/Update/Remove + latched teardown/shutdown, запрет позднего Remove, retirement witness и reentrant unload, terminal ORPHANED, before-MapReady rejection и reconnect blocker. Mock retirement не заменяет native Drain. Старые Gemini тесты и исходники не менялись и не выдаются за новую native приёмку. `M11Prepare` success/registration не проверялись новым экспериментальным DLL run: такая DLL не загружалась. Историческое исправление «prepared может установиться на отказе» сохранено; [PRE_NATIVE_GATE](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/PRE_NATIVE_GATE.json) не содержит ложного общего PASS.

| Gate | Статус DOC-58B сохранён | Что добавил DOC-58B.1 |
|---|---|---|
| GATE-PSE-58C-01 Pre-Unload | OPEN | native no-Proxy order и sector free; live ownership не подтверждён |
| GATE-PSE-58C-02 Remove/Drain | VERIFIED bounded | новая native проверка не выполнялась |
| GATE-PSE-58C-03 Pool sweep | VERIFIED bounded | новая native проверка не выполнялась |
| GATE-PSE-58C-04 Portal boundary | DEFERRED | не проверялся |
| GATE-PSE-58C-05 Sim affinity | VERIFIED bounded DOC-58B paths | новый no-Proxy StateExit/Unload/free на Sim39756; universal coverage не заявлено |
| GATE-PSE-58C-06 Seven proxies | DEFERRED | не проверялся |
| GATE-PSE-58C-07 Reentrancy | VERIFIED bounded | новые mock interleavings; native fault injection не выполнялась |
| GATE-PSE-58D-08 InsertActor | PARTIAL | не проверялся |
| GATE-PSE-58D-09 Slot demux | VERIFIED slots0/1 | нового native network run нет |

В этом milestone новых закрытых Gates нет; ранее частичный G8 не повышен. Определения и IDs взяты из канонического Gemini DOC-58D.1, а исторические статусы — из принятого DOC-58B. [NATIVE_GATE_RESULTS](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/NATIVE_GATE_RESULTS.json).

Матрица B1-01..12 сохранена в [NATIVE_ACCEPTANCE_REVIEW](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/NATIVE_ACCEPTANCE_REVIEW.json): B1-02 выполнен bounded без Proxy; B1-04 offline; B1-09 offline; B1-12 bounded recon only. B1-01 не повторён native, остальные зависимые native сценарии BLOCKED/NOT_EXECUTED. Старые результаты не заменяют новую приёмку.

## E. Завершение и ограничения evidence

После выхода в title снят trace script. Независимый readback подтвердил **9/9 exact16 игровых prefixes**, а CRT free сравнен с байтами его загруженного PE (`ucrtbase.dll` RVAad80). Isolation Nt* hooks оставались до normal exit; их post-exit восстановление не заявляется. Штатный WM_CLOSE → retained launcher handle → **exit0 в 20:29:27.527191Z**. REA sessions закрыты, anubis processes0. Не было forced termination. [HOOK_RESTORATION](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/HOOK_RESTORATION.json), первичный `N58B1_TRACE_01/exit.json`.

Сырые журналы, screenshots, command logs, TRACE_CONFIG и полные REA bundles сохранены. Отсутствие native memory fault в одном no-Proxy run не является UAF/ghost/ownership proof. Не создано пустых `RECONNECT_NETWORK_CORRELATION.jsonl` и `PROXY_GENERATION_LIFECYCLE.jsonl`: соответствующие native события не происходили. Их отсутствие и причины перечислены в acceptance review. Ошибки schema/discovery/startup/helper и ограничения desktop/guard явно сохранены в [HARNESS_ERRORS](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/HARNESS_ERRORS.md).

| Категория | Итог |
|---|---|
| G1 | OPEN |
| Reconnect | BLOCKED_AT_PROXY_INTEGRATION |
| Spawn overlap | DEFERRED |
| Native lifecycle | bounded no-Proxy Unload order установлен; live Proxy retirement не выполнен |
| Network | contract PASS; native reconnect не выполнен |
| Player integrity | новая 12-field Proxy проверка не выполнена |
| Hook restoration | PASS bounded для recon hooks; guard до exit |
| OS exit | один recon process exit0; Host/Client-пара не запускалась |
| Overall | **PARTIAL** |

Ограничения DOC-58B сохранены: один stock Actor1007 flags0x40, Tomb/sector259, Z+88, no cross-sector, no seven-proxy stress, focus/Sim gating и отсутствие jitter buffer. Wire v1.10, production transport, замороженные DLL/хуки, DOC-58A/B и Gemini не изменялись. Полная проверка защищённых historical files, оригинальной установки и saves — [INTEGRITY_FINAL](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/INTEGRITY_FINAL.json).

## F. Единственный следующий этап

Узко аттестовать **read-only ownership Actor1007/ObjectNode в StateExit actor-pool cleanup после Level::Unload** на том же штатном title-exit пути. Это устранит оставшийся native lifetime blocker перед проектированием rearm и любым live-Proxy unload/reconnect. Этап здесь не начат. DOC-58B.1 остановлен с сохранённым PARTIAL.

## Фактические новые исходники и SHA-256

Принятые исходники DOC-58B и production не изменялись; новая gameplay DLL не создавалась.

| Файл в лаборатории | SHA-256 |
|---|---|
| `CMakeLists.txt` | `f94985a25bd8d308bd436d3f728b6fdb7fd9b475b801f52703a3604c55ceeaef` |
| `finalize.py` | `2c1a7529bc875d1314d495aea8059ea70830c5de5b4f23864e07de414ad21d80` |
| `launch_bound.py` | `5bdb1cd3beabec079613dc029fef5076725bbb4f179dba5d8516b41f39dce0e9` |
| `lifecycle_contract.cpp` | `e1b3c29db457e8c67176b85486f907744eba3d8e05f4a36d926031d5a7d4e553` |
| `recon.py` | `7eb9cfa8bb551410287c522439dd54b3c9d920c95e4d7f1fae6a6a3a635c6205` |
| `report.py` | `e6f3d39b397efc890c984367e76c9e8299c6f4d131dc76b168950b83cd233aee` |
| `restore_observation.js` | `70545541f5962b57c45a2d2f708b7f0b0606fbec7f9e8f2e513f187e50eca8f8` |
| `trace.js` | `d7934103a228e1979950b6949222c35d56288816962ed01b4151fccc89061f27` |
| `workflow.py` | `36d13dee8e410759b8aabdb2cb92c6cfc09d471ad0e0ddbe6ced6024ce419985` |

Манифест: [SOURCE_MANIFEST.json](evidence/m1/NATIVE_PROXY_LIFECYCLE_58B1/SOURCE_MANIFEST.json). Build outputs и command logs находятся в `b581/` и новом Evidence.
