# П24: загрузчик Proxy-DLL / exact EXE gate

**PASS (bounded), режим В**, 2026-11-05 — независимая приёмка [DOC-83](../../../../docs/83_P24_INDEPENDENT_ACCEPTANCE.md), завершение [DOC-82](../../../../docs/82_P24_COMPLETION_AND_VALIDATION.md), реализация [DOC-69](../../../../docs/69_P24_PROXY_DLL_LOADER.md). Fresh CTest 32/32, loader 756/756; actual GUI отказ frozen shim/exit193 подтверждён13/13 вне KEX без Frida. Полностью Frida-free KEX и portable release не аттестованы.
Принятое пользователем D5, 2026-11-04: Proxy-DLL `pe-together.dll`, новый инжектор разрешён.

`pe-together.dll` сама не импортируется игрой. Поэтому entry shim — `SDL2.dll`:
738 исходных экспортов/ordinal, 737 PE forwarders на `SDL2_pse_original.dll`,
только `SDL_Init(unsigned)->int` вызывает broker перед оригинальным SDL_Init.
Игра/SDL/Steam EXE не патчатся. Штатная SDL только переименовывается в тестовой копии.
Оба DllMain сохраняют HMODULE, без загрузки DLL, hooks, потоков или cleanup.

Shim проверяет SHA EXE и штатной SDL, broker повторяет SHA EXE и проверяет
канонические пути/роль/SHA замороженной DLL П1. Неизвестный EXE — понятное
RU/EN сообщение с expected/actual SHA и ExitProcess(193), до загрузки broker/payload.
`pe-check.exe <EXE>` показывает тот же текст и проверяет файл без запуска игры;
stdout — UTF-16LE (для перенаправления в файл). Ошибка чтения возвращает Win32 code.
Автопоиска адресов/обхода SHA gate нет.

Broker грузит соседнюю `pse-runtime.dll`, вызывает принятый `M11Prepare` на
потоке SDL_Init и доставляет его APC через SleepEx(0,TRUE). Payload/broker
закреплены до выхода процесса. Выгрузка в работающей игре не поддерживается.
Загрузчик **не** вызывает M11PermitTest/PSE_P1_PermitProxySpawn; сеть, P2 и proxy
не включаются до отдельной принятой игровой процедуры. Это загрузчик текущей
лабораторной DLL, не готовый публичный кооп-релиз.

## Сборка и проверки

```powershell
cmake -S experiments/pse_together -B b69 -G "Visual Studio 17 2022" -A x64
cmake --build b69 --config Release --target pse_loader_p24
python -X utf8 -B experiments/pse_together/tools/p24.py offline auto
```

Выходы `b69/p24/`: `SDL2.dll`, `pe-together.dll`, `pe-check.exe`.
`SDL2.def` выведена из сохранённого E005, без нового реверса.
MSVC 19.44 mixed-local/forward .def давала LNK2001; CMake сначала создаёт
`.exp` через LIB, затем линкует его как external object. Offline проверяет
**фактическую** PE export table, runtime resolve по имени/ordinal и ABI forwarders.

`pse_all` включает загрузчик; `p24_loader_contract` входит в общую команду
`tools/run_offline.py` вместе с остальными 31 suites (32 на дату приёмки). Флаг среды
`PSE_P24_BUILD_DIR` выбирает каталог артефактов только для offline-стенда.

## Ограниченный игровой стенд

Поддержаны только точные существующие тестовые EXE по путям DOC-41/51 и
замороженные P1_HOST_03/P1_CLIENT_03 (DOC-64), SHA заданы в loader_common.hpp.
Это намеренная граница: новый b60 может включать непринятые правки П4/П14.
Переносимость путей, UI роли/порта, конфиг сетевой игры и набор для друзей — другие пакеты.

Рядом с EXE: shim, `SDL2_pse_original.dll`, `pe-together.dll`, `pse-runtime.dll`,
UTF-16 `pe-together.ini`:

```ini
[loader]
role=host
native_log=<absolute fresh workspace directory>\native.jsonl
```

`native_log` обязан ещё не существовать; его каталог — канонический, внутри
workspace. Установка/снятие в стенде выполняются `tools/p24.py stage|restore`.
Оригинальная Steam-установка отвергается; все 77 файлов тестовой копии до stage
сверяются, hardlinks на оригинал запрещены. Restore требует завершённого PID.

Игровой запуск — через прежний REA Python spawn lane:

1. `p24.py baseline`, `stage host <NEW_HOST_RUN>`, `stage client <NEW_CLIENT_RUN>`.
2. В REA spawn Python: `p24.py launch <role> <run>`; resume helper, игра пока suspended.
3. `p24.py bind <role> <run>`; REA attach к игре и загрузка **только** generated_isolation.js.
4. Через REA Python helper: `p24.py action <role> <run> resume`.
5. UI/input — тот же принятой OS-стенд (`action ... ui_start`, `input focus <role>`, обычный RETURN на карте).
6. Сохранить status/bundle, isolation gate, native logs; не выдавать gameplay permit в loader smoke.
7. Через REA helper `action ... ui close`; дождаться exit.json; снять stage,
   сверить integrity, закрыть свои REA sessions.

Завершённые P24_HOST_01/P24_CLIENT_01 не повторять. Новый run требует нового
receipt; исторические STAGE/RESTORE/evidence не перезаписывать.

Игра в DOC-69 имела Frida **только для прежней защиты профилей и наблюдения**;
загрузка DLL выполнена proxy самостоятельно. Полностью Frida-free процесс KEX
не аттестован: перенос защиты профилей в native код не входил в S9.1/S9.2.
Проверки forwarding/checker/foreign-process refusal выполнялись без Frida.

## П27 / S9.6: native write guard, shim v2, `pe-play.exe` (срез 1, offline)

**Статус: offline PASS 70/70, в игре не запускалось.** Исполнитель и проверяющий — одна модель по указанию пользователя; независимой приёмки В не было. Отчёт — [DOC-89](../../../../docs/89_P27_NATIVE_WRITE_GUARD.md).

| Файл | Роль |
|---|---|
| `pe_guard.cpp/.hpp` | Перенос принятого Frida write-guard (`experiments/m0/isolation_guard.js`) и четырёх path remaps (`observer.js`) в native: MinHook на `NtCreateFile`/`NtOpenFile`; запись вне `PSE_GUARD_ROOT` — `STATUS_ACCESS_DENIED`; Saved Games / Steam overlay log / shadercache 1678430 / AMD DxcCache — перенаправление в профиль; reparse-предки, ADS, `FILE_OPEN_BY_FILE_ID` отклоняются; существующая папка при `FILE_CREATE`/`OPEN_IF` без прав записи пропускается (иначе `SHGetKnownFolderPath(KF_FLAG_CREATE)` падает с E_ACCESSDENIED) |
| `sdl_proxy_v2.cpp` | Shim v2 = shim П24 + установка guard в `DllMain` (до `main` EXE и до `-userpath`); без env `PSE_GUARD_*` игра отклоняется на `SDL_Init` (fail-closed). P24 broker (`pe-together.dll`) грузится, только если лежит рядом |
| `pe_play.cpp` | `pe-play.exe [--root R] [--dry-run] [-- аргументы игры]`: проверяет SHA EXE, создаёт дерево профиля (как `launch.py`), `steam_appid.txt`, env (APPDATA/LOCALAPPDATA/TEMP/USERPROFILE, отключение Valve Vulkan-слоёв, `PSE_GUARD_*`), стартует игру с `-basepath/-userpath`, CWD=профиль |
| `guard_test.cpp` | Только offline: `guard_probe.dll` (имитация shim) + `guard_fixture.exe` |

Сборка: `cmake --build b94 --config Release --target pse_loader_p27`; проверка: `python -X utf8 -B experiments/pse_together/tools/p27.py offline auto` (набор `p27_guard_contract` входит в общий `run_offline.py`). Guard — не OS sandbox: прямые syscalls, унаследованные writable handles, rename API и другие процессы не покрыты (границы те же, что у Frida-guard). Ограничения среза 1: broker и payload остаются привязаны к workspace (S9.5), permit не выдаётся (S9.7), KEX-дампы/логи не покрыты (S9.8).

### П27 / срез 2 (S9.7, минимум S9.5): broker v2, permit без REA, `pe-play --role`

Offline 85/85, прогон RUN02 17/17 (host + client одновременно на одном ПК, оба exit 0, Frida/REA не использовались), [DOC-89](../../../../docs/89_P27_NATIVE_WRITE_GUARD.md). Лобби CREATE/CONNECT и связь host-client не проходились; независимой приёмки не было.

| Файл | Роль |
|---|---|
| `pe_together_v2.cpp` | Broker v2 (`pe-together.dll`): корень/роль/профиль/журнал из окружения `pe-play`, SHA payload из `pe-together.ini` (`[payload] host_sha256/client_sha256`), прежние гейты П24; после `M11Prepare` и APC сам вызывает `M11PermitTest`, `PSE_P23_PermitInterface`, у client `PSE_P1_PermitProxySpawn`; принимает `guardInstalled=1` только от shim v2 |
| `../native/portable_policy.hpp` | Политика корня установки для payload П23 (`PSE_GUARD_ROOT`); без переменной действует лабораторная |
| `pe_play.cpp` | `--role host\|client` (профиль `profile\<role>`, D3D11-конфиг, `PSE_ROLE`, `PSE_NATIVE_LOG`) |

Установка: `python -X utf8 -B experiments/pse_together/tools/p27.py install <новая папка>` создаёт `game\` (77 проверенных файлов, shim v2, broker v2, `pse-host.dll`, `pse-client.dll`, ini) и `pe-play.exe`, `host.bat`, `join.bat`. Установка — копия файлов игры, Steam-установка не затрагивается (D15a).
