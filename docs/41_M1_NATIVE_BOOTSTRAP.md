# M1.1 — Native DLL Bootstrap

Дата: 2026-11-01. **PASS в проверенном bounded scope M1.1:** Windows x64 C++20 DLL, отдельный настоящий P2 Puppet,120 собственных native updates/команд, сохранённый P1, безопасный StateExit и последующий native Quit из title screen. Это не приёмка всего M1 или сетевого кооператива. Работа остановлена после M1.1; ENet и Thread Bridge не подключались.

Основание — [полный native journal](evidence/m1/M11_NATIVE_03/native.jsonl), [машинная сверка сохранённого runtime](evidence/m1/M11_NATIVE_03/RESULT_SUMMARY.json), [финальный provider capture](evidence/m1/M11_NATIVE_03/status_final_after_quit.json). Компиляция и exit0 отдельно доказательством не являются. Новый gameplay run содержал ровно одно создание P2 и одну разрешённую последовательность; два предшествующих startup/preflight failures сохранены ниже.

## Проверка критериев

| Критерий | Результат и Evidence |
|---|---|
| DLL загружена без ошибок | `LoadLibraryW`, `M11Prepare=0`; native APC→bootstrap_ready→реальный simulation tick. [Load](evidence/m1/M11_NATIVE_03/load_native.json), journal1–5 |
| P1 продолжает работать | P1 graph/health не изменились при Spawn и движении P2;120 сравнений input/identity/health/inventory/weapon state непосредственно вокруг P2 Update прошли. После последовательности обычный W через очередь окна дал held1→5 и native движение. Journal38–48 |
| P2 — отдельный настоящий Puppet | Native factory type0, отдельные Actor/node/Player, обратные owner/node/payload связи; adapter проверяет штатную Puppet vtable перед принятием identity. [Spawn/sequence records](evidence/m1/M11_NATIVE_03/SEQUENCE_SNAPSHOT.json) |
| Команды реально выполняются |120 P2 command calls,120 native controller calls и120 собственных Puppet world ticks до marker; native XY displacement max220.981552, finite coordinates. Journal39–43 |
| Нет наблюдаемого identity/thread/lifetime нарушения | Все KEX operations/наблюдения journal после bootstrap — TID37472; foreign_calls0, guards=true, blocked/native_fault records0. Полный provider capture113messages, not truncated, script errors0 |
| Безопасный выход выбранного сценария | Один Reset внешнего Player до owned Actor destructor return и original StateExit return; exact control restore; затем Quit без повторного Reset, pinned code/storage до native final exit. [World exit](evidence/m1/M11_NATIVE_03/WORLD_EXIT_SNAPSHOT.json), journal49–69 |

Офлайн validator проверил17 условий согласованности **этого** Evidence; это не17 новых игровых тестов и не замена ручному review. [Integrity](evidence/m1/M11_IMPLEMENTATION_20261101/INTEGRITY.json): оригинал77/copy77 неизменны, защищённые профили before/after равны,89 проверенных файлов M0/транспортного кода не менялись. [Процессы](evidence/m1/M11_NATIVE_03/PROCESS_CHECK.json):0 игровых процессов; [созданные sessions закрыты](evidence/m1/M11_NATIVE_03/SESSIONS_CLOSED.json).

## Сборка и границы изоляции

- EXE: `anubis_Shipping_Steam_x64.exe`, SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Проверена существующая независимая копия `experiments/m0/game`; оригинальная установка использовалась только для чтения/хешей.
- DLL: `experiments/m1/native_bootstrap/build/Release/pse_m11_bootstrap.dll`, SHA-256 **`0cb84521c47dddd20bdebf3a2f42dda79f2f50ef1cfc3c280c14878812648629`**,168448bytes. Для run заморожены [DLL/PDB/исходники](evidence/m1/M11_NATIVE_03/native_artifact/) и [manifest](evidence/m1/M11_NATIVE_03/NATIVE_ARTIFACT.json).
- MSVC19.44.35225, VS2022 BuildTools, SDK10.0.26100.0, C++20, Release x64, `/MT /W4 /WX`. [Полный build result](evidence/m1/M11_IMPLEMENTATION_20261101/BUILD_20261101_202721_800243.json), [PE/imports/exports](evidence/m1/M11_IMPLEMENTATION_20261101/DLL_PE_FINAL.txt): зависимости только KERNEL32/bcrypt; публичные exports — Prepare и mailbox Permit, unload export отсутствует.
- До игры реальная DLL загрузилась в disposable Python process и отклонила его с `ERROR_BAD_EXE_FORMAT(193)` до hooks/APC/KEX calls/создания log. [Preflight](evidence/m1/M11_IMPLEMENTATION_20261101/PREFLIGHT_202724_002501.json) — проверка отказа чужому процессу, не gameplay proof.
- Новый профиль/CWD: `experiments/m1/native_bootstrap/profile/M11_NATIVE_03`; env, ранние SavedGames/log paths, relative saves и Steam AppID file — внутри workspace. Никакие пользовательские saves в профиль не копировались. [Launch](evidence/m1/M11_NATIVE_03/launch.json).
- Использован существующий M0 native OS write-open guard/path adapter без изменения его исходников. [Gate PASS](evidence/m1/M11_NATIVE_03/ISOLATION_GATE.json), затем финальный capture без ledger/provider overflow.169 denied write-open attempts в последней summary не являются успешными внешними записями; успешных внешних CreateFileW write opens не записано. Guard не является полным OS sandbox: direct syscalls, inherited writable handles, rename APIs/другие процессы им не покрыты. Последняя summary предшествует финальным config/history opens; они отдельно записаны CreateFileW observer внутри профиля. Protected hashes проверены после выхода.

## Минимальная реализация

[bootstrap.cpp](../experiments/m1/native_bootstrap/src/bootstrap.cpp) — новый typed native adapter; KEX остаётся opaque. Это не KEX source и не перенос всего Frida fixture. Сохранённые M0 contracts переиспользованы из [M0 handoff](M0_TO_M1_HANDOFF.md), C01/C20/C22/C25; новых Ghidra import/decompile или повторов M0 gameplay experiments не было.

`DllMain` только сохраняет HMODULE. Loader вызывает `M11Prepare` вне loader lock: exact path/SHA gate, новый log с CREATE_NEW, pin модуля через `GetModuleHandleExW(FROM_ADDRESS|PIN)`, same-process APC на suspended primary thread. APC не вызывает KEX; exact-prefix-checked detours устанавливаются на primary thread. Первый реальный tick отдельно подтверждает тот же TID до Player operations. [Microsoft APC contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-queueuserapc), [module pin](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getmodulehandleexw).

Загрузка/изоляция выполняется диагностическим Frida harness. В принятом run03 **нет Frida hooks на KEX или DLL gameplay wrapper** и нет JS-вызовов игровых функций: загружены только OS isolation, DLL loader и atomic Permit. DLL сама подключается к native tick/controller/lifecycle. До свежего isolation PASS и наблюдаемого P1 update Permit не выдаётся; mailbox читается на simulation thread, затем ожидаются60 реальных P1 updates.

P2 получает собственные aligned408bytes в статическом storage DLL с guards. Только в этой новой памяти задаются подтверждённые C01 defaults и self-relative ready refs; singleton initializer не повторяется, живые Player/Puppet не копируются. Native Reset создаёт inventory state; scoped GetPlayer выбирает P2 только при native factory Spawn на simulation TID. После Spawn getter восстанавливается exact16; native weapon-ready получает P2+e0/mode0. Game-owned handle/owner/refcounts меняет native код.

Native P1 Update проходит исходный путь один раз. Затем на том же tick/TID native P2 Update выполняется один раз до штатного actor traversal; сам Puppet вручную не тикается. Только P2 command/turning seams подают45 backward,15 neutral,45 forward,15 neutral. Held counters P2 принадлежат adapter; consuming mouse/controller builders P2 не исполняет. После120 updates scheduling выключается с neutral buttons0; rearm/respawn в M1.1 отсутствует.

MinHook v1.3.4 взят из [официального tag](https://github.com/TsudaKageyu/minhook/tree/v1.3.4), vendored без изменения C sources. [Источник/хеши](evidence/m1/M11_IMPLEMENTATION_20261101/DEPENDENCY.json), [BSD license](../experiments/m1/native_bootstrap/third_party/minhook/LICENSE.txt) сохранены. Сетевых зависимостей нет.

## Native coordinates и ABI

SHA-256 EXE для **всех** строк — `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`. Названия ролей аналитические. VA ниже preferred; фактический адрес = runtime module base + RVA, file offset отдельно. [Полный координатный manifest](evidence/m1/M11_IMPLEMENTATION_20261101/NATIVE_COORDINATES.json) содержит exact16 prefixes и конкретный M0 Evidence для каждой строки.

| Роль | Preferred VA | RVA | File offset |
|---|---|---|---|
| Tick |0x1401b6ec0|0x1b6ec0|0x1b62c0|
| Player Update |0x1401dfe30|0x1dfe30|0x1df230|
| Commands |0x1401e34a0|0x1e34a0|0x1e28a0|
| Turning |0x1401e2ad0|0x1e2ad0|0x1e1ed0|
| GetPlayer |0x1401b5520|0x1b5520|0x1b4920|
| StateExit |0x1401b6670|0x1b6670|0x1b5a70|
| Platform shutdown |0x1400fcc20|0xfcc20|0xfc020|
| Actor base destructor observer |0x14019f210|0x19f210|0x19e610|
| Puppet Tick observer |0x1401db8a0|0x1db8a0|0x1daca0|
| Engine shutdown observer |0x1400a4c90|0xa4c90|0xa4090|
| Native final exit observer |0x1400fc580|0xfc580|0xfb980|
| Player Reset |0x1401dfa70|0x1dfa70|0x1dee70|
| Factory Spawn |0x1401d4780|0x1d4780|0x1d3b80|
| Weapon-ready |0x14020f9e0|0x20f9e0|0x20ede0|

Явные вызовы: Win64 `Reset(Player*)`; factory `(factory*, int type, Vec3 const*, float yaw, int sector)` — RCX/EDX/R8/XMM3, пятый sector на stack; weapon-ready `(P2+e0,int mode0)`; Update `(Player*)`. Остальные detours сохраняют принятый M0 Win64 this-in-RCX contract; getter возвращает pointer, final exit — native noreturn path без аргументов. Другие типы Spawn, weapon modes, скрытые зависимости/все способы вызова этих функций этим run не проверены. Handler Actor destructor **наблюдает** оригинальный вызов движка; adapter не инициирует destructor.

## Фактический run03 — RUNTIME OBSERVED

PID35904; primary/APC/наблюдаемый simulation TID37472. DLL base0x7ffa2d5f0000. Все времена ниже UTC.

| На независимом Spawn | P1 | P2 |
|---|---|---|
| Player |0x140af33e0|0x7ffa2d6170e0|
| Puppet |0x632408f0|0x63241b70|
| Node |0x10e63d18|0x10ad1b40|
| Owner |свой Player|свой Player|
| Health |200|200|

- **20:30:15.052:** independent Spawn, guards=true, P1 graph/input/inventory unchanged, resolver hits2 и exact getter restore. Native type0 вернул отдельный Puppet; не visual proxy.
- **20:30:15.705–16.959:** checkpoints45/60/105/120. P1 origin неизменно `[-7420,-6775,1152]`; P2 реально переместился, max XY220.981552, конечная позиция `[-7418.870117,-6563.549316,1152]`.120 controller/command/native Puppet ticks,120 local-state comparisons, оба health200, faults/foreign calls0.
- **20:31:01.889–.957:** адресованный owned HWND W (`PostMessageW`, не physical input) дал native held_forward1…5 и изменение P1 позиции на14.36 units в самих samples. До unbind накопленное native перемещение P1 составило88.23; graph/health сохранились. [UI/input records](evidence/m1/M11_NATIVE_03/).
- **20:32:00.509:** native меню Exit to Title→Yes вошло в StateExit. Control entries восстановлены exact16. **.636–.638:** единственный exit Reset вернулся, внешний+d8=0, P1 state unchanged/guards=true. **.641–.643:** owned Actor destructor entry/return уже при handle0. **.645:** original StateExit return, owned destructor returns1. После destructor Actor не читается.
- **20:33:34.839–.946:** native title menu Quit→Yes. Handle уже0; второго Reset нет. Quiesce→engine shutdown entry/return→native final exit, pinned code/storage, guards=true, ticks_after_shutdown0, foreign_calls0. **.973:** launcher exit0; финальный provider state detached. Code/storage/trampolines не выгружались.

**Граница Direct Quit:** в этом единственном разрешённом P2 run Quit был **после** StateExit. Ветка новой DLL «Quit при ещё живом P2 без StateExit» не исполнялась; её собственный runtime статус UNKNOWN. Она реализует тот же общий once-only native unbind и принятый [C25 pin-until-exit contract](evidence/m0/C25_NATIVE_DIRECT_QUIT_BOUNDED/RESULT.md). Не выдавать этот run за повтор C25/live-P2 Direct Quit. Выполненный StateExit→title Quit полностью закрывает безопасный выход выбранного M1.1 сценария.

## Сохранённые ошибки и ограничения

1. [M11_NATIVE_01](evidence/m1/M11_NATIVE_01/RESULT.md): Python launcher не нашёл preparation manifest после переноса OUT. Игра/профиль/DLL не запускались; полный traceback сохранён. Исправлено предоставлением byte-identical read-only manifest copy в M1 Evidence.
2. [M11_NATIVE_02](evidence/m1/M11_NATIVE_02/RESULT.md): DLL bootstrap/tick работали, но periodic log завершился до gameplay. Одноразовый поиск собственного DLL symbol через DebugSymbol запустил PDB search по загруженным модулям и переполнил256-path guard ledger. Gate PENDING, **Permit/P2/Reset/Spawn0**. Полный raw capture317messages/not truncated и исходная DLL сохранены; owned WM_CLOSE штатно закрыл процесс. Guards=false означали неинициализированный для P2 storage, не его повреждение.
3. Для run03 исправлен только readiness diagnostic: первый реальный P1 update логируется независимо от длительности menu. Symbol lookup/дополнительный wrapper observer не использовались; native ABI/lifecycle механизмы не менялись. Журнал guard больше не переполнялся.
4. Это exact-build experimental adapter с диагностическим Frida loader/OS guard. Нет автономного distribution/injector, production input queue, camera/HUD integration или приёмки всего hardware input. P2 native13-button polling loop всё ещё выполняется;120 проверок сохранения P1 и отдельный W — только bounded наблюдение.
5. Модуль, Player storage и return trampolines намеренно pinned до завершения процесса, в том числе после StateExit. Произвольный hot unload, general allocator free, alternate/crash exits и полный asynchronous exception/unwind contract UNKNOWN. Native fault logger покрывает AV/illegal instruction/stack overflow;0 recorded faults не означает полную трассировку всех исключений.
6. Не проверялись сеть/Thread Bridge,3–8 игроков, combat/оружейный урон, respawn, campaign/save-load, полные transitions, достижения и длительные сессии. Никакого принудительного refcount cleanup, target/sourceOwner repair, global arena teardown или вызова destructor adapter не делает.

## Изменённые файлы и воспроизведение

Новые файлы:

- `experiments/m1/native_bootstrap/src/bootstrap.cpp`, `src/build_profile.hpp` — DLL и version-locked profile.
- `CMakeLists.txt`, `generate_profile.py`, `build.py` — C++20 x64 build и retained build logs.
- `runner.py`, `ui_sequence.py`, `review_results.py`, `README.md` — diagnostic launch/isolation/mailbox/UI helper, offline Evidence review и инструкция.
- `third_party/minhook/` — pinned official source/license; `build/Release/` — DLL/PDB; `profile/` — только отдельные тестовые профили.
- `docs/evidence/m1/` — baseline/dependency/coordinates/build/preflight, все failures, raw runtime и frozen artifacts. [M1_FILES manifest](evidence/m1/M1_FILES.json) перечисляет хеши файлов; [последний harness snapshot](evidence/m1/M11_IMPLEMENTATION_20261101/final_harness/) сохраняет диагностические дополнения после freeze DLL.

Обновлены этот DOC-41, [PROGRESS](PROGRESS.md) и [EVIDENCE_INDEX](EVIDENCE_INDEX.md). M0/ENet/Thread Bridge исходники и DOC-40 не редактировались.

Сборка: `C:/Python314/python.exe -B experiments/m1/native_bootstrap/build.py`; rejection preflight — `runner.py preflight`. Для нового отдельно разрешённого runtime использовать новый run/profile/PID согласно [README](../experiments/m1/native_bootstrap/README.md), без старых bound sources. Порядок: provider launch→bind→attach→OS isolation+native loader→primary resume→реальный gameplay/native P1 witness→save full status→gate→Permit. В выполненном run native menu start был7×Return до карты и ещё Return для входа; все screenshots/key journals сохранены. После sequence — W, ESC/4×Down/Return/Left/Return для StateExit; на title Return/Up/Return/Left/Return для Quit. Каждую стадию проверять по собственному screenshot/state, не слепо повторять меню.

Офлайн повторная сверка без запуска игры: `C:/Python314/python.exe -B experiments/m1/native_bootstrap/review_results.py`.

**M1.1 завершён. M1.2, ENet и Thread Bridge integration требуют нового задания.**
