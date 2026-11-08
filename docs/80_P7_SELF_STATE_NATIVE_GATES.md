# DOC-80 — П7: состояние своего игрока с хоста

2026-11-05. **П7 / S2.2–S2.3 — PASS (bounded), режим В, поднят с Б**
(новые native apply/lifetime/damage gates и отдельный wire v1.12).
Принято независимым gpt-6-astra: [DOC-81](81_P7_INDEPENDENT_ACCEPTANCE.md),
[машинный вердикт](evidence/m1/PSE_TOGETHER_P7/P7_INDEPENDENT_ACCEPTANCE.json).
Этот документ — отчёт исполнителя; решение режима В принадлежит проверяющему.

EXE SHA-256: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
Прочитаны AGENTS, CURRENT_STATE, ROADMAP §§1/3/4.1/4.2, DOC-03, DOC-05,
M0_TO_M1_HANDOFF; переиспользованы DOC-55/56, П3/П4 и ADR-1 DOC-62/77.
Принятые исследования и runs не повторялись. Новые runs относятся только
к П7 и разным исправленным условиям кандидата.

## 1. Результат исполнения

| Задача | RUNTIME OBSERVED в RUN03 | Граница |
|---|---|---|
| S2.2 health | Host P2: 200→183→83→103→−97; клиентское native readback совпало | Последний переход — смерть; респауна нет |
| Ammo / weapon | Все семь ненулевых ammo pools изменены; выбраны native weapons0…7, ownership1→255; обратные7→4→3→0 | Безопасные dry presentation paths, без удержанной атаки при forced switch |
| Keys | После независимых gates:0→15→0→5; mask16 отвергнута без изменения | Свежая GetKeys reference, два байта, Sim thread, текущая жизнь |
| Inventory | Artifacts0/1/63, abilities0/1/3, quest0/1/255, ankhs/count0/1/5 с mask0/1/31, dolls0/1/31, затем отзыв | Stock scalar domains; achievement OnTouch/выдача не вызывались |
| Death | Один native death apply, death_sequence1, повторные dead snapshots без второго события | Death→transition закрывает admission; П16/П17 не выполнены |
| S2.3 | Четыре вызова native common damage entry своему клиенту, kind0…3/amount25:HP183→183 | Натуральные падение/вода/forcefield и все weapon damage sources отдельно не испытывались |

[Execution audit](evidence/m1/PSE_TOGETHER_P7/RUN03_EXECUTION_AUDIT.json):
**40/40**, все **842/842** commit/readback сопоставлены с конкретными snapshot_seq
и server_tick host native scalars. Значения не брались из defaults worker.
Перед/после каждого host fixture и native P2 update проверялся P1.
[HUD](evidence/m1/PSE_TOGETHER_P7/P7_CLIENT_03/window_20261105_012056_998714.png)
показывает нож, ключи и HP после host heal. Это corroboration, не пиксельная
проверка каждого значения; точность доказывает native readback перед renderer.

Область: два KEX-процесса на одном Windows/QPC, TOMB, один remote Full Player,
одна native incarnation. Cross-PC clock mapping, весь S2, position reconciliation,
общий мир, внешний LAN/Radmin и П8/П9 не закрыты.

## 2. Код и протокол

- `src/session/self_state_p7.hpp`: scalar DTO/stock ranges, opaque identity/
  reset/epoch, ingress/application watermarks; один pending с deadline500ms и
  lease1s, sticky disconnect/barrier, reentrancy fencing, read→apply→readback.
  NativeCapabilities по умолчанию закрыты. Standalone draft PS7S102 bytes —
  только codec этого контракта, не сетевой player record.
- `src/wire112/`, `cmake/p7_wire112.cmake`: отдельные core/session/worker/
  bridge для **v1.12 (0x0112)**. Player record121 bytes (65+56); datagram одного
  player145, двух266, восьми992. Ammo8, keys/inventory, pending, stateTimeNs,
  reset/discontinuity включены явно. Legacy ammo обязан совпадать с ammo
  выбранного оружия. Строгие peer/epoch/channel/version/EOF/finite/range checks.
  Downgrade нет. v1.10 и identity-only v1.11 неизменны; отдельная версия по D10.
- `src/native/host_p7.cpp`: отдельный target, реальные post-world scalars обоих
  Players; pending==current кодируется−1. Death sequence растёт один раз,
  dead P2 больше не получает controller input, но Puppet tick сохраняется.
  Exports — только mailboxes, engine calls на Sim с P1 before/after guards.
- `src/native/self_native_p7.{hpp,cpp}`: настоящий KEX INativeState, exact disk
  SHA/prefix gates,14hooks, reciprocal graph/heap allocation sequence.
  `client_p7.cpp`+`client_self_p7.inc` связывают adapter с authenticated own
  slot и завершённым baseline admission. Новая жизнь, утрата lease/graph/карты,
  disconnect/unload закрывают текущую capability без автоматического rearm.

Targets `pse_dll_host_p7`/`pse_dll_client_p7` отделены от принятых DLL; имена
файлов DOC-51 сохранены, каталоги свои. P7 client не создаёт remote Full Player
или proxy. Текущий input prototype пересылает W/S; полноценный combat input
этим пакетом не заявляется.

## 3. Native contracts

Все адреса относятся к указанному SHA; preferred VA=0x140000000+RVA. File offset,
prefix и Evidence отдельно в исходном
[static manifest](evidence/m1/PSE_TOGETHER_P7/NATIVE_SITES.json), финальном
[23/23 pins](evidence/m1/PSE_TOGETHER_P7/FINAL_NATIVE_PINS.json) с PE-derived
file offsets и `sites[]` native adapter. Имена ниже аналитические, FUN/LAB Ghidra не являются
исходными C++ именами; декомпиляция — не исходник KEX.

| Операция | RVA | Win64 / применённая граница |
|---|---|---|
| Scalar pack / restore | 1b5b80 / 1b5ce0 | RCX=GI, RDX=48-byte scalar buffer; ammo/owned/inventory/pending; MOV-only restore |
| Health / difficulty | 1e0850 / 1ae6a0 | Player,int / GI; учёт native amount scaling и cap от ankhs |
| Damage / Puppet response | 1a1370 / 1dbd00 | victim,instigator,type,int16 amount; отрицательная разница и смерть в lexical hostEvent |
| Key reference | 1ae0c0 | Player→int16&; LEA RAX,[RCX+98];RET; пользовательское исключение после gate В |
| Weapon ready / advance | 20f9e0 / 210000 | Native lower→assignment→idle, max4096steps, без записи current/animation pointers |
| Mutable actor health | 194ac0 | Свой клиент получает DLL TLS shadow, game reference не изменяется |
| Direct health writer | 19f2d0 | Неподдержанный live flags1000 path закрывает adapter |

Client pack использует настоящий GI; переносится только48-byte scalar format,
сохраняется неизвестный scalar194, pending health обнуляется и проверяется.
Host fixture для P2 использует арифметический receiver P2−40: сохранённая полная
assembly pack/restore касается только receiver+c4…1d7 внутри реального Player84…197.
Это узкий контракт без fake vtable, whole checkpoint restore, копирования
игровых структур и записи указателей. Actual pending P2 сохраняются.

Weapon preflight допускает только проверенные definitions/frames/actions/delays
и no-action/sound/stop-sound/stop-tactile paths. Zero-delay stock bomb frames
разрешены по saved native advance. Fire/script/RNG/damage/Steam actions при
correction не исполняются. Held Attack и неподдержанные water branches закрывают
forced switch; это ограничение, не универсальная combat reconciliation.

Graph проверяет primary GI/Player, reciprocal node/Puppet/owner, exact Puppet
vtable, tagged heap header/size1e0/allocation sequence, Puppet destructor thunk1e1000
(Actor1007 d9980 неприменим). Wire не несёт адреса. Foreign thread сравнивает
только atomic weak identities, не разыменовывает игровые объекты. Trampolines
после exact restore pinned. Ref/target/SourceOwner/deletion flags не меняются.

## 4. Ключи: приёмка до записи

[Решение пользователя](evidence/m1/PSE_TOGETHER_P7/KEYS_DECISION_AND_GATE.md):
«Разрешить scalar write через проверенный native getter после gate режима В».
Исключение только для двухбайтового keys, без расширения прав на указатели,
структуры, checkpoint/world init или вызов pickup/Steam.

RUN03 сначала выполнил read-only probes, reference equality и writes0.
Независимый **gpt-6-astra**, не автор реализации, принял оба receiver/ABI/lifetime
gates: [270/270](evidence/m1/PSE_TOGETHER_P7/KEYS_GATE_ASTRA_RUN03_MACHINE_CHECKS.json),
[отчёт](evidence/m1/PSE_TOGETHER_P7/KEYS_GATE_INDEPENDENT_ASTRA_RUN03.md),
[host](evidence/m1/PSE_TOGETHER_P7/KEYS_GATE_ACCEPTED_HOST_RUN03.json),
[client](evidence/m1/PSE_TOGETHER_P7/KEYS_GATE_ACCEPTED_CLIENT_RUN03.json).
Затем mailbox permit(probe1,review2026110503) потреблён на Sim, capability открыта
и actual grant/revoke/readback выполнены.

Receipts привязаны к EXE/DLL, PID, epoch/generation, node/actor/allocation sequence
и log prefix; следующий run/rearm они не разрешают. Свежий getter каждый раз
проверяется заново, ссылка через тик не хранится. Попытка Opus CLI завершилась401
expired OAuth до проверки; сохранена и не повторялась. Это не review evidence.

## 5. Runs и сохранённые отказы

Перед каждым run exact EXE, свежие private profiles, isolation PASS,
REA MCP/Frida и принятый DOC-51 loader. Frida не вызывает KEX mutators:
управление — DLL mailboxes, input/capture — принятые OS helpers.

| Run | Условие и результат | Evidence в evidence/m1/PSE_TOGETHER_P7/ |
|---|---|---|
| 01 | До admission отказал неправильный Actor1007 dtor gate для Puppet. Apply/keys не выполнялись. Исправлено по прежнему Puppet ctor evidence | RUN01_FAILURE_AND_CLEANUP.json |
| 02 | Native health/ammo/keys/inventory/weapon1…3; switch4→3 отвергнут preflight. Установлены stock zero-delay и tactile-stop paths; остальные guards сохранены | RUN02_RESULT_AND_CLEANUP.json, BOMB_SWITCH_RESOURCE.json, SWITCH_DELTA_REVIEW_RUN03.md |
| 03 | Финальный frozen candidate:842exact matches,8weapons, grant/revoke/max inventory, damage4kinds, single death | RUN03_EXECUTION_AUDIT.json, raw P7_HOST_03/native.jsonl и P7_CLIENT_03/native.jsonl |

Смерть: records201935/201936 — один apply/commit HP−97/deathseq1; commits201987/
202109 повторили dead state. На tick56567 graph() отверг binding; в том же tick
`client_map_status` зафиксировал transition/invalidated, затем barrier/StateExit.
После barrier0native apply,14exact restores. Первичная причина конкретного graph
predicate **UNKNOWN**; native transition наблюдался, связь со stock death→gameover
**INFERRED**. Второй текст `unsupported_direct_health_flag1000` не доказывает bit:
его ветка печатается также при уже установленном fault latch. Guard не ослаблялся.
Респаун/admission после этого не заявлены.

Во всех3runs оба процесса штатно exit0; retained+duplicate handle wait0 сохранены.
RUN03: все15owned REA sessions
[закрыты](evidence/m1/PSE_TOGETHER_P7/REA_CLOSED_RUN03.json),
[REA bundle](evidence/m1/PSE_TOGETHER_P7/REA_RUN03_BUNDLE.json).
[Historical integrity](evidence/m1/PSE_TOGETHER_P7/INTEGRITY_AFTER_RUN03.json)
совпала с baseline, но её старый checker проверяет другую отсутствующую папку
PowerSlave Exhumed. Поэтому независимый проверяющий отдельно перехешировал
**все77originals,4файла реального Powerslave EX и Steam remotecache** относительно
обоих protected_before манифестов: [31/31 independent checks](evidence/m1/PSE_TOGETHER_P7/P7_INDEPENDENT_RUN03_MACHINE.json),
изменений нет. Исторический checker не выдаётся за проверку настоящих4saves.

## 6. Сборка и машинные проверки

- [OFFLINE_NATIVE_05](evidence/m1/PSE_TOGETHER_P7/OFFLINE_NATIVE_05/RESULT.json):
  **32/32 PASS**, scalar296/0, wire112252/0 (реальный ENet loopback двух workers,
  malformed scalar/version/EOF gates). Это не native runtime.
- Новая финальная сборка b81, /W4 /WX, MSVC19.44/SDK10.0.26100:
  [configure](evidence/m1/PSE_TOGETHER_P7/FINAL_BUILD_CONFIGURE.log),
  [build](evidence/m1/PSE_TOGETHER_P7/FINAL_BUILD.log),
  [equivalence](evidence/m1/PSE_TOGETHER_P7/FINAL_BUILD_EQUIVALENCE.json).
  Обе DLL=реально загруженные RUN03 по **7/7 masked PE sections** и exports.
  Raw SHA отличается build identity; runtime hash не подменяется.
- RUN03 host DLL SHA189549ec24b4d0722ed197d9417d61a4adac15d049195d71cc943e00d13b93cd;
  client3b175e6b7b84e0913e944c94638034916b2d60399ffc43b065136e3efb2f46a8.
  До запуска source/DLL/PDB заморожены в PRE_NATIVE_RUN03_snapshot;
  pre-native12/12 — только входной gate.
- Первые offline30/31 с2snapshot_contract failures сохранены в OFFLINE_01.
  Отдельный42/42 и следующие полные31/31,32/32 прошли; причина первого отказа
  UNKNOWN, доказанным flake не объявляется.
- Старый DLL_EQUIVALENCE.json b80/b79 host П4 4/7 сохраняет FAIL. Это чужая цель,
  не final P7 equivalence; причина не исследовалась вне П7.
- Первый execution audit ошибся схемами читателя: склеивал9повторов commit одного
  seq, wire LIFE_DEAD2 против scalar Dead1, имена полей local fixture/game wrapper.
  RUN03_EXECUTION_AUDIT_SCHEMA_ERRORS.json сохранён; исправлен только audit,
  все842raw commits сопоставлены. Журналы и загруженный код не редактировались.

## 7. Приёмочная область

Независимый проверяющий принял П7 и обе задачи: **31/31** собственных checks,
842совпадения capture→send→receive→commit,54native apply,4damage suppressions,
однаdeath/3deadreadbacks,0applyпослеbarrier,14exactrestores,77+4+1файлов
независимо перехешированы. Блокирующих замечаний нет; N1/N2 сохранены в DOC-81.

Для независимой приёмки: frozen artifacts, native sources, raw logs и receipts
выше, audit_p7_run03.py и свежая сборка. Начальный NATIVE_SITES.json с
runtime_observed:false — исторический static manifest, не актуальный runtime
результат, переписанный задним числом.

Натуральные local pickup/attack, held fire во время forced switch, все среды/
карты и все14hooks как вызванные gameplay paths отдельно в игре не подтверждены.
Guard paths изучены статически, неподдержанные состояния fail-closed.
Bounded PASS П7 не означает полный играбельный S2/M1, универсальный HUD,
cross-PC time sync, reconnect/rearm или новый respawn contract П16/П17.
