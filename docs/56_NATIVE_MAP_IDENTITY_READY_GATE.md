# DOC-56 — Native Map Identity / Epoch / MapReady Gate

2026-11-03 UTC. **Итог: PARTIAL.** Native identity source установлен статически (VERIFIED static), fail-closed gate реализован в production C++ и прошёл автономные тесты и все регрессии. **Настоящий двухпроцессный native KEX PoC не выполнялся** — принятый стенд DLL-loader/isolation (REA MCP Frida sessions) в этой сессии недоступен ([blocker](evidence/m1/NATIVE_MAP_READY_56/NATIVE_RUN_BLOCKED.json)). Кроме того, Wire v1.10 не позволяет Host независимо проверить карту клиента; [предложение v1.11](evidence/m1/NATIVE_MAP_READY_56/PROTOCOL_CHANGE_PROPOSAL_V111.md) не реализовано. `world_synchronized=false` сохранено везде.

Evidence: [build manifest](evidence/m1/NATIVE_MAP_READY_56/BUILD_MANIFEST.json), [autonomous gate](evidence/m1/NATIVE_MAP_READY_56/AUTONOMOUS_GATE.json), [source diff](evidence/m1/NATIVE_MAP_READY_56/SOURCE_CHANGES.patch), [static coordinates](evidence/m1/NATIVE_MAP_READY_56/NATIVE_MAP_IDENTITY_COORDINATES.json), [narrow Ghidra exports](evidence/m1/NATIVE_MAP_READY_56/ghidra/), [baseline](evidence/m1/NATIVE_MAP_READY_56/BASELINE.json).

## 1. Аудит существующего контракта (до DOC-56)

| Вопрос | Факт в коде DOC-55 |
|---|---|
| Кто создаёт mapEpoch | Host Simulation: `configureNativeSnapshots(ticks)` — номер StateTick в момент старта Session (7365 в N55) |
| Identity карты | Константа `DOC55_TOMB_RAMSES` + witness стартовой позиции `[-7420,-6775,1152]`/sector259 на обоих процессах. Native источника имени карты не было |
| Начало эпохи | `setInitialSessionMap` до старта NETWORK; Welcome сразу несёт domain/epoch |
| Когда Client шлёт MapReady | Первый gameplay tick после Welcome (`localThisTick==1`), без проверки карты |
| Проверки Host | `active && slot==1 && gen==current && epoch==current && !ready`; дубликат молча игнорируется |
| MapReady↔slot/generation | Host Worker подставляет generation аутентифицированного peer; Client Worker не отправляет DTO старой generation |
| Разрешение snapshot | `wantsPlayer && levelReady` (= input READY) |
| Проверка epoch snapshot | Host Worker: `== m_sessionMapEpoch`; Client Worker: `== Welcome epoch`; Simulation `snapshot55::Guard` |
| Преждевременный READY | Принимался при совпадении epoch, даже если Host level не готов |
| Disconnect/load/shutdown | CLIENT_LEFT/recover сбрасывают ready; StateExit/DirectQuit → `unbind`/`Session::stop` |

Вывод: input READY был единственным признаком; «совместимость миров» не проверялась. DOC-56 разделяет состояния: session authenticated (Welcome/generation) → native map loaded (state3/next3/pending0) → native identity verified → MapReady accepted → snapshot eligible. World synchronized — отдельное, по-прежнему `false`.

## 2. Native map identity source (VERIFIED static, exact SHA b8f131…a76d)

Узкий read-only RE: 5 новых headless Ghidra 12.2 dossiers на существующем проекте `PSE_2022_cpu2` (`-readOnly -noanalysis`), сохранены в `NATIVE_MAP_READY_56/ghidra`. Широкий реверс не повторялся.

| Seam | VA | RVA | File offset | Evidence / наблюдение |
|---|---|---|---|---|
| GameInstance (inline) | 140af33a0 | af33a0 | — (.data bss) | принятый `instance()` DOC-41…55 |
| `map <filename>` | 1401aeb10 | 1aeb10 | 1adf10 | пишет `GI+0x28=4` (pending) и kexStr assign в `0x140af3580`=`GI+0x1e0` |
| kexStr assign | 14008d650 | 8d650 | 8ca50 | layout `{char* data; size_t length; size_t capacity}`, NUL в `data[length]` |
| EnterState | 1401b6a00 | 1b6a00 | 1b5e00 | case4: case-insensitive compare mapInfo key `entry+0x58` с `*(GI+0x1e0)`, `GI+0x1f8=entry+8`, `LoadMap(GI+0x1e0)`, успех → state3 |
| strcasecmp | 140090870 | 90870 | 8fc70 | 0 при равенстве (tolower) |
| kexSlaveLevelLocal (inline) | 140a45940 | a45940 | a43740 | `this` LoadMap; vtable 14052b478 (RVA 52b478, file 529a78): slot1 LoadMap, slot2 Unload |
| LoadMap | 1401c7b70 | 1c7b70 | 1c6f70 | `"kexSlaveLevelLocal::LoadMap: Opening {}"`, PSM magic, version byte `+0x208`, file-derived counts `+0xd8`, `+0x18` |
| Unload | 1401c90e0 | 1c90e0 | 1c84e0 | освобождает массивы, обнуляет `+0xd8`/`+0x18` |

Identity = `GameInstance+0x1e0` kexStr (имя файла карты, которое LoadMap фактически открыл, например стоковое `maps/TOMB.MAP` из `defs/mapInfo.txt`), действительное только при `state==3 && next==3 && pending==0`. Перекрёстная проверка: `*(GI+0x1f8)+0x50` — char* ключа mapInfo, который native compare признал равным имени (защита от устаревшего `+0x1f8`). Локальный structural witness: level vtable, PSM version, counts A/B; нулевые counts = выгруженный уровень. Это inline static object + heap-строка, не vtable/временный буфер; runtime адрес = actual base + RVA.

Ограничения: **runtime значение строки не наблюдалось** (native run не выполнен). Не доказано, что все legal writers `GI+0x1e0` (ChangeMap script API, menu/new game, loadgame) выставляют pending — поэтому identity перепроверяется каждый tick и любое изменение терминально. Content hash файла карты не получен: безопасного seam к буферу нет.

## 3. Epoch lifecycle

- Создание: Host, первый stable gameplay tick после attestation, `BCryptGenRandom`, `nextEpoch()` исключает 0, легаси-константу 1 и предыдущий. Привязан к одному `HostGate::bind(identity)` на время жизни Session.
- Session start: epoch уходит в Welcome (через `setInitialSessionMap`) и targeted MapChange с native именем.
- Level unload / identity change / transition: `verifyNativeMap` → `World::Invalidated` терминально: ready=false, P2/snapshot eligibility снята, повторного принятия нет.
- Level re-entry / map transition: StateExit hook → существующий `unbind`/`Session::stop`; новая эпоха потребует нового процесса-сессии (transition не реализован).
- Disconnect: peer → Disconnected; Reconnect: новая generation, тот же world/epoch, полный повтор MapReady + StableTicks; старая generation → `stale_generation` (Host) / `outboundNoPeer` (Client Worker).
- Stale MapReady/snapshot: `stale_epoch`; Worker drop, client gate refusal.
- Shutdown: inbound retired, pending MapReady не продвигается.
- `entityGeneration` по-прежнему отдельный native binding counter; connection generation не копируется.

## 4. Gate / state machine

`map56::HostGate` (World: Unbound→Verified→Invalidated; Peer: Disconnected→AwaitMapReady→MapReadyAccepted) и `map56::ClientGate` (Disconnected→AwaitNativeMap→IdentityVerified→MapReadySent→Ready; терминальные Mismatch/Invalidated) — [map_identity.hpp](../experiments/m1/native_map_ready/src/map_identity.hpp). KEX чтения — [map_reader.hpp](../experiments/m1/native_map_ready/src/map_reader.hpp): bounded/SEH copy, только на attested simulation thread.

Host MapReady принимается только при: world Verified этого tick, Host level ready, authenticated peer текущей generation, slot1, epoch binding, не дубликат. Иначе `map_ready_rejected` с причиной. Snapshot eligible = `running && ready && levelReady && HostGate.snapshotEligible(gen)`; Host DLL публикует только тогда и логирует `snapshot_eligibility` при изменении.

Client шлёт MapReady только при: аутентифицированный Welcome (gen>prev, slot≠0, epoch>1, имя ≤32), локальная identity заблокирована при старте Session и не менялась, `local.name == Welcome.mapName` (exact), 30 подряд stable ticks, gameplay/`localThisTick==1`. Mismatch → терминально, READY не отправляется. После READY любой fault/transition/unload → Invalidated, snapshot отвергаются (`snapshot55::Guard` получает `ready=false`). Snapshots не применяются к Player/Puppet/camera.

**Граница Host-проверки:** `MsgMapReady(slot,epoch)` не содержит identity клиента. Host проверяет client-attested epoch echo (`identity_source:"client_attested_epoch_echo_v110"`), не карту клиента. Тест фиксирует это как LIMITATION. Это не криптографическое доказательство и не Host-side verification.

## 5. Production changes

Существующие файлы (только 4, [diff](evidence/m1/NATIVE_MAP_READY_56/SOURCE_CHANGES.patch)): `native_bootstrap/src/bootstrap.cpp`, `native_second_instance/src/client.cpp` (всё под `PSE_DOC56_MAPREADY`), `network_input/src/network_input.hpp/.cpp` (opt-in `configureNativeMap/verifyNativeMap/snapshotEligible`; без конфигурации поведение прежнее). Новые: `experiments/m1/native_map_ready/{src/map_identity.hpp, src/map_reader.hpp, tests/map_ready_contract.cpp, CMakeLists.txt, workflow.py, freeze.py}`. Wire `net_protocol.h` SHA `8272aa3e…bac0f` не изменён; transport, rate, batching, Channel1, timeout6/150, focus lease `+0x319`, pause gates, sequence/generation hardening не тронуты. Новых hook sites нет; добавлены только read-only чтения.

Артефакты: Host DLL `2b66f075752b2863add8ee59fb768e2943a77d888982deafda67d13c042ecdf7`, Client DLL `92bd90b9b294a073f95a2f05ec5f43dbfa3fd7e43cbecad581a0238267a5e09d` (VS2022 x64 Release /MT, /W4 /WX). Обе в чужом процессе возвращают 193 без лога ([rejection](evidence/m1/NATIVE_MAP_READY_56/WRONG_PROCESS_REJECTION.json)).

```powershell
& 'C:/Python314/python.exe' -B experiments/m1/native_map_ready/workflow.py baseline
& 'C:/Python314/python.exe' -B experiments/m1/native_map_ready/workflow.py build
& 'C:/Python314/python.exe' -B experiments/m1/native_map_ready/workflow.py tests
& 'C:/Python314/python.exe' -B experiments/m1/native_map_ready/workflow.py integrity
& 'C:/Python314/python.exe' -B experiments/m1/native_map_ready/freeze.py
& 'C:/Python314/python.exe' -B docs/research/invoke_ghidra.py function 0x1401aeb10 0x140090870 --output m1/NATIVE_MAP_READY_56/ghidra
& 'C:/Python314/python.exe' -B docs/research/invoke_ghidra.py function 0x14008d650 0x14008d220 0x1401c90e0 --output m1/NATIVE_MAP_READY_56/ghidra
```

## 6. Автономные тесты (OFFLINE ONLY — не runtime KEX)

`map_ready_contract`: **210 checks / 0 failures, 3 прогона**. Первый прогон дал 2 FAIL из-за ошибки фикстуры (строки 36/35 байт вместо 33/32); лог сохранён, исправлена только фикстура.

- Derivation: state/pending4/StateExit-next, unreadable/capacity/terminator/quote/empty, 33 байта отвергнуто (не усекается), 32 принято, нет mapInfo, устаревший key, vtable, unload; epoch никогда 0/1/предыдущий.
- HostGate/ClientGate unit: чужой slot, старая generation, duplicate без re-promotion, reconnect, identity change, unload, disconnect, welcome с epoch1/gen0/slot0, pre-READY transition сбрасывает только stability, announced MapChange → fail closed.
- Reader на синтетическом образе со статическим layout (не KEX): offsets `+1e0/+1e8/+1f0/+1f8/+50`, level counts; невалидный указатель → SEH refusal; oversize length не копируется.
- Реальный Host `Session` + реальный client `ENetWorker` по loopback UDP:
  - положительный: native имя и non-constant epoch в Welcome; stable identity → MapReady → ровно одно accepted → baseline → eligibility → оба slot snapshot приняты client gate+guard; существующий INPUT доходит до native buttons4; решения map/ready только на SIMULATION thread; SPSC affinity 0.
  - отрицательные: разные map IDs (client Mismatch, READY нет, Host не eligible); MapReady до Host level readiness; stale epoch; duplicate; snapshot старой epoch (Worker drop, client refusal); client transition после READY; Host level unload после READY и запрет re-promotion; disconnect во время handshake; reconnect со старым queued DTO; shutdown с pending MapReady (join <3s, без promotion).
  - LIMITATION check: клиент, эхом повторивший epoch при чужой карте, принимается Host (v1.10).

## 7. Регрессии (реально выполнены на DOC-56 sources)

| Target | Результат |
|---|---|
| pse_test_suite | 46/46 ×3, без timer fixture |
| pse_stress_test / pse_fault_injection / pse_bridge_acceptance | 11/11 / 16/16 / 11/11 |
| background_contract (DOC-53) | 37/0 |
| r1_live + routing_contract (DOC-46) | R1 not reproduced; 330 checks, MAP_READY по-прежнему exact v1.10 |
| r2_fixed + admission_contract (DOC-47) | PASS; 112 checks |
| timeout_contract (DOC-49) / sequence_contract + sequence_live (DOC-50) | PASS / 282 + 12 |
| native_client_contract | 136 checks, 3 scenarios |
| snapshot_contract (DOC-55) | 42/0 |
| map_ready_contract (DOC-56) | 210/0 ×3 |

Все argv/логи/SHA бинарников — в [AUTONOMOUS_GATE](evidence/m1/NATIVE_MAP_READY_56/AUTONOMOUS_GATE.json).

## 8. Native PoC (Stage D)

**D1/D2 не выполнены (BLOCKED tooling).** Нет journals, OS socket/process evidence и exit codes DOC-56. Ни одна игра не запускалась. Требования D1.3–D1.12 и D2 остаются UNKNOWN; mock-проверки выше их не заменяют. Для повтора: те же DOC-55 runner/bind/gate шаги с артефактами из BUILD_MANIFEST, при доступном REA MCP provider.

## 9. Integrity

Baseline 15 077 файлов до изменений; после — изменены только 4 заявленных source files, исторические Evidence/документы/сборки не тронуты. game_check: original/copies/full original snapshot/protected profiles совпадают с baseline. Steam/saves/EXE не менялись.

## 10. UNKNOWN / BLOCKED

- Runtime значение и поведение `GI+0x1e0`/`+0x1f8`/level counts на Tomb of Ramses (и на втором уровне) — UNKNOWN до native run.
- Все writers `GI+0x1e0` и их связь с pending — частично; компенсировано per-tick fail-closed.
- Host-side проверка карты клиента — BLOCKED wire v1.10 (proposal v1.11).
- Native reconnect, level transitions, re-entry, общая epoch registry — не реализованы.
- Visual Proxy, camera, world replication не начинались.

## Verdict

**PARTIAL.** Реализован и автономно подтверждён fail-closed MapReady gate с native identity source, но native contract не подтверждён на KEX, и Host не может независимо проверить карту клиента на v1.10. Для Sol Max: (1) провести D1 (и D2, если вторая карта доступна стоковым UI/консолью) с принятым REA provider; (2) решить, принимать ли v1.11 MapReady identity. **Остановка после DOC-56.**
