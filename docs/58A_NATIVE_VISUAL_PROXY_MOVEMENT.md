# DOC-58A — Native Visual Proxy Movement

2026-11-03 UTC. **PARTIAL.** Один stock Actor1007 действительно перемещался и менял yaw в настоящем KEX. Cross-sector перенос Actor не выполнялся: безопасный destination/portal contract не доведён до допуска. Общий DOC-58A PASS не объявляется. DOC-58B не начат.

Основные результаты: [75 проверок текущего опыта](evidence/m1/NATIVE_VISUAL_PROXY_58A/NATIVE_ACCEPTANCE_REVIEW.json), [transform/render/interpolation](evidence/m1/NATIVE_VISUAL_PROXY_58A/MOVEMENT_OBSERVATIONS.json), [lifecycle](evidence/m1/NATIVE_VISUAL_PROXY_58A/LIFECYCLE_JOURNAL.json), [36 полных сравнений P1](evidence/m1/NATIVE_VISUAL_PROXY_58A/PER_FIELD_COMPARISON.json), [исходный журнал](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/native_v2.jsonl).

| Критерий | Результат этого задания |
|---|---|
| SAME_SECTOR_MOVEMENT | PASS: четыре разных XYZ, три teleport и один native interpolation step |
| YAW | PASS: 0 → 0.25 → 0.5 → 0.75 → 1; значения дошли до sprite entry |
| RENDER_TRANSFORM | PASS: native sprite submissions и просмотренные игровые PNG |
| INTERPOLATION | PARTIAL: teleport reset и три natural interpolant outputs доказаны; промежуточные sprite submissions не записаны |
| CROSS_SECTOR | BLOCKED: вызовы LinkSector только с текущим sector259 |
| Sector lists | PASS внутри проверенного сектора: identity/payload, reciprocal links, один membership; P1 штатно 259→123 |
| P1 | PASS ограниченного DOC57-контракта: ownership/control/local camera и все 12 обязательных полей |
| Stability | 5235 distinct Simulation Steps / 5235 Actor Tick callbacks; 1994 distinct steps после последнего movement |
| LIFECYCLE | PASS: один Remove → Drain → OnRemove → PoolReturn → ObjectNode retirement |
| OS shutdown | PASS: WM_CLOSE, retained/duplicate wait0, exit0, игровых процессов0 |
| Integrity | Исторические17008 файлов проверены; изменены только два текущих индекса; game/copies/protected saves сохранены |

## Exact build и native audit

EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`; stock KPF `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`. Один KEX PID27128, attested Simulation TID12384, runtime base `0x140000000`, карта `maps/TOMB.MAP`. Использована прежняя независимая копия `experiments/m0/game`, свежий профиль `experiments/m1/native_visual_proxy_movement/profile/N58A_01`.

Переиспользованы exact-build dossiers DOC57 и сохранённый Ghidra project. Focused read-only export без import/auto-analysis добавил только interpolant callbacks, scalar angle lerp, point-in-sector и registry references. [Static audit](evidence/m1/NATIVE_VISUAL_PROXY_58A/STATIC_AUDIT.json), [sites/prefix/file offsets](evidence/m1/NATIVE_VISUAL_PROXY_58A/SITES.json), [dossiers](evidence/m1/NATIVE_VISUAL_PROXY_58A/static/). Это декомпиляция бинарника, не исходники KEX. Имена ролей ниже аналитические.

| Роль | RVA | Preferred/actual VA | File offset | Подтверждённый ABI/использование |
|---|---|---|---|---|
| SpawnActor |1d4780|1401d4780|1d3b80|Win64 Actor*(Factory*, int32, Vec3*, float yaw, int32 sector); один вызов |
| LinkSector |1d3d70|1401d3d70|1d3170|void(Actor*, int32); четыре вызова с259 |
| Interpolant reset |1d3e60|1401d3e60|1d3260|void(Interpolant*); RCX subobject; previous и output ← referenced vector |
| Previous capture |1d3eb0|1401d3eb0|1d32b0|void(Interpolant*); previous ← referenced vector; вручную не вызывался |
| Position interpolate |1d3ef0|1401d3ef0|1d32f0|void(Interpolant*, float t); RCX/XMM1; только read-only observer естественных вызовов |
| Rotation interpolate |1d30e0|1401d30e0|1d24e0|RCX/XMM1; три scalar shortest-angle lerps; вручную не вызывался |
| Sprite collector |1e9a20|1401e9a20|1e8e20|Читает render outputs и переводит local AABB в world bounds; static evidence |
| Sprite renderer |1eb650|1401eb650|1eaa50|Принятый DOC57 entry; real origin/angles/sector записаны read-only |
| Remove |1a02b0|1401a02b0|19f6b0|void(Actor*); один вызов на Sim |

StateTick1b6ec0, Actor Tick19f2d0, Drain1b61d0, Sweep1b6360, OnRemove195000, PoolReturn1d5000 переиспользованы из DOC57. Все KEX mutations выполнялись из existing StateTick.onLeave после реального P1 Tick с проверенным TID, world identity, stable state3/3/0 и no StateExit. Worker API и Player/Puppet bindings не добавлялись.

## Три отдельных контракта

**Transform.** Общий штатный SetPosition/SetYaw с доказанным ABI не найден. Использован минимальный закрытый adapter: только три float origin+18/1c/20 и float yaw+38. Это не произвольный raw-memory API. Actor Tick19f2d0 содержит штатные direct origin stores, затем LinkSector; child-attachment branch также меняет origin существующего Actor непосредственно. Для пассивного1007 physics branch выключен stock flags0x40. Не выставлялись MOVEABLE/SOLID, не вызывался Tick вручную. Native результат подтверждает безопасность только этих операций в данном bounded run.

**Interpolants.** Constructor19ef20 устанавливает разные vtables: Actor+50→52bdf0 (Vec3), +78→52be68 (slaveRotation). Spawn1d34d0 один раз связывает position ref+58 с origin+18 и angle ref+80 с rotation+30 и регистрирует оба subobject в registry63d368. Повторный Spawn/registration для reposition запрещён.

| Поля Actor | Значение |
|---|---|
|+18/1c/20|Current simulation float XYZ |
|+30/34/38|Current rotation components; yaw последний |
|+58 / +80|Borrowed native ref pointers к origin/rotation; проверяются до mutation |
|+60/64/68|Position previous capture |
|+6c/70/74|Position render output, не simulation origin |
|+88/8c/90|Rotation previous capture |
|+94/98/9c|Rotation render output |
|+ec/f0/f4, +f8/fc/100|Local AABB min/max; здесь[-12,-12,-24]/[12,12,24] |
|+10, +3c, +40|ObjectNode, native sectorIndex, removal byte |
|+16c|Flags; stock0x40 всё время live |

Teleport: identity/thread/geometry/interpolants validation → origin stores → native reset1d3e60(Actor+50). Yaw: finite bounded input → нормализация → yaw store → native reset1d3e60(Actor+78). Reset копирует current в previous и output, не меняет registry/ref pointers. После каждого teleport previous/output равны target. Плавный технический step записывает current origin, сохраняя previous для естественного renderer lerp. Полноценного сетевого interpolation buffer нет.

Формула native position callback: output = previous + (current−previous)×t. Между `[-7360,-6880,1240]` и `[-7456,-6880,1240]` реально получены outputs X −7385.1748046875, −7417.177734375, −7449.18017578125 на одном engine stamp39320; восстановленные t≈0.262238/0.595601/0.928960. Это реальные read-only outputs native callback, не screenshots и не вызов lerp стендом. t вычислен из изменявшейся X-компоненты, XMM1 независимо не измерялся; error0 для этой компоненты не независимая проверка коэффициента. Y/Z оставались постоянными. Sprite observer записывал settled targets и пропускал несовпадающие intermediate origins. Поэтому подтверждены reset и native interpolation state, а полное доказательство промежуточного sprite transform/визуальной плавности остаётся PARTIAL. Непрерывное сетевое движение не принято.

**Bounds/render.** Collector1e9a20 читает local bounds+ec/+f8 и render output+6c/+70/+74, складывает их при построении world AABB. Для этого stock Actor не обнаружен отдельный cached world AABB, требующий setter. Local extents не менялись. Native sprite entry получил новые XYZ и yaw после всех четырёх операций; старый origin не выдаётся за renderer update.

## Геометрия и sector contract

Plan формировался из реальных faces array RVAa45980/counta45988, sector arraya45968/counta45970, stride88. Каждая точка проходила все plane tests восьми AABB corners и актуальный native camera frustum/clip. Геометрия sector259: first2083/last2087, проверены также ceiling2088/floor2089. Полупространства включают x≥−7552, x≤−7296, y≤−6656, y≥−6912, z≤1664, z≥1152. Duplicate y-plane сохранён из native данных. Target origins/AABB не касались внешних границ.

| Шаг | XYZ | Yaw | Sector | Режим |
|---|---|---|---|---|
| Spawn |[-7424,-6880,1240]|0|259|Native factory |
|1|[-7488,-6880,1240]|0.25|259|Teleport/reset |
|2|[-7488,-6848,1240]|0.5|259|Teleport/reset |
|3|[-7360,-6880,1240]|0.75|259|Teleport/reset |
|4|[-7456,-6880,1240]|1|259|Native interpolation step; yaw reset |

LinkSector проверяет signed index через native array range assertion. При existing ObjectNode и другом sector он штатно unlink/reinsert того же node и лишь затем пишет sectorIndex. При том же секторе links не меняются. Он **не определяет destination по XYZ и не проверяет геометрию**. Нельзя заранее писать sectorIndex или вручную менять links. В штатном Actor Tick origin записан перед LinkSector; physics transition дополнительно обновляет floor/ceiling-related values+11c/+120.

Planes sector123 действительно сняты (faces885..890, y∈[−7168,−6912], две наклонные боковые плоскости). Его соседство по поверхности y=−6912 и нормальное P1 traversal подтверждены. Но отдельная destination point для Actor AABB, portal aperture/route и необходимые floor/ceiling cache semantics не аттестованы до cross-sector call. Controller отказывает `DESTINATION_GEOMETRY_NOT_ATTESTED` для другого сектора. Ни одного вызова LinkSector(123), ручного unlink, sectorIndex store или linked-list repair не было. Cross-sector остаётся BLOCKED; никакого general transfer PASS из same-sector no-op не выводится.

## Контроллер, runtime и P1

[NativeVisualProxyMovementController](../experiments/m1/native_visual_proxy_movement/controller.js) — временный закрытый интерфейс start/stop/spawn/setPosition/setYaw/moveToSector/remove. Один spawn attempt без retry; lifecycle владеет weak identity до PoolReturn. Unknown/stale identity, wrong thread, StateExit, invalid interpolation и неаттестованный target отказывают. Remove имеет собственную once-only защиту. У вызывающего кода нет raw Actor memory API. Native adapter допускает только plan whitelist, проверяет P1 graph, Actor type/vtable/flags/target/sourceOwner, payload/in_use и reciprocal membership.

[Launch](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/launch.json), [исходный preflight](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/native.jsonl), [rebind до Spawn](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/REBIND_BEFORE_SPAWN.json). Пользователь подтвердил ручной ввод после первого preflight: P1 ушёл в176 до Spawn. Первый script не создал Actor. В том же KEX обычными OS keys/mouse P1 возвращён в259; exact fixed P1 origin predicate заменён актуальными sector/camera/geometry gates. Прямых P1/camera writes не было. Перед factory снова снят полный BEFORE state. Нормальная yaw/pitch/position от ввода не являются ownership mutation.

Factory вернула Actor`0x615b3840`, node`0x61adb270`, stock vtable`0x140528608`, отдельные от P1. Единственный Actor оставался в259. P1: Player`0x140af33e0`, Puppet`0x61542650`, node`0x61ad4568`, reciprocal owner исходный. После движения Actor обычный W перевёл P1259→123 при сохранённой membership Actor; после retirement S вернул P1 в259. Проверялось конкретное node/payload присутствие в обоих текущих списках, а не равенство sector counts.

9 полных snapshots,12 mandatory fields каждый,36 pairwise comparisons: все raw contents и identity EQUAL. Health200, weapon/pending0/0, owned[1,0,0,0,0,0,0,0], ammo[0,60,38,15,65,16,250,6], ankh/masks/artifacts/abilities/keys/quest/teamDolls0. Полный контракт типов/size/getters переиспользован из DOC57; raw16 comparison не восстанавливает неизвестную C++ signedness ammo. Snapshot after each move, AFTER_REMOVE и POST_RETIREMENT сохранены отдельно. Связь камеры с original P1 render position + native zterms[0,104] подтверждена на Sim12384; retired Actor не читался.

Native game PNG просмотрены: [положение1](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/window_20261103_162250_159000.png), [положение2](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/window_20261103_162302_951021.png), [положение3](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/window_20261103_162315_690417.png), [положение4](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/window_20261103_162337_468721.png), [после Remove](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/window_20261103_162431_424175.png). Это неизменённые owned-window captures. Stock body-rest sprite широкий и при близкой камере занимает большую часть кадра; это не заявление об окончательной модели remote player. Старый FRAME_live снят **до Spawn** и не считается live proof. FRAME_move1 metadata сопоставляется фактическому timestamp, а не порядку запуска helper.

## Retirement, shutdown, ошибки стенда

При последнем sector259 вызван один Remove. Drain снимает native node с актуального списка, OnRemove очищает own node, PoolReturn инвалидирует Actor identity до дальнейших наблюдений; Sweep witness проверяет payload=null/refs0/in_use0/sector occurrences0. После retirement все weak pointers null, нет Actor reads или sprite_after_remove. Общие destructor/delete/free semantics повторно не реверсировались.

[OS_SIGNALED](evidence/m1/NATIVE_VISUAL_PROXY_58A/N58A_01/OS_SIGNALED.json): retained+duplicate одного kernel process object wait0, native exit0; launcher exit0. Emergency termination отсутствует. Перед WM_CLOSE выгружены experiment/camera hooks и проверены все20 prefix sites на точное восстановление. Isolation redirection/guard оставались до normal exit. [REA final](evidence/m1/NATIVE_VISUAL_PROXY_58A/REA_FINAL.json), [33 закрытых session IDs](evidence/m1/NATIVE_VISUAL_PROXY_58A/SESSIONS_CLOSED.json). `target_state:unknown` при закрытии уже завершённого helper не является exit proof; OS handles дают независимое доказательство для KEX.

Сохранены harness ошибки: первый ui_start до видимого окна; некоторые input helpers отказали при отсутствии actual foreground до keydown; первая попытка rebind получила `prefix mismatch tick`, поскольку дополнительный read-only diagnostic hook ещё стоял на Tick. Оба scripts выгружены, prefix восстановлен, тот же bound_v2 успешно загружен **до первого Spawn**. Это Frida harness rejection, не native fault. Первоначальная попытка сохранить большой MCP JSON через shell превысила Windows command-line limit; вместо неё использован export_evidence_bundle. Startup Steam initialization warning сохранён; Steam/achievements функционально этим опытом не проверялись. Все сообщения/error/truncation доступны в REA bundles; truncation=false.

## Регрессии, исходники и границы

Перед KEX:18 offline controlling-logic tests, включая все15 обязательных случаев, и JS/Python syntax. После native run controller получил только exception-to-status hardening: неизвестные ошибки adapter validation/identity/scope теперь возвращают explicit status; добавлен19-й fixture case. Финальный19/19 и syntax заново PASS. Эта guard-only редакция **не переигрывалась в KEX**: точные исполненные версии находятся в immutable probe_bound_v2.js/REBIND manifest. Review текущего runtime evidence:75 проверок, все успешны; это согласованность PARTIAL evidence, не whole-task acceptance.

Production C++/DLL, ENet, protocol v1.10, Snapshot DTO и принятые DOC53/55/56/57 файлы не менялись; сетевые тесты не повторялись и исторические PASS не объявляются новыми. [Changed files с абсолютными S:-путями](evidence/m1/NATIVE_VISUAL_PROXY_58A/CHANGED_FILES.json), [source hashes](evidence/m1/NATIVE_VISUAL_PROXY_58A/SOURCE_MANIFEST.json), [historical integrity](evidence/m1/NATIVE_VISUAL_PROXY_58A/INTEGRITY_FINAL.json). Все новые source/scratch/profile/evidence/docs лежат внутри указанного S:-корня. Ghidra user.home/cache/temp также task-local. Принятые launcher/isolation helpers использованы без нового injector. Guard ограничен своими Nt file-open seams и не является полной OS sandbox; original/copy/protected profiles дополнительно сверены содержимым.

UNKNOWN: cross-sector destination/portal/cache contract и renderer culling после реального перехода; intermediate sprite lerp samples; многомерное/angular continuous interpolation, yaw wrap boundary; arbitrary destinations/types/maps, populated inventory states, непрерывное сетевое движение. Даже same-sector API ограничен четырьмя approved точками и одной Actor identity.

**Для DOC-58B проверенный полный movement interface пока не готов.** Transform/reset/LinkSector signatures пригодны как ограниченный research foundation; authoritative snapshot→validated transform→Actor pipeline не реализован. Thread bridge, stale network transforms, disconnect/map invalidation и network Actor binding здесь не вводились. Остановка после DOC-58A PARTIAL.
