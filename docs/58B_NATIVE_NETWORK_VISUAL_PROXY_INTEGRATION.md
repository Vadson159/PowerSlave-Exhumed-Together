# DOC-58B — Native Network Visual Proxy Integration

2026-11-03 UTC. **Итог: DOC-58B OVERALL — bounded PASS** для одного удалённого Visual Proxy (Host P1 → Client), сектор 259 Tomb of Ramses, stock Actor1007. CROSS_SECTOR — DEFERRED. G1 (unload с живым proxy) — OPEN. DOC-58A/55/56/57 и оригиналы Gemini не изменялись.

Evidence: [`evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/`](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/) — [CODE_REVIEW](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/CODE_REVIEW.json), [PRE_NATIVE_GATE](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/PRE_NATIVE_GATE.json), [NETWORK_CORRELATION](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/NETWORK_CORRELATION.jsonl), [NATIVE_ACTOR_LIFECYCLE](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/NATIVE_ACTOR_LIFECYCLE.jsonl), [RENDER_EVIDENCE](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/RENDER_EVIDENCE.json), [PLAYER_INTEGRITY](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/PLAYER_INTEGRITY.json), [NATIVE_GATE_RESULTS](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/NATIVE_GATE_RESULTS.json), [NATIVE_ACCEPTANCE_REVIEW](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/NATIVE_ACCEPTANCE_REVIEW.json), [INTEGRATION_MANIFEST](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/INTEGRATION_MANIFEST.json), [SOURCE_MANIFEST](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/SOURCE_MANIFEST.json), [RESULT_SUMMARY](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/RESULT_SUMMARY.json), `INTEGRITY_FINAL_*.json`, [HARNESS_ERRORS](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/HARNESS_ERRORS.md), [RUN01_RESULT](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/RUN01_RESULT.json). Сырые журналы: `N58B_HOST_02/native.jsonl`, `N58B_CLIENT_02/{native,adapter,unload_witness}.jsonl`, REA `STATUS_*.json`, `rea_bundles/`.

## A. Code Review Gemini DOC-58D.1

Кандидат воспроизведён без изменений в отдельном build-каталоге: 73 PASS / 0 FAIL / 1 NVR (каталог Gemini не тронут, хеши до/после равны). **Вердикт: REJECTED as-is для нативной интеграции, PATCHED** в изолированной копии `experiments/m1/native_network_visual_proxy_58b/src/visual_proxy_58b.hpp` (`MultiSlotGuard8` скопирован побайтно).

| Contract | Source | Evidence | Status | Defect | Required Action (сделано) |
|---|---|---|---|---|---|
| Remove синхронный | hpp:517-522 | DOC-58A + G2 здесь: Remove → Drain на следующем шаге | BLOCKED | D1: ABSENT до PoolReturn | `Requested`→RETIRING; ABSENT только по native retirement witness |
| Spawn exception → SPAWN_PENDING | hpp:447-450 (тест Gemini это утверждает) | вызов фабрики мог состояться | BLOCKED | D2: слепой re-Spawn | Unknown/exception → QUARANTINED, бюджет Spawn исчерпан |
| Spawn nullptr → ABSENT | hpp:453-456 | nullptr не доказывает rollback | BLOCKED | D2b | только адаптер-доказанные Deferred/Refused (без вызова) допускают повтор |
| `onPreUnload` return при `m_inSeamCall` | hpp:587-588 (и shutdown/disconnect) | — | BLOCKED | D3: намерение unload/shutdown теряется | все lifecycle-намерения латчатся и применяются после seam |
| Teardown → ABSENT, `m_lastUnloadedActorPtr` | hpp:478-485, 565-571, 603-614 | G1 OPEN | BLOCKED | D4 | ORPHANED (терминально, ни одного native вызова, без re-Spawn), только opaque handle |
| `configureSession` = разрешение Spawn | hpp:273-287 | DOC-56: Welcome ≠ native MapReady | BLOCKED | D5 | раздельно: session / native map token (ClientGate Ready) / sector readiness (адаптер) / spawn permit |
| `handleSnapshot` вызывает seam | hpp:372-383 | DOC-58A handoff | BLOCKED | D6 | intake только сохраняет; применение раз в безопасный шаг (coalescing) |
| результат update игнорируется | hpp:373-381 | — | BLOCKED | D7 | Applied/Unchanged/Refused/Stopped/Unknown |
| `void* actor` | hpp:56-65 | запрет raw-memory API | BLOCKED | D8 | opaque `ProxyHandle` от адаптера |
| re-register TID | hpp:265 | — | INFERRED | rebinding | bind once |
| `m_inSeamCall` | hpp:241 | TID-проверка идёт раньше | VERIFIED | — | оставлен как доп. защита |
| MultiSlotGuard8 | guard.hpp | логика = DOC-55 Guard ×8 | VERIFIED | local exclusion не в guard; `slot()` молча → 0; wrap uint32 | exclusion/invalid/capacity в `SnapshotRouter` |
| catch(...) на C-ABI | hpp | /EHsc не ловит SEH | INFERRED | — | SEH-граница в `seam_58b.hpp`, 0 = UNKNOWN, seam отключается навсегда |
| 0x1401c90e0 покрывает все unload | GATE_ID_CORRECTIONS | только vtable-witness DOC-56 | UNKNOWN | — | G1 остаётся OPEN |

Адаптированные API: `spawn/update/remove/retirement` с кодами 0=UNKNOWN (JS-исключение в NativeCallback возвращает 0), `configureSession(gen,epoch)` + `onNativeMapReady(epoch,token)`, единый `tick(seam,TickContext)`, `requestRemove` (политика DOC-58A: fault → один Remove), `SnapshotRouter<Capacity>`. Старые тесты Gemini не менялись; утверждения, кодирующие D2/D3/D4, перечислены в CODE_REVIEW и не выдаются за PASS исправленной копии. Новые тесты: `proxy58b_contract` 32/32.

## B. Архитектура интеграции

Новых протокола/транспорта/инжектора нет. Host = **замороженная DLL DOC-56** (`2b66f075…`). Client = `pse_native_client58b.dll` (`3249b735…`): копия принятого DOC-56 `client.cpp`, сгенерированная `make_client58b.py` якорными вставками под `PSE_DOC58B` ([diff](../experiments/m1/native_network_visual_proxy_58b/src/client58b.diff)). Wire v1.10, ENet, ThreadBridge, DOC-55 Guard, DOC-56 ClientGate — без изменений.

Цепочка: Host capture (Sim) → ENet Worker → OS UDP → Client Worker → ThreadBridge → **Client Sim `consume()`** → DOC-55 Guard → `SnapshotRouter` (local slot исключён) → `VisualProxyStateMachine` (только хранит цель) → `phase58` после `consume()` в DLL-детуре StateTick (эквивалент StateTick.onLeave) → seam-callback → Frida-адаптер (`adapter58b.js` + **неизменённые** DOC-58A `controller.js`, `one_actor.js`, `movement_native.js`, `common.js`, `cross_sector.js`, `inventory_snapshot.js`, `camera_math.js`) → KEX Actor1007 → нативный lerp → sprite renderer.

Почему так: DLL уже владеет MinHook-хуками StateTick/Update/StateExit, а адаптер DOC-58A ставил Frida-хук на тот же StateTick. Двойной хук исключён: DLL вызывает адаптер через таблицу NativeCallback (`tickEnter`, `worldReady`, `spawn`, `update`, `remove`, `retirement`, `tickLeave`, `worldExit`), зарегистрированную до Permit (`PSE58B_RegisterSeam`, одноразово, только после успешного Prepare). Экспорты из Frida-потока лишь публикуют атомики; все действия — на Sim TID. Адаптер выгружается только после `PSE58B_RequestDetach` → DLL отцепляет seam на Sim-потоке, когда все прокси quiescent.

Адаптер: только операции DOC-58A — origin +18/+1c/+20 (native_step без reset ≤160 ед., иначе teleport + reset1d3e60), yaw +38 + reset(+78), Spawn через factory 1d4780, Remove 1a02b0. LinkSector не вызывается (cross-sector DEFERRED). Аттестованная область = сектор 259: все 8 углов AABB внутри нативных плоскостей, native PointInSector(259), отсутствие в любом другом из 270 секторов, native floor/ceiling = кешу Actor (1152/1664), physics bit7 сброшен. Смещение z +88 — фиксированное презентационное (пол 1152 → проверенная z 1240 Actor1007), не общий маппинг. Spawn дополнительно требует mailbox-разрешения стенда.

## C. Нативные запуски

Exact EXE `b8f131…a76d`, копии `experiments/m0/game` (Host) и `native_second_instance/game` (Client), свежие профили `native_network_visual_proxy_58b/profile/N58B_*`, принятый suspended launcher (REA spawn), isolation guard, DOC-51 loader, DOC-53 retained-handle watcher. Isolation gates PASS для всех 4 процессов.

**Run 01 — BLOCKED до Spawn** ([RUN01_RESULT](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/RUN01_RESULT.json)). Host P2 (DOC-56) спавнится в позиции P1; W поднял P1 на P2 и перенёс через односторонний уступ в 123; вернуться в 259 мир Host не дал. Разрешение Spawn выдано намеренно: адаптер отказал `OUT_OF_ATTESTED_AREA`, вызовов фабрики 0. Штатное завершение: proxy shutdown → seam Detached → выгрузка адаптера → WM_CLOSE, оба exit 0.

**Run 02 — основной.** Host PID 39840 / Sim TID 23616 / Worker 36376; Client PID 23436 / Sim TID 19712 / Worker 19384. Host P1 смещён двумя W по 0.03 с до подключения клиента (y −6843.9, сектор 259). Welcome: client slot 1, gen 1, epoch 1612646765, `maps/TOMB.MAP`; native MapReady (ClientGate Ready, токен идентичности) отдельно от Welcome. Slot mapping по сессии: slot 0 = Host P1 → Remote Proxy; slot 1 = собственный игрок клиента на Host → LocalExcluded.

| Счётчик | Значение |
|---|---|
| Host captured (slot0/slot1) | 14 844 (7 422 / 7 422) |
| Host Worker send / OS UDP send | 14 844 / 14 844 |
| Client Worker receive / Sim accepted / rejected | 14 844 / 14 844 / 0 |
| Client OS UDP receive (окно движения) | 11 456 |
| Routed slot0 / LocalExcluded slot1 | 7 422 / 7 422 |
| Native actions | SpawnCreated 1, UpdateApplied 2 477, UpdateUnchanged 1 719, UpdateRefused 2 035, RemoveRequested 1, Retired 1 |
| Бит-точные несовпадения capture→UDP→Sim→Actor | 0 из 6 232 строк корреляции |
| Native factory / Remove calls | 1 / 1 |
| Simulation steps / Actor Tick callbacks | 12 454 / 12 454 |

Actor `0x61751e70` / node `0x61c30760`, handle 1, сектор 259, создан из snapshot seq 1605 / server tick 10670; кеши 1152/1664 = native floor/ceiling; ровно одно membership; отдельно от Local Puppet.

**Движение (обычный ввод Host: W/strafe/мышь).** 325 различных целей: x −7461.99…−7386.50, y −6844.18…−6674.17, z 1240…1389.97 (P1 поднимался на host P2), yaw −3.1376 → −2.7626 (мышь). Для каждого применённого обновления origin Actor = (x, y, z+88) snapshot бит-в-бит, yaw = нормализованный snapshot. Когда P1 вышел из 259, 2 035 обновлений отклонены без записи, прокси держался в последней валидной точке. Клиент применяет только при тикающем мире (foreground); в фоне цели коалесцируются (фокус-гейт KEX, DOC-53).

**Рендер/интерполяция** ([RENDER_EVIDENCE](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/RENDER_EVIDENCE.json)): 11 628 sprite submissions с identity `OURS` (entry = live render outputs этого Actor, 0 ambiguous), 91 ревизия отрисована, 530 промежуточных submissions — все совпадают с последним native lerp; 2 109 native lerp-сэмплов. Пример (seq 3375): host capture 20:04:17.351 → UDP 17.354 → Sim 17.358 → UpdateApplied 17.359 → sprite 17.360 с интерполированным origin. t восстанавливается по одной оси, поэтому formula_error до 0.072 ед. — это точность восстановления t, не ошибка KEX. Sprites шли пока прокси был в кадре (до 20:04:20; затем P1 был над/позади камеры). [Кадр](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/N58B_CLIENT_02/window_20261103_200402_016135.png).

**Disconnect/lifecycle.** WM_CLOSE Host → клиент `LOCAL_SESSION_LOST` (20:07:28.507) → REMOVE_PENDING → в том же безопасном тике один Remove (removed=1, flags 0x2000, refs 1, ещё в списке) → RETIRING → Drain (unlink, refs 0) → OnRemove → PoolReturn в Sweep (valid scope) → payload null / refs 0 / in_use 0 / occurrences 0 → `Retired` → ABSENT (.556). 2 340 шагов после retirement, спрайтов после Remove 0, weak pointers null. Затем seam Detached → адаптер выгружен → WM_CLOSE клиента.

**Local Player клиента:** 10 полных снимков × 12 полей (BEFORE, AFTER_SPAWN, LIVE ×6, AFTER_REMOVE, POST_RETIREMENT) — все EQUAL; граф Player/Puppet/node/owner неизменен; локальная позиция не менялась; записей в Player/камеру 0.

**Shutdown.** Host: network stop → 4 exact16 restore → exit 0. Client: DirectQuit → worker join → Update exact16 restore → exit 0. Оба: retained+duplicate handles wait 0, без emergency cleanup. 8 REA-сессий закрыты, игровых процессов 0.

## D. Native Gates

| Gate | Status | Evidence | Remaining Risk |
|---|---|---|---|
| GATE-PSE-58C-01 Pre-Unload | **OPEN** (NATIVE_VERIFICATION_REQUIRED) | Unload с живым proxy не выполнялся (по плану); witness `1c90e0` при DirectQuit не вызывался в обоих клиентах; барьер латчится на StateExit/DirectQuit (`seam_world_exit`); offline C13/C14/C20/C28 | переход карты с активным proxy не безопасен/не заявлен |
| 58C-02 Remove/Drain | **VERIFIED** (bounded) | после Remove узел ещё в списке, unlink в Drain следующего шага | другие типы/сектора |
| 58C-03 Pool sweep | **VERIFIED** (bounded) | PoolReturn в Sweep → payload/refs/in_use/occurrences = 0 до ABSENT | тайминг под нагрузкой |
| 58C-04 PointInSector portal | DEFERRED | только same-sector аттестация | cross-sector не выполнялся |
| 58C-05 Sim affinity | **VERIFIED** (пути клиента этого запуска) | все proxy/seam/adapter события на Sim 19712; ENet только Worker 19384; 12 454 шага; affinity violations 0 | unload-путь не выполнялся |
| 58C-06 Seven proxies | DEFERRED | capacity 1 | — |
| 58C-07 Reentrancy | **VERIFIED** (bounded) | reentrancy_blocked 0, unload_during_seam 0; seam вызывается только из phase58 | нативной fault-инъекции нет |
| 58D-08 InsertActor validation | PARTIAL | использованный путь: фабрика только с аттестованным 259, LinkSector 0; invalid ID только offline | 1e58f0 не трассировался |
| 58D-09 Slot demux | **VERIFIED** для slot 0/1 | 7 422 Routed / 7 422 LocalExcluded, entity per slot неизменны | слоты 2–7, старый epoch/stale — offline |

## E. Acceptance

| Категория | Вердикт |
|---|---|
| NETWORK | BOUNDED_PASS |
| NATIVE_SPAWN | BOUNDED_PASS |
| SAME_SECTOR_MOVEMENT | BOUNDED_PASS |
| RENDER | BOUNDED_PASS |
| INTERPOLATION | BOUNDED_PASS |
| CROSS_SECTOR | DEFERRED |
| LIFECYCLE | BOUNDED_PASS |
| LOCAL_PLAYER_INTEGRITY | BOUNDED_PASS |
| HOOK_RESTORATION | BOUNDED_PASS |
| OS_EXIT | BOUNDED_PASS |
| **OVERALL** | **BOUNDED_PASS** |

Ограничения: один Remote Proxy; один тип Actor (stock 1007, flags 0x40); только сектор 259 и смещение z +88; cross-sector DEFERRED; G1 OPEN — переход карты с живым proxy запрещён; позиции Host P1 ограничены коллизиями мира Host (наложение host P2 из DOC-56, уступы), run 01 BLOCKED; клиент применяет только при тикающем мире (foreground), в фоне — коалесцирование; jitter-буфера нет; 8-слотовая маршрутизация проверена только offline; INTERPOLATION подтверждена в пределах same-sector.

## F. Offline gate и изменения

[PRE_NATIVE_GATE](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/PRE_NATIVE_GATE.json) 16/16: exact EXE, сборка (/W4 /WX), freeze DLL, proxy58b_contract 32/32, offline58b.js 11/11 (неизменённый DOC-58A controller), DOC-58A offline_test.js 39/39, snapshot/map_ready/native_client contracts (DOC-55/56, пересобраны), Gemini 74 (73/0/1), node/python syntax, wrong-process preflight (193, регистрация seam до Prepare 5023), экспорты, 23 site-префикса по байтам файла в обеих копиях. Две ранние попытки gate сохранены (`SUPERSEDED_PRECHECK_01/02`); вторая нашла реальную деталь (`M11Prepare` латчит `prepared` при отказе) — исправлено.

Новые файлы — только `experiments/m1/native_network_visual_proxy_58b/`, `docs/evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/`, этот документ и build-каталог `b58/` (короткий путь из-за MAX_PATH MSBuild). Обновлены только текущие `PROGRESS.md`, `EVIDENCE_INDEX.md`. TEMP/TMP — `…_58b/scratch`. Production DLL, Wire v1.10, DOC-55/56/57/58A файлы, оригиналы Gemini, оригинальная игра и защищённые сохранения не изменялись ([integrity](evidence/m1/NATIVE_NETWORK_VISUAL_PROXY_58B/)`INTEGRITY_FINAL_*.json`).

## G. Next Step

Минимальный следующий шаг: **native G1 + reconnect gate для одного proxy** — штатный map transition/StateExit при живом прокси в изолированном стенде: подтвердить путь вызова Level::Unload относительно DLL StateExit-барьера и выполнить Remove до teardown (или доказать штатную очистку KEX), затем повторный MapReady и новый Spawn в той же сессии. Параллельно — вынести позиционирование host P2 из точки P1 (DOC-56 host), чтобы движение Host P1 не зависело от наложения. Следующий milestone не начат.
