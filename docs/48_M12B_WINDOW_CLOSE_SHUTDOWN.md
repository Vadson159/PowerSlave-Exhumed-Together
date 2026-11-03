# M1.2b Preflight — Window Close & Network Shutdown Safety

2026-11-02. **PASS в ограниченном сценарии одного WM_CLOSE при живом native P2 и активном ENet Worker.** Оконный путь подтверждён независимо от прежнего Direct Quit. Worker действительно завершился и был joined, socket закрыт до native unbind и engine service cleanup. Production-код не изменялся. Выполнен **один** новый игровой запуск; повторов нет. Полноценный M1.2b этим результатом не принимается.

Основные Evidence: [runtime summary / 35 проверок согласованности](evidence/m1/M12B_WINDOW_CLOSE_01/RESULT_SUMMARY.json), [native journal / 5568 записей](evidence/m1/M12B_WINDOW_CLOSE_01/native.jsonl), [полный Frida capture / 313 messages, not truncated](evidence/m1/M12B_WINDOW_CLOSE_01/status_final.json), [извлечённая OS sequence](evidence/m1/M12B_WINDOW_CLOSE_48/OS_SEQUENCE.json), [native lifecycle](evidence/m1/M12B_WINDOW_CLOSE_48/LIFECYCLE.json). Эти 35 проверок — офлайн-сверка одного run, не дополнительные игровые эксперименты.

## 1. Статический call path до запуска

**VERIFIED static** ниже означает наблюдение исходника адаптера или exact-build assembly/PE. Это отдельно от **RUNTIME OBSERVED**. Имена ролей KEX аналитические; `FUN_...` — имена анализа. Декомпиляция не является исходным C++ KEX.

Прочитаны AGENTS, PROGRESS/EVIDENCE_INDEX, DOC-40/41/43/44/45/46/47, исторический handoff и M0 gates. Сохранённые dossiers переиспользованы при совпадении SHA. Выполнен только узкий read-only/noanalysis запрос существующего Ghidra project: references к engine singleton и три функции для разрешения непрямого shutdown-вызова. Общий реверс/M0 не повторялись, REA MCP не исправлялся. [Static review, SHA, PE tables и source references](evidence/m1/M12B_WINDOW_CLOSE_48/STATIC_REVIEW.json), [новые raw dossiers/invocations](evidence/m1/M12B_WINDOW_CLOSE_48/static/).

SHA-256 EXE для **всех KEX координат в этом документе**:
`b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.
VA preferred; runtime address = module base + RVA. File offset указан отдельно.

| Роль | Preferred VA | RVA | File offset | Evidence |
|---|---|---|---|---|
| SDL event pump | 0x140100d80 | 0x100d80 | 0x100180 | [dossier](evidence/m0_static_final_acceptance/G_FUNC_140100d80.json) |
| Event dispatcher | 0x140100510 | 0x100510 | 0xff910 | [dossier](evidence/m0_static_final_acceptance/G_FUNC_140100510.json) |
| Platform quit request | 0x1400ed8a0 | 0xed8a0 | 0xecca0 | [dossier](evidence/m0_static_final_acceptance/G_FUNC_1400ed8a0.json) |
| Engine singleton constructor, только анализ | 0x1400a4580 | 0xa4580 | 0xa3980 | [dossier](evidence/m1/M12B_WINDOW_CLOSE_48/static/G_FUNC_1400a4580.json) |
| Engine quit dispatch | 0x1400a5800 | 0xa5800 | 0xa4c00 | [dossier](evidence/m1/M12B_WINDOW_CLOSE_48/static/G_FUNC_1400a5800.json) |
| Game pre-shutdown | 0x1401b0e20 | 0x1b0e20 | 0x1b0220 | [dossier](evidence/ghidra_2022/G_FUNC_1401b0e20.json) |
| Platform Shutdown / существующий hook | 0x1400fcc20 | 0xfcc20 | 0xfc020 | [dossier](evidence/m0_static_host_death/G_FUNC_1400fcc20.json) |
| Engine service cleanup | 0x1400a4c90 | 0xa4c90 | 0xa4090 | [dossier](evidence/m0_static_final_acceptance/G_FUNC_1400a4c90.json) |
| Native Player Reset | 0x1401dfa70 | 0x1dfa70 | 0x1dee70 | [принятые coordinates/ABI](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json) |
| Native final exit | 0x1400fc580 | 0xfc580 | 0xfb980 | [dossier](evidence/m0_static_final_acceptance/G_FUNC_1400fc580.json) |

SDL2.dll SHA-256 `f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0`, Windows resource version 2.0.14.0. Upstream SDL 2.0.14 [WIN_WindowProc](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.0.14/src/video/windows/SDL_windowsevents.c) переводит WM_CLOSE в window CLOSE; [SDL_SendWindowEvent](https://raw.githubusercontent.com/libsdl-org/SDL/release-2.0.14/src/events/SDL_windowevents.c) для последнего окна также ставит SDL_QUIT. Это до run было **INFERRED для shipped SDL**: совпадение resource version не доказывает идентичность сборки. Обе исходные страницы сохранены с URL/SHA в static Evidence. Реальный переход проверен ниже на shipped DLL.

KEX pump `100d80` вызывает SDL_PollEvent и dispatcher `100510`. Для event type 0x100/0x101 dispatcher вызывает platform virtual +0x88 → `ed8a0`. Тот вызывает thread interface и tail-dispatch engine virtual +0x20. Constructor `a4580` устанавливает engine vtable RVA 0x511158; её +0x20 = `a5800`, +0x10 = `a4c90`.

`a5800` сначала вызывает game virtual +0x18 → `1b0e20`, затем platform virtual +0x90 → `fcc20`. Принятая platform table RVA 0x5189d0 содержит +0x88 = quit request, +0x90 = Shutdown, +0x80 = final exit. Game table RVA 0x52ad08 содержит +0x18 = `1b0e20`. Static PE values и реальные live tables в новом процессе сохранены раздельно.

**Уточнение порядка:** game pre-shutdown происходит раньше нашего platform hook. Его статика содержит config/list cleanup; его нельзя называть StateExit или actor arena destruction. Принятый DOC-43/44 Reset contract не переносился на новую точку. Новый run наблюдает возврат этого вызова и после него ещё живой P2/owner/handle перед Reset.

`fcc20` входит в существующий `hookShutdown` → `unbind`. После возврата адаптера оригинальный Shutdown вызывает engine virtual +0x10, затем SDL subsystem/window/Quit/streams cleanup и platform +0x80 → `fc580` → импортированный `_exit(0)`, native noreturn path. Это не немедленное уничтожение процесса Windows на WM_CLOSE.

ABI: существующий Win64 this-in-RCX contract hooks и `Reset(Player*)` сохранён. Новые точки только пассивно наблюдаются, вручную не вызываются; Ghidra signatures, неизвестные аргументы game/engine методов не объявляются восстановленными API. Constructor в runtime не вызывался диагностикой. Значение для кооператива — возможность сохранить прежний network/native teardown на оконном выходе, без нового lifecycle seam.

## 2. Ownership, потоки и отсутствие необходимого патча

Точные source entry points:

- [bootstrap.cpp](../experiments/m1/native_bootstrap/src/bootstrap.cpp): `hookShutdown` строка 268 → `unbind` 232; `restoreControls` 132; engine observer 277; final observer 282; `M11Prepare` 441; `DllMain` 464.
- [network_input.cpp](../experiments/m1/network_input/src/network_input.cpp): `Session::stop` 140; `start` 12 регистрирует единственного SIMULATION owner; `tick` 126 после stop становится no-op.
- [enet_worker.cpp](../tools/enet-prototype/src/enet_worker.cpp): `stop` 76, `threadLoop` 171, `finalizeOnStop` 208.
- [enet_host.cpp](../tools/enet-prototype/src/enet_host.cpp): `stop` 72, `poll` 92; [ENet host.c](../tools/enet-prototype/third_party/enet/host.c) `enet_host_destroy` 141 → [win32.c](../tools/enet-prototype/third_party/enet/win32.c) `enet_socket_destroy` 316 → closesocket.

Подтверждённая последовательность: simulation owner помечает Session stopped/neutral → Worker running=false → синхронный `std::thread::join` → Worker-owned ENet teardown закончен → host/client unique_ptr reset → terminal queues/outbox/roles retired → control hooks exact restore → один native Reset внешнего Player → оригинальный KEX cleanup. Worker не хранит KEX pointers/callbacks и не вызывает KEX. Его callbacks касаются transport DTO/SPSC; `m_lifecycleMutex` ими не захватывается.

Worker poll использует 1 ms, pending terminal drain имеет срок 250 ms и явный RESYNC при недоставленных critical events. Поэтому stop не требует неограниченного дренажа simulation, пока она находится в join. Это не hard realtime гарантия: scheduler, socket/OS stalls и непрерывный malicious flood этим заданием не тестировались. В данном run реальный join занял примерно 4 ms.

`Session::stop` идемпотентен через started/stopped. После join `ENetHostServer` destructor вызывает stop повторно, но running=false и host уже null: второго socket destroy нет. `resetAttempted` запрещает второй native Reset; empty handle не сбрасывается повторно. В run наблюдались ровно один network stop и exit Reset. Initial Reset перед Spawn — отдельный принятый initialization operation.

Shutdown идёт из Windows/SDL event pump на attested simulation TID, вне loader lock. DllMain не делает hooks/join/KEX/cleanup. `M11Prepare` pin-ит образ до установки hooks. [DOC-43 ownership proof](43_M1_DIRECT_QUIT_FINAL.md) остаётся применим: static Context/Player storage и retained Session живут до process reclaim; MH_DisableHook восстанавливает targets, не освобождает trampolines. RemoveHook/Uninitialize/FreeLibrary и hot unload не добавлялись. Lifecycle hooks сохраняются до выхода; control hooks снимаются после join, перед Reset и engine service cleanup. Puppet/refcount/KEX memory вручную не освобождаются.

**Production changes: 0.** Опасного альтернативного WM_CLOSE path, требующего коррекции, не обнаружено. Новые файлы только в [window_close](../experiments/m1/window_close/README.md), Evidence и документации. Пассивный Frida probe наблюдает точные prefixes, SDL/message routing, live interface tables, OS I/O/waits; replacements/KEX calls/game writes = 0. Он не является второй игровой DLL/client integration.

## 3. Новая согласованная сборка

[BUILD_MANIFEST](evidence/m1/M12B_WINDOW_CLOSE_48/BUILD_MANIFEST.json), [frozen sources](evidence/m1/M12B_WINDOW_CLOSE_48/frozen_sources/), [новые artifacts](evidence/m1/M12B_WINDOW_CLOSE_48/artifacts/), [окончательный diagnostic harness](evidence/m1/M12B_WINDOW_CLOSE_48/HARNESS_MANIFEST.json).

| Объект | SHA-256 |
|---|---|
| Новая `pse_m11_bootstrap.dll` | `0e48b179eb955939bafeb8498a5fc2d7230d3d13992ac9423de4a32505680b96` |
| Source manifest digest, canonical JSON списка path/hash | `7281f2c720cf082a43bdd3e961eb40ffc3428907e9b5f969f19933b3843600d0` |
| BUILD_MANIFEST.json, bytes | `727a898e4d00e200a7221d88ea38712f5afe70a23501b47782e566db8d57eb93` |
| Native bootstrap source | `31901ae01f23377766f1566ef9320e5f841b03224d4afba9a11e59668b6026c6` |
| Session source, DOC-46 + DOC-47 | `d1fdd04422ed78b76b7c38c73af612600a2d32c630a3442da52b38e67cc51ee6` |
| Worker source, DOC-46 | `1dcfd67c0850a2d976d36b9e8b7da23a58cfe3abad965ecc670c7e8222401b2d` |

Fresh tree `experiments/m1/window_close/build`, Release x64, MSVC/SDK 10.0.26100.0, /MT, C++20 adapter, PSE_M12A. [Все configure/build команды и exit codes](evidence/m1/M12B_WINDOW_CLOSE_48/BUILD_20261102_080634_650760.json); рядом полные три raw logs. Все compiled C/C++/header/CMake inputs после сборки повторно сверены. Diagnostic Python/JS дополнены после build и сохранены отдельно в final_harness; они не выдаются за изменённые compiled inputs.

В summary имеется проверка хешей 8 routing/admission source files против final manifest DOC-47; они совпадают. Следовательно, в новую DLL действительно включены обе коррекции. Старая DOC-44 DLL `24e2809f…826ad` и M1.1 DLL не пересобирались и не подменялись. Новая frozen DLL загружалась одна, через сохранённый loader surface.

## 4. Единственный native run

[Launch](evidence/m1/M12B_WINDOW_CLOSE_01/launch.json): PID **24916**, primary/simulation TID **23060**, Worker **11320**, headless client PID **19864**, HWND **0xe0918**. Independent game copy `experiments/m0/game`, fresh profile `experiments/m1/window_close/profile/M12B_WINDOW_CLOSE_01`. [Frozen loaded artifact](evidence/m1/M12B_WINDOW_CLOSE_01/NATIVE_ARTIFACT.json), [изоляция до Permit](evidence/m1/M12B_WINDOW_CLOSE_01/ISOLATION_GATE.json), [live OS process/thread/UDP snapshot](evidence/m1/M12B_WINDOW_CLOSE_48/OS_live.json), [pre-close gate](evidence/m1/M12B_WINDOW_CLOSE_01/PRE_CLOSE_GATE.json).

P1/реальный gameplay подтверждены до Permit. Native Spawn в 08:13:38.050Z: P1 Player `0x140af33e0`, Actor `0x619d53a0`; P2 Player `0x7ffacd2a20e0`, Actor `0x619d4a60`, Node `0x61da6be0`, собственный owner, оба health200. До закрытия **2066** native P2 Updates/Commands/Puppet ticks; ни одного повторного update в tick, сравниваемые поля P1 сохранены во всех 2066 updates. Клиент передал1384 INPUT; Worker received1384; simulation приняла1378, все найдены в client journal по slot/gen/seq/forward. Последние transport commands могли не дойти до simulation при terminal shutdown и не объявляются применёнными.

Ввод был neutral; доказаны существование и настоящая simulation P2. Повторная movement/collision приёмка не заявляется. Соединение generation1; native reconnect/adversarial R1/R2 внутри игры не воспроизводились — это отдельные автономные проверки ниже.

Все времена следующей таблицы UTC, дата 2026-11-02. Native seq и OS order — разные журналы; порядок внутри каждого сохранён.

| Время | Evidence / событие |
|---|---|
| 08:14:12.554 | Клиент ещё отправляет INPUT seq1384. |
| .557 | OS order5: DispatchMessageW, **WM_CLOSE0x10**, нужный HWND, TID23060; stack проходит shipped SDL и KEX pump100d80. |
| .557–.558 | SDL_PushEvent window CLOSE(0x200/event14), SDL_QUIT(0x100) → PollEvent → KEX dispatch100510 → quit request ed8a0 → engine a5800. |
| .558–.563 | Game pre-shutdown1b0e20 entry/return на23060; live tables отдельно совпали с ожидаемыми. |
| .563 | native seq5553: Shutdown entry, live external handle=true, StateExit не предшествовал. |
| .565–.569 | seq5555–5556: stop → joined. OS order16: TID23060 **WaitForSingleObjectEx(handle0xa34, worker11320, INFINITE)**. |
| .569 | OS orders17–18: **closesocket(0xa30) на worker11320, return0**. До close наблюдались15383 WSARecvFrom и179 WSASendTo calls на этом Worker/socket; это OS calls, не число доставленных INPUT. |
| .569 | OS order19: wait return **WAIT_OBJECT_0(0)**; GetExitCodeThread успешно, **worker exit code0**. Предварительный join доказан независимо от exit игры. |
| .571 | seq5557: network retired, queues0/0; cancelled critical1 = terminal CLIENT_LEFT, lost_on_stop0, retired outbound0, resync0, affinity violations0. |
| .604–.669 | seq5558–5561: Update/Turning/Commands/Getter restored exact16. seq5562: P2 ещё живой, health200, 2066 updates, guards=true. |
| .670–.671 | seq5563–5564: **один exit Reset**; external handle null, P1 unchanged, guards=true. |
| .673–13.046 | quiesced → engine service cleanup entry/return, seq5565–5567. |
| 08:14:13.048–.054 | SDL_DestroyWindow, SDL_Quit → native final, seq5568: handle null, guards=true, foreign_calls0, ticks_after_shutdown0, failed=false, retained module/storage. |
| 08:14:13.079 | [Process exit0](evidence/m1/M12B_WINDOW_CLOSE_01/exit.json). |

Оконный запрос отправлен существующим owned-HWND helper через **PostMessageW(WM_CLOSE)**. Единственная close-request запись и matching OS receipt находятся в summary. TaskKill/TerminateProcess/crash injection и внутриигровой Quit не использовались. Старый текстовый `reason:"DirectQuit"` в native hook означает общий прежний teardown label; происхождение нового выхода установлено отдельной WM_CLOSE/SDL трассой.

[После выхода игры, пока клиент ещё жив](evidence/m1/M12B_WINDOW_CLOSE_48/OS_game_exited.json): game PID и host endpoint отсутствуют; client socket ещё существует. Затем клиент получил обычную команду quit и завершился [exit0](evidence/m1/M12B_WINDOW_CLOSE_01/client_exit.json). [Итог OS](evidence/m1/M12B_WINDOW_CLOSE_48/OS_final.json): игровых/client процессов и проверяемых UDP endpoints0. [Все семь diagnostic sessions закрыты](evidence/m1/M12B_WINDOW_CLOSE_48/SESSIONS_CLOSED.json); закрытие sessions после exit не используется как доказательство предварительного join.

## 5. Integrity и регрессии

[Baseline](evidence/m1/M12B_WINDOW_CLOSE_48/BASELINE.json), [до runtime](evidence/m1/M12B_WINDOW_CLOSE_48/INTEGRITY_20261102_081101_973041.json), [после runtime](evidence/m1/M12B_WINDOW_CLOSE_48/INTEGRITY_20261102_081502_170413.json): **1311 immutable files unchanged**, включая current production/test sources, исторические accepted DLL/Evidence и весь Gemini cadence stand. Original77/copy77 SHA совпадают с preparation; aliases/hardlinks нет; полный original snapshot unchanged. Оба protected profiles равны baseline; отдельные [before](evidence/m1/M12B_WINDOW_CLOSE_01/protected_before.json)/[after](evidence/m1/M12B_WINDOW_CLOSE_01/protected_after.json) этого run также равны.

Diagnostic guard имеет errors0/unique_overflow0, успешных наблюдавшихся external disk write-opens0; внешние Steam auxiliary attempts отклонялись. Последний periodic summary перед exit: write_opens481, allowed13, denied108, device exemptions360. File-open records продолжаются во время shutdown. Это bounded instrumentation, не OS sandbox и не утверждение о всех возможных syscalls/inherited handles/других процессах.

Хотя production patch не потребовался, suites и R1/R2 пересобраны из тех же current sources до игры:

| Проверка | Результат / raw |
|---|---|
| pse_test_suite | **46/46, exit0** — [log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_pse_test_suite/console.log) |
| pse_stress_test | **11/11, exit0** — [log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_pse_stress_test/console.log) |
| pse_fault_injection | **16/16, exit0**, включая штатный F16 enforcement child — [log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_pse_fault_injection/console.log) |
| pse_bridge_acceptance | **11/11, exit0** — [log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_pse_bridge_acceptance/console.log) |
| R1 live predicate | gen2 survives queued KICK1, ready/authenticated — [exit0](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_r1_live/console.log) |
| DOC-46 routing contract | **5 cases,330 assertions,0 failures** — [log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_routing_contract/console.log) |
| R2 strict predicate, правильный argv r2 | beta rejected/disconnected; alpha slot1/gen2/ready, **exit0** — [log/argv](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_r2_fixed_correct_argv/) |
| DOC-47 admission contract | **4 scenarios,112 assertions,0 failures** — [log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_admission_contract/console.log) |

[Первичная regression manifest](evidence/m1/M12B_WINDOW_CLOSE_48/REGRESSION.json) намеренно сохраняет exit10 ошибочного вызова `r2_fixed.exe` **без аргумента r2**. Он выполнил историческую R1-ветку с известным post-stop verdict bug; live gen2 перед stop был корректным. [Его raw log](evidence/m1/M12B_WINDOW_CLOSE_48/RUN_r2_fixed/console.log) не скрыт. После проверки argv запущена настоящая ветка R2; binary/test source не менялись. Единственный native run начался уже после её PASS. Повторных игровых запусков ради отчёта не было.

Исторические tests/assertions, Wire Protocol v1.10, generation routing/admission и KEX hooks/lifecycle сохранены. Ошибка раннего чтения занятого client.jsonl не меняла runtime; окончательная сверка выполнена после штатного client exit. Все source/build/run SHA и exit codes сохранены.

## 6. Вердикт и оставшиеся границы

**PASS** только обычному WM_CLOSE этого exact build с одним готовым P2 и активной сетью: наблюдены route, simulation affinity, реальный Worker join, socket close, handle null перед engine service cleanup, exact controls, once-only Reset/stop и retained lifetime. Записанных native AV/illegal instruction/stack overflow, script errors или признаков UAF нет. Handler не является универсальным sanitizer; отсутствие всех видов исключений/UAF не утверждается.

General crash/forced exits, repeated WM_CLOSE races, hot unload, flood/все thread schedules, новая игровая client DLL, world replication и полноценный M1.2b остаются вне приёмки. OS ExitProcess entry отдельно не снимался; process-lifetime retention опирается на DOC-43 ownership proof и новый native final witness. Один private Frida trampoline в returnAddress engine_quit не трактуется как новый адрес KEX или восстановленный source edge.

**P2-2 остаётся NOT FIXED.** Не менялись cadence, packetThrottle, unreliable policy и input timeout `6 ticks || 150 ms`. [Gemini REPORT](../tools/m12b-input-cadence-qa/REPORT.md) SHA `77033666fbcdf4e57951126509aea58028f9dd3a999ae7436b0f05c8614703bd` и [INTEGRITY_AUDIT](../tools/m12b-input-cadence-qa/INTEGRITY_AUDIT.md) SHA `fe514f3a7451e59b4c23713d6b780c3b895b5a774b1626d48236ed03835609ce` неизменны; это входные материалы следующего отдельного задания. Их рекомендации в production не переносились.

После DOC-48, обновления PROGRESS/EVIDENCE_INDEX и файловой проверки работа остановлена.
