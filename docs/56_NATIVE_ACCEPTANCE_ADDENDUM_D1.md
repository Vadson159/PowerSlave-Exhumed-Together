# DOC-56 — Native Acceptance Addendum (attempt 3): D1 PASS

2026-11-03 UTC. После восстановления Frida пользователем REA MCP загружен в сессии. **Native D1 PASS. Native D2 UNKNOWN. DOC-56 принят как bounded PASS для trusted-client single-map сценария** (Tomb of Ramses, один Host + один Client, Wire v1.10). Исходный [DOC-56](56_NATIVE_MAP_IDENTITY_READY_GATE.md) (PARTIAL) и [addendum attempt 2](56_NATIVE_ACCEPTANCE_ADDENDUM.md) (preflight BLOCKED) сохранены как исторические результаты без правок.

Evidence: [D1 summary (36 checks)](evidence/m1/NATIVE_MAP_READY_56/D1_RESULT_SUMMARY.json), [full correlation](evidence/m1/NATIVE_MAP_READY_56/D1_CORRELATION.jsonl), [Host journal](evidence/m1/N56_HOST_01/native.jsonl), [Client journal](evidence/m1/N56_CLIENT_01/native.jsonl), [engine LoadMap witness](evidence/m1/NATIVE_MAP_READY_56/ENGINE_LOADMAP_WITNESS.json), [REA bundles](evidence/m1/NATIVE_MAP_READY_56/rea_bundles/), [harness errors](evidence/m1/NATIVE_MAP_READY_56/HARNESS_ERRORS_D1.md), [D2 UNKNOWN](evidence/m1/NATIVE_MAP_READY_56/D2_UNKNOWN.json).

## 1. Preflight

REA MCP 6.8.0: `list_frida_devices` → local; spawn/attach/load/status/export/close работали. Использован принятый DOC-55 поток с DOC-56 обёртками в `experiments/m1/native_map_ready/` (`runner.py`, `watch.py`, `input_fixture.py` — exec принятых DOC53F исходников с заменой имён/манифеста; `wait_native.py`; `rea_extract.py`; `map_witness.js`; `review.py`). Новый injector не создавался: DLL загружает принятый `load_native.js` → `M11Prepare`. Launcher'ы порождены REA spawn (как в DOC-55); UI/resume/close/input хелперы запускались из Bash на проверенном `WinSta0\Default`. Большие Frida-наблюдения сохранялись через `frida_session_status(summary)` + `export_evidence_bundle`; `rea_extract.py` кладёт неизменённые записи в run-папки в конверте, который читает принятый gate.

## 2. Native map identity (runtime, оба процесса)

Exact EXE SHA `b8f131…a76d`, image base `0x140000000` в обоих процессах. Три независимых источника совпали:

| Источник | Host 38192 | Client 40352 |
|---|---|---|
| DLL reader на Simulation thread (`map_identity_observed`) | TID 9452: `maps/TOMB.MAP`, len13/cap32, mapInfo key `maps/TOMB.MAP`, vtable ok, PSM1, counts 71/28608 | TID 25624: то же |
| Read-only Frida witness (`n56_map_witness`) | state/next/pending 3/3/0; kexStr data heap `0x2eba1d0`, bytes `6d6170732f544f4d422e4d415000` (NUL ровно на length); `+1f8`→key `maps/TOMB.MAP`; level vtable `0x14052b478`, slot1 `1c7b70` LoadMap, slot2 `1c90e0` Unload | то же, data `0x2ebc5a0` |
| Engine stdout | `kexSlaveLevelLocal::LoadMap: Opening maps/TOMB.MAP...` | то же |

До gameplay (Client, меню, state 1/1/0): kexStr length 0/capacity 32 (тот же буфер, позже заполненный), mapInfo `0x0`, level counts 0/0 — identity отсутствует, gate отказал бы. Статический layout подтверждён; GameInstance не модифицировался (witness: writes 0). Расхождений static↔runtime не найдено.

## 3. D1 — проверенная последовательность

Host PID 38192 / Sim TID 9452 / Worker TID 34256; Client PID 40352 / Sim TID 25624 / Worker TID 39540. Отдельные копии игры (`experiments/m0/game`, `native_second_instance/game`) и свежие профили `native_map_ready/profile/N56_*`. Isolation gates PASS (Host 530 write-opens/158 denied, Client 532/158, errors 0, overflow 0).

| UTC 2026-11-03 | Событие |
|---|---|
| 01:24:08.553 | Host Sim: `map_identity_bound` `maps/TOMB.MAP`, epoch **3718259626** (BCryptGenRandom), tick 4551; listener 28745 |
| 01:25:35.450 | Client: Welcome slot1/gen1/epoch 3718259626, host_map = local_map = `maps/TOMB.MAP` |
| 01:25:35.467→.951 | Client stable ticks 1→30 (ticks 2737→2766), fault none на каждом |
| 01:25:35.951 | Client: `client_map_ready_queued` на tick 2766 (тот же tick, где достигнуто 30) |
| 01:25:35.954 | Client Worker 39540: первая реальная UDP INPUT отправка (seq1, result0) — input cadence открывается только веткой отправки MapReady |
| 01:25:35.956 | Host Sim: `network_ready` map_ready_accepted, slot1/gen1/epoch, `client_attested_epoch_echo_v110`; rejected 0 |
| 01:25:36.063 | Host: `snapshot_eligibility` true (после acceptance) |
| 01:25:36.065→.084 | Первый P1 snapshot: capture → Worker → UDP send → UDP receive → Client Simulation |

Особая проверка: 30 stable ticks и `localThisTick==1` не противоречат друг другу — MapReady реально отправлен на том же tick 2766, где state стал `identity_verified`.

Результаты (все 36 проверок PASS): 1266 captured = 1266 Host Worker = 1266 OS UDP send = 1266 OS UDP receive = 1266 Client Worker = 1266 Client Simulation accepted (633/633 по slot), 0 rejected, 1266/1266 побитных совпадений по всей цепочке, монотонность по slot. Ни одного capture до eligibility. W/S: 43 forward (buttons4) + 43 back (buttons8) native P2 updates из 1746 уникальных, 194 различных позиции P2; 1267 принятых INPUT имеют реальный UDP provenance. Во всех P2 updates P1 до/после равны, 1 native command на update. Snapshot/socket события только на Worker TID, map/ready события только на Sim TID; SPSC affinity 0; native faults 0. Snapshots не применялись к клиентскому Player/Puppet (`player_writes:0`).

Shutdown: Client WM_CLOSE → Worker join/socket close → 1 exact restore → retained+duplicate handles wait0/exit0 (01:25:57.232); Host → Worker join → 4 exact restores → один exit Reset (handle null, P1 unchanged) → wait0/exit0 (01:26:05.526). Launchers exit0. TerminateProcess/emergency cleanup не применялись. Protected профили before = after. Все 5 REA Frida-сессий закрыты; игровых процессов и порта 28745 не осталось.

Ограничение доказательства: принятые socket-probes декодируют только INPUT и snapshot (0x20), не `MsgMapReady`; его транспорт подтверждён тем, что Host Worker создаёт `BridgeMapReady` только из пакета аутентифицированного peer, и временем первой INPUT отправки.

## 4. D2 — NATIVE D2 UNKNOWN

Свежий профиль в overworld даёт только Tomb of Ramses. Единственный штатный селектор другой карты — консольная `map <filename>` (`0x1401aeb10`), которая сначала вызывает `FUN_140107820` («Cheats have been enabled. All achievements will be disabled.»), то есть переводит экземпляр в cheat-режим и отключает достижения — против AGENTS.md; доступность консоли при `developer 0` не доказана. Копирование пользовательских saves и запись в память KEX исключены. Остаётся автономный mismatch-тест `map_ready_contract`.

## 5. v1.10

Host получает только epoch echo от аутентифицированного peer текущей generation; независимого подтверждения карты клиента нет. Защита от несовпадения держится на честной client-side проверке (имя, locked identity, 30 stable ticks). v1.11 не реализован.

## 6. Регрессии

Исправлений не потребовалось; production не менялся. Полный набор всё равно перезапущен после native run на тех же замороженных бинарниках (`workflow.py tests`, [REGRESSIONS_20261103_013004_356903.json](evidence/m1/NATIVE_MAP_READY_56/REGRESSIONS_20261103_013004_356903.json), все exit 0): map_ready_contract 210/0; snapshot_contract 42/0; pse_test_suite 46/46 (без timer fixture); stress 11/11; fault injection 16/16; bridge acceptance 11/11; background_contract 37/0; R1 not reproduced + routing 330; R2 PASS + admission 112; timeout_contract failures 0; sequence 282 + live 12; native_client_contract 135/0 (в предыдущем прогоне того же бинарника 136/0 — число проверок зависит от тайминга сценария, failures 0 в обоих).

## 7. Изменения

Production C++ и DLL не менялись (хеши BUILD_MANIFEST совпадают). Добавлены только harness/review файлы в `experiments/m1/native_map_ready/` (см. §1) и Evidence. Исторические Evidence не изменялись: [INTEGRITY_D1](evidence/m1/NATIVE_MAP_READY_56/INTEGRITY_D1.json) — относительно baseline 15 077 файлов изменены только 4 заявленных DOC-56 source files; original/copies/full original snapshot/protected profiles совпадают; frozen manifest без расхождений.

## Verdict

**DOC-56 bounded PASS** для trusted-client single-map сценария: native identity, non-constant epoch, Welcome, 30-tick MapReady, Host acceptance текущего slot/generation/epoch, eligibility только после acceptance, snapshot transport и native shutdown доказаны на двух настоящих KEX-процессах. Ограничения сохраняются: нет Host-side проверки карты клиента (v1.10), D2 UNKNOWN, transitions/reconnect/re-entry не проверены natively, world_synchronized=false. Следующий минимальный шаг — решение по v1.11 MapReady identity или native reconnect gate; DOC-57 не начинался.
