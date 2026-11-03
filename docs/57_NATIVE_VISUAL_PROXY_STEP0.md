# DOC-57 — Native Visual Player Proxy Step 0

2026-11-03 UTC. **BLOCKED. Один дополнительный штатный Actor действительно создан, но его видимость и полный управляемый lifecycle не подтверждены.** Повторного native spawn или второго KEX-run не было. После остановки эксперимента выполнен обычный WM_CLOSE; retained process handle и его duplicate дали wait0/exit0.

Главные Evidence: [итог](evidence/m1/NATIVE_VISUAL_PROXY_57/RESULT_SUMMARY.json), [native journal](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/native.jsonl), [read-only failure witness](evidence/m1/NATIVE_VISUAL_PROXY_57/FAILURE_READONLY_WITNESS.json), [полный REA capture](evidence/m1/NATIVE_VISUAL_PROXY_57/REA_05_FIXTURES.json), [OS exit](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/OS_SIGNALED.json). Offline review проверяет согласованность **остановленного** опыта; его 13 положительных checks не являются native acceptance PASS.

## 1. Результат и граница

| Вопрос | Фактический результат |
|---|---|
| Создан ли отдельный Actor? | VERIFIED NATIVE: сектор259 содержит исходный Puppet и один обычный Actor, разные pointer/node/vtable |
| Выбранный тип | `Obj_Ramses_Body_Rest`, actorType **1007**, stock `kexSlaveActor` |
| Был ли он реально виден? | **Не подтверждено.** На сохранённом игровом кадре дополнительный sprite не установлен |
| Стабильность ≥120 Actor ticks | **UNKNOWN**: 0 зарегистрированных Actor Tick samples; фактическое число не измерено |
| P1 | Исходные Player/node/Puppet/owner, position/sector и health200 совпали до вызова и в failure snapshot. Управление/движение после spawn и inventory preservation не проверены |
| Remove / retirement | Native Remove не вызывался; deferred retirement и destructor completion UNKNOWN |
| Выход | VERIFIED NATIVE/OS: обычный WM_CLOSE, exit0, same-object retained+duplicate handles wait0; forced termination отсутствует |
| DOC-58 | **Не готово.** Сначала требуется закрыть DOC-57 |

Существующие M0, DOC-53/55/56 contracts не менялись. DOC-53 остаётся PARTIAL вне принятого follow-up; DOC-55 bounded PASS и DOC-56 trusted-client single-map bounded PASS сохранены. Wire v1.10, ENet, Session/MapReady, focus-byte lease+0x319 и Player input pipeline не редактировались. `world_synchronized=false`, native D2/reconnect/transitions UNKNOWN остаются прежними ограничениями.

## 2. Стенд, идентичность и исходники

Один KEX PID **32064**, launch primary / attested simulation TID **32172**. Actual image base **0x140000000**, подтверждена scout и probe; runtime VA всегда вычислялся base+RVA. Отдельная принятая копия `experiments/m0/game`, свежий профиль `experiments/m1/native_visual_proxy/profile/N57_STEP0_01`; оригинальная Steam-установка использовалась только для чтения/хешей. [Launch](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/launch.json), [scout](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/scout.jsonl), [успешный isolation follow-up](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/ISOLATION_GATE_FOLLOWUP.json).

| Artifact | SHA-256 |
|---|---|
| `anubis_Shipping_Steam_x64.exe` | `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d` |
| Stock `PowerslaveEX.kpf` | `d34b63551713bf46c4215632d9b5f92b452791023937fe7d22a196bff9ff4bbd` |
| Исполненный `probe_bound.js` | `50b17518eb34d8916952be9a853978771d6f069d18ed412f7f9771cc18056e7b` |
| Исполненный `scout_bound.js` | `79d96525c2eb81e6d6009d0ab5ce1e2d14b91228b70f12987fc559ea6074f538` |

Новой DLL, native injector или нового Player context нет. Использован принятый M0/M11 suspended launcher, существующие isolation/window helpers и REA MCP 6.8.0 Frida attach/load/status/export/close. Новый [экспериментальный код](../experiments/m1/native_visual_proxy/README.md) вызывает настоящие KEX factory/Remove через typed Win64 NativeFunction из simulation-thread callback. **Remove path реализован в исходнике, но в KEX не исполнялся.** Exact Frida runtime version tools не сообщили; историческая версия из DOC-43 не подставлялась.

Существующий Ghidra проект не переимпортировался, auto-analysis не повторялся, REA configuration не исправлялась. Переиспользованы сохранённые exact-build Ghidra dossiers; критические аргументы/инструкции независимо прочитаны, PE bytes текущей копии сравнены с anchors. [STATIC_AUDIT](evidence/m1/NATIVE_VISUAL_PROXY_57/STATIC_AUDIT.json), [копии dossiers](evidence/m1/NATIVE_VISUAL_PROXY_57/static/), [runtime coordinates](evidence/m1/NATIVE_VISUAL_PROXY_57/RUNTIME_COORDINATES.json) содержат file offsets, prefixes, hashes и provenance. Имена функций — аналитические роли; псевдокод не является исходником KEX.

## 3. Actor Factory ABI — VERIFIED STATIC

```cpp
// Windows x64 ABI; opaque types, independently reconstructed contract.
Actor* SpawnActor(Factory* self, int32_t type,
                  const Vec3* position, float yaw, int32_t sector);
```

| Аргумент / return | Расположение |
|---|---|
| factory instance | RCX; current native inline object = base+`0x5a10a0`, vtable `0x14052c078`, slot+0x18 → factory |
| actorType | EDX; signed 32-bit key, здесь1007 |
| position | R8; pointer на три float XYZ |
| yaw | XMM3, scalar float; factory сохраняет в XMM6 и затем Actor+0x38 |
| sector | Пятый аргумент: `[caller RSP+0x20]` перед CALL, **`[entry RSP+0x28]`** внутри callee |
| return | RAX = Actor pointer или null в проверенных class-lookup failure branches; это не ObjectNode handle |

Точный prologue factory `0x1401d4780`: PUSH RDI/R14/R15 и SUB RSP,0x40 меняют RSP на **0x58**. Инструкция `0x1401d4adf`, bytes `8b942480000000`, читает `EDX,[RSP+0x80]`, что равно **entry RSP+0x28**, затем вызывает LinkSector. Это независимо подтверждает stack5; смещение caller/callee не смешивалось. Runtime callee-entry observer в этом run не сработал; его данные не вымышлялись.

Factory body использует глобальную таблицу definitions через RVA`0xaf3350`, key lookup `0x1bb140`, class resolution `0x0becf0`, class-specific pools через RVA`0xaf45f0`/count`0xaf45f8`. RCX после входа заменяется глобальными operands; это не разрешение передавать произвольный `self`.

Последовательность: definition/class lookup → allocate или take from class pool → native RTTI checks → ActorDef+0x170 → origin+0x18/+0x1c/+0x20 → normalized yaw+0x38, zero other rotation components+0x30 → **LinkSector** → virtual+0x38 **Spawn** → RAX Actor. LinkSector создаёт/регистрирует ObjectNode; base Spawn регистрирует interpolants и выбирает spawn animation. Frame actions потенциально выполняются при выборе animation, поэтому отдельно проверен конкретный stock ресурс.

Предусловия: загруженные definition/class/pools/world, валидный сектор, simulation thread, stable gameplay и отсутствие teardown. Type0 дополнительно требует Puppet class и при несоответствии ведёт в fatal path. Range/RTTI violations ведут в native assertion, не в безопасный error object. Проверенные null branches не являются общей гарантией graceful allocation failure.

Factory владеет allocation/pool lifecycle; caller получает non-owning Actor pointer без автоматически удержанного ObjectNode reference. `Actor+0x10` и `ObjectNode+0x28` — отдельная identity. Адрес recycled node не является generation ID. Никаких AddRef/DecRef/raw pointer repairs мод не делает.

## 4. Критические native seams

Все строки относятся к exact EXE SHA выше. Actual runtime base этого run140000000, поэтому preferred и actual VA численно совпали; вывод actual VA для **неисполненных** functions не означает, что они были вызваны.

| Роль | RVA | Actual runtime VA | ABI / контекст / побочные эффекты |
|---|---|---|---|
| Factory SpawnActor |1d4780|1401d4780|RCX/EDX/R8/XMM3/stack5; RAX Actor; world/pool/list/Spawn side effects; только Sim |
| Actor Spawn |1a02f0|1401a02f0|RCX Actor; useful return не установлен; init/release managed fields, flags/anim/floor/interpolants |
| Puppet Spawn |1dbc60|1401dbc60|RCX Puppet; полезный return не используется; **Player binding reassignment** |
| LinkSector |1d3d70|1401d3d70|RCX Actor, EDX signed sector; полезный return не установлен; native node creation/unlink/insert |
| Actor Remove |1a02b0|1401a02b0|RCX Actor; meaningful return не установлен; flags/removal/interpolants/deferred queue |
| Actor Tick |19f2d0|14019f2d0|RCX Actor; return не потребляется observer; animation, guarded physics/gameplay; only original native traversal |
| Common OnRemove |195000|140195000|RCX object; retirement callback, releases target/own-node state; не destructor |
| PoolReturn |1d5000|1401d5000|RCX factory, RDX payload; class-specific pooled storage; не delete/free Actor |
| DeferredSweep |1b6360|1401b6360|нет явных аргументов; ref0 callback/pool return/node retirement; Sim |
| DrainQueue |1b61d0|1401b61d0|нет явных аргументов; unlink current list, pending-list insert/transient refs release |
| StateTick |1b6ec0|1401b6ec0|RCX GameInstance; attested simulation owner; original исполняется один раз |
| Player Update |1dfe30|1401dfe30|RCX Player; только read-only observer, original не заменяется/не подавляется |
| StateExit |1b6670|1401b6670|RCX GameInstance; наблюдение неожиданного transition → stop; transitions не тестировались |

Инструкции и inclusive function extents — в соответствующих G_FUNC JSON из static/. Thread ownership функции не объявлялся безопасным из-за `LOCK`/atomic операций: эти операции защищают отдельные counters, не полный Actor lifecycle.

## 5. Раздельные layout и ownership

| Объект | Проверенные поля, используемые или разобранные здесь |
|---|---|
| GameInstance | embedded P1 Player+0x40; state+0x24, next+0x20, pending+0x28; current-map kexStr+0x1e0, mapInfo+0x1f8 |
| Player | managed Puppet node+0xd8; input+0..0x7f; weapon+0x88/pending+0x8c, ammo+0xbc, owned+0xcc |
| Actor prefix | own node+0x10; XYZ+0x18/1c/20; target node+0x28; rotation+0x30, yaw+0x38; sectorIndex int32+0x3c; removal byte+0x40 |
| SlaveActor | animation+0x108; scale+0x124; sourceOwner node+0x148; flags u32+0x16c; ActorDef+0x170; health int16+0x17a |
| SlavePuppet | отдельная vtable `0x14052d198`; owner Player+0x1b8. Это поле **не читалось у ordinary Actor** |
| ObjectNode | prev+0, next+8, callback+0x10, in-use byte+0x24, payload+0x28, refs int32+0x40 |
| Sector array | pointer RVAa45968, count qword RVAa45970, stride0x88; actor-list head pointer+0x78 |

Actor removal byte+0x40 и ObjectNode refcount+0x40 принадлежат разным объектам. Actor не содержит доказанных intrusive sector next/previous по тем же смещениям: links находятся в ObjectNode. Actor interpolant registration/unregister использует nested objects+0x50/+0x78 и registry RVA63d368; они не перезаписывались. Полный C++ layout/размер ordinary Actor не объявляется восстановленным.

```text
GameInstance+40 → original P1 Player
    Player+d8 → original Puppet ObjectNode → payload+28 → original Puppet
    original Puppet+1b8 → original P1 Player

native Factory/pool → ordinary Actor1007 ↔ own ObjectNode
                                         sector259 native list
experimental observer: non-owning identity only, added refs = 0
```

Puppet Spawn1dbc60 после base Spawn вызывает GI virtual+0xa8, заменяет `Player+d8` новым Puppet node с native refcount operations и пишет `Puppet+1b8=Player`. Это независимо подтверждает запрет ordinary `SpawnActor(0)`. Base Actor Spawn1a02f0 в проверенном теле этого singleton path не содержит; resource callbacks проверяются отдельно.

## 6. Sector Link и фиксированная позиция

LinkSector валидирует signed sector через unsigned range check. При null Actor+0x10 вызывает node allocator1d5820, устанавливает refs0/in-use1, native callback1d3230/payload Actor и вставляет node после leading sentinel. При существующем node и другом секторе сам выполняет unlink/insert; при том же sector пропускает манипуляции list, затем записывает sectorIndex. Ручного unlink, sectorIndex write или дополнительного LinkSector после factory не было.

Фиксированная позиция **[-7420.8037109375, -6848.013671875, 1152]**, сектор **259**. Это ранее реально занятая grounded P1 точка C22: [status_07_after_close.json](evidence/m0/C22_DIRECTED_PHYSICS_FINITE/status_07_after_close.json), `structuredContent.normalized_result.messages[159].payload.primary`, C_alive_sample, TID13316, health200/velocity0; повторные samples162/172/173. Это не произвольное P1+150.

Новый scout на Sim32172 независимо прочитал текущую Tomb geometry: sectors270, faces2169; sector259 faces2083..2089. Fixed point удовлетворяет всем прочитанным native planes:

```text
x +7552 = 131.196289; -x -7296 = 124.803711
-y -6656 = 192.013672; +y +6912 = 63.986328
-z +1664 = 512;       +z -1152 = 0
```

Полупространства подтверждают принадлежность сектору. Initial list содержит только P1/count1; оба sentinel/prev/next проверены scout. Failure snapshot после factory содержит count2 и отдельный Actor в этом секторе, но полный post-spawn list-invariant checker не был достигнут.

**Важный незакрытый gate:** проверка `sin(yaw)*dx + cos(yaw)*dy > 0` была предположением о camera-forward convention. Native camera view/projection basis и полный FOV этим не подтверждены. Геометрически допустимая точка не доказана как видимая из текущего вида. Это placement/visibility UNKNOWN, а не подтверждение неисправности Renderer.

## 7. Кандидат и flags

[Точные stock ресурсы](evidence/m1/NATIVE_VISUAL_PROXY_57/resources/) прочитаны из текущего KPF, не из названия объекта:

- `defs/defines.txt:91`: `Obj_Ramses_Body_Rest=1007`.
- `defs/actors/misc.txt:129–139`: `classname kexSlaveActor`, radius16, height32, stepHeight0, scale1.5, spawnAnim `misc/ramses_body_rest`, fullbright1. Solid/shootable/moveable/touchable/AI/pickup/script/map-destination properties отсутствуют.
- `sprites/misc/ramses_body_anim.txt`: одна frame, пять sprite segments, без action/script/sound callbacks. `ramses_body_rest_sprite.txt`/PNG stock, новые assets не добавлялись.
- `Obj_Debris=123` отвергнут как менее подходящий: moveable/randomizeFrames/noAdvanceFrames.

| Маска | Независимый результат |
|---|---|
| solid0x1 | VERIFIED STATIC: definition→Actor mapping/collision consumer; не выставлена у наблюдённого Actor |
| shootable0x20 | VERIFIED STATIC: definition mapping/damage consumer; не выставлена |
| moveable0x80 | VERIFIED STATIC: Def0x40→Actor0x80; Tick VA14019f540 bytes`40 84 ff 0f 89 88 08 00 00`, TEST DIL,DIL/JNS |
| fullbright0x40 | VERIFIED STATIC: parser OR Def0x20 → base Spawn Actor0x40; VERIFIED NATIVE observed flags64 |
| hidden0x2000 | VERIFIED STATIC: parser `hidden` sets Def0x800, Spawn maps to Actor0x2000; фактический sprite-hide timing не тестировался |
| Gemini moveable0x80000000 | **REFUTED** для проверенного Tick condition |
| Gemini hidden0x01000000 | **REFUTED**: в приведённом path этот bit управляет scale multiplication |

Flags не очищались и не записывались модом: ожидался stock0x40. Static passive class/frame/flags подтверждают пригодность кандидата для bounded опыта. Они не заменяют отсутствующие native collision/callback/control checks.

Gemini `RenderSectors1a2580` / `DrawActorBillboard1a34c0` не использованы как доказанный sprite renderer. Проверенный код1a2580/1a34c0 обхода geometry/отрисовки XY-сегментов соответствует **INFERRED automap/debug path**; в cited actor branch нет доказанного texture/sprite submission. Поэтому его invocation не был бы Visual PASS. Настоящий sprite-submission seam остаётся UNKNOWN.

## 8. Remove / deferred lifecycle — только VERIFIED STATIC

Remove1a02b0 меняет `flags=(flags & 0xfffffc5d)|0x2004`, обнуляет Actor+0x110, вызывает1a0f20 (его полная семантика здесь UNKNOWN) и передаёт управление1d3460. Последний выставляет removal byte+0x40, unregister interpolants+0x50/+0x78 через0xa7f10 и ставит own node в GI virtual+0xb0/1b5530 с native queue reference.

В StateTick **Sweep1b6360 находится до DrainQueue1b61d0**. Drain снимает node с текущего списка и переносит в pending list; sweep использует два pending heads со сменой поколений списка. Нельзя требовать синхронного unlink/retirement в момент Remove.

При refs0 sweep вызывает virtual+0x28 **OnRemove**, затем factory virtual+0x30/PoolReturn1d5000, очищает node.payload+0x28, unlink pending node и in-use+0x24. Native class-specific Actor storage сохраняется в пуле. OnRemove195000 не является deleting destructor; он очищает target/own node, но не доказан как release sourceOwner+0x148. Принятый [M0 lifetime correction](evidence/m0_static_lifetime/RESULT.md) отделяет его от Actor destructor19f210/deleting destructor19f1d0 и world teardown.

**REFUTED:** «sweep virtual+0x28=destructor», «pool return=физический free Actor», «Remove мгновенно доказанно убирает sprite указанным renderer». В DOC-57 нет runtime Remove/retire witnesses. Чистый process exit не является доказательством выполнения конкретного destructor.

## 9. Единственный native run и остановка

| UTC | Событие |
|---|---|
|09:31:47|Принятый launcher создаёт единственный KEX32064 suspended; свежий профиль |
|09:32:44|OS observer24148 удерживает handle0x2d4 и duplicate0x2d8, CompareObjectHandles=true; wait258/code259 |
|09:32:52|Scout loaded, actual base140000000; только read-only world/Player/list observations |
|09:33:41|Sim32172: Tomb state/next/pending3/3/0, mapInfo/key/PSM1/counts71/28608; P1 start/ownership; fixed point inside; list count1 |
|09:38:12|После unload scout загружен один `probe_bound.js`; prefix gates прошли; mailbox только в собственной памяти Frida |
|09:42:53.789|Sim32172, probe tick16883: spawn_preflight; один nativeSpawn(factory,1007,pos,0,259); затем `blocked: Error: native factory failure` |
|09:43:04|Настоящий игровой кадр после попытки — дополнительный sprite не подтверждён |
|09:45:48.299|Одноразовый failure witness без game calls/writes: ordinary Actor найден; original P1 сохранён в перечисленных endpoint fields |
|09:45:48.569/.732|Обычный WM_CLOSE → launcher exit0 → retained process wait0/code0, duplicate wait0; observer closes both handles |

Failure witness:

| Identity | Native value |
|---|---|
| Extra Actor |`0x61a21c60`, vtable`0x140528608` |
| Extra ObjectNode |`0x61a13950`, refs0 |
| Actor position/sector/flags |fixed point выше /259/0x40 |
| ActorDef / animation |`0x618649f8` / `0x5ce8a378` |
| Original P1 |Player`0x140af33e0`, node`0x61a151b8`, Puppet`0x6186ed10`, owner`0x140af33e0`, health200 |

Этот failure snapshot сделан **Frida diagnostic TID31168**, не simulation-thread attestation. Он подтверждает существование отдельного объекта в наблюдённом мире; не заменяет непрерывное tracking/lifetime или P1 control test.

### Причина false block

Probe потребовал `factoryWitness` от отдельного Interceptor listener внутри вызова NativeFunction из StateTick.onLeave. Вложенный listener не сработал; код проверил этот дополнительный predicate **до сохранения/log returned pointer**, остановился и не выполнил status/Actor Tick tracking/Remove. Поэтому `spawnCount:0` в blocked journal — счётчик недостигнутого success marker, **не доказательство нулевого native создания**. Read-only list после вызова доказывает противоположное.

Отдельные [benign Python/CModule fixtures](evidence/m1/NATIVE_VISUAL_PROXY_57/TOOL_FIXTURES.json) изолировали coverage issue: top-level NativeFunction→hook1; тот же call внутри Interceptor.onLeave → hook0 при return42; `traps:'all'` также hook0. Они не запускают KEX и не являются mock-доказательством Actor PASS. Это подтверждает дефект observer gate; точный пропущенный RAX KEX вызова восстановленным не объявляется.

Ни возврат управления эксперименту, ни повторный spawn, ни попытка Remove через заново найденный pointer не выполнялись. Код остался загружен до normal process exit; никаких прямых delete/free/destructor/refcount/pointer repairs. Failed bound script сохранён неизменным; reusable production acceptance для него отсутствует.

### Frame evidence

| До native call | После native call |
|---|---|
|[Game frame09:42:35](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/window_20261103_094235_233518.png)|[Game frame09:43:04](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/window_20261103_094304_434253.png)|

Кадры получены существующим owned-window PrintWindow helper, без ImageGen или редактирования. Ненулевой animation pointer/правильная vtable не переименованы в visual proof. Позиция/проекция, sprite render conditions и причина отсутствия видимого объекта UNKNOWN.

## 10. Acceptance gates

| Gate | Статус |
|---|---|
|1 Exact binary/ABI|VERIFIED STATIC + measured runtime base; callee-entry argument observer отсутствует |
|2 Factory creates object|VERIFIED NATIVE отдельным node/list snapshot; прямой RAX не записан |
|3 No Player singleton use|VERIFIED STATIC для base Actor/этого ресурса; endpoint native graph сохранён |
|4 P1 ownership unchanged throughout|Endpoint VERIFIED NATIVE; непрерывный invariant/control proof UNKNOWN |
|5 Valid sector|VERIFIED NATIVE geometry и presence259; complete post-spawn list checker не достигнут |
|6 Visible in real world|**Не выполнен** |
|7 No physics/combat/story influence|Static passive properties + native flags40; полный bounded interaction/callback proof UNKNOWN |
|8 Stable ≥120 ticks|**UNKNOWN**, 0 recorded Actor Tick samples |
|9 Normal Remove|**Не выполнялся** |
|10 No observed native lifetime/affinity faults|Crash/OS failure не наблюдались; recording stopped on observer defect, полная lifetime acceptance отсутствует |
|11 P1 works after Remove|**Не тестировалось** |
|12 Clean OS exit0|**VERIFIED NATIVE/OS PASS** |

Общий verdict **BLOCKED**, не Visual PASS и не network-ready foundation.

## 11. Регрессии и воспроизведение

Все **15** существующих test commands заново выполнены перед KEX, в новых рабочих каталогах Evidence. Никакого ENet во время native game run; loopback ENet использовался только отдельными существующими regression programs после их старта до KEX. Это новые результаты неизменённых accepted regression binaries, не исторические логи и не проверка новой DLL. [Полные argv/cwd/exit/SHA](evidence/m1/NATIVE_VISUAL_PROXY_57/REGRESSIONS_20261103_093001_316289.json), [сводки фактических stdout](evidence/m1/NATIVE_VISUAL_PROXY_57/REGRESSION_SUMMARIES.json).

| Test | Новый результат |
|---|---|
|pse_test_suite|46/46, без timer fixture |
|pse_stress_test / pse_fault_injection / pse_bridge_acceptance|11/11 /16/16 /11/11 |
|DOC46 r1_live + routing_contract|not reproduced /330 checks,5 cases |
|DOC47 r2_fixed r2 + admission_contract|PASS /112 checks,4 scenarios |
|DOC49 timeout_contract|failures0 |
|DOC50 sequence_contract + sequence_live|282/0 +12/0 |
|DOC53 background_contract|37/0 |
|native_client_contract|134/0,3 scenarios; count timing-dependent, raw output retained |
|snapshot_contract|42/0 |
|map_ready_contract|210/0 |

Нового C++ build нет: experimental source — Frida JS/Python; preflight `node --check`/Python compile и независимый static/resources audit. Existing foundation C++/DLL/ENet не пересобирались. Tool-only fixtures лежат отдельно и не влияют на verdict.

```text
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/workflow.py ready
# Выполненный REA spawn argv, program=C:\Python314\python.exe:
-X utf8 -B <absolute>/experiments/m1/native_visual_proxy/runner.py launch N57_STEP0_01
# Далее bind → isolation+scout → accepted resume/UI → full export/extract → gate.
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/runner.py bind N57_STEP0_01
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/runner.py gate N57_STEP0_01
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/runner.py command N57_STEP0_01 1
# REA load command_1.js; после false block — только failure witness и ordinary close.
C:\Python314\python.exe -X utf8 -B experiments/m1/native_visual_proxy/review.py
```

Это описание истории, **не инструкция повторно привязаться к старому PID**. Archived failed probe нельзя считать готовым к новому run. Полный accepted launcher/isolation API не переписывался. Старый `resume --stop`/TerminateProcess не использовался.

## 12. История, cleanup, изменения

[BASELINE](evidence/m1/NATIVE_VISUAL_PROXY_57/BASELINE.json):16 368 historical/source/build files. [Итоговая integrity](evidence/m1/NATIVE_VISUAL_PROXY_57/INTEGRITY_FINAL.json), [changed files](evidence/m1/NATIVE_VISUAL_PROXY_57/CHANGED_FILES.json), [source diff](evidence/m1/NATIVE_VISUAL_PROXY_57/SOURCE_CHANGES_FINAL.patch), [final file hashes](evidence/m1/NATIVE_VISUAL_PROXY_57/FILES_FINAL.json). Исходные принятые DOC-53/55/56 и их Evidence не переписывались. Git repository отсутствует; поэтому используются baseline hashes, before copies индексов и unified source diff.

Добавлены только DOC57 lab/report/Evidence и новый disposable profile; в существующих документах обновлены только PROGRESS/EVIDENCE_INDEX. По отдельному [harness error ledger](evidence/m1/NATIVE_VISUAL_PROXY_57/HARNESS_ERRORS.md) сохранены начальные tool/shell, missing-dossier, export/gate, offline-review и critical observer ошибки.

[OS handle identity](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/OS_HANDLE_IDENTITY.json), [OS signaled](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/OS_SIGNALED.json), [observer handles closed](evidence/m1/NATIVE_VISUAL_PROXY_57/N57_STEP0_01/OBSERVER_CLOSED.json), [sessions/process cleanup](evidence/m1/NATIVE_VISUAL_PROXY_57/SESSIONS_CLOSED.json). Все13 native-run/helper REA sessions и2 benign fixture sessions закрыты; final process list не содержит KEX или task-owned launcher/watcher/fixtures. REA close возвращает `target_state:unknown`; exit0 доказан отдельными OS handles, не этим полем. Worker ENet в KEX не создавался.

Final Frida capture224messages, script errors0, not truncated. Последняя periodic isolation summary **до spawn**:531 write-opens, allowed_disk14, denied158, device exemptions359, errors0, overflow0. Её нельзя выдавать за непрерывный terminal coverage. Protected before/after и final file integrity проверяются отдельно; guard остаётся bounded observer, не полным OS sandbox.

## 13. Минимальный handoff

Для следующего отдельного **DOC57 continuation**, до ещё одного KEX run:

1. Устранить зависимость success predicate от заведомо подавленного nested listener. Сразу записывать результат native call и безопасную weak identity до дополнительных telemetry checks; не подменять missing listener «factory returned null». Benign fixtures выше — воспроизводимый regression для этой ошибки. `traps:'all'` само по себе не решение.
2. Независимо подтвердить native view-direction/FOV и fixed placement либо настоящий sprite-submission seam. Valid sector/animation pointer недостаточны; camera synchronization/hooks не нужны.
3. В новом отдельно разрешённом single-process run подтвердить actual visible frame, ≥120 native Actor ticks, P1 movement/health/inventory/callback/list invariants, один native Remove, unlink/OnRemove/pool/node retirement, P1 after Remove и normal OS exit.

**DOC-58 сейчас не начинается.** Проверенные static seams: ordinary factory1007/LinkSector ownership, fields/flags и раздельный deferred contract. Native seam доказан пока только как creation/presence отдельного passive Actor. После полного DOC57 PASS возможный минимальный будущий scope — Host P1 Snapshot → Client B → passive Actor visual position update; перед ним потребуются valid placement/visibility, безопасный owned lifecycle, transform/interpolant update contract и map-lifetime guards. ENet binding, snapshots application, sector crossing, interpolation и Camera Synchronization в этом задании отсутствуют.
