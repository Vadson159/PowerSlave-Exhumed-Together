# DOC-58A Completion — Native Cross-Sector Movement & Renderer Interpolation

2026-11-03 UTC. Продолжение [DOC-58A PARTIAL](58A_NATIVE_VISUAL_PROXY_MOVEMENT.md) (исторический документ и его Evidence не изменялись). DOC-58B не начат.

**Итог: DOC-58A OVERALL — bounded PASS**, собранный из двух изолированных KEX-процессов. N58A_C01 закрыл cross-sector (interpolation там остался PARTIAL из-за дефекта стенда, обнаруженного до исполнения). После отдельного разрешения пользователя N58A_C02 с исправленной командой 4 закрыл interpolation. Код cross-sector (`cross_sector.js`, `movement_native.js`) в обоих процессах бит-идентичен; различается только `probe.js` (выбор цели команды 4).

| Результат | Статус | Процесс | Основание |
|---|---|---|---|
| SAME_SECTOR | PASS | C01, C02 | Smoke текущего controller/adapter: teleport, yaw, same-sector LinkSector, exception-to-status |
| INTERPOLATION | **PASS** | C02 | 3 native lerp outputs; каждый бит-в-бит дошёл до sprite renderer следующего render pass, identity `OURS`, yaw неизменен; reset без stale значений |
| CROSS_SECTOR | **PASS** (bounded) | C01 | Аттестованный перенос 259→123 через LinkSector1d3d70, ровно одно membership, renderer в sector123, 1153 steps после переноса |
| LIFECYCLE | PASS | C01, C02 | C01: Remove из sector123; C02: Remove из 259 → Drain → OnRemove → PoolReturn → ObjectNode retirement |
| DOC-58A OVERALL | **bounded PASS** | C01+C02 | Все критерии таблицы §7 задания подтверждены в настоящем KEX; ограничения — §7 |

C01 (исторически) показывал PARTIAL; его [review](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/NATIVE_ACCEPTANCE_REVIEW.json) не переписывался.

Evidence: [`evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/`](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/). C01: [review 75 checks](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/NATIVE_ACCEPTANCE_REVIEW.json) (73 PASS; 2 не выполненных — опциональные interpolation checks), [cross-sector](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/CROSS_SECTOR_EVIDENCE.json), [движение/sprites](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/MOVEMENT_OBSERVATIONS.json), [lifecycle](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/LIFECYCLE_JOURNAL.json), [P1 comparisons](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/PER_FIELD_COMPARISON.json), [raw journal](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/N58A_C01/native.jsonl). C02: [review 49 checks](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/C02/NATIVE_ACCEPTANCE_REVIEW.json), [lifecycle](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/C02/LIFECYCLE_JOURNAL.json), [P1 comparisons](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/C02/PER_FIELD_COMPARISON.json), [source manifest](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/C02/SOURCE_MANIFEST.json), [raw journal](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/N58A_C02/native.jsonl).

## 1. Exact build и запуск

EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`, KPF `d34b6355…bbd`. Существующая копия `experiments/m0/game`, свежий профиль `experiments/m1/native_visual_proxy_movement/profile/N58A_C01`. Принятый suspended launcher (REA Frida spawn Python → `runner.py launch`), isolation guard (`generated_isolation.js`), REA attach, новый injector не создавался. C01: KEX PID 6444, attested Simulation TID 18892, профиль `N58A_C01`. C02: KEX PID 20132, Sim TID 37744, профиль `N58A_C02`, отдельный pre-native gate `COMPLETION/GATE_N58A_C02/` (39/39 offline, syntax, 23 prefixes). В обоих все события журнала после загрузки — на Sim TID (render и simulation в KEX на одном потоке), `maps/TOMB.MAP`, state 3/3/0.

## 2. Focused static audit (только этот контракт)

Псевдокод использован для навигации; утверждения ниже опираются на инструкции exact build ([STATIC_AUDIT](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/STATIC_AUDIT.json), новые dossiers [static/](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/static/): 1c98f0, 1a3eb0, 1a4060, 1d33e0, 1a02f0; ранее 1d3d70, 19f2d0, 1cf540, 1e9a20). Имена ролей — аналитические.

| Роль | RVA / VA / file offset | ABI | Вывод (VERIFIED по инструкциям) |
|---|---|---|---|
| LinkSector | 1d3d70 / 1401d3d70 / 1d3170 | void(Actor* RCX, int32 EDX) | `CMP RDI,[a45970]; JC` иначе assert7b3c0 (возврат assert не доказан → caller обязан держать 0≤i<count). Existing node: `CMP [Actor+3c],EDI; JZ` — same-sector без записей links. Иначе unlink `[prev+8]=next; [next]=prev`, insert после head=[sector+78], затем `MOV [Actor+3c],EDI`. Других вызовов нет: ни геометрии, ни floor/ceiling, ни interpolant, ни callbacks |
| Factory placement | 1d4780 | Actor*(Factory*,int32,Vec3*,float,int32) | Штатный порядок для существующего объекта: origin stores → LinkSector(actor, sector) → vtable+38 |
| 1007 vtable+38 | 1a02f0 (slot 0x528608+38) | void(Actor*) | Spawn1d34d0 (interpolants) → ceiling1a4060 → `MOVSS [RBP+120]` (1401a08df), floor1a3eb0 → `MOVSS [RBP+11c]` (1401a092c); water flag 0x400 из sector+14&0x100 |
| Actor Tick | 19f2d0 | | `14019f3b1 MOV EDI,[RBX+16c]` … `14019f540 TEST DIL,DIL; 14019f543 JNS 14019fdd1`: при flags bit7=0 (stock 1007 flags 0x40) пропускается весь physics/sector-transition блок со всеми записями +11c/+120 и его LinkSector |
| Floor / ceiling | 1a3eb0 / 1a4060 | float XMM0(float* RCX, Sector* RDX, char R8B) | Faces last+2 / last+1; в ассемблере только home-space saves RBX/RSI и assert — без других stores. Вызывались read-only на буфере Frida |
| Point-in-sector | 1cf540 | bool AL(Sector* RCX, float* RDX, float eps XMM2) | Faces first..last (стены), без stores. Вызывался read-only |
| FindSector | 1c98f0 (level vtable 52b478+38) | int(level*, float*, hint) | **Не вызывался**: пишет visit marks sector+34 и глобальный стек 140a48258 (возможен рост). Read-only зеркало в `cross_sector.js` (BFS через faces flags0x2000, adjoin s16 face+6, тест faces first..last+2) |

Портальная раскладка (из сырых face bytes N58A_01 и подтверждена в C01): portal face flags&0x2000, adjoin = s16 face+6, sector self-index s32 sector+80, sector flags u16 sector+14.

## 3. Cross-sector contract и геометрия

**Cross-sector relocation** (не continuous traversal и не collision navigation): пассивный Actor из валидной точки 259 переносится в валидную точку 123.

| Параметр | Значение |
|---|---|
| Actor | stock Actor1007 `Obj_Ramses_Body_Rest`, vtable 140528608, flags 0x40, scale 1.5, AABB ±[12,12,24] |
| Source | sector 259, XYZ [-7424,-6880,1240] (spawn/home transform) |
| Destination | sector 123, XYZ [-7424,-7008,1240] |
| Portal | 259.face2087 (n=(0,1,0), d=6912, adjoin123) ↔ 123.face886 (n=(0,-1,0), d=-6912, adjoin259), копланарны; source margin +32, target margin −96 |
| Aperture | Пересечение отрезка с плоскостью портала [-7424,-6912,1240]; AABB в этой точке проходит все не-портальные плоскости обоих секторов с минимальным запасом 64 |
| Source AABB | Все 8 углов внутри 259, мин. запас 20 |
| Target AABB | Все 8 углов внутри 123 (включая наклонные faces887/888, floor890 z≥1152, ceiling889 z≤1664), мин. запас 64; ни один угол не внутри 259 |
| Уникальность | Полный JS-скан всех 270 native секторов (faces first..last+2): центр цели только в [123], пропущенных секторов 0 |
| Native witnesses | 1cf540(123,target)=true, 1cf540(259,target)=false; FindSector-зеркало из hint 259 → 123 (посещено 2) |
| Floor/ceiling cache | Native 1a3eb0/1a4060 в цели внутри 123 = 1152/1664, бит-в-бит равны кешам Actor+11c/+120 (1152/1664); sector flags14 = 0/0; physics bit7 clear → **дополнительная cache mutation не требуется**; кеши после переноса неизменны |
| Renderer | Collector1e9a20 обходит список [sector+78] видимого сектора; цель внутри frustum актуального P1 view |

Последовательность (одна StateTick.onLeave на Sim18892 после реального P1 Tick, stable world, без StateExit, live identity Actor/node/reciprocal payload):

1. Preconditions: identity, `[Actor+3c]==259`, list259 found 1, list123 found 0, полная аттестация (18 checks) с измеренным кешем.
2. Запись трёх float origin +18/+1c/+20 = цель.
3. `LinkSector1d3d70(actor, 123)`.
4. `InterpolantReset1d3e60(actor+50)` (previous = output = target; без повторной регистрации).
5. Postconditions: walkers обоих списков валидны; Actor node отсутствует в 259, ровно один в 123 с payload Actor; counts 259: 2→1 (P1 остаётся), 123: 0→1; `[Actor+3c]=123`; own node тот же `0x61b40540`; кеши неизменны; P1 graph неизменен.

Запрещённые действия не выполнялись: ручных links/unlink, прямой записи sectorIndex, поиска Actor по памяти, изменения flags/physics, записи floor/ceiling кешей, прямого destructor/free.

## 4. Native run N58A_C01 (D1–D7)

**D1.** `native_preflight`: TOMB.MAP, 3/3/0, Sim18892, P1 Player `0x140af33e0` / Puppet `0x6178a690` / node `0x61b41da8` один в 259, список 123 пуст, экспериментального Actor нет; BEFORE snapshot 12 полей. Pre-spawn геометрический план: все геометрические checks PASS, единственный отказ — `FLOOR_CEILING_CONTRACT_UNKNOWN`, так как кеш Actor ещё не существует (по дизайну).

**D2.** Один вызов factory → Actor `0x61712a00`, node `0x61b40540`, sector 259. Smoke: teleport → [-7360,-6848,1240], yaw 0.25, same-sector LinkSector(259), sprite `OURS`. Exception-to-status в KEX: второй экземпляр controller с настоящей identity и бросающей диагностикой вернул `VALIDATION_UNKNOWN`, затем `STOPPED`; adapter writes 0, Actor/revision неизменны. Повреждения KEX не вносилось.

**D3. Interpolation — PARTIAL.** Причина пропуска шага: план кандидатов упорядочивается по frustum актуальной камеры; в этом процессе `positions[2]=[-7360,-6848]`, а команда 4 требовала X-only пару ≥64 к `positions[3]=[-7456,-6880]`. Из любой достижимой точки (positions[2] или home) условие невыполнимо; бросок в Tick запустил бы safe-cleanup Remove до cross-sector. Команда 4 сознательно не отправлялась. Return teleport (команда 5) проверил reset без stale render values: previous = output = [-7424,-6880,1240], первый sprite той же revision settled. Ни одного `native_interpolation_sample` после resets (stale lerp отсутствует). Исторические три natural outputs N58A_01 остаются; новый observer (идентичность по live render outputs Actor + отсутствие same-sector двойника) работает, но intermediate submission не наблюдалась. Native lerp output не выдаётся за sprite submission.

**D4. Cross-sector — PASS.** Аттестация ATTESTED, переход выполнен, все postconditions выполнены (см. §3). Первый sprite новой revision: identity `OURS`, sector 123, origin [-7424,-7008,1240], settled. [Кадр](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/N58A_C01/window_20261103_170200_360911.png): stock body-rest sprite дальше в sector123 за проёмом.

**D5.** Через 120 distinct steps: list259 found 0, list123 found/count 1, 718 identity-matched sprites с sector 123, 0 ambiguous, P1 snapshot EQUAL. Remove выполнен через 1153 distinct steps после переноса.

**D6.** Remove из фактического sector123: flags 8260 (0x2000), removed 1 → Drain (list123 1→0, refs 0) → OnRemove (own node cleared) → PoolReturn в контролируемом Sweep без чтений Actor → node payload null, refs 0, in_use 0, occurrences 123:0, 259:0. Всего 5065 distinct Simulation Steps = 5065 Actor Tick callbacks. После retirement обычные W/S: P1 штатно 259→123→259, inventory неизменен, sprite после Remove 0; камера следует original P1 (eye = render pos + zterms[0,104]).

**D7.** Probe/camera выгружены, все 23 prefix sites восстановлены; обычный WM_CLOSE; retained + duplicate handle wait 0, exit 0, emergency cleanup нет; launcher exit 0; обе REA sessions закрыты; игровых процессов 0; protected profiles before = after.

**P1.** 8 полных snapshots (BEFORE, AFTER_SPAWN, AFTER_SMOKE, AFTER_RETURN, AFTER_CROSS_SECTOR, AFTER_CROSS_STABILITY, AFTER_REMOVE, POST_RETIREMENT) × 12 полей, 28 pairwise comparisons — все EQUAL; ownership/graph/камера сохранены.

## 4b. Native run N58A_C02 — Interpolation Acceptance

Отдельно разрешённый пользователем второй процесс; тот же порядок D1/D2, затем шаг, возврат и Remove (cross-sector не повторялся). Actor `0x61adf300` / node `0x61ace210`, flags 0x40, sector 259. Smoke (teleport [-7360,-6848,1240], yaw 0.25, same-sector LinkSector, exception-to-status) — PASS.

**Шаг (команда 4):** native_step из [-7360,-6848,1240] в самую дальнюю одобренную точку плана [-7488,-6880,1240] (132 units, X и Y); только origin, без reset; yaw не менялся.

| Lerp (1d3ef0 output) | t | Render frame | Sprite submission (1eb650 entry origin) | Frame |
|---|---|---|---|---|
| [-7387.21044921875, -6854.802734375, 1240] | 0.212582 | 4473 | тот же бит-в-бит, `OURS`, sector 259, angles [0,0,0.25] | 4474 (×2) |
| [-7429.88037109375, -6865.47021484375, 1240] | 0.545940 | 4474 | тот же, `OURS` | 4475 (×2) |
| [-7472.55029296875, -6876.1376953125, 1240] | 0.879299 | 4475 | тот же, `OURS` | 4476 (×2) |

Все три — на одном engine stamp 7539 / state call 7531 (несколько render passes между двумя simulation steps). previous/current lerp = точно from/to шага; формула output = previous + (current − previous)·t, ошибка ≤1.22e-4 (float32; t вычислен по X и применён к Y). Каждый sprite следует за своим lerp в следующем render pass (scene-hook инкремент между ними); на каждый pass sprite renderer вызывался дважды с тем же entry. Identity: frame/sector/flags/scale/angles entry = live render outputs Actor `0x61adf300`, origin = его +6c/+70/+74 (z offset +128 = 0), ни один другой объект sector 259 не имеет того же frame и transform (0 ambiguous). Rotation previous = current = output = [0,0,0.25] во всех samples → позиционная интерполяция отделена от yaw. **Native lerp output и sprite submission записаны раздельно и сопоставлены, а не выведены друг из друга.**

**Reset (команда 5):** teleport home + yaw 0 → reset1d3e60: previous = output = [-7424,-6880,1240]; первый sprite новой revision settled `OURS`; после reset ни одного lerp sample (stale значений нет).

**Lifecycle/Shutdown:** Remove (627 steps после шага) → Drain → OnRemove (own node cleared) → PoolReturn в Sweep → payload null/refs 0/in_use 0/occurrences 0; 2324 steps = 2324 Actor Tick callbacks; sprite после Remove 0. 7 snapshots × 12 полей, 21 pairwise comparisons — EQUAL; камера следует original P1. 23 prefixes восстановлены, WM_CLOSE, retained+duplicate wait 0, exit 0, launcher exit 0, REA sessions закрыты, процессов 0, protected profiles equal. [Кадр после reset](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/N58A_C02/FRAME_return.json).

## 5. Offline gate

`offline_test.js` (существующий harness): 19 прежних + 20 новых случаев = 39 PASS до и после native run. Геометрические случаи используют настоящие native копии faces 259/123 из журнала N58A_01. Покрыты все 19 требуемых: valid source/destination, contained AABB, partially outside, invalid portal, unknown floor/ceiling (missing/stale/water), invalid index, stale identity, duplicate membership, reciprocal failure, wrong thread, StateExit, repeated same/cross, update after retirement, cleanup в destination, sprite identity mismatch/ambiguity, teleport reset, exception→status. Offline PASS не заменяет native acceptance.

## 6. Изменённые файлы (все на S:)

Lab `experiments/m1/native_visual_proxy_movement/`: новые `cross_sector.js`, `rea_extract.py`, `review_completion.py`, `finalize_completion.py`; изменены `movement_native.js`, `probe.js`, `offline_test.js`, `precheck.py`, `runner.py`, `workflow.py`, `README.md`. `controller.js` не менялся (его guard-only редакция теперь исполнена в KEX). Patch-скрипты и pre-task копии — `scratch/completion/`. Новые Evidence — `COMPLETION/`, профиль — `profile/N58A_C01`. Обновлены текущие `docs/PROGRESS.md`, `docs/EVIDENCE_INDEX.md`. Production C++/DLL, ENet, wire v1.10, DOC53/55/56/57 контракты, game EXE/KPF, saves — не менялись. Полный список с SHA: `COMPLETION/CHANGED_FILES_*.json`, [source manifest](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/SOURCE_MANIFEST.json), integrity: `COMPLETION/INTEGRITY_*.json`.

**После C01 изменён `probe.js`**: команда 4 выбирает самую дальнюю одобренную точку ≥64 из плана (вместо фиксированной X-only пары) и при отсутствии такой точки отказывает статусом без броска. Эта версия **исполнена в C02** (C02 исполнил ровно текущие исходники, [manifest](evidence/m1/NATIVE_VISUAL_PROXY_58A/COMPLETION/C02/SOURCE_MANIFEST.json)). Для C02 также: `workflow.GATE`, `runner.RUN='N58A_C02'`, новый `review_interpolation.py`. Исполненные версии — `N58A_C01/probe_bound.js` и `N58A_C02/probe_bound.js`. `SUPERSEDED_PRECHECK_01/` — первый pre-native gate C01, заменённый до запуска KEX.

Ошибки стенда сохранены: (1) **исправление**: ранее здесь было написано, что в C01 лишний RETURN пришёл в уже загруженный уровень. Это неверно: в C02 уровень с экрана карты сам не загрузился за 40+ с, и preflight появился через 1.6 с после RETURN; значит и в C01 именно этот RETURN запустил уровень (preflight 16:59:52 после нажатия). Это обычный пункт навигации, не gameplay input. (2) первый `gate` C01 вернул PENDING, пока REA status не был экспортирован в run (затем PASS без изменения условий процесса).

## 7. UNKNOWN и ограничения

- Приёмка собрана из двух процессов: cross-sector и interpolation не выполнялись в одном процессе; одновременного случая «интерполяция через границу сектора» нет (перенос всегда teleport с reset).
- XMM1 (t) напрямую не читался — t восстановлен из output; количество intermediate passes зависит от расписания KEX (здесь 3 на simulation step).
- Возврат assert7b3c0 при out-of-range индексе.
- Только одна пара 259→123, одна цель, один тип Actor с flags 0x40 и плоскими floor/ceiling; обратный переход не аттестовывался и не выполнялся. Наклонные полы, водные сектора, другие карты, Actor с physics bit7 — не проверены; там кеш +11c/+120 может потребовать штатного обновления.
- Читатели +11c/+120 вне Actor Tick/spawn не перечислялись; доказательство «mutation не требуется» основано на бит-равенстве native значений в цели, а не на отсутствии читателей.
- Continuous portal traversal / collision navigation не проверялись; aperture-проверка — геометрическая по плоскостям, не по полигонам вершин.
- Отражение FindSector — JS-зеркало, совпадение с native 1c98f0 в общем случае не доказано (здесь подтверждено независимыми 1cf540 и полным сканом).

## 8. Handoff для DOC-58B (не реализован)

- **Position/yaw**: только через controller статусы; origin три float +18/+1c/+20 и yaw +38 (нормализованный), затем reset1d3e60(+50/+78) для teleport/смены сектора. Обычный update внутри сектора — native_step без reset: previous capture и lerp1d3ef0 KEX сами дают промежуточные render transforms (подтверждено до sprite renderer в C02). Снимок сети применять как новый current раз в simulation step; не вызывать lerp вручную.
- **Смена сектора**: origin → LinkSector1d3d70(actor, dest) → reset1d3e60(actor+50). Destination: полный скан секторов (единственность), portal BFS от текущего сектора, native 1cf540 witness, все 8 углов AABB, floor/ceiling native == кеш Actor и одинаковый water flag; иначе отказ без записей.
- **Identity**: weak actor/node pair, reciprocal node payload, in_use, removal byte; никакого поиска по памяти; после PoolReturn — никаких чтений.
- **Thread**: все мутации внутри StateTick.onLeave на attested Sim TID после реального P1 Tick; stable world 3/3/0; отказ при StateExit.
- **Отказ при stale world/actor**: STALE_IDENTITY / STATE_EXIT / WRONG_THREAD / VALIDATION_UNKNOWN → controller stop, затем только штатный Remove.
- **Cleanup**: один Remove из фактического сектора → Drain → OnRemove → PoolReturn → node retirement.
- **Из PlayerSnapshot потребуется**: XYZ, yaw, sector hint, map epoch/identity и sequence/tick для отбрасывания устаревших transforms.
- **Остаётся**: обобщённый destination (сейчас whitelisted 259→123 и одобренные точки), другие геометрии/типы, network jitter-буфер, отказ при disconnect/map change, перенос в движении (lerp через портал).

**Можно ли переходить к DOC-58B:** да, как к отдельному заданию, в границах этого bounded PASS (один тип Actor, плоские полы, whitelisted destinations). DOC-58B не начат; остановка после DOC-58A.
