# DOC-53 — Background Host Simulation

2026-11-02. **PARTIAL всего DOC-53. Ограниченная цепочка background simulation/input/movement и штатный выход этого сценария — PASS.** Один Host и один Client, без synthetic activation fixture и без повторного gameplay run. Положительный native held Attack и ненулевой mouse-delta fixture не получены; эти требования остаются UNKNOWN. Причина terminal hang DOC-51/DOC-52 по-прежнему UNKNOWN.

[Машинная сверка](evidence/m1/HOST_BACKGROUND_53/RESULT_SUMMARY.json), [выбранные native записи](evidence/m1/HOST_BACKGROUND_53/NATIVE_SELECTED.json), [Host raw journal](evidence/m1/N53_HOST_MAIN_01/native.jsonl), [Client raw journal](evidence/m1/N53_CLIENT_MAIN_01/native.jsonl). 37/37 checks в сверке — проверка согласованности **одного** опыта, не 37 новых игровых тестов.

## 1. Проблема и статус утверждений

В DOC-51 второй KEX получал foreground, Host продолжал StateTick/приём INPUT, но не выполнял native primary Player Update. Тогда движение удалось получить только после fake activation Host HWND. Это не production background policy. DOC-52 подтвердил нормальный OS exit другого сценария, но не установил причину старого зависания.

Метки этого отчёта: **VERIFIED** — статический source/PE/assembly факт; **RUNTIME OBSERVED** — сохранённое исполнение; **INFERRED** — ограниченный вывод; **UNKNOWN** — не проверено. Имена функций/полей аналитические, кроме явно указанных RTTI names. Псевдокод Ghidra не является исходниками KEX. Предыдущие документы и Gemini материалы не редактировались.

## 2. Независимый критический аудит Gemini

Exact EXE SHA256 во всех KEX строках этого документа: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

Переиспользованы сохранённые StateTick/Player/Commands/Turning dossiers. Новые узкие запросы использовали сохранённый Ghidra 12.2 project, `-noanalysis -readOnly`; import/общий реверс и ремонт REA MCP не выполнялись. [Исходные invocations/dossiers](evidence/m1/HOST_BACKGROUND_53/static/), [operand audit](evidence/m1/HOST_BACKGROUND_53/static/FIELD_OPERANDS.json), [exact-file coordinates](evidence/m1/HOST_BACKGROUND_53/NATIVE_COORDINATES.json).

| Утверждение Gemini | Независимый результат |
|---|---|
| `a43f30` — pointer на Pause Manager | **Неверно.** Это встроенный global object, первый qword которого равен preferred VA `14052b420`. Native код использует `LEA RCX,[base+a43f30]`, не разыменованный object pointer. |
| `IsPaused` в vtable+10 | **Неверная семантика.** RTTI таблицы — `kexSlaveInventoryScreenLocal::vftable`. Getter RVA `1c34a0` возвращает один byte `[RCX+1b0]`: активность inventory screen. |
| `*reinterpret_cast<Byte**>(base+a43f30)` затем `[+1b0]=0` | Получается адрес vtable, затем запись в `vtable+1b0`, preferred VA `14052b5d0`. Предложенная запись **не выполнялась**. |
| `14c090` — focus-lost/pause handler | **Неверно.** SDL window subtype9 (minimized) вызывает RHI method; `148810` содержит строку `kexRHIStateVK::Resize: Re-creating swap chain`. Subtypes12/13 ставят другие KEX events в очередь. |
| `GameInstance+20c` — Esc pause-menu flag | **Неверно для обычного Esc.** Это byte выбора native TravelScreen path, см. ниже. Реальное Esc меню наблюдалось при `20c=0`, inventory=0. |
| `network->active()` — действующая MP-сессия | Такого публичного метода Session нет; `active` — private state. Один Worker/running/transport connection не означает READY. |
| `localThisTick==1` исключает double update | Старый guard проверял это **после** original Update. Новый Host guard проверяет duplicate/affinity до native вызова; также проверяется native return RVA. |
| Windows сам нейтрализует P1 | Не принято за контракт. Native held W15 и S15 положительно наблюдены перед loss; локальные native builders могут сохранять состояния/очереди. |
| Suppression безопасен для Esc/shutdown/8 игроков | Не было доказательством. Esc/один exit проверены отдельно. General shutdown, 8 игроков, loading/cutscenes не приняты. |

В Gemini REPORT также указан Host port27999; действующий adapter и новые OS/native journals используют28745. Материалы Gemini полезны как список адресов, их VERIFIED/PASS labels не использованы как authority.

## 3. Inventory object и TravelScreen: реальные границы знания

**VERIFIED:** object RVA `a43f30`, начальный qword — vtable RVA `52b420`, RTTI `kexSlaveInventoryScreenLocal`. Native getter `1c34a0(this)` возвращает byte+`1b0` в AL. Setter `1c4340(this,char)` меняет его вместе с binding reset, UI state и другими эффектами; `1c4490` сбрасывает byte и сопутствующие fields, `1c44c0` включает inventory presentation. Это engine-owned static UI object, не память мода. Texture/shared references и полный layout/lifetime всех UI members не реконструировались. Безопасность прямого изменения этого UI state для multiplayer **не установлена**, поэтому никаких его записей, vtable replacement или вызовов setter для устранения паузы нет.

Runtime base Host выводится из attested `GameInstance=140af33a0` и RVA `af33a0`: `140000000`; адрес inline inventory object в этом run — `140a43f30`. Runtime byte+1b0 наблюдался на simulation TID и был0. Runtime qword vtable отдельно не снимался; его exact initial value — статическое PE Evidence, совместимое с фактически пройденным native vcall. Это разграничение не является разрешением на запись.

**VERIFIED:** `GameInstance+20c` — однобайтный selector path для `GameInstance+30`. В initializer `1b01d0` объект+30 получает vtable `52ae90`, RTTI `kexSlaveTravelScreen`; shared owner находится в+38. `1b19d0` сбрасывает локальные команды/bindings и ставит20c=1; `1b1a40` ставит0; StateExit также сбрасывает0. StateTick при1 вызывает `2097d0`, responder — `209a90`. [Дополнительные dossiers](evidence/m1/HOST_BACKGROUND_53/travel_static/), [setter/clear](evidence/m1/HOST_BACKGROUND_53/decision_static/). Все пользовательские triggers и переходы этого экрана не проверены. Focus handler не является его setter.

**Имена `MenuByte`, `Conditions.menu` и JSON key `menu` в замороженном DOC53 source/logs — ранние аналитические обозначения raw+20c, а не восстановленное имя Esc state.** Их следует читать как TravelScreen-active. Код консервативно отказывает override при1 и никогда не записывает20c. Обычный Esc сохраняется другими original gates; его exact skip gate отдельно не локализован.

## 4. Фактическая focus → simulation цепочка

**VERIFIED static:**

```text
Real OS foreground transfer
  → shipped SDL window event subtype12/13
  → KEX dispatcher100510 → queued type13, reason1/2
  → GameInstance responder1b1640(this,event)
       reason1: byte[this+319]=0
       reason2: byte[this+319]=1
  → StateTick1b6ec0: CMP byte[this+319],0; JNZ epilogue
  → subsequent original service/state/TravelScreen/pending/inventory/modal gates
  → CALL Player Update at1b7013, native return1b7018
  → existing P2 Update on the same simulation TID
  → original world/Puppet traversal
```

Отдельный operand audit нашёл явные GameInstance+319 writes в initializer и двух focus branches; readers — StateTick и `1b1310`. Это поиск defined instructions, не доказательство отсутствия произвольного pointer alias во всём бинарнике. Хук дополнительно требует original focus event/ожидаемое значение/точный this/TID; неизвестное происхождение fail-closed.

**RUNTIME OBSERVED:** до Permit/сети реальный foreground Client23004 вызвал reason2 →319=1 у Host3236. Host StateTick продолжался, `local_updates=7581` оставался неизменным; TravelScreen/inventory/pending=0. [Singleplayer baseline](evidence/m1/HOST_BACKGROUND_53/SINGLEPLAYER_FOCUS_BASELINE.json). Это новая причинная проверка раннего gate; исторический DOC51 конкретный branch задним числом не объявляется измеренным.

В основной фазе тот же focus loss давал319=1; после matching READY lease временно ставила только этот byte в0 перед original StateTick. Primary Update проходил native call site, а после Tick319 вновь был1. Никаких Windows activation messages для этого не посылалось. Остальные native gates выполнялись штатно: например, Esc сохранял остановку Player Update даже при действующей focus lease.

## 5. Координаты и ABI

Preferred VA ниже равен `140000000+RVA`; **runtime address всегда actual base+RVA**, не preferred VA literal. Основной [coordinate manifest](evidence/m1/HOST_BACKGROUND_53/NATIVE_COORDINATES.json) содержит exact16; прежние [native sites](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json) переиспользованы.

| Роль | RVA | File offset | ABI/ограничение |
|---|---|---|---|
| StateTick |1b6ec0|1b62c0|Win64 RCX=this; прежний detour |
| Focus early CMP |1b6eee|1b62ee|native branch, не отдельная C++ API |
| Focus responder |1b1640|1b0a40|RCX=GameInstance, RDX=KEX event; return RAX передаётся original; полная исходная C++ signature UNKNOWN |
| Focus lost store |1b1689|1b0a89|один byte this+319=1 |
| Focus gain store |1b1691|1b0a91|один byte this+319=0 |
| Player Update |1dfe30|1df230|принятый Win64 Player* |
| Native primary CALL |1b7013|1b6413|return RVA1b7018 проверяется до выполнения Update |
| Reset local bindings |95f20|95320|native no-argument function; zeroes local device binding entries; не Player Reset |
| Inventory object |a43f30|a41d30|inline object; первый qword=vtable52b420 |
| Inventory open getter |1c34a0|1c28a0|RCX=this, AL=byte[this+1b0] |
| TravelScreen activation |1b19d0|1b0dd0|RCX=GameInstance, ставит20c=1 |
| TravelScreen clear |1b1a40|1b0e40|RCX=GameInstance, ставит20c=0 |

`95f20` проверен в [raw dossier](evidence/m1/HOST_BACKGROUND_53/input_static/G_FUNC_140095f20.json): перебирает native local devices/bindings, сбрасывает entries, которые читает 13-button polling Player Update. Он уже вызывается native UI transitions. Используется только при реальном primary Update, когда input subsystem заведомо доступна; не из Worker/loader/Shutdown.

## 6. Разделение pause reasons и выбор архитектуры

| Причина | Действие DOC53 |
|---|---|
| Focus loss, проверенный reason2/319=1 | Разрешён scoped override только для READY Host gameplay, stable next-state3, pending0, без teardown/failure. |
| Esc/Game Paused | Все native gates сохраняются; фактическая пауза/Resume проверены. Не подменяется проверкой20c. |
| Pause key/пользовательская console pause-команда | Binding label `pause` найден статически; самостоятельный runtime contract не принят. Чужие pause flags не меняются. |
| TravelScreen/inventory | Собственные native gates сохранены;20c/1b0 не записываются. |
| Переход уровня/loading/WorldExit | Predicate отказывает вне state3/pending0/next3 и при worldExit. Новый native level transition не выполнялся. |
| Cutscene/modal/Steam UI service gates | Original checks остаются; неизвестные flags не очищаются. Отдельные runtime scenarios UNKNOWN. |
| Shutdown | backgroundStopping до stop/join; lease завершается, новых overrides/native updates нет. |

Сравнены варианты:

1. **Network-aware focus decision:** выбран scoped вариант внутри существующего host StateTick adapter. Original event decision сохраняется между ticks; только доказанный focus byte арендуется на один original Tick. Один дополнительный responder detour наблюдает provenance/serial, не подавляет события. Это сохраняет native render/input/focus policy вне simulation call.
2. **Hook pause predicate:** Gemini inventory getter не является подходящим predicate. У фактического focus check нет отдельной функции: это inline CMP/JNZ. Новый interior branch detour/переписывание чужой vtable не нужны при уже принятом Tick seam. Они добавили бы ABI/relocation/lifetime поверхность.
3. **Два физических ПК без изменения KEX:** пригодны для отдельных тестов, когда оба окна активны; не решают Host Alt+Tab во время listen-server session. Не выбраны в качестве замены решения.

Нового simulation thread, manual physics step или KEX вызова из ENet Worker нет. Не обходятся остальные skip conditions. Проверенный Puppet traversal положительно наблюдён; полная работоспособность всех world subsystems этим ограниченным опытом не сертифицируется.

## 7. Activation policy и P1 input safety

Используется существующее `Session::running() && Session::wantsPlayer()`. Последнее означает simulation-owned `active && ready && !stopped`, после matching slot1/generation/epoch events, а не UDP connectivity. Session/Worker/pipeline не переписывались.

| Состояние | Background override |
|---|---|
| Нет сети / listener без клиента | false |
| Transport connecting / unauthenticated | false |
| Authenticated, ещё не READY | false |
| READY текущей generation, Host gameplay | true только для известного focus loss |
| Disconnect/resync / новая gen без READY | false |
| Reconnect + новый matching READY | вновь допустим; actual Session/ENet test |
| WorldExit/Shutdown/failure | false |

Session.tick дренирует inbound **перед** original StateTick даже при319=1. Predicate не требует наличия P2: новый run дал `p2_exists:false` → first background Update → Spawn. Начальный launch/isolation/60-update attestation до старта listener сохранён; полностью background-only startup до этого gate не заявляется. READY относится к принятому `M12A_INPUT_ONLY` domain, не к синхронизированному миру клиента.

P1 получает neutral commands при background override. На начале background периода вызывается native reset bindings; original Commands/Turning продолжают потреблять local mouse/controller queues, затем обнуляется только доказанный command block0..7c (buttons,13held counters, axes/deltas). Actor velocity, health, inventory, ownership и world physics не обнуляются. P2 использует прежний отдельный digital seam.

При focus gain первый native primary Update также дренирует старый local ввод и остаётся neutral; затем обычное управление восстанавливается. Старые held inputs не сохраняются для replay. Native W после возврата дал110 положительных samples; S перед следующим loss дал15, затем все background command fields вновь0.

Mouse Attack down/up отправлялись, но **positive native Attack held counter не появился**. Из PostMessageW нельзя выводить этот результат. Причина отсутствия positive input UNKNOWN; Attack input safety не объявлена полностью runtime-tested. Положительный ненулевой mouse-delta fixture также отсутствует. Общий neutral output доказан, эти конкретные positive-precondition tests — нет.

## 8. Реализация, diff и lifetime

Изменён один существующий production file: [bootstrap.cpp](../experiments/m1/native_bootstrap/src/bootstrap.cpp). Добавлены [policy](../experiments/m1/native_bootstrap/src/background_policy.hpp) и [version-locked profile](../experiments/m1/native_bootstrap/src/background_profile.hpp). [Before source](evidence/m1/HOST_BACKGROUND_53/before/), [финальный source diff](evidence/m1/HOST_BACKGROUND_53/SOURCE_CHANGES.patch), [compiled frozen sources](evidence/m1/HOST_BACKGROUND_53/frozen_sources/).

FocusLease восстанавливает319 на нормальном выходе original Tick. Если original responder получил новое focus event внутри scope, serial меняется и новый native value сохраняется. Nested lease отказывается. Перед unbind устанавливается backgroundStopping и завершается возможная lease. Prefix16 responder/reset/early gate проверяются до hooks; this/TID/event/value подтверждаются на simulation thread. Неизвестное происхождение/значение не переопределяется.

P1 duplicate/foreign guard перенесён **до** original call; для Host дополнительно требуется native caller1b7018. P2 прежний per-tick guard сохранён. При focus return нет catch-up updates. 14 274 уникальных application ticks, столько же Commands и actual Puppet ticks, зарегистрированных duplicate/affinity faults0.

Worker stop/join → четыре exact control restores → один native P2 Reset → engine cleanup сохранены. Responder detour, Tick/lifecycle callbacks, static storage/Session и все trampolines остаются pinned до process reclaim. Responder после backgroundStopping только передаёт original call; никакого hot unload/free/vtable repair нет. Exceptional/crash exits и все CRT schedules не приняты.

Wire Protocol1.10, ENet, DOC46/47 routing/admission, DOC49 scheduler, DOC50 bounds, timeout6ticks||150ms, Player/Puppet lifecycle и Client DLL не изменены. World replication/camera/combat/campaign/3–8 scaling не начинались.

## 9. Build и автономные regressions

[Build manifest / SHA](evidence/m1/HOST_BACKGROUND_53/BUILD_MANIFEST.json), [gate](evidence/m1/HOST_BACKGROUND_53/AUTONOMOUS_GATE.json), [команды оставшихся tests](evidence/m1/HOST_BACKGROUND_53/REGRESSIONS_20261102_195211_701604.json), [build logs](evidence/m1/HOST_BACKGROUND_53/).

MSVC19.44 / VS2022 / SDK10.0.26100.0, Release x64 C++20 /MT; native/policy `/W4 /WX /permissive-`. Pristine ENet/MinHook. После добавления native telemetry пересобран только Host target; транспортные test binaries/sources не менялись. Wrong-process DLL rejection193 до hooks/KEX/log creation — PASS.

| Артефакт | SHA256 |
|---|---|
| Новая Host DLL, реально загруженная |`e0c7c233ea7bc73a9fb85db88bfb3a33dad48cfa4125ece5fdbe92d6a71ae52d`|
| Client DLL, retained DOC51 MAIN |`4887fddaa4071321ab9a89fefb5bc8e41426fb58440c7137fd19759a917fdef3`|

| Проверка | Фактический результат |
|---|---|
| Новый background contract |37 checks/0fail: policy, restore/new-event precedence, actual Session auth→READY→disconnect→gen2 READY→joined stop; без KEX; raw20c condition не моделирует обычный Esc |
| Pristine pse_test_suite, default timer |**44/46, exit1**, failures8/9: client всё ещё видел dead snapshot после respawn. Этот exe не линкует native bootstrap. |
| Та же исходная suite/assertions/deadlines, отдельный scoped1ms timer fixture |**46/46, exit0**. Только test-process timeBeginPeriod/timeEndPeriod; production timer/transport не менялись. Одно сравнение не доказывает универсальную причину первого FAIL. |
| pse_stress_test |11/11 |
| pse_fault_injection |16/16, F16 enforcement preserved |
| pse_bridge_acceptance |11/11 |
| DOC46 R1 / routing |R1 not reproduced;5cases/330checks |
| DOC47 R2 / admission |strict R2 PASS с argv `r2`;4cases/112checks |
| DOC49 timeout |default6/150 и существующий contract PASS |
| DOC50 sequence |282boundary checks и12live checks PASS |
| DOC51 native-client transport |3 real-ENet scenarios/135checks PASS; число checks зависит от наблюдаемого числа stale-neutral samples |

Existing assertions не ослаблены; исходные test files byte-identical. Первая suite failure сохранена. Native gate явно ограничен проходящим вариантом timing fixture и действующими contracts; default-timer44/46 остаётся ограничением итоговой приёмки. Повторной исходной suite ради PASS не запускали. Применимые runtime cases не называются автономно проверенными: new contract не вызывает KEX.

## 10. Единственный двухпроцессный integration run

Предварительно проверены exact EXE, independent77-file copies, native DLL hashes, protected snapshots и OS0games/0checked endpoints. [Host launch/isolation](evidence/m1/N53_HOST_MAIN_01/ISOLATION_GATE.json), [Client launch/isolation](evidence/m1/N53_CLIENT_MAIN_01/ISOLATION_GATE.json), [preflight](evidence/m1/HOST_BACKGROUND_53/AUTONOMOUS_GATE.json). Profiles/CWD независимы под `background_host/profile`; only task-owned config cl_engineNoSleep1/cl_engineFPS180, без копирования user saves.

Host3236/simulation23012; Client23004/simulation4196. Loader/guard до resume, затем native gameplay witness и Permit. Plain SetForegroundWindow сначала отказал. Новый **test-only** fixture временно AttachThreadInput helper к current/target queues, запрашивал настоящий OS foreground и detach-ил обе в finally. Actual foreground PID проверялся после запроса и во время input. Никаких WM_ACTIVATEAPP/WM_ACTIVATE/WM_SETFOCUS injection, SetFocus substitutes или production focus manipulation нет. [Fixture source](../experiments/m1/background_host/foreground_fixture.py), `FOREGROUND_OS_*.json` и `UI_*.json` в Evidence сохраняют реальные результаты.

Клавиши — posted Windows input fixture, не физическая клавиатура. При W/S Client действительно foreground; при первом background Spawn Host foreground не запрашивался. Действительные Host returns использованы отдельно для singleplayer/return-control/menu checks, не для прохождения background movement.

| UTC | Наблюдение |
|---|---|
|19:57:28.116|Host reason2 →319=1; P1 updates freeze без сети, Tick продолжается |
|19:58:51.771|Listener started, READY=false; одного Worker недостаточно |
|19:58:58.833|Host теряет focus с native W held15; actual Client foreground |
|19:58:58.932–.937|Matching READY gen1/tick14848 → background lease, P2 ещё нет |
|19:58:59.020|Native independent Spawn, оба health200, собственные Player/node/owner |
|20:00:19.918–20.180|Реальный Host focus return; drain first Update, затем ordinary W |
|20:01:02|Esc screenshot Game Paused; P1/P2 Update остановлены, network intake продолжен |
|20:01:39.967|Client foreground при Host Esc; focus lease допустима, но original manual pause по-прежнему запрещает Update |
|20:02:27–20:03:02|Host return/Resume, native updates вновь идут; S held15 перед повторным loss |
|20:03:26.680773|Client retained process object signaled/exit0 |
|20:04:32.465840|Host retained process object signaled/exit0 |

## 11. INPUT correlation, движение и P1 isolation

**RUNTIME OBSERVED:**16036 Client local dispatch →16036 successful synchronous UDP INPUT sends →16036 Host accepts, matching slot/gen/seq/forward. Actual UDP отдельно от enqueue. [Client full retained capture](evidence/m1/N53_CLIENT_MAIN_01/STATUS_EXIT_RETAINED.json), [Host capture](evidence/m1/N53_HOST_MAIN_01/STATUS_EXIT.json). Межпроцессная latency вычитанием разных часов не рассчитывалась.

14274 native P2 Updates/Commands/Puppet ticks, из них6298 при background Host и actual foreground Client. Все14274 глубоких P1 before/after совпали; все6298 background command blocks полностью neutral. P1/P2 health200, guards=true, native faults/blocked/foreign0. 7976 foreground updates включают return-control и neutral после disconnect; они не выдаются за background evidence.

| Native input | seq | Updates | Net XY / path XY |
|---|---|---:|---:|
| W/4 |2620…2697|78|8.378906 /8.378907 |
| Release/0 |2698…2829|132|0 /0 |
| S/8 |2830…2907|78|75.701270 /80.511277 |
| Release/0 и дальнейший neutral |2908…16036 cached|11369|3.160645 /3.160645 |

Последние30 samples каждого release interval имеют XY step0. Native collision/inertia сохранены, позиция/скорость не писались. Малое W displacement не заменено телепортацией или manual physics. Последний interval включает foreground и manual-menu gap; total duration не трактуется как непрерывный background tick. P1 собственное перемещение от W/S допустимо; его position не объявляется неизменной. Сохранение identity/input/inventory/weapon fields вокруг P2 — отдельное положительное наблюдение.

## 12. Shutdown и OS process evidence

Client WM_CLOSE: Worker36692/socket0xa38 closesocket return0 и simulation4196 thread wait0/exit0 → exact Update restore → engine cleanup → final. Один best-effort terminal invalid DTO явно retired, не объявлен доставленным Release; нейтральный INPUT уже был доставлен ранее.

Host WM_CLOSE: [full native tail](evidence/m1/HOST_BACKGROUND_53/NATIVE_SELECTED.json) — Worker24664/socket0x560 closesocket0 и simulation23012 wait0/thread-exit0 до four exact restores, один exit Reset/handle null/P1 preserved, engine return, native final, ticks_after_shutdown0/failed=false. [Host OS signaled](evidence/m1/N53_HOST_MAIN_01/OS_SIGNALED.json), [Client OS signaled](evidence/m1/N53_CLIENT_MAIN_01/OS_SIGNALED.json) относятся к удерживаемым с начала handles с проверенной PID/path/creation identity; duplicate handles того же kernel object также signaled0. Launcher exit0 подтверждён отдельно.

TerminateProcess/forced cleanup0. Это сценарный PASS, не исправление DOC52. Passive terminal ledger/PSS/WCT helper был подготовлен на случай delayed signal; hang не возник, terminal stack/dump не снимались. Отсутствие быстрых terminal ledger entries не используется как API-entry proof. Все25 созданных REA sessions закрыты; оба внешних observers вернули0 и закрыли handles.

## 13. Изоляция и целостность

[Baseline11215 files](evidence/m1/HOST_BACKGROUND_53/BASELINE.json), timestamped INTEGRITY JSON, оба protected before/after и current snapshots. Original77/Host77/Client77, полный original installation snapshot и protected user profiles unchanged. Единственное изменение существующего product source — bootstrap.cpp. Historical DOC40–52/Evidence/accepted builds, Gemini REPORT/SOL_HANDOFF и transport/client sources unchanged.

Native isolation guard errors/overflow0, зарегистрированных successful external disk write-opens0. Прежние границы guard сохраняются: не полный OS sandbox; direct syscalls, inherited handles, rename/другие processes не покрываются. Нативный Steam init failure снова наблюдён штатно; Steam API/licensing/achievements не менялись. Игра запускалась только из двух проверенных workspace copies.

## 14. Ошибки стенда, ограничения и завершение

[HARNESS_ERRORS](evidence/m1/HOST_BACKGROUND_53/HARNESS_ERRORS.md) сохраняет ранний Ghidra project lock, преждевременный bind, rejected foreground, invalid Esc/+20c QA assumption, initial suite44/46 и status wrapper issue. Большой final Client capture восстановлен **целиком** из retained REA Evidence `ev_3d39ff61a5096c1f6c5dbd4cffe6e669bdda2b1705446348d15f00ecb65270c4` в [bundle](evidence/m1/HOST_BACKGROUND_53/REA_SESSION_BUNDLE.json); messages16195/not truncated/errors0. Пустой первоначальный STATUS_EXIT.json оставлен как harness error, не Evidence успеха.

**Итог PARTIAL.** Background primary→secondary native simulation без fake activation, настоящие W/S/Release, P1 neutral output/return, manual Esc behavior и normal OS exit одного сценария доказаны. Не закрыты positive held Attack/mouse-delta tests, exact Esc predicate localization, default-timer suite failure; native loading/cutscene/WorldExit/reconnect/minimized/general shutdown и8players не приняты. Source-level guards и автономные tests не заменяют этих runtime scenarios.

После DOC53 работа остановлена. Никаких следующих игровых систем или повторных native runs автоматически не начинать.
