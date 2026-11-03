# DOC-55 — Native Player Snapshot Transport PoC

2026-11-03 UTC. **PASS ограниченного native transport PoC.** Один Host и один Client, одна проверенная Tomb of Ramses. Полный DOC-53 остаётся PARTIAL; [follow-up](53_BACKGROUND_SIMULATION_FOLLOWUP.md) завершён до этого этапа, [gate](evidence/m1/DOC53_FOLLOWUP/DOC55_GATE.json) принят отдельно.

**2 631 Client simulation samples обоих slot побитно совпали с настоящими Host native transforms и соответствующими OS UDP send/receive buffers. Оба KEX-процесса завершились штатно, retained и duplicate process handles signal0/exit0.** Это не World Replication: клиентские акторы, камера, HUD, interpolation, combat/campaign не изменялись.

[Финальная сверка](evidence/m1/NATIVE_SNAPSHOT_55/RESULT_SUMMARY_FINAL.json), [полная корреляция](evidence/m1/NATIVE_SNAPSHOT_55/CORRELATION_FINAL.jsonl), [accounting](evidence/m1/NATIVE_SNAPSHOT_55/DELIVERY_ACCOUNTING.json), [исходный PARTIAL analyzer](evidence/m1/NATIVE_SNAPSHOT_55/RESULT_SUMMARY.json), [ошибки стенда](evidence/m1/NATIVE_SNAPSHOT_55/HARNESS_ERRORS.md). 32 checks финальной сверки относятся к одному опыту, не к32 native runs.

## 1. Независимый аудит wire и существующего пути

VERIFIED SOURCE/OFFLINE: NET_PROTOCOL_VERSION0x0110/v1.10, MSG_PLAYER_SNAPSHOT0x20, MsgHeader11bytes, snapshot header13bytes, PlayerStateSnapshot65bytes. Одно сообщение с одним игроком89bytes; с двумя154bytes. `net_protocol.h` не изменён, SHA256 `8272aa3ef7bcd9e555e8a7b6bc12a1fe320b378dc34c8edbe6b38891e3bbac0f`. Gemini размеры подтверждены непосредственно codec и реальным Worker/UDP, не его SPEC ONLY checks.

Native Host сохраняет существующие per-player messages: P1 и P2 отправляются отдельно на Channel1. Channel3 остаётся world-snapshot каналом и в native PoC не выбран. ENet иногда объединяет два отдельных PSE messages в один196-byte UDP datagram; это не batching двух player records в MsgPlayerSnapshot.154bytes измерены автономно, native сообщение каждого slot было89bytes.

Обычный ENetClientNode имеет глобальный snapshot watermark. Но bridgeMode dispatch обходит этот обработчик и направляет snapshot в Worker/custom path. Mock simulation проверяет sequence per slot. Поэтому нельзя переносить глобальный duplicate сценарий на native bridge без анализа. PoC назначает уникальную возрастающую sequence каждому packet: P1 odd/P2 even; Client simulation Guard отдельно проверяет каждый slot. Дополнительно автономно проверен существующий двух-record packet с одной общей sequence: оба slot принимаются per-slot guard. Новый формат/batching не внедрены.

В BridgePlayerSnapshot добавлены internal mapEpoch, recipient connectionGeneration и targetSlot. На wire добавленных полей нет: epoch уже есть в MsgPlayerSnapshot; connection generation на приёме берётся из текущего authenticated ENet connection, на отправке проверяется target slot/current generation до enqueue. Старый queued DTO не переназначается новой generation. Entity generation — отдельный native binding lifetime counter; в этом run1/1, а не копия connection generation1. Автономный Worker test передал entity generations7/8 при connection1 и проверил stale route.

Snapshot receive проверяет wire version и Channel1 в ENetClientNode; Worker — deserialize, exact EOF, sequence/header agreement, nonzero epoch/current authenticated epoch, slots/IDs/nonzero generations/no duplicate slots. Simulation Guard требует READY+baseline+stable gameplay, matching epoch/connection, nonzero identity, finite transforms, per-slot monotonic sequence/tick и неизменную entity lifetime identity. Необъяснимый новый entity generation в этом ограниченном PoC блокирует adapter; respawn integration не заявляется.

## 2. Реальный, но ограниченный epoch/readiness contract

Host Simulation Thread привязал epoch **7365** к фактическому StateTick7365, когда после accepted60-update attestation стартовала первая Session текущего уровня. Это источник эпохи текущего bounded level/session, не константа1. До старта NETWORK существующий stored session-map mechanism получает domain DOC55_TOMB_RAMSES/epoch7365, поэтому даже ранний Welcome согласован. Далее используются существующие Welcome/MapChange/MapReady и пустой diagnostic baseline.

Оба свежих private profiles через native UI выбрали Tomb of Ramses; сохранены map-selection screenshots. На обоих simulation threads до permit/start проверены P1 graph, start[-7420,-6775,1152], sector259. Host logs snapshot_epoch_bound/network_ready, Client logs snapshot_client_map_witness/client_welcome/client_input_ready_queued подтверждают один tuple. Client WELCOME/READY и все принятые snapshots содержат7365. Не было map transition, auto-load удалённой карты или полноценного native MapReady contract. READY относится к ограниченному диагностическому domain; world_synchronized=false сохранено. General map epoch/transition/reconnect contract остаётся UNKNOWN.

## 3. Native reads, ABI и ownership

Exact EXE SHA256 обоих процессов: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Actual base обоих140000000: Host derived from attested GameInstance140af33a0 minus RVAaf33a0, Client explicitly logged image_base. Runtime addresses получаются только base+RVA; prefixes/hash проверены прежним M11Prepare/bootstrap. [Прежние координаты](evidence/m1/NATIVE_SNAPSHOT_55/REUSED_NATIVE_COORDINATES.json), [field contract](evidence/m1/NATIVE_SNAPSHOT_55/NATIVE_FIELD_CONTRACT.json).

| Reused native seam | Preferred VA | RVA | File offset | ABI / Evidence |
|---|---|---|---|---|
| StateTick |1401b6ec0|1b6ec0|1b62c0|Win64 RCX=this; existing detour; G_FUNC_1401b6ec0|
| Player Update |1401dfe30|1dfe30|1df230|Win64 opaque Player*; existing controller seam; G_FUNC_1401dfe30|
| Puppet Tick |1401db8a0|1db8a0|1daca0|existing observer, original call only; G_FUNC_1401db8a0|
| Commands |1401e34a0|1e34a0|1e28a0|existing input seam; G_FUNC_1401e34a0|
| Turning |1401e2ad0|1e2ad0|1e1ed0|existing input seam; G_FUNC_1401e2ad0|

Эти dossiers находятся в docs/evidence/ghidra_2022 и относятся к указанному binary SHA; декомпиляция не является исходниками. Новых KEX hook sites/API/ABI из Gemini Camera/Proxy не использовано. Heap member offsets не являются RVA/VA/file offsets.

Player+d8→node, node+28→Puppet, Puppet+10→node и Puppet+1b8→owner проверяются перед capture. Origin — float XYZ+18/+1c/+20; rotation X/Z+30/+38 передаются как raw native pitch/yaw; health int16+17a читается с native actor. Native source counter увеличивается при первом подтверждённом P1/P2 binding, entity IDs21760/21761 назначены модом, не являются адресами. In-place actor reuse/respawn в этом run не было; general generation registry не заявляется.

Capture выполняется на Host Sim37644 после возврата original StateTick/world traversal, только при actual P2 Update текущего tick, live owner graph, guards, READY, stable gameplay, no teardown и finite fields. Каждые два Host ticks формируются два scalar DTO с уникальными packet sequences. Puppet Tick вручную не вызывается; snapshot не меняет позицию/ориентацию. В каждом capture p2_updates==p2_puppet_ticks; все4625 P2 Update ticks уникальны. Client получает DTO на Sim30920 и пишет лог, не применяя transforms к своим Player/Puppet/camera.

Worker получает только скаляры, ни один KEX pointer в BridgePlayerSnapshot не передаётся. Его observers читают metadata DTO и пишут owned journal; passive OS probes читают только network buffers. Native log affinities подтверждены: Host Worker1976, Client Worker34552; no KEX memory reads/calls из Worker. Остальные snapshot fields имеют существующие DTO defaults и не являются принятой репликацией inventory/armor/weapon/velocity/life state.

## 4. Build и автономные проверки

[127 frozen sources / exact binaries](evidence/m1/NATIVE_SNAPSHOT_55/BUILD_MANIFEST.json), [8-file diff к принятому follow-up](evidence/m1/NATIVE_SNAPSHOT_55/SOURCE_CHANGES.patch), [autonomous gate/argv/stdout/hash](evidence/m1/NATIVE_SNAPSHOT_55/AUTONOMOUS_GATE.json). VS2022 x64 Release C++20 /MT, native targets /W4 /WX, PSE_DOC55_SNAPSHOT только для новых Host/Client DLL. ENet/MinHook pristine; timeout6ticks/150ms, focus-byte lease+319, original pause gates и existing input admission/sequence сохраняются.

```powershell
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/workflow.py baseline
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/workflow.py build
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/workflow.py tests snapshot_contract background_contract pse_test_suite pse_stress_test pse_fault_injection pse_bridge_acceptance r1_live r2_fixed routing_contract admission_contract timeout_contract sequence_contract sequence_live native_client_contract
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/freeze_gate.py freeze
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/freeze_gate.py gate
```

| Реально выполнено на DOC55 sources | Результат |
|---|---|
| snapshot_contract: codec/guards/real two-Worker UDP/route/framing |42checks/0fail|
| pse_test_suite |46/46, без timer fixture|
| pse_stress_test / pse_fault_injection / pse_bridge_acceptance |11/11 /16/16 /11/11|
| background policy/actual Session contract |37checks/0fail|
| DOC46 R1 + routing / DOC47 R2 + admission |PASS;330/112 checks; R2 argv r2|
| DOC49 timeout / DOC50 sequence + live |PASS;282 boundary/12 live|
| native-client transport contract |3scenarios PASS|
| wrong-process rejection обоих DLL |193, no log/hook/KEX call|

Initial test build errors и ошибочное analyzer event name сохранены. Assertions не отключались, timer resolution не менялся. Автономные42checks не заменяют native acceptance ниже.

## 5. Один настоящий Host→Client run

Host33340/Sim37644/Worker1976; Client39156/Sim30920/Worker34552. Separate77-file copies, new profiles under experiments/m1/native_snapshot/profile. [Host journal](evidence/m1/N55_HOST_01/native.jsonl), [Client journal](evidence/m1/N55_CLIENT_01/native.jsonl), обоих STATUS_EXIT.json и ISOLATION_GATE.json. Все source/DLL hashes проверены до загрузки. Host DLL `29f8497e9adc74a612e163ceabd49d40f20ccea5a89223014dc6dfdcb042460d`; Client DLL `306ac732bb10b3aee2ee67a36840c919040e6452e3dd2d50c8388b8c1a3894bb`.

Запуск/управление выполнялись accepted REA provider на Default desktop: start_frida_session spawn Python runner.py launch role fresh-run, resume parent; bind role/run, attach exact launch PID; load generated_isolation.js, load_native.js, os_probe.js и bound snapshot_os_probe.js; resume child через runner action; native UI/actual foreground; isolation/gameplay gate; mailbox-only permit. Все generated scripts, prefixes, target identities, source hashes и полные MCP results сохранены в соответствующих run folders и [REA bundle](evidence/m1/NATIVE_SNAPSHOT_55/REA_BUNDLE.json). Старые PID не использовались.

```powershell
# Provider-spawned Python argv (Default desktop); launch leaves child suspended.
-B experiments/m1/native_snapshot/runner.py launch host N55_HOST_01
-B experiments/m1/native_snapshot/runner.py launch client N55_CLIENT_01
# File-only bind/gate commands, before permitted gameplay instrumentation.
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/runner.py bind host N55_HOST_01
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/runner.py bind client N55_CLIENT_01
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/runner.py gate host N55_HOST_01
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/runner.py gate client N55_CLIENT_01
# Provider argv after both ready: ordinary controlled Windows input.
-B experiments/m1/native_snapshot/input_fixture.py client_inputs N55_HOST_01 N55_CLIENT_01
# Optional C orientation fixture failed at foreground request, before mouse input.
-B experiments/m1/native_snapshot/input_fixture.py mouse N55_HOST_01 N55_CLIENT_01
# Provider argv: normal native close, no TerminateProcess.
-B experiments/m1/native_snapshot/runner.py action client N55_CLIENT_01 ui close
-B experiments/m1/native_snapshot/runner.py action host N55_HOST_01 ui close
# Post-run correlation; no game execution.
& 'C:/Python314/python.exe' -B experiments/m1/native_snapshot/review.py final
```

| UTC | Runtime observation |
|---|---|
|00:00:35.658|Host epoch7365 bound to actual tick7365; original fixed-level witness|
|00:01:40.8xx|Matching READY, background P2 native Spawn, first post-world samples|
|00:01:40.882|Client Sim first P1 seq1/P2 seq2, both Host tick11128/epoch7365|
|00:02:09.505|Optional Host foreground request rejected; actual foreground remains Client; fixture stops before any relative mouse SendInput|
|00:02:24.618|Client stop begins; subsequent terminal seq2633/2634 retired during disconnect|
|00:02:24.818907|Client retained process object+duplicate signaled0/exit0|
|00:02:58.499014|Host retained process object+duplicate signaled0/exit0|

Client W/S дала42/41 actual native P2 applications, native health200/guards preserved. Host выполнил4625 P2 Updates, из них2633 при background Host; последующий neutral disconnect tail не выдаётся за непрерывную UDP movement phase. Вокруг каждого P2 Update P1 fields совпали; все background command blocks neutral. Новых affinity/duplicate-update/lifetime/native faults не зарегистрировано.

## 6. Correlation и границы доставки

| Stage | PSE message records |
|---|---:|
| Host native captured / queued |2634|
| Host Worker ENet enqueue |2634|
| Successful synchronous OS UDP send recognition |2633|
| Actual Client OS UDP receive recognition |2633|
| Client Worker delivered DTO |2631|
| Client Simulation accepted/logged |2631: slot0=1315, slot1=1316|
| Exact entire-chain bit matches |2631/2631|

Сверены slot/packet sequence/Host tick, epoch, entity ID/generation, recipient connection generation и пять IEEE754 float bit patterns XYZ/yaw/pitch. Каждый принятый Client sample имеет Host capture, Host Worker enqueue, actual WSASendTo return0, actual WSARecvFrom return0 и Client Worker record. Это source/UDP/native evidence, не одни diagnostic print statements. OS probe распознаёт полные PSE spans внутри ENet datagrams; ENet channel отдельно подтверждается receive guard/source, а не вымышленным полем snapshot.

Snapshot355 (P1) был captured/enqueued, но OS UDP send не наблюдён. Точная причина этого best-effort discard **UNKNOWN**; enqueue не переименован в отправку. Последние2633/2634 получены OS в00:02:24.620 после client_stop_begin00:02:24.618 и не доставлены simulation during normal disconnect drain. Они не включены в2631 delivered. Native sequence gaps разрешены; duplicate/stale identities не приняты. Формат/transport ради счётчиков не менялись.

## 7. Acceptance по заданным10 пунктам

| Критерий | Native result |
|---|---|
|1 Host P1/P2 simulation|PASS:4625 unique controller/Puppet steps, P1 comparisons preserved|
|2 Client W/S|PASS: real Windows input→UDP→Host native4/8,42/41 updates|
|3 Real P1/P2 transforms|PASS: live graph/version/stable-level/post-world read gates; separate actor/node/owner|
|4 Actual ENet opcode20|PASS:2633 recognized messages in successful OS sends|
|5 Both client slots|PASS:1315/1316 native accepted samples|
|6 sequence/entity generation/epoch|PASS bounded contract: unique packet sequences, per-slot guards, native binding generation1, epoch7365 READY tuple|
|7 Host/Client numeric correlation|PASS:2631 entire-chain exact5-float bit matches|
|8 No Worker KEX memory|PASS: scalar DTO/source audit, actual Worker affinities and socket-buffer-only probes|
|9 No new native/affinity/lifetime/duplicate faults|PASS in this one run; no general lifecycle certification|
|10 Normal OS exits|PASS: same retained handles+duplicate waits0, exit0; no forced termination|

Worker close/join доказаны passive OS socket-close and actual thread waits before native control restores. Host four exact16 restores, один exit Reset/handle null/P1 unchanged, then engine cleanup/native final/ticks_after_shutdown0/failed=false. Client one exact Update restore after joined Worker, engine return/native final. [Host OS](evidence/m1/N55_HOST_01/OS_SIGNALED.json), [Client OS](evidence/m1/N55_CLIENT_01/OS_SIGNALED.json), all16 REA sessions closed, both process observers exited0. DOC51/52 old terminal-hang root cause остаётся UNKNOWN.

## 8. Изменения и integrity

Изменены восемь existing source files относительно завершённого follow-up: bootstrap.cpp, native client.cpp, network_input.cpp/.hpp, thread_bridge_dto.h, enet_worker.h/.cpp, enet_client.cpp. Это opt-in capture/consume seams, internal metadata, observer и snapshot framing/route guards; Worker/Session/Bootstrap не переписывались. Native lifecycle contracts/offsets сохранены. [Полный список всего задания](evidence/m1/NATIVE_SNAPSHOT_55/CHANGED_FILES.json), [overall diff](evidence/m1/NATIVE_SNAPSHOT_55/ALL_SOURCE_CHANGES.patch). В preceding A отдельно изменены только test_suite/test_bridge_acceptance waits и input telemetry.

[Final integrity](evidence/m1/NATIVE_SNAPSHOT_55/INTEGRITY_FINAL.json):14104 baseline files, только8 разрешённых C source changes; historical Evidence/accepted builds/Doc53/addendum unchanged. Original77/copies77/full original и protected profiles совпали; protected_before/after равны для всех четырёх task runs. Guard errors/overflow0, успешных external write-opens0. Guard не является полным OS sandbox. Steam init failure был штатным; achievements/API/licensing не патчились. [OS final](evidence/m1/NATIVE_SNAPSHOT_55/OS_FINAL.json):0 games/test executables/checked endpoints. Product sources и DLL не менялись после native freeze.

## 9. Ограничения и остановка

- Единственный native Host+Client run на одном ПК/loopback и одной Tomb of Ramses. Реальные два ПК LAN/Radmin VPN и8players не проверены.
- Только capture/transport/diagnostic log. Client world независим; visual actors/camera/HUD/interpolation/world/combat/campaign replication отсутствуют.
- Native level transition/MapReady, reconnect, respawn/entity rebind/general epoch registry не приняты. Epoch7365 относится к этому bounded first-level session. New unexplained entity generation блокируется.
- Orientation fields реально прочитаны и побитно переданы, но дополнительная C попытка динамически менять P1 mouse orientation прервана Windows foreground rejection до SendInput. Положительный mouse delta/Attack proof принадлежит завершённому DOC53 follow-up. Его результат здесь не подменяется повтором.
- Exact cause enqueue355→no observed UDP остаётся UNKNOWN; best-effort delivery не объявляется reliable. General terminal hangs, loading/cutscene/minimized/Esc predicate и другие DOC53 unknowns не закрыты.

**Остановлено после DOC55.** Следующий минимальный отдельный этап — явный native map/epoch/MapReady contract до применения snapshots к клиентскому миру. Никакая следующая система автоматически не начиналась.
