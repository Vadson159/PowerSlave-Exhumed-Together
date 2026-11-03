# DOC-57 continuation — Native Visual Proxy Step 0

2026-11-03 UTC. **Общий статус: PARTIAL. Native visual/lifecycle subexperiment: bounded PASS.** Один настоящий stock Actor1007 создан, виден, прожил более120 измеренных simulation steps, штатно удалён и возвращён в native pool с retirement ObjectNode. P1 продолжил движение после удаления; KEX завершился с OS exit0. Полное сохранение inventory/story fields P1 не измерено, поэтому весь DOC-57 не получает PASS.

Исторический [DOC-57 BLOCKED](57_NATIVE_VISUAL_PROXY_STEP0.md), старый probe и исходные Evidence сохранены неизменными. Это addendum относится только к новому `N57_CONT_01`, PID29204 / simulation TID18952. DOC-58 не начат.

Основные материалы: [native review](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/NATIVE_REVIEW.json), [main journal](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/native.jsonl), [cleanup journal](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/cleanup.jsonl), [visual evidence](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/VISUAL_EVIDENCE.json), [OS exit](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/OS_SIGNALED.json), [errors and corrections](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/HARNESS_ERRORS_CONTINUATION.md).

## 1. Результат и acceptance

| Gate | Результат |
|---|---|
| Exact binary/native ABI | VERIFIED STATIC + NATIVE; exact SHA, byte anchors, runtime base140000000 |
| Native Actor Factory | VERIFIED NATIVE; один вызов, настоящий RAX Actor pointer сохранён непосредственно после NativeFunction |
| Нет Player singleton reassignment | VERIFIED STATIC для ordinary Spawn; VERIFIED NATIVE исходный P1 graph сохранён |
| P1 Player/Puppet ownership | VERIFIED NATIVE; исходные Player/node/Puppet/owner до Spawn, во время stability и после retirement |
| Валидный сектор | VERIFIED NATIVE; sector259, native planes, целый двусвязный список, отдельный ObjectNode |
| Реальная видимость | VERIFIED NATIVE; 3D sprite submission,20 emitted vertices на submission, настоящий игровой кадр |
| Нет нежелательных gameplay effects | PARTIAL: passive resource/flags, неизменные health/weapon/ammo, нет наблюдаемого damage; полный inventory/story-state comparison отсутствует |
| Минимум120 simulation steps | VERIFIED NATIVE;585 измерены до guard stop, ещё12 в отдельном cleanup scope; общий подтверждённый минимум597 |
| Native Remove | VERIFIED NATIVE; ровно один вызов на simulation thread |
| Deferred lifecycle/affinity | VERIFIED NATIVE в пределах OnRemove → pool return → ObjectNode retirement; observed native faults0 |
| P1 работает после удаления | VERIFIED NATIVE; обычный S input дал новое перемещение, health/weapon/ammo и ownership сохранены |
| Clean exit | VERIFIED OS; WM_CLOSE, retained+duplicate handles wait0/exit0, без forced termination |

23 положительных offline checks проверяют согласованность фактических Evidence; **их число не заменяет общий acceptance**. Точное число ticks за всё wall-clock время жизни UNKNOWN. Это не мешает минимальному120-step gate, но расширенный inventory остаётся незакрытым критерием.

## 2. Только S: и один новый KEX

Единственный корень: `S:\_Vibe_Coding\PowerSlave_Exhumed_Together\PowerSlave_Exhumed_Together_mod`.

- Новый код: `experiments/m1/native_visual_proxy/continuation/`.
- Scratch/TEMP/TMP/Ghidra runtime/user.home: `experiments/m1/native_visual_proxy/scratch/continuation/`.
- Новые Evidence: `docs/evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/`.
- Новый профиль: `experiments/m1/native_visual_proxy/profile/N57_CONT_01/`.
- Документ: `docs/57_NATIVE_VISUAL_PROXY_CONTINUATION.md`; изменены только текущие индексы PROGRESS/EVIDENCE_INDEX.

Исторический `native_visual_proxy/workflow.py` с C:-staging **не запускался**. Использован существующий REA MCP6.8.0 Frida workflow и принятый suspended launcher/isolation/window helpers из M0/M11. Новый injector, DLL, игровые ресурсы и копия Steam-установки не создавались. Запускались установленные Python/Node/Ghidra; пути создаваемых task-файлов находятся на S:. Применённая skill: [reverse-engineer-anything](C:/Users/vadso/.agents/skills/reverse-engineer-anything/SKILL.md), focused exact-build static analysis и отдельный safety review.

[Launch](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/launch.json) подтверждает fresh PID29204, primary thread18952, accepted copy `experiments/m0/game`, fresh profile и CREATE_SUSPENDED. До Spawn выполнен [isolation/native gate](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/ISOLATION_GATE.json): gameplay state3/next3/pending0, `maps/TOMB.MAP`, runtime kexStr/mapInfo, level counts71/28608, исходный P1 и единственный исходный node в sector259. Observed P1 Puppet Tick на TID18952 внутри StateTick подтверждает simulation context.

Isolation guard ограничен наблюдаемыми NtCreateFile/NtOpenFile и CreateFileW; он не объявляется полной OS sandbox. В текущем captured scope нет успешных external write opens, guard errors/overflow0. Original/copies и защищённые профили совпали с baseline. Старые принятые M0/DOC53/55/56, Wire v1.10, ENet, MapReady, background focus policy и input pipeline не редактировались.

## 3. Исправление nested listener — VERIFIED FIXTURE

Перед запуском KEX проверен общий `one_actor.js` на benign Python/CModule target. Первый fixture A01 имел собственную ошибку read-only mutable CModule storage; сохранён как ошибка, не как game crash. A02 использовал явно выделенный/обнулённый RW extern buffer и прошёл фактическую Frida проверку.

[A_HARNESS_GATE](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/A_HARNESS_GATE.json), Evidence `ev_98486cf32e4bda8e9fc81b52a0dcd1015ef2f146448dcae8833763521087a9fa`: nested factory listener calls0; реальный returned pointer удержан; native factory1/remove1;125 distinct fixture stamps и0 Actor callbacks; telemetry exception вызывает cleanup; недостоверная identity запрещает Remove/retry; null return отделён от диагностики. `traps:'all'` не использован как предполагаемое исправление.

Новая последовательность: single-shot attempt → NativeFunction → **немедленно actor/result identity** → direct ObjectNode/sector validation → необязательная диагностика. Отсутствие nested listener не считается null. Ошибка журнала сначала устанавливает cleanup/stop state; StateExit сначала инвалидирует weak pointers. Native reference удержания в моде нет; все JS pointers non-owning.

## 4. Exact ABI, seams и layout

EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

Stock KPF SHA-256: `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd`.

```cpp
// Windows x64 ABI, exact-build opaque types; caller receives borrowed Actor*.
Actor* SpawnActor(Factory* self, int32_t type, const Vec3* xyz,
                  float yaw, int32_t sector);
```

RCX=factory `0x1405a10a0`, EDX=1007, R8=three floats, XMM3=yaw0, stack5 sector259: caller RSP+20 / callee entry RSP+28. Factory RVA1d4780/actual VA1401d4780; pushes+SUB change RSP by58. Instruction1401d4adf, bytes `8b942480000000`, reads `[RSP+80]` = entry+28. RAX is Actor, not ObjectNode. Prefix/stack proof повторно проверены по actual EXE до запуска; ABI не заимствован у Gemini. [Static gate](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/B_STATIC_GATE.json), [historical independent ABI audit](evidence/m1/NATIVE_VISUAL_PROXY_57/STATIC_AUDIT.json).

| Seam | RVA / actual VA | ABI, ownership и side effects |
|---|---|---|
| Ordinary Actor Spawn |1a02f0 /1401a02f0|RCX Actor; native interpolants/animation/flags; полезный return не используется; нет Puppet Player binding |
| Forbidden Puppet Spawn |1dbc60 /1401dbc60|RCX Puppet; GetPlayer binding side effect; type0 не применялся |
| LinkSector |1d3d70 /1401d3d70|RCX Actor, EDX sector; native node/list registration; factory вызвала сама; дополнительных вызовов/sector writes0 |
| Actor Remove |1a02b0 /1401a02b0|RCX Actor, meaningful return не используется; hidden/removal, interpolant unregister, native deferred queue reference |
| Actor Tick |19f2d0 /14019f2d0|RCX Actor; observer original traversal, не ручной Tick |
| P1 Puppet Tick |1db8a0 /1401db8a0|RCX original P1 Puppet; distinct native stamp внутри StateTick подтверждает world step |
| StateTick |1b6ec0 /1401b6ec0|RCX GameInstance; native factory/Remove только в attested onLeave scope |
| Drain / Sweep |1b61d0 /1401b61d0;1b6360 /1401b6360|deferred unlink/ref-release, затем поздний sweep callback/pool/node retirement |
| OnRemove |195000 /140195000|RCX object; native retirement callback, **не destructor** |
| PoolReturn |1d5000 /1401d5000|RCX factory, RDX payload; pool lifecycle, не raw delete/free |
| Scene / sprite / vertex |1eff40 /1401eff40;1eb650 /1401eb650;055f40 /140055f40|read-only renderer observers; scene RCX view/RDX Sector; sprite RCX view/RDX Sector/R8 entry; actual vertex submission |

[Runtime coordinates](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/RUNTIME_COORDINATES.json) и [SITES](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/SITES.json) содержат все18 проверенных anchors. Actual base140000000 получена из Process.mainModule, не предположена. После unload экспериментальных hooks **18/18 prefixes восстановлены** до WM_CLOSE. Thread safety не выводится из atomic operations.

Actor/node offsets приняты раздельно: Actor own-node10, XYZ18/1c/20, yaw38, sector3c, removed40, interpXYZ6c/70/74, interpAngles94/98/9c, AABB ec/f8, animation108, scale124, bob128, sourceOwner148, flags16c, ActorDef170, frameId178. Node prev0/next8, in_use24, payload28, refs40. Puppet owner1b8 читается **только у исходного P1 Puppet**, не у ordinary Actor.

## 5. Камера, frustum и фиксированная позиция

Focused exports использовали существующий saved Ghidra project, readOnly/noanalysis, без повторного импорта/общего реверса. Static evidence находится в [CONTINUATION_01/static](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/static/); RenderState использован из immutable DOC53 exact-build Evidence. Имена ролей аналитические; псевдокод не исходник движка.

Native scene RVA1eff40 получает prepared view **в stack storage**. Observer сохранял числовые копии в этом callback, stack pointer не удерживался. View offsets: planes0..5×10, eye c0/c4/c8, tan halfFOV cc/d0, projection e0, modelview120, combined clip1a0, quaternion3e0, FOV3f0, near3f8/far3fc, width400/height404/aspect408, horizontal-mode45c. Clip = column-major P×M. Camera eye Z = Puppet interpolatedZ + **GI+1cc +GI+1b4**, не одноимённые Player offsets.

Реальный preflight: eye `[-7420,-6775,1256]`, yaw `-3.1376049518585205`, pitch0, forward `[-0.003987661858172311,-0.9999920492448443,4.3651963805083234e-8]`, vertical FOV64°,960×540/aspect1.77777779, near0.1/far32768. Depth scale1.071428592; это не вертикальный world scale.

Старый point `[-7420.8037109375,-6848.013671875,1152]` с Actor AABB ±[12,12,24] **не пересекает этот реальный frustum**: NDCy примерно[-3.13584,-1.40473], максимум нижней plane distance−36.88784. Исторический кадр старого run не имел camera telemetry, поэтому точная причина его невидимости остаётся **INFERRED**. Текущее отсечение старой точки — VERIFIED NATIVE view + derived geometric test.

Новая фиксированная точка **[-7424,-6880,1240], sector259**. Из native sector planes получены минимальные all-corner margins `[116,116,212,212,20,400,64]`; весь AABB внутри сектора. Все8 corners внутри measured clip/frustum, NDCy[-0.642657,0.128530]. Native scene initial sector совпал с sector259. Это fixed point внутри начальной комнаты, выбранный по геометрии и реальной камере; не произвольное `P1+150`. Actor никогда не перемещался и не relink-ался вручную. Camera writes/override0.

Collector RVA1e9a20 проверяет animation/type/hidden и translated native AABB до render queue. Hidden=2000, moveable=80; прежние Gemini masks01000000/80000000 к этому контракту неприменимы. Automap-функции не использованы как 3D proof.

## 6. Actor type, ownership и реальный sprite

Выбран неизменённый stock **Obj_Ramses_Body_Rest /1007**, class kexSlaveActor/vtable140528608. Exact stock definition/animation ранее проверены и сохранены: visible opaque sprite, одна frame, нет frame actions/scripts/sounds/AI/pickup; нет solid/shootable/moveable. Flags40, scale1.5, target/sourceOwner null, refs0. Никаких direct writes в Actor flags/transform/sector/refcounts не выполнялось.

```text
GameInstance+40 -> original P1 Player140af33e0
  Player+d8 -> node61a7e358 -> original Puppet61666340 -> owner140af33e0
Factory native pool -> ordinary Actor61aa4310
  Actor+10 -> node61a7b7e0 -> payload61aa4310
  sector259 list owns native membership; no Player binding; no mod native refs
```

Native main journal seq6–9: factory return **0x61aa4310** непосредственно сохранён; node **0x61a7b7e0** независим от P1. Двусвязный sector list первоначально содержал ровно P1 и дополнительный Actor. Generation ID не установлен; recycled pointer не объявлен generation identity.

Seq10–13: actual sprite RVA1eb650 обработал entry с frame **0x5cf2dc90**, фиксированной XYZ, scale1.5, flags40, sector259, normal0/transparency255. Пять native tile records: width64/height53, x offsets−158/−94/−30/34/98, y−53; каждый submission вызвал20 native vertices. Два прохода renderer не считаются двумя Actor.

Настоящие owned-window frames, без генерации/редактирования изображения:

| Кадр | Наблюдение |
|---|---|
|[Gameplay before](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/window_20261103_151218_644928.png)|В начальной комнате нет дополнительной мумии |
|[Live](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/window_20261103_151233_382881.png)|Большой непрозрачный горизонтальный sprite мумии явно виден перед P1 |
|[P1 moved](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/window_20261103_151244_327473.png)|Sprite остаётся видимым после обычного движения P1 |
|[After Remove](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/window_20261103_151730_213828.png)|Мумия исчезла; та же локальная сцена без дополнительного sprite |
|[P1 after retirement movement](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/window_20261103_151741_414383.png)|P1 продолжил штатное движение |

`FRAME_before.json` относится к более раннему level-select screen и **не используется** как gameplay-before proof. Полные hashes/связи кадров — VISUAL_EVIDENCE.json.

## 7. Stability, ошибочный sector-count guard и границы inventory

Simulation steps считались по **distinct native stamps RVAaec390 с original P1 Puppet Tick внутри StateTick**, независимо от Actor Tick. До остановки счётчика585 world steps; отдельно586 Actor Tick callbacks. Во время cleanup отдельно12 world steps/12 Actor callbacks. Интервалы не перекрываются: общий измеренный минимум **597 steps /598 Actor callbacks**. Gap между scopes не заполняется предположительными ticks. Wall-clock Spawn→Remove291.704s и engine-stamp difference17502 не объявляются числом simulation steps.

Перед guard stop Actor сохранял XYZ/sector/flags/refs/target/sourceOwner; health P1=200, weapon/pending0, owned bytes `0100000000000000`, ammo bytes `00003c0026000f0041001000fa000600`. Наблюдаемых damage callbacks0. Actor Tick сам по себе ожидаем и не означает AI либо gameplay damage.

Обычный W через native window input переместил P1 из `[-7420,-6775,1152]`/sector259 в `[-7462.8798828125,-7013.56787109375,1152]`/sector123. Ownership сохранился. Камера продолжила локальный input-driven путь; adapter её не перепривязывал и не писал yaw/pitch. Запрет движения **Actor** между секторами соблюдён; обычный переход P1 между соседними секторами не является map transition.

Main `validateActor` ошибочно требовал постоянный count2 в sector259. После выхода P1 там остался только extra Actor, count1. Guard остановил эксперимент и отказал cleanup; исходный returned pointer не потерян, native Remove ещё не вызывался. Fresh read-only witness и последующая12-step revalidation подтвердили неизменный Actor/reciprocal node/payload/class/type/flags/refs, корректные sentinel links и исходный P1. Физическое повреждение list не наблюдалось.

Main probe выгружен; дальнейшего experiment control/Spawn не было. Выполнен только проверенный cleanup уже известного Actor отдельным [cleanup-only plan](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/CLEANUP_PLAN.json). Memory search, guessed pointers и memory repair отсутствуют. В текущем source membership guard исправлен: expected nodes зависят от фактического P1 sector. Девять offline regression checks воспроизводят captured count2/count1 и отвергают отсутствующие/дублированные/чужие nodes. **Исправленный полный probe после этого native не переигрывался**; исполнившийся frozen probe сохранён.

Пробел inventory: native сравнивались health,8 owned weapon bytes,8 ammo uint16, current/pending weapon. **Player artifacts+90, abilities+94, keys+98, questItems+9a, teamDolls+a4 и ankh+84/+86 не снимались до Spawn.** Их unchanged native state — UNKNOWN. Известные exact-build getters и static passive Actor обосновывают безопасный выбор объекта, но не заменяют отсутствующий before/after comparison. Это причина общего PARTIAL; bounded visual/lifecycle proof не объявляется campaign/inventory acceptance.

## 8. Native Remove → deferred retirement

Все события ниже на attested simulation TID18952, без raw delete/free/destructor/refcount edits.

| Stamp / cleanup seq | Факт |
|---|---|
|21323 /2|Прямая известная identity перепроверена; дальнейшие12 distinct world steps валидируют Actor и P1 |
|21334 /3–4|Один native Remove1401a02b0; flags40→**2044**, removed0→1, node refs0→1; node пока в sector list |
|21335 /5–6|Native Drain: sector membership1→0, refs1→0, payload ещё Actor, in_use1 |
|21336 /7–8|Дополнительное queue progression observation; node ещё pending, refs0 |
|21337 /9–10|Native OnRemove195000; Actor own-node очищен |
|21337 /11–12|PoolReturn1d5000, factory1405a10a0, return address1401b6417 внутри sweep; Actor/node weak pointers инвалидированы **до** диагностического emission |
|21337 /13|В том же sweep: node payloadnull, refs0, in_use0, sector occurrences0; последний node pointer очищен |

Sprite отсутствует после Remove на настоящем кадре; matching sprite submissions после Remove0 в observed scope. Actor pointer после PoolReturn не читается. ObjectNode читается только в том же завершающемся sweep и затем также инвалидируется; дальнейшие samples читают только P1/живой sector list.

Отдельный S input после retirement дал P1 `[-7482.94091796875,-7080.96044921875,1152]`, сохранены original graph/health200/weapon/ammo. Последний post-retire journal сообщает **1920 distinct world steps**. Нет double-remove/observed native exception/affinity fault. Complete C++ destructor/delete/free path **UNKNOWN / не заявляется**: подтверждён именно штатный pooled retirement.

## 9. Shutdown, regressions и integrity

После retirement pointers null, cleanup hooks выгружены,18 exact KEX prefixes восстановлены read-only witness. Затем обычный WM_CLOSE. [OS handle identity](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/N57_CONT_01/OS_HANDLE_IDENTITY.json), OS_SIGNALED и OBSERVER_CLOSED: same-kernel-object retained+duplicate, оба wait0, exit0; оба handle закрыты. Launcher exit0.17 native-run/helper REA sessions и2 benign fixtures закрыты; оставшихся KEX/task processes0. `close_frida_session.target_state=unknown` не используется вместо OS exit proof.

Production C++/DLL и protocol sources не менялись. Поэтому15 неизменённых production regression commands повторно не запускались и исторические PASS не приписываются новой работе. Выполнены относящиеся к изменениям проверки: реальный Frida A02 shared-state fixture, JS syntax/Python compile, cleanup-bound syntax,9-case captured-native membership regression,23-check native Evidence review. Новая DLL не собиралась.

Из корня S: reproducible offline commands (живой PID/профиль не переиспользовать):

```text
C:\Python314\python.exe -X utf8 -B experiments\m1\native_visual_proxy\continuation\workflow.py syntax
C:\Python314\python.exe -X utf8 -B experiments\m1\native_visual_proxy\continuation\review.py FRESH_REVIEW_TAG
C:\Python314\python.exe -X utf8 -B experiments\m1\native_visual_proxy\continuation\workflow.py integrity
```

Artifacts используют create-new semantics; повторный offline review требует нового output tag, иначе intentionally fail, без перезаписи Evidence. Фактические argv/cwd/TEMP/exit codes сохранены в RESULT.json/fixture records/invocations. Runtime source freeze и исходные hashes — PRE_NATIVE_GATE, PROBE_MANIFEST, CLEANUP_PLAN и FILES_FINAL.

| Исполненный файл | SHA-256 |
|---|---|
|probe_bound.js|`b567820a3ed33074748b0031da9c4c5591dc1cb673740bdc43ccb6e8ecc03fdd`|
|cleanup_bound.js|`207c621feb8179290911522c21fe368fac55a70d6d49456d151a09f85e3bff1c`|
|A02_FIXTURE_BOUND.js|`881343a48ef0f7bcf3820f4dafffb81686cc7dfc405b7e4d4c464107de842913`|

[Baseline](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/BASELINE.json):16579 historical/source/build files. Final integrity допускает только актуальные PROGRESS/EVIDENCE_INDEX из ранее существовавших файлов; accepted DOC53/55/56, historical DOC57 и failed-run Evidence/profile неизменны. [Final integrity](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/INTEGRITY_FINAL.json), [source/doc diff](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/SOURCE_CHANGES.patch), [files and hashes](evidence/m1/NATIVE_VISUAL_PROXY_57/CONTINUATION_01/FILES_FINAL.json).

## 10. Confidence и handoff

| Метка | Итог |
|---|---|
| VERIFIED STATIC | Win64 stack5/factory/pool/Remove contracts, раздельные layouts, ordinary sprite/collector/frustum path, passive stock resource |
| VERIFIED NATIVE | Returned Actor/node, sector/camera geometry, настоящая видимость,≥120 measured world steps, реальные Actor callbacks, P1 graph/movement/health/weapon/ammo, один Remove/retirement, OS exit0 |
| INFERRED | Историческая невидимость старой точки объясняется тем же frustum rejection; exact old view не сохранён |
| UNKNOWN | Полный inventory/story-state comparison, total continuous lifetime/count gap, general destructor completion, другие Actor types/maps/transitions/reconnect/network/render movement |
| REFUTED | Nested listener required for successful Spawn; Remove мгновенно завершает lifecycle; OnRemove195000 является destructor; постоянный sector259 count при движении P1 |

Фундамент native **creation/render/retirement** теперь доказан в заданном single-process bound. **К переходу DOC-58 по всем строгим критериям DOC-57 пока не готово:** перед его отдельным заданием требуется закрыть missing inventory comparison и выполнить цельный corrected-harness acceptance, если пользователь решит продолжить native acceptance.

Будущий DOC-58 может опираться на exact factory ABI, ordinary passive type, native sector membership, verified renderer и Remove/pool/node retirement. Ещё нужно отдельно подтвердить безопасный position update/interpolants, актуальный sector placement policy, generation identity и ownership remote visual lifetime. Network snapshots/ENet binding, movement replication, sector crossing/interpolation, Client Camera и World Replication здесь не реализованы. **Остановка после DOC-57.**
