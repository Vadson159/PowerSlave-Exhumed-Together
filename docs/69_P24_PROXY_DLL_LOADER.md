# П24 — Proxy-DLL загрузчик и проверка EXE

> Обновление 2026-11-05: **П24/S9.1/S9.2 — PASS (bounded), режим В**, независимая приёмка [DOC-83](83_P24_INDEPENDENT_ACCEPTANCE.md), завершение [DOC-82](82_P24_COMPLETION_AND_VALIDATION.md). GUI-пробел закрыт настоящим окном frozen shim/exit193; CTest32/32, rebuilt7/7sections и actual protected roots проверены. Ниже сохранён отчёт исполнения от 2026-11-04 с его первоначальными границами. Полностью Frida-free KEX и переносимый релиз не аттестованы.

2026-11-04. Исполнитель: Codex / Sol. **Исходная передача в REVIEW, режим В; bounded loader smoke.**
S9.1 и S9.2 реализованы как кандидат; PASS может поставить только другая модель
(по ROADMAP — Opus extra). П25 ждёт приёмки. Решение пользователя D5 принято
этой постановкой: Proxy-DLL `pe-together.dll`, запрет на новый инжектор снят.
Оригинальная установка и пользовательские сохранения остаются только для чтения.

## Результат и границы

Загрузчик сам загружает принятую native DLL П1, без Frida DLL-loader и без
изменения EXE. Отдельная `pe-together.dll` не является импортом EXE: входом
служит SDL2 shim рядом с EXE. S9.2 проверяет точный SHA-256, сообщает expected/
actual и Win32 error на русском и английском. Сигнатурный поиск не добавлен.

| Наблюдение | Метка и evidence |
|---|---|
| 738 имён и ordinal SDL сохранены; 737 forwarders, локальный SDL_Init; покрыты все 99 SDL imports EXE | VERIFIED, [OFFLINE_03](evidence/m1/PSE_TOGETHER_P24/OFFLINE_03/RESULT.json), сохранённый [E005](evidence/E005_BINARY_MANIFEST.json) |
| В реальном foreign Python child без Frida разрешаются все 738 имён/ordinal; GetVersion=2.0.14, strlen и sin передают аргументы/результаты | RUNTIME OBSERVED вне KEX, [forwarding_stdout](evidence/m1/PSE_TOGETHER_P24/OFFLINE_03/forwarding_stdout.txt); это не gameplay proof |
| Реальный pe-check.exe: exact EXE → 0, изменён один байт → 193, отсутствующий → 2; RU/EN expected/actual SHA | RUNTIME OBSERVED вне KEX, [valid](evidence/m1/PSE_TOGETHER_P24/OFFLINE_03/valid.txt), [changed](evidence/m1/PSE_TOGETHER_P24/OFFLINE_03/changed.txt), [missing](evidence/m1/PSE_TOGETHER_P24/OFFLINE_03/missing.txt) |
| Broker отказывает foreign EXE с 193 до native DLL/APC/log; повторный отказ стабилен; buffer/null проверены | RUNTIME OBSERVED вне KEX, тот же OFFLINE_03, 756/756 checks |
| Host и client в KEX загрузили shim → pe-together → замороженную P1 native DLL; M11Prepare и bootstrap на primary TID; реальные state=3/local_updates>0 | RUNTIME OBSERVED, [RUNTIME_CHECKS](evidence/m1/PSE_TOGETHER_P24/RUNTIME_CHECKS.json), [REA_EXIT](evidence/m1/PSE_TOGETHER_P24/REA_EXIT.json), native logs ниже |
| Exit 0, host 4 exact16 control restores, client 6 exact16 proxy hook restores и client_control_restored, terminal failed=false/foreign_calls=0 | RUNTIME OBSERVED того же loader smoke; P2/proxy не создавались |
| Общая регрессия, включая loader contract | [28/28 suites](evidence/m1/PSE_TOGETHER_P24/OFFLINE_ALL/RESULT.json), все summary patterns проверены общим runner |
| Оригинал77, обе тестовые копии, защищённые профили совпали с baseline; добавленные DLL/ini сняты | [INTEGRITY_FINAL](evidence/m1/PSE_TOGETHER_P24/INTEGRITY_FINAL.json), [host restore](evidence/m1/PSE_TOGETHER_P24/RESTORE_host.json), [client restore](evidence/m1/PSE_TOGETHER_P24/RESTORE_client.json) |

**Полностью Frida-free процесс KEX не наблюдался.** В двух новых игровых runs
Frida обеспечивала прежнюю защиту профилей и пассивное наблюдение; загрузку
DLL/Prepare она не выполняла. Native реализация защиты профилей не входила в
S9.1/S9.2. Это доказательство самостоятельной загрузки, не разрешение обходить
isolation gate и не готовый публичный кооп-релиз. Графическое окно отказа shim
также не аттестовано: диагностический UI-тест не дал пригодного результата;
текст/коды checker и broker проверены отдельно. Подробности — [FAILURES](evidence/m1/PSE_TOGETHER_P24/FAILURES.md).

## Устройство

Код: [loader](../experiments/pse_together/src/loader/README.md),
[SDL shim](../experiments/pse_together/src/loader/sdl_proxy.cpp),
[broker](../experiments/pse_together/src/loader/pe_together.cpp),
[общие SHA/path contracts](../experiments/pse_together/src/loader/loader_common.hpp),
[CLI checker](../experiments/pse_together/src/loader/check_main.cpp).

1. Штатная SDL в тестовой копии сохраняется как `SDL2_pse_original.dll`.
   `SDL2.dll` экспортирует тот же набор имён/ordinal из E005. Внутренний ABI
   остальных функций не угадывается: PE forwarders отправляют их прямо в
   штатную библиотеку. SDL_Init имеет документированный C ABI
   `int SDL_Init(Uint32 flags)` ([официальная SDL2 Wiki](https://wiki.libsdl.org/SDL2/SDL_Init)).
2. При SDL_Init проверяются SHA текущего EXE и штатной SDL, затем по абсолютному
   соседнему пути грузится `pe-together.dll`. Нет нового injector/remote thread,
   изменения import table EXE, KEX JS hooks или gameplay callback через JS.
3. Broker повторяет EXE gate, проверяет точную роль/путь, канонический каталог
   нового native log, SHA payload. Для каждого SHA читается файл через CNG;
   handle не разрешает write/delete sharing и удерживается до окончания LoadLibrary
   и Prepare. Проверяется фактический путь загруженного модуля. Log path с
   reparse/неканоническим каталогом/ADS/существующим файлом отклоняется.
4. Используется **замороженная** принятая DLL П1, переименованная в
   `pse-runtime.dll`. Цели b60, затронутые П4/П14, не используются. Prepare
   дополнительно сохраняет свой прежний exact-path/SHA gate.
5. M11Prepare вызывается на потоке SDL_Init; принятый same-process primary APC
   доставляется через `SleepEx(0, TRUE)`. В observed runs это primary/simulation
   thread. Broker и payload закреплены до выхода процесса. Выгрузки в живом
   процессе нет. `M11PermitTest` и `PSE_P1_PermitProxySpawn` не вызываются.

DllMain обеих новых DLL только сохраняет HMODULE; нет I/O, LoadLibrary,
hooks/worker/wait/cleanup под loader lock. Это следует
[Microsoft DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices).
PE forwarding оформлен через `.def` и промежуточный LIB `.exp`:
[Microsoft EXPORTS](https://learn.microsoft.com/en-us/cpp/build/reference/exports?view=msvc-170),
[LIB/EXP](https://learn.microsoft.com/en-us/cpp/build/reference/working-with-import-libraries-and-export-files?view=msvc-170).
Доставка APC через alertable SleepEx описана в
[Microsoft SleepEx](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-sleepex).

Новых ABI/адресов игровых функций нет. Native contracts, SHA, VA/RVA/file
offset и ограничения переиспользованы из [DOC-41](41_M1_NATIVE_BOOTSTRAP.md),
[handoff](M0_TO_M1_HANDOFF.md), [DOC-61](61_P1_NATIVE_PROXY_DLL.md) и
[DOC-64](64_P1_ACCEPTANCE_REVIEW.md). Нового реверса/повтора принятых runs не было.

## Артефакты

Сборка Release x64 MSVC 19.44.35225, C++20, `/MT /W4 /WX`, короткое дерево
`b69`. [BUILD_04](evidence/m1/PSE_TOGETHER_P24/BUILD_04.log) — рабочий loader;
[BUILD_ALL_02](evidence/m1/PSE_TOGETHER_P24/BUILD_ALL_02.log) — вся база.
Отдельные targets `pse_loader`, `pse_sdl_proxy`, `pse_exe_check`, aggregate
`pse_loader_p24`; входят в `pse_all`. Gameplay source/feature flags, wire v1.10,
Steam calls, native lifecycle П1 не менялись. b60 и принятые DLL П1 не пересобирались.

| Файл | SHA-256 |
|---|---|
| EXE | `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d` |
| Штатная SDL2 | `f65b50d693484d5d5a2bb8df1cf520628dd744e99e9a937bb936839b990943a0` |
| SDL2 shim | `12e3a958f25e61c46015397d4c7141e9f852a2ecbdc210cf55a30ea53eb61f30` |
| pe-together.dll | `d9e0af0e596ebd95d8b03c3aad27db7a38200ac78937df4bac76d6adea2baa25` |
| pe-check.exe | `35dc47a6332fd3963dcad6b270251714c5152f6987d2dae2456cfe58ad0bdb7d` |
| Host payload, DOC-64/P1_HOST_03 | `73b76e1f0698d314905f5991a478237557f50b5c7db4daeac2536a66a9303376` |
| Client payload, DOC-64/P1_CLIENT_03 | `2366326a99ee05c4a9e4ec388431af901cbc15d99d9d427041a2e38c81758e97` |

Тестированные shim/broker hashes из обоих STAGE receipts совпадают с итоговыми
артефактами. Замороженные DLL/PDB/исходники и manifest —
[ARTIFACT_MANIFEST](evidence/m1/PSE_TOGETHER_P24/ARTIFACT_MANIFEST.json),
[artifacts](evidence/m1/PSE_TOGETHER_P24/artifacts),
[sources](evidence/m1/PSE_TOGETHER_P24/sources).

## Новые игровые runs

Порядок и команды — [README loader](../experiments/pse_together/src/loader/README.md).
Инструмент [p24.py](../experiments/pse_together/tools/p24.py) переиспользует
DOC-41/51 launcher, OS isolation и input; единственное намеренное исключение
copy manifest — shim вместо SDL2 с проверенным backup. Оригинал — чтение.
Stage пишет recovery receipt до замены; restore проверяет завершение PID.
Имена уже выполненных runs повторять нельзя.

| Run | PID / primary TID | Факт |
|---|---|---|
| [P24_HOST_01](evidence/m1/PSE_TOGETHER_P24/P24_HOST_01) | 5948 / 39724 | Prepare 13:45:35.687 UTC → bootstrap_ready 13:45:36.122; native tick/state3; native final 13:47:50.373; exit0 |
| [P24_CLIENT_01](evidence/m1/PSE_TOGETHER_P24/P24_CLIENT_01) | 31852 / 35152 | Prepare 13:45:35.700 UTC; native bootstrap/seam/tick/state3; native final 13:47:50.635; exit0 |

Raw [host native](evidence/m1/PSE_TOGETHER_P24/P24_HOST_01/native.jsonl),
[client native](evidence/m1/PSE_TOGETHER_P24/P24_CLIENT_01/native.jsonl),
[host gate](evidence/m1/PSE_TOGETHER_P24/P24_HOST_01/ISOLATION_GATE.json),
[client gate](evidence/m1/PSE_TOGETHER_P24/P24_CLIENT_01/ISOLATION_GATE.json),
[REA startup](evidence/m1/PSE_TOGETHER_P24/REA_STARTUP.json),
[gameplay observation](evidence/m1/PSE_TOGETHER_P24/REA_GAMEPLAY.json),
[exit observation](evidence/m1/PSE_TOGETHER_P24/REA_EXIT.json).
В каждый game session загружены только generated_isolation.js и read-only
module-list observer; ни LoadLibrary/M11Prepare/permit JS, ни gameplay controller
не загружались. Native logs фиксируют loader_tid = primary_tid.

[RUNTIME_CHECKS](evidence/m1/PSE_TOGETHER_P24/RUNTIME_CHECKS.json) — 30/30
сверок **этих** свидетельств, не 30 новых игровых тестов. Recheck без игры:

```powershell
python -X utf8 -B experiments/pse_together/tools/check_p24_runtime.py --bundle docs/evidence/m1/PSE_TOGETHER_P24/REA_EXIT.json --out docs/evidence/m1/PSE_TOGETHER_P24/REVIEW_RECHECK_NEW.json
```

No-permit startup оставляет host storage guards неинициализированными:
`guards:false` в terminal при пустом external handle здесь не трактуется как
проверка initialized P2 guards. В native terminal обоих ролей `failed:false`,
`foreign_calls:0`. Новых P2/Actors, подключения по ENet и gameplay acceptance
этот пакет не утверждает.

Профили свежие; known folder/write guards установлены до resume игры.
Gate выполнен по native tick/guard до какой-либо выдачи gameplay permit
(permit вообще не выдавался). Guard — принятый bounded NtCreateFile/NtOpenFile
и CreateFileW observer, не полный OS sandbox; ограничения direct syscalls,
inherited writable handles, rename APIs/других процессов сохранены.
Post-exit protected snapshots равны pre-run. Оба процесса закрыты штатным
WM_CLOSE, 16 собственных REA sessions закрыты:
[SESSIONS_CLOSED](evidence/m1/PSE_TOGETHER_P24/SESSIONS_CLOSED.json),
[PROCESS_CHECK](evidence/m1/PSE_TOGETHER_P24/PROCESS_CHECK.json).

## Что проверять при приёмке В

- Прочитать native shim/broker и фактические PE exports, проверить SHA gate до
  payload, пути, файлы с reparse/ADS, init-once failure latch и отсутствие работы
  в DllMain. Исходники/бинарники заморожены; самостоятельная пересборка допустима.
- Сверить сохранённые logs/modules/primary TID, отсутствие Frida loader/permit,
  exact restores/terminal и integrity; новый gameplay run для повтора этих фактов
  не нужен.
- Не расширять scope до полностью Frida-free KEX или публичной переносимости:
  текущие accepted payload допускают только два точных workspace-пути; ENet/role
  UI и portable payload/config ещё не продукт. П25 — упаковка, а native profile
  isolation для отдельного Frida-free runtime требует собственной аттестации.
- Графический отказ shim не подтверждён runtime. Если приёмка требует именно
  window proof, нужен исправленный диагностический fixture с новым условием;
  идентичные неудачные вызовы не повторять. Checker/broker evidence сохранить.

Пакет и обе задачи переданы в **REVIEW** с указанными границами. Исполнитель
не ставит PASS режима В и не заявляет готовность продакшн-релиза.
