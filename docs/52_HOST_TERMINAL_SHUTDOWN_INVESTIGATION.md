# DOC-52 — Host terminal shutdown investigation

2026-11-02. **Общий результат PARTIAL: причина зависания DOC-51 остаётся UNKNOWN, исправление не вводилось. Ограниченный PASS одному новому диагностическому двухпроцессному сценарию:** оба KEX завершились штатно, Host process object действительно signaled, exit code0, внешний TerminateProcess не понадобился. Это не устранение прежнего дефекта и не универсальная приёмка shutdown.

[Машинная сверка](evidence/m1/HOST_TERMINAL_52/RESULT_SUMMARY.json), [build/source manifest](evidence/m1/HOST_TERMINAL_52/BUILD_SOURCE_MANIFEST.json), [статический preflight](evidence/m1/HOST_TERMINAL_52/STATIC_PREFLIGHT.json), [исходники диагностики](../experiments/m1/terminal_shutdown/). Production sources/DLL не изменены и не пересобраны. Выполнен ровно **один diagnostic scenario: один Host и один native Client**. Verification run после исправления —0; доказанного исправления нет. После DOC-52 работа остановлена.

## 1. Исходный симптом и границы доказательства

DOC-51 Host34728/simulation3964/Worker6252 завершил network stop/join, socket close, четыре control restore, один exit Reset, engine cleanup и записал `native_final_exit`. Однако в13:29:31.451377Z отдельный probe сообщил query success/code0 и process wait258. В13:25:57.9727168Z CIM ещё видел тот же path/PID/creation date и единственный TID3964. Session close не завершился до внешней очистки; в13:30:29.847793Z TerminateProcess дал последующий wait0. Исторический launcher exit0 относится к этому forced cleanup.

Основные исходники наблюдений: [Host native journal](evidence/m1/N51_HOST_01/native.jsonl), [Host Frida status](evidence/m1/N51_HOST_01/STATUS_EXIT.json), [terminal WinAPI](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_TERMINAL_STATE.json), [OS после shutdown](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_after_shutdown.json), [forced cleanup](evidence/m1/NATIVE_SECOND_INSTANCE_51/OS_CLEANUP_ATTEMPT.json), [DOC-51 §9](51_NATIVE_SECOND_INSTANCE_INPUT_POC.md). Последнее поле `reason` в cleanup JSON, содержащее слова `normal ExitProcess`, — исторический текст оператора, **не наблюдение входа в ExitProcess**.

Метки: **VERIFIED** — статический source/PE факт с Evidence; **OBSERVED** — runtime запись; **INFERRED** — вывод; **UNKNOWN** — данных нет. Имена ролей KEX аналитические. Декомпиляция не является исходником KEX. Статические связи не доказывают исполнение каждого ребра.

Три разных события:

1. **Запись final callback.** OBSERVED в DOC-51 на3964 в13:25:56.428Z. В `hookFinal` сначала выполняется `log`, затем вызов original trampoline. Внутри logger после WriteFile ещё идут FlushFileBuffers и release SRW lock. Поэтому последняя строка доказывает выполнение callback до записи; возврат logger, вход original FinalExit и `_exit` отдельным witness **не подтверждены**.
2. **Инициирование Windows termination.** Для DOC-51 точный вызов/время UNKNOWN. Исчезновение остальных потоков и code0 согласуются с частичным termination, но не заменяют stack/API checkpoint.
3. **Завершение kernel process object.** Для DOC-51 normal path не подтверждён: сохранён wait258; после внешнего cleanup wait0. В новом DOC-52 сценарии положительно получен wait0 на удерживаемом с начала handle без cleanup.

## 2. Timeline DOC-51: часы, TID, координаты и переходы

Все времена ниже — **UTC 2026-11-02 по локальным часам записывающего процесса**. Native journal использует GetSystemTime; Frida — JS Date; UI helper — Python UTC; OS snapshot — PowerShell UTC. Они не являются одним monotonic clock; одинаковая миллисекунда не задаёт порядок между журналами. Native seq и OS order относятся каждый к своему источнику. No cross-process latency claims.

Полная выбранная последовательность, включая OS waits: [DOC51_TERMINAL_EXTRACT](evidence/m1/HOST_TERMINAL_52/DOC51_TERMINAL_EXTRACT.json). Каждый ряд ниже продолжает предыдущий и предшествует следующему; отсутствующие checkpoints сохранены как UNKNOWN.

| UTC | TID | Checkpoint / функция, RVA | Источник | Статус перехода к следующему |
|---|---:|---|---|---|
|13:25:56.205079|Sender TID UNKNOWN|PostMessageW(WM_CLOSE), HWND0x70dc2, result=true|[UI request](evidence/m1/N51_HOST_01/ui_20261102_132556_205079.json)|OBSERVED enqueue; receipt UNKNOWN|
|UNKNOWN|UNKNOWN|DispatchMessageW → shipped SDL window CLOSE → SDL_QUIT|В DOC-51 OS probe эти точки не наблюдал|INFERRED по exact-build path DOC-48; время/receipt UNKNOWN|
|UNKNOWN|UNKNOWN|KEX pump100d80 → dispatch100510 → quit request ed8a0 → engine a5800 → game pre-shutdown1b0e20|Сохранённая статика DOC-48, совпадающий EXE SHA|VERIFIED static edges; DOC-51 промежуточный runtime UNKNOWN|
|13:25:56.210|3964|Platform Shutdown fcc20, hookShutdown|native21026, live handle=true, StateExit=false|OBSERVED entry → network stop|
|.212|3964|neutral → Session::stop → ENetWorker::stop|native21027–21028|OBSERVED stop begin; adapter RVA не KEX RVA|
|.213|3964|WaitForSingleObjectEx(INFINITE), worker6252, handle0xa5c|OS order7|OBSERVED wait entry → worker teardown|
|.215|6252|closesocket0xa58, return0|OS8–9; socket связан с bind order6/port28745|OBSERVED close → successful join|
|.215|3964|WaitForSingleObjectEx return0, GetExitCodeThread0|OS10|OBSERVED Worker signaled → Session retirement|
|.215|3964|network_joined / network_retired; queues0/0|native21029–21030|OBSERVED → restore controls|
|.253 / .287 / .326 / .327|3964|Update1dfe30 / Turning1e2ad0 / Commands1e34a0 / Getter1b5520 exact16 restore|native21031–21034|OBSERVED → live pre-unbind snapshot|
|.328|3964|before_unbind; P1/P2 health200, P2 refs1, guards=true|native21035|OBSERVED → Reset|
|.328–.330|3964|native Reset1dfa70; once-only exit_reset|native21036–21037|OBSERVED return; handle null, P1 unchanged → quiesced|
|.330|3964|shutdown_quiesced|native21038|OBSERVED → original platform Shutdown|
|.332|3964|Engine cleanup a4c90 entry|native21039|OBSERVED entry → native cleanup waits|
|.336–.338|3964|Waits on2144,33976,26276,31748,31260,3184,11772,31564,17344,34344|OS11–34|OBSERVED waits/returns0; это не ENet6252|
|.405|3964|WaitForSingleObject/Ex on34212, timeout100, return0|OS35–38|OBSERVED → engine cleanup return|
|.414|3964|Engine cleanup return|native21040|OBSERVED → remaining platform cleanup|
|UNKNOWN, between .414 and final|UNKNOWN|SDL subsystem/window/Quit/streams cleanup|Статика fcc20; DOC-51 нет SDL cleanup callbacks|VERIFIED static ordering; intermediate runtime UNKNOWN|
|.428|3964|native_final_exit callback for fc580|native21041; pinned/storage/guards, foreign_calls0, ticks_after_shutdown0|OBSERVED record; logger return / original call UNKNOWN|
|UNKNOWN|UNKNOWN|ucrt `_exit(0)` → Windows terminal APIs / DLL detach / last thread termination|Отсутствуют отдельные API checkpoints и stack|UNKNOWN|
|13:25:57.9727168|OS observer; target3964|CIM sees34728, creation13:15:10.093Z; .NET thread snapshot only3964 state2|OS_after_shutdown.json|OBSERVED snapshot; code location UNKNOWN|
|13:29:31.451377|OS observer TID UNKNOWN|GetExitCodeProcess success/code0; WaitForSingleObject(process,0)=258|OS_TERMINAL_STATE.json|OBSERVED reported pair; full handle identity/ABI UNKNOWN|
|After final, before cleanup; exact UTC UNKNOWN|observer / agent TIDs UNKNOWN|Frida close request incomplete|DOC-51 §9; raw close-start timestamp отсутствует|Reported observation; blocking site UNKNOWN|
|13:30:29.847793|external observer TID UNKNOWN|Path-verified TerminateProcess; wait2s0|OS_CLEANUP_ATTEMPT.json|OBSERVED forced termination; normal exit FAIL|

Роли этих engine/service threads без стеков не назначаются. Duplicate SingleObject/Ex records могут быть вложенными вызовами и не означают дополнительных Workers.

### Native coordinate contract

EXE SHA-256 for **all KEX addresses in this document**: `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Preferred base0x140000000; runtime VA = actual base + RVA. File offsets are distinct.

| Analytical role | Preferred VA | RVA | File offset |
|---|---|---|---|
|Event pump|0x140100d80|0x100d80|0x100180|
|Event dispatch|0x140100510|0x100510|0xff910|
|Platform quit request|0x1400ed8a0|0xed8a0|0xecca0|
|Engine quit dispatch|0x1400a5800|0xa5800|0xa4c00|
|Game pre-shutdown|0x1401b0e20|0x1b0e20|0x1b0220|
|Platform Shutdown|0x1400fcc20|0xfcc20|0xfc020|
|Engine cleanup|0x1400a4c90|0xa4c90|0xa4090|
|Player Reset|0x1401dfa70|0x1dfa70|0x1dee70|
|Player Update|0x1401dfe30|0x1dfe30|0x1df230|
|Turning|0x1401e2ad0|0x1e2ad0|0x1e1ed0|
|Commands|0x1401e34a0|0x1e34a0|0x1e28a0|
|Getter|0x1401b5520|0x1b5520|0x1b4920|
|FinalExit|0x1400fc580|0xfc580|0xfb980|

Evidence: [DOC-48 STATIC_REVIEW](evidence/m1/M12B_WINDOW_CLOSE_48/STATIC_REVIEW.json), [native coordinates](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json), [FinalExit dossier](evidence/m0_static_final_acceptance/G_FUNC_1400fc580.json), [DOC-51 coordinate audit](evidence/m1/NATIVE_SECOND_INSTANCE_51/NATIVE_CONTRACT_AUDIT.json), [DOC52 exact PE offsets/prefixes/imports](evidence/m1/HOST_TERMINAL_52/STATIC_TERMINAL_PE.json). Координаты/префиксы переиспользованы по совпадающему SHA, нового Ghidra/REA binary analysis не было.

ABI: принятый Win64 `void(void*)`/this in RCX у существующих detours и Reset; FinalExit adapter `void()`, оригинальный код не использует входной this. Неизвестные дополнительные arguments KEX virtual methods не объявляются восстановленным API. Новые observer callbacks их не вызывают. Значение для coop — сохранение принятого shutdown seam; новые игровые seams не нужны.

## 3. WinAPI: identity, partial termination и расхождение наблюдений

Для DOC-51 terminal JSON сохранены PID34728/path и два результата, но **нет** numeric process handle, creation time на этом handle, access mask, ctypes/PInvoke prototype, observer PID/TID или выполненного source. По одному этому JSON нельзя независимо проверить, что обе операции получили один kernel object. Не обнаружено положительного Evidence ошибочного handle/API; это пробел provenance, не найденная причина.

Из предшествующих CIM snapshots совпадают PID/path/ParentProcessId11796 и creation date13:15:10.093Z, включая post-shutdown snapshot. Это поддерживает непрерывность identity до13:25:57.972, но не заполняет отсутствующую terminal creation time в13:29:31. `launch.started_utc`13:15:10.134261 — время отчёта после создания, не точное GetProcessTimes значение.

Контракты Windows: [GetExitCodeProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess) требует QUERY_INFORMATION или QUERY_LIMITED_INFORMATION; его BOOL success и выходной DWORD различны. [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject) требует SYNCHRONIZE:258 — unsignaled,0 — signaled,0xffffffff — failure. GetLastError нужен при failure; `last_error=0` после success не даёт дополнительного доказательства. Закрывать handle параллельно wait нельзя. В DOC-51 корректность этих деталей по executed source UNKNOWN; DOC-52 явно проверяет их.

`Process.HasExited=true` упомянут в DOC-51 §9, но отдельного raw результата/handle/version этой проверки нет. Кроме того, [Microsoft .NET Framework source](https://github.com/microsoft/referencesource/blob/main/System/services/monitoring/system/diagnosticts/Process.cs) в getter сначала принимает non-STILL_ACTIVE exit code, а только другая ветка делает WaitOne. Поэтому такое true **может зависеть от того же code0**, а не быть независимым process-signaled proof. Это VERIFIED по reference source, не утверждение об exact IL старого observer. [Сохранённый source/hash](evidence/m1/HOST_TERMINAL_52/primary_sources/DOTNET_MANIFEST.json).

CIM перечисляет identity в конкретный момент и не выполняет наш wait на удерживаемом handle. Process object может сохраняться из-за открытых handles после выхода; **само удержание handle не мешает его signal**. Frida `state:running` отражает состояние session manager, а зависший close может быть как причиной instrumentation problem, так и следствием остановившегося target. Ни CIM presence, ни HasExited, ни session detach не заменяют kernel wait.

Windows [ExitProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-exitprocess) имеет фазу завершения других потоков и DLL_PROCESS_DETACH до окончательного завершения вызывающего потока/process object; lock, принадлежавший убитому потоку, может блокировать detach. Это поддерживает **возможность** partial teardown. Однако сочетание DOC-51 code0/unsignaled плюс один running thread не определяет ни конкретную фазу, ни владельца lock. Без user stack нельзя различить spin/deadlock в user mode, instrumentation callback, logger/CRT/DLL teardown и задержку kernel/I/O rundown. State2 одного thread snapshot не доказывает бесконечный spin или kernel hang.

Короткая обычная race между final record и OS exit не объясняет длительное наблюдение в минуты при условии корректных probes. Но точные repeated waits/continuous handle старого run отсутствуют; длительность устойчивого несигнального состояния не измерена непрерывно. Разные часы/моменты не объединяются в атомарную observation.

### Что исправлено в наблюдаемости DOC-52

[observer.py](../experiments/m1/terminal_shutdown/observer.py) открывает **один** handle до resume, access0x100410 = SYNCHRONIZE|QUERY_INFORMATION|VM_READ. Сохраняет GetProcessId, QueryFullProcessImageNameW, точный GetProcessTimes creation FILETIME, GetHandleInformation и собственный PID. Все ABI имеют явные argtypes/restype. DuplicateHandle(DUPLICATE_SAME_ACCESS) и [CompareObjectHandles](https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-compareobjecthandles) подтверждают underlying kernel object; оба handles retained до final wait, close results записаны. Wait/exit failures проверяются. GetLastError сохраняется только для ошибочных результатов.

| DOC-52 identity | Host | Client |
|---|---|---|
|PID / simulation / Worker|14848 /33092 /26544|35852 /16008 /10156|
|Creation FILETIME|134381200802860749|134381201192322358|
|Creation UTC|19:08:00.2860749Z|19:08:39.2322358Z|
|Observer process handle|0x12c, duplicate0x130|0xc0; duplicate в identity JSON|
|Actual signaled observation|19:10:02.713882Z, wait0/code0|19:09:58.068551Z, wait0/code0|

[Host handle identity](evidence/m1/N52_HOST_DIAG_01/OS_HANDLE_IDENTITY.json), [same-handle samples](evidence/m1/N52_HOST_DIAG_01/PROCESS_WAIT.jsonl), [Host signal](evidence/m1/N52_HOST_DIAG_01/OS_SIGNALED.json), [Client identity](evidence/m1/N52_CLIENT_DIAG_01/OS_HANDLE_IDENTITY.json), [Client signal](evidence/m1/N52_CLIENT_DIAG_01/OS_SIGNALED.json). Numeric handles имеют смысл только в соответствующем observer process. Поле `identity` в OS_SIGNALED — сохранённая **начальная** identity, его exit_filetime0 не является новой GetProcessTimes проверкой после выхода.

Сопоставимый [CIM/.NET snapshot обоих живых процессов](evidence/m1/HOST_TERMINAL_52/OS_BOTH_LIVE.json) записал matching path/start times и HasExited=false. После выхода [CIM/UDP snapshot](evidence/m1/HOST_TERMINAL_52/OS_AFTER_CLOSE.json) пуст. Это дополнительные источники, независимый signaled proof — удерживаемый handle.

## 4. Differential DOC-48 / DOC-51

[Полный список source hash differences/equalities/artifacts](evidence/m1/HOST_TERMINAL_52/SOURCE_DIFFERENTIAL.json), [function-body hashes и точные shutdown diffs](evidence/m1/HOST_TERMINAL_52/SHUTDOWN_FUNCTION_DIFF.json). Function-body hashes вычислены над извлечённым UTF-8 source, **не над машинным кодом**.

| Фактор | DOC-48 | DOC-51 | Вывод |
|---|---|---|---|
|EXE / SDL2|b8f131…a76d / f65b50…43a0|Те же exact-build bytes|Изменение game binary не объясняет разницу|
|Host DLL SHA|0e48b179eb955939bafeb8498a5fc2d7230d3d13992ac9423de4a32505680b96|90412198bb08f52b0099426fc07b558aa3fadd2c0a88bfab901d03cc2e8f795f|Build отличается; нельзя называть DLL byte-identical между48/51|
|bootstrap.cpp|31901ae01f23377766f1566ef9320e5f841b03224d4afba9a11e59668b6026c6|Тот же SHA|Все native shutdown/control/lifetime hooks source-identical|
|build_profile.hpp / MinHook|ba09f3a8…3db68 / retained dependency sources|Те же SHA в full differential|Native coordinates/lifetime seam не менялись|
|Worker::stop source body|67a970f8d94d64910bfd41f742073bfc0137892ad3ceced2e086e6d2eaa7e3da|Тот же SHA|stop/join executable source body одинаков|
|Worker::finalizeOnStop|7960ba…08496|d336e7…1d830|Добавлено resetClientInput только при CLIENT; Host branch сохранена|
|Session::stop|8bce3c…47d45d|6d7f96…ca3d|Единственный body diff — additive rejected_generation/jump journal args|
|Остальной Session/Worker/DTO|DOC46/47|DOC49/50/51 additions|Timeout config/stats, sequence ingress, client-only cadence/DTO изменились; это не весь byte-identical network build|
|Client|Headless exe19864,4 threads в snapshot|Native KEX27724 +4887fdd…def3|Два KEX вместо одного; отдельный SDL/graphics/Steam/CRT lifecycle|
|Длительность Host|Примерно94 s от launch report до exit|Примерно646 s до final; network около514 s|Не проверены одинаковые timing/history; причинность длительности UNKNOWN|
|Инструментация Host|Isolation+DLL+DOC48 rich route/SDL/OS probe; later permit|Isolation+DLL+DOC51 OS-only probe+permit|Точки/нагрузка различаются; оба используют Frida; trace complexity не доказывает причинность|
|Frida exit monitor|Отдельный вход ExitProcess не наблюдён|Тоже не наблюдён|Сам факт FinalExit не даёт terminal API stack|
|Окно / foreground|Один KEX, WM_CLOSE с live P2|Второй foreground KEX, явная synthetic Host activation fixture|Конкретная изменившаяся history; Windows focus policy не исправлялась|
|Launch / env|REA Default provider → Python CREATE_SUSPENDED; workspace copy; isolated profile|Тот же механизм; Host copy m0/game, отдельная Client copy|CWD/profile roots разные, argv structure/Valve disable env совпадают; env целиком не снимался|
|Steam / SDL|Нативный stdout и SDL shutdown|Оба KEX сообщают Steam init failure|Steam success/identical IPC состояния не доказаны; Steam API не патчились|
|OS snapshot threads|Host35 + headless4|Host29 + native Client29|Это counts одного момента, не diff ролей; DOC51 terminal snapshot только Host3964|
|OS observer|Launcher wait/exit + snapshots; normal exit|Дополнительный terminal probe code0/wait258 + forced cleanup|В DOC51 probe handle/prototype source не сохранён|

Путь native shutdown одинаков **по source hashes**, но вся DLL/весь транспорт — нет. Наиболее узкий подтверждённый остаток находится после engine cleanup и внутри final-callback/logger/original/CRT/Windows terminal пути либо в корректности старого terminal observer. Уже завершённый ENet join повторять/переустраивать без нового Evidence оснований нет.

## 5. Terminal lifecycle, оставшиеся hooks и Frida

VERIFIED static FinalExit fc580: `sub rsp,0x28; xor ecx,ecx; call [IAT preferred VA0x140482c40]`. IAT RVA0x482c40; imported symbol `API-MS-WIN-CRT-RUNTIME-L1-1-0.DLL::_exit`. После вызова стоят trap padding bytes, нормального RET нет; dossier отмечает computed-call terminator/noreturn. Проверенная сборка предполагает **noreturn `_exit(0)`**, а не новый ExitProcess из mod DLL. Runtime entry оригинала в DOC51 отдельно UNKNOWN. В DOC52 live binding IAT разрешился в ucrtbase.dll+runtime address; это сохранено в [terminal probe config](evidence/m1/N52_HOST_DIAG_01/LEDGER_CONFIG.json), не является entry witness.

Оригинальный fcc20 после adapter вызывает engine a4c90, затем SDL/window/streams cleanup, platform virtual+0x80 → fc580. Engine cleanup действительно вернулся в DOC51. Возврат из platform Shutdown и FinalExit не ожидается по статике; они не объявляются observed return.

После four control restores остаются process-lifetime hooks Tick, StateExit, Shutdown, ActorDtor, PuppetTick, EngineShutdown, FinalExit и VEH. PIN удерживает DLL/static Context/storage; Session pointer намеренно retained, его destructor/Worker join не переносится в CRT global destructor. Собственный DllMain на detach ничего не освобождает; MinHook Disable не освобождает trampolines, RemoveHook/Uninitialize/FreeLibrary отсутствуют. [DOC43 ownership proof](43_M1_DIRECT_QUIT_FINAL.md) и hash equality применимы к этому source path. Это lifetime proof, **не гарантия завершения любого стороннего DLL detach**.

До terminal стадии кроме ENet существуют SDL/audio/input/graphics/OS и Frida threads. DOC51 raw waits показывают завершение нескольких engine threads; runtime roles остальных не восстановлены. После join Worker не может выполнять KEX, но это не означает завершение всех потоков. Нет Evidence, требующего уничтожать Worker/Session повторно или вручную освобождать Player/Puppet.

Frida hypothesis проверена отдельно. [Frida17.23.2 ExitMonitor source](https://github.com/frida/frida-core/blob/17.23.2/lib/payload/exit-monitor.vala) устанавливает listener на ExitProcess; при допустимой invocation depth ждёт подготовку handler. [Agent source](https://github.com/frida/frida-core/blob/17.23.2/lib/agent/agent.vala) готовит sessions и flush connection; async support зависит от присутствия agent thread. Эти sources и hashes [сохранены](evidence/m1/HOST_TERMINAL_52/primary_sources/MANIFEST.json). Installed REA Frida npm package при DOC52 —17.23.2; exact loaded agent version старого run отдельно не удостоверена.

Следовательно, JS callback/IPC/exit preparation способны быть частью terminal path. Но нет старого stack на Frida lock и нет доказанного deadlock cycle. DOC51 close был запрошен **после** final record; сам post-final detach не доказан как trigger initial hang. `close` мог ждать уже непроходимое состояние target. DOC52 оставил instrumentation attached до actual signaled exit; обе sessions стали detached, затем close commands успешно завершились. Это отвергает утверждение «данная Frida/данная pinned DLL всегда блокирует двухпроцессный выход», но не intermittent instrumentation fault.

## 6. Новая OS-диагностика и её пределы

[snapshot.cpp](../experiments/m1/terminal_shutdown/snapshot.cpp) — внешний diagnostic EXE. Проверяет path/PID/creation FILETIME до чтения, снимает Toolhelp modules, documented PSS_CAPTURE_THREADS|THREAD_CONTEXT, WCT, CONTEXT, bounded StackWalk64 и raw stack words. Нет injections, WriteProcessMemory, ручного SuspendThread, drivers или global hooks. PSS/stack acquisition может кратко влиять на scheduling; это не zero-impact observation.

Symbols берутся из локального empty workspace symbol dir с export fallback; stack labels — best-effort. **Module+RVA и unwound frames отдельно от raw candidate stack words**; ближайший экспорт не гарантирует точное имя внутренней функции. PSS сохраняет contexts, а stack bytes читаются позже из живого процесса; atomic VA clone не снимается, поэтому scheduling/изменение stack может ограничивать unwind. WCT не видит все типы locks/spins/ожиданий; cycle0 не доказывает отсутствие всех deadlocks. Success не гарантируется для terminating/protected процесса. Helper имеет внешний timeout25s; в таком случае завершается только helper, target не kill-ится. Minidump ветка подготовлена на случай hang; в этом сценарии она не запускалась и не объявляется проверенной.

Получены [Host live snapshot](evidence/m1/N52_HOST_DIAG_01/LIVE_SNAPSHOT.log):32 threads/277 frame rows; [Client live snapshot](evidence/m1/N52_CLIENT_DIAG_01/LIVE_SNAPSHOT.log):33 threads/266 frame rows. Оба PSS capture/walk/free завершились успешно, WCT cycle1 не записан. Это **живые pre-shutdown snapshots**, не стеки зависшего FinalExit. В normal run terminal snapshot не нужен и не создавался.

Дополнительный [native-only terminal probe](../experiments/m1/terminal_shutdown/terminal_probe.js) ставит пассивные CModule listeners на actual IAT `_exit`, ExitProcess, RtlExitUserProcess, LdrShutdownProcess entry/return и NtTerminateProcess. Фиксированные slots только primary TID, без JS terminal callbacks/KEX writes/locks/IPC. Ledger находится в adapter-owned memory и читается через внешний RPM, чтобы сохранять checkpoints при **длительном hang**, даже если JS перестанет обслуживаться.

**Telemetry limitation OBSERVED:** polling250ms не застал короткий normal terminal interval: предыдущие шесть slots0; после actual signal RPM error299/bytes0, память уже недоступна. Поэтому entry/return этих API и конкретный Windows exit initiator **не объявляются runtime observed**. Это не доказательство обхода hooks. Вход logger/final и действительный OS signal по разным журналам достаточны только для normal-exit результата сценария. Следующая диагностика, если будет отдельно разрешена, должна сохранять terminal entries до reclaim через заранее открытый native checkpoint journal/другой ограниченный durable channel; быстрый polling не гарантирует полноту.

## 7. Единственный bounded runtime DOC-52

До запуска сохранены baseline10900 historical/source/build files, original77/host77/client77/full original/protected hashes, no games/endpoints, exact EXE gates, preflight81 compiled-source hashes относительно **final MAIN DOC51 manifest**. Initial DOC51 manifest отличается от final harness/Client logger fix; это ожидаемая историческая разница, не source drift принятого MAIN build. Повторный reverse/constructor/AI/physics не выполнялся.

Один Host [launch](evidence/m1/N52_HOST_DIAG_01/launch.json), один Client [launch](evidence/m1/N52_CLIENT_DIAG_01/launch.json). Независимые CWD/profiles под `terminal_shutdown/profile/`; использованы прежние independent game copies. Guard/path redirects загружены до resume; [Host isolation gate](evidence/m1/N52_HOST_DIAG_01/ISOLATION_GATE.json) и [Client gate](evidence/m1/N52_CLIENT_DIAG_01/ISOLATION_GATE.json) PASS до Permit, successful observed external disk writes0, errors/overflow0. Это bounded native write-open guard, не полный OS sandbox. [Host raw journal](evidence/m1/N52_HOST_DIAG_01/native.jsonl), [Client raw journal](evidence/m1/N52_CLIENT_DIAG_01/native.jsonl).

Не повторялись W/S proof, weapon/combat/campaign. Neutral INPUT566, Host accepts566, один independent native Spawn,845 native updates; P1 comparisons preserved. Network сессия клиента около9,5 s, Host network около33 s. Эта короткая сессия **не воспроизводит** долгую activation/input history DOC51. Existing ordinary owned-window focus requests/posted menu keys использовались; часть SetForegroundWindow вернулаfalse и сохранена. Новых focus APIs, Windows policy changes или synthetic WM_ACTIVATE fixture в DOC52 не было. Actual sustained foreground ownership не заявляется; game Update/Spawn положительно наблюдались.

Нативный stdout снова сообщил штатный Steam init failure. Steam checks/API/achievements не патчились. Примерная lifetime нового Host122 s вместо646 s DOC51; добавлены native terminal ledger listeners и external live snapshots. Поэтому нормальный выход здесь не является контрольным опытом, изолировавшим ровно один causal factor.

| UTC | TID | Новый Host checkpoint |
|---|---:|---|
|19:09:57.727–.874|Client16008 /Worker10156|Client stop, close socket0x9cc return0, real wait/thread exit0, Update restore, engine return, Final, launcher normal exit0|
|19:09:58.068551|External observer|Client retained process handle signaled0/code0|
|19:10:02.373|33092|Host shutdown_entry, live external handle|
|.377–.380|33092 waits26544|Worker stop/join; socket0x598 close return0 on26544, wait handle0x280 return0/thread code0|
|.381–.499|33092|network retired queues0/0 → четыре exact16 restores|
|.503–.504|33092|Один exit Reset, external handle null, P1 unchanged/guards|
|.507–.582|33092|Engine cleanup entry/return|
|.591|33092|Native final record, no native faults/foreign calls/ticks after shutdown|
|.616262|Launcher|Popen process wait завершён normal exit0|
|.713882|External observer|**Тот же удерживаемый process handle: wait0/code0; duplicate wait0**|
|19:10:06.583648|External OS snapshot|CIM games0 / checked UDP endpoints0|

[Host OS exit trace](evidence/m1/N52_HOST_DIAG_01/STATUS_EXIT.json), [Client OS exit trace](evidence/m1/N52_CLIENT_DIAG_01/STATUS_EXIT.json) not truncated/script errors0. До signal Frida detach не запрашивался. [9 diagnostic sessions closed](evidence/m1/HOST_TERMINAL_52/SESSIONS_CLOSED.json), два external observers вернулиexit0 и закрыли оба handles. All test processes closed. Emergency cleanup calls0. Никакого hidden rerun после PASS нет.

### Сохранённые ошибки подготовки

[HARNESS_ERRORS](evidence/m1/HOST_TERMINAL_52/HARNESS_ERRORS.md) отличает tool transcript excerpts от сохранённых raw logs. Initial CompareObjectHandles import ошибочно искал Kernel32 вместо documented KernelBase; исправлен до fixtures/game. Первый StackWalk fixture обрывался на повторённом initial frame; исправлен и проверен на втором benign Python fixture. Initial REA launcher получил лишний argv0; exited до создания игрового run; после чтения installed manager, который сам prepend-ит program, исправлен только argv. Ранний bind отказал `Unknown owned run` до появления launch.json. Один UI helper отказал unsupported args, первое focus request не нашло ещё не созданное окно. Исправления helper invocation на тех же игровых PID; target/native ABI не менялись.

## 8. Гипотезы и вердикты

| Гипотеза | Evidence / решение |
|---|---|
|ENet Worker не joined / socket открыт|Противоречит положительным OS wait/thread exit/socket close DOC51; в DOC52 также подтверждены. Не основание переписывать stop|
|Двойной Reset / раннее освобождение Player/trampoline/pinned DLL|Нет такого source path; exact restores, один exit Reset, PIN/retention. Не установлена как причина; all-memory safety не доказана|
|Engine a4c90 не вернулся|Отвергнуто для сохранённого пути: есть engine_shutdown_return до final|
|Windows termination уже началось, один thread завис в detach/CRT/lock/spin|Совместимо с terminal thread snapshot; **INFERRED, конкретная фаза UNKNOWN**|
|Final logger/FlushFileBuffers/SRW lock не вернулся|Последняя строка не исключает; нет стека/return witness. **UNKNOWN**, patch не вводился|
|Frida terminal prep/JS wait hook/IPC вызвал hang|Возможный механизм по primary source, старый stack отсутствует. **UNKNOWN**, session close сам по себе не причина|
|Некорректный historical observer/handle/PID reuse|Exact terminal handle/prototype/creation не сохранены. **UNKNOWN**, не объявляется виновником. Новая same-object диагностика дала согласованные результаты|
|Открытые observer handles удерживают unsignaled process|Удержание object lifetime не препятствует signal; новый run вышел с двумя открытыми handles. Не объясняет unsignaled само по себе|
|Два KEX всегда не могут завершиться с этими DLL|Опровергнуто новым bounded scenario; intermittent/timing-dependent дефект не исключён|
|Механизм устранения доказан|**Нет**: несколько runtime условий различаются, cause isolation отсутствует|

## 9. Source/build hashes, регрессии и integrity

Production diff **0**. Все добавления — diagnostics under `experiments/m1/terminal_shutdown/`, новые Evidence/DOC52 и верхние записи PROGRESS/EVIDENCE_INDEX. DOC40–51 и их Evidence не переписаны. Никаких TerminateProcess/ExitProcess substitutes, FreeLibrary pinned DLL, manual object frees, hooks removal ради code0 или изменений DOC48 shutdown.

| Артефакт | SHA256 |
|---|---|
|Reused Host DLL|90412198bb08f52b0099426fc07b558aa3fadd2c0a88bfab901d03cc2e8f795f|
|Reused final MAIN Client DLL|4887fddaa4071321ab9a89fefb5bc8e41426fb58440c7137fd19759a917fdef3|
|Host original build manifest bytes|d797b5e2c268c91e6698f6cfdf9cabe46cdc66589410c8206243f71fb831b265|
|Client original MAIN build manifest bytes|96a500fb179367c5758b496f77ff4806817e11a07ed18f664371ef3cdce7f70f|
|External terminal_snapshot.exe|81a8b86eee3e7edd6873524dd7eb96c21086fa336474b5983bbb2ae55857e259|
|snapshot.cpp|28733cd1856b489074d91d822ab6e4b37c09df266d00434ffc3c943a461d2fae|
|observer.py|d2c0a0f4083a4ef9e83eaa83bb54f7c48dc1965db8c27c9c49d8e45e14b70ccd|

[Manifest всех diagnostic source hashes/frozen harness](evidence/m1/HOST_TERMINAL_52/BUILD_SOURCE_MANIFEST.json); [configure/build logs](evidence/m1/HOST_TERMINAL_52/BUILD_STACKWALK_FIX.log). VS2022/MSVC19.44 SDK10.0.26100.0 Release x64 C++20 /MT /W4 /WX.

Targeted harness tests: [first external observer fixture](evidence/m1/HOST_TERMINAL_52/OBSERVER_TEST.json) и [corrected StackWalk fixture](evidence/m1/HOST_TERMINAL_52/fixture_stackwalk_fix/OBSERVER_TEST.json): active259/wait258 → signaled0/exit37; wrong path/creation rejected; successful external PSS/WCT/free. [CModule compile fixture](evidence/m1/HOST_TERMINAL_52/FRIDA_CMODULE_FIXTURE.json) проверяет компиляцию и non-null native callbacks, **не entry delivery на terminal**. [Review assertions](../experiments/m1/terminal_shutdown/review.py) сверили same handle, actual signal, Worker/socket identity, restores, once-only Reset, quiescence, no native faults, closed sessions/profiles.

Production не менялся: applicable transport/native regression results DOC51 переиспользованы, не выдаются за новые runs. [DOC51 regressions](evidence/m1/NATIVE_SECOND_INSTANCE_51/REGRESSIONS.json): pse46/46, stress11/11, fault16/16, bridge11/11; R1/R2, routing5/330, admission4/112, timeout6/150, sequence boundary282/live12, native-client3/136. DLL/build artifacts и все historical assertions byte-identical; wire1.10, sequence/admission/rejoin/timeout contracts не менялись.

[Integrity console](evidence/m1/HOST_TERMINAL_52/INTEGRITY_CONSOLE.log) / timestamped INTEGRITY JSON: **10900 baseline files unchanged, original77/Host77/Client77/full original snapshot и protected profiles unchanged**. Before/after snapshots каждого нового run равны. У guard registered successful external disk writes0; это не обещание обо всех syscalls/других процессах. Итоговый OS games/endpoints0.

## 10. Приёмка и минимальное продолжение

Полный PASS DOC52 **не присвоен**: требования root cause/mechanism и causal before/after не выполнены. Bounded normal-exit scenario PASS по реальному signaled object; старый DOC51 normal-exit FAIL сохраняется. Исправление не обосновано и не внесено. Неиспользованный verification budget не даёт основания запускать ещё одну диагностическую комбинацию.

Если terminal investigation будет отдельно продолжено: сначала durable native terminal checkpoints (включая logger return / original-entry boundary), затем один согласованный reproduction plan с достаточной duration/activation history; при hang — удерживаемый same-object wait, PSS/WCT/stack до detach/cleanup, explicit errors при недоступности. Только после stack/checkpoint локализации выбирать минимальный harness или lifecycle fix. Старый PID34728 не использовать.

**Остановка после DOC52. DOC53/background Host simulation, replication/camera/weapons/campaign,3–8 players и network refactor не начинались; только по новому заданию.**
