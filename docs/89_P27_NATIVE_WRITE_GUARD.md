# П27 / срезы 1–2 — native write guard, shim v2, pe-play, broker v2 и permit без REA

2026-11-07. Исполнитель и проверяющий — одна модель (Claude) по указанию пользователя; **независимой приёмки режима В не было**. Решения D15a (копия игры) и D15b (изоляция и permit в native) приняты пользователем 2026-11-07 ([предложение](P27_PORTABLE_INSTALL_PROPOSAL.md)).

**Статус среза: offline 70/70 и один guard-only прогон KEX (RUN01, 9/9), PARTIAL.** Прогон выполнен с явного согласия пользователя после бэкапа Saved Games. П27 в целом не закрыт: S9.5, S9.7–S9.11 не начаты; payload и сеть под shim v2 не проверялись.

## Что сделано

Принятая Frida-изоляция (`experiments/m0/isolation_guard.js` write-guard + `observer.js` remaps) перенесена в нативный модуль без Frida: [src/loader/README.md](../experiments/pse_together/src/loader/README.md).

- `pe_guard` — MinHook на `NtCreateFile`/`NtOpenFile`. Политика та же, что у Frida-guard (маска write-intent `0x520d0156`, device exemptions, reparse-предки, `FILE_OPEN_BY_FILE_ID`), плюс перенаправление четырёх классов внешних записей (Saved Games `Powerslave EX`, Steam overlay log, `shadercache\1678430`, AMD DxcCache) в профиль. Один JSONL-журнал уникальных отказов/перенаправлений с access/disposition/options.
- `SDL2.dll` v2 — shim П24 + установка guard в `DllMain`; без env `PSE_GUARD_*` игра отклоняется на `SDL_Init`.
- `pe-play.exe` — лаунчер: SHA EXE, дерево профиля, `steam_appid.txt`, env, `-basepath/-userpath`, CWD=профиль.

## Отличия от Frida-guard (намеренные)

| Что | Frida-guard | native |
|---|---|---|
| Alternate data stream в пределах root | допускался | **отклоняется** (двоеточие в имени сегмента) |
| Открытие существующей папки с `FILE_CREATE`/`OPEN_IF` без прав записи | KnownFolder замещался хуком, Nt-вызов не доходил | пропускается (иначе shell возвращает E_ACCESSDENIED); создать ничего нельзя |
| Redirect Saved Games | `SHGetKnownFolderPath` + `CreateFileW` | на уровне Nt (`early_native_logs`), без хука shell32 |
| Установка | Frida при spawn | `DllMain` shim v2 (статический импорт SDL2.dll, до `main`) |

## Проверки (проверяющий)

[p27.py offline](../experiments/pse_together/tools/p27.py): реальные дочерние процессы, guard ставится в `DllMain` статически импортируемой DLL, как у shim.

- Контроль без guard: запись вне root проходит (тест чувствителен).
- Разрешены: запись/дозапись/чтение/mkdir внутри root; чтение внешнего файла; list внешней папки; NUL; `SHGetKnownFolderPath(SavedGames, KF_FLAG_CREATE)` на реальной папке (mtime не изменился).
- Отклонены (`ACCESS_DENIED`, файла нет): запись вне root, повтор того же пути (в журнале одна запись), delete, delete-on-close, mkdir, `..`, ADS, запись через junction внутри root.
- Перенаправлены: shadercache (файл, подпапка, вложенный файл), Steam overlay log; на исходном месте ничего.
- Стресс 8 потоков × 400: 3200 разрешённых записей, 3200 отказов, 3200 перенаправлений, 3200 чтений, 0 утечек; ledger ограничен 256 уникальными, остальное — счётчик overflow.
- Fail-closed конфиг: профиль вне root, относительный root, журнал вне root, существующий журнал → install отклонён, хуков не осталось, существующий журнал не изменён; повторный install → 183.
- Таблицы: 11 правил redirect (включая негативные), 10 канонизаций; shim v2 exports = E005 (738 имён/ordinal, 737 forwarders, SDL_Init локален), x64, без Frida/Python импортов; `pe-play --dry-run`: отказ без EXE, дерево профиля и `steam_appid.txt`, отказ модифицированного EXE (193).

Итог: [OFFLINE](evidence/m1/PSE_TOGETHER_P27/) **70/70**. Дефект, пойманный проверкой: первая версия отклоняла `FILE_CREATE` существующей папки, из-за чего `SHGetKnownFolderPath(KF_FLAG_CREATE)` возвращал E_ACCESSDENIED (ловушка C06 стенда); правило исправлено, добавлен тест идемпотентного mkdir (183, не 5).

## Прогон в игре RUN01 (guard-only)

[RUN01/RESULT.json](evidence/m1/PSE_TOGETHER_P27/RUN01/RESULT.json) — **9/9**. Условия: чистая копия ровно 77 файлов игры (сверка по SHA с `M0_001_PREPARATION`), `SDL2.dll` = shim v2, штатная SDL в `SDL2_pse_original.dll`, **без broker и payload**, запуск `pe-play.exe` без Frida/REA, один процесс, окно 960×540. До запуска сделаны бэкап реального Saved Games (`RUN01/saved_games_backup`) и [снимок](evidence/m1/PSE_TOGETHER_P27/RUN01/snapshot_before.json): содержимое и mtime файлов и папок, включая родителей `Nightdive Studios` и `Saved Games`.

| Наблюдение | Результат |
|---|---|
| Окно | `Powerslave EX- Version Date: Aug 12 2025 (Vulkan)` через 8,2 с; живо через 20 с |
| Закрытие | штатный `WM_CLOSE`, выход через 1,0 с, код 0 |
| Реальный Saved Games | 6 файлов, содержимое и mtime, а также родительские папки **идентичны до/после**; бэкап = содержимое до |
| Оригинальная установка (77 файлов) и защищённые профили | аудит `game_check` равен до/после, ошибок нет |
| Guard ловит ранние записи | перенаправлены 4 реальных пути (`stdout.txt`, `stderr.txt`, `kexengine.cfg`, `consoleHistory.txt` из реального Saved Games в `profile\early_native_logs`), то есть `DllMain` срабатывает раньше первых записей движка |
| Отказы (6, игра продолжила работу) | write-open с неразрешимым RootDirectory; создание `\Reference`; три записи `C:\WINDOWS\INF\*.PNF` (SetupAPI/HID при опросе устройств); создание каталога `C:\Users\vadso\.cache\AMD` |
| Лог движка | инициализация до «Loading definitions», затем штатное завершение (job system … SDL) |

**Дефектов не найдено, но выводы узкие:** прогон один; источник двух отказов (`<unresolved RootDirectory>` и `\Reference`) не установлен — по логу вреда нет; лог движка после «Loading definitions» молчит до закрытия, экран не проверялся, главное меню не подтверждено; это не проверка payload (второй MinHook вместе с guard) и не сеть.

## Срез 2 — S9.7 и минимум S9.5: запуск и permit без REA (RUN02)

Исполнитель и проверяющий — одна модель; независимой приёмки нет. Игра запускалась с согласия пользователя («Да, делай S9.7») при тех же мерах: бэкап и снимок реального Saved Games до/после.

**Что добавлено**

- `pe_guard`: **чтение** четырёх классов путей тоже отдаётся из профиля (раньше Frida ремапила каждый `CreateFileW`): движок читает `kexengine.cfg` профиля, а не реальный; поэтому `pe-play --role` кладёт в профиль `seta r_rhirenderfamily "d3d11"` (HUD П23 требует D3D11). Остальные чтения проходят без изменений.
- `native/portable_policy.hpp` + правка `M11Prepare` в `host_p23.cpp`/`client_p23.cpp`: при `PSE_GUARD_ROOT` (его ставит `pe-play`) процесс должен быть `<root>\game\anubis_Shipping_Steam_x64.exe`, а DLL и журнал — внутри `<root>`; **без переменной действует прежняя лабораторная политика** (`profile::Executable`, `ClientExe`, `profile::Workspace`). Это правка принятых исходников П23: бинарники теперь другие, чем в DOC-86.
- `pe_together_v2.cpp` — broker v2: корень/роль/профиль/журнал из окружения `pe-play`, SHA payload из `pe-together.ini` (`[payload] host_sha256/client_sha256`, пишет установщик), все прежние гейты П24 сохранены (точный SHA и путь EXE, broker рядом с EXE, канонический каталог нового журнала без reparse/ADS/перезаписи, handle payload держится через `LoadLibrary`, путь загруженного модуля = путь хеша, pin). Принимает `guardInstalled=1` только от shim v2; без подтверждения отказ (5), на чужом EXE 193, отказы защёлкиваются.
- **Самовыдача permit:** после `M11Prepare` и bootstrap APC broker сам вызывает почтовые ящики payload: `M11PermitTest`, `PSE_P23_PermitInterface`, у client ещё `PSE_P1_PermitProxySpawn`. Проверено по исходникам: все три — атомарные флаги без вызова игры и аллокаций; сам геймплей по-прежнему заперт внутри payload (tick/gameplay, spawn-on-ready, запрос UI).
- `pe-play --role host|client`: профиль `profile\<role>`, `PSE_ROLE`, `PSE_NATIVE_LOG`, D3D11-конфиг. `p27.py install <root>` собирает переносимую установку (77 проверенных файлов, shim v2, broker v2, `pse-host.dll`, `pse-client.dll`, ini с SHA, `host.bat`, `join.bat`).

**Проверки**

- [p27.py offline](../experiments/pse_together/tools/p27.py): **85/85** (+13): чтение из профиля (есть/нет файла), роль, D3D11-конфиг, отказ неизвестной роли, broker v2 (экспорты, отказ без подтверждения, отказ на чужом EXE, защёлки в отдельных процессах), установщик (77 файлов, SHA, ini, ASCII-bat, отказ на существующем корне).
- [RUN02/RESULT.json](evidence/m1/PSE_TOGETHER_P27/RUN02/RESULT.json) — **17/17**: host и client одновременно на одном ПК из одного установленного корня, без Frida/REA: оба окна `(Direct X)` через 4–6 с, 25 с работы, `WM_CLOSE` → оба exit 0 за 1 с; реальный Saved Games, родительские папки и original77 неизменны, бэкап совпадает; в обоих `native.jsonl` — `dll_prepared`, `bootstrap_ready`, `p23_graphics_ready`, `p23_overlay` (интерфейс разрешён и рисует), `simulation_attested`, 5/5 хуков восстановлено, `native_final_exit failed=false foreign_calls=0`; нет `test_permit_consumed` и `network_started` (сеть без запроса UI не стартует); guard: все redirect внутри профиля роли, отказы — те же 6 безвредных классов, что в RUN01.

**Не проверено / границы**

- **Лобби не проходилось:** меню CREATE/CONNECT никто не нажимал, host и client между собой не соединялись; значит выдача permit проверена только как «флаги выставлены, интерфейс рисует, ничего не стартует само». Ручная проверка: `RUN02\root\host.bat` и `join.bat` (127.0.0.1).
- Payload — П23 (HOST+P4+P7): **без респауна/оружейной политики П16**, без П14 (AI/подбор на хосте); единой интеграции П16+П23 нет.
- SHA payload берётся из ini рядом с DLL (защита от порчи, не от подмены: файл не подписан); подписанный манифест релиза — за S9.5/S9.10.
- Лабораторный режим payload (без `PSE_GUARD_ROOT`) не перепроверялся прогоном.
- Новые бинарники П23 не проходили независимую приёмку и пересборочное сравнение 7/7 с DOC-86.
- Не покрыто: S9.8 (KEX-дампы), S9.9 (build id), S9.10 (батник релиза игрового набора), S9.11 (чистая машина), два физических ПК.
- **Дефект, найденный при подготовке ручной проверки лобби:** `pe-play --role` сеял в профиль только строку D3D11, поэтому в игре не было клавиш (W/A/S/D, мышь, Tab): так же, как в стенде DOC-85 (там три строки binds вводили через консоль). Исправлено: профиль получает D3D11 и 35 стандартных binds (идентичны лабораторному host-конфигу и реальному конфигу пользователя); посев идемпотентный и дописывает недостающее в конфиг, переписанный движком. Offline теперь **87/87**; смоук [RUN03](evidence/m1/PSE_TOGETHER_P27/RUN03/) (host + client 15 с, оба exit 0, `DirectX 11 Initialized`, graphics ready, binds 35/35 после выхода, реальный Saved Games и original77 неизменны).
- Регрессия после этой правки: [OFFLINE_ALL_06](evidence/m1/PSE_TOGETHER_P27/OFFLINE_ALL_06/RESULT.json) — 38/39, упал `snapshot_contract` (живой ENet-набор, известная чувствительность к нагрузке; в одиночном запуске 1 из 3 раз упал, 2 прошли).
- **Регрессия после среза 2:** [OFFLINE_ALL_05](evidence/m1/PSE_TOGETHER_P27/OFFLINE_ALL_05/RESULT.json) (`run_offline.py --build-dir b94`, сборка не в `b60`) — **39/39**, `p27_guard_contract` 85/85, `p24_loader_contract` прежний. Это единственный чистый полный прогон из пяти: прогоны 01–04 (срез 1) дали 38/39, 37/39, 38/39, 38/39 из-за живых ENet-suites под нагрузкой машины (A/B со старым `b92` — выше).

## Не проверено / границы

- **Не проверено под shim v2:** загрузка принятого payload П1 (broker рядом с shim, второй MinHook в процессе), два процесса, сеть, геймплей, повторные запуски. Это S9.7 и независимая приёмка.
- Не OS sandbox: прямые syscalls, унаследованные writable handles, rename API и другие процессы не покрыты.
- TOCTOU: проверка существования папки и открытие неатомарны; для реальных пользовательских папок риск пренебрежимо мал, но не нулевой.
- S9.5 (broker/payload без жёстких workspace-путей), S9.7 (запуск и permit без REA), S9.8 (KEX-логи/дампы), S9.9–S9.11 — не начаты.
- **Общая регрессия не чистая, 39/39 не получено.** `run_offline.py --build-dir b94`, четыре прогона [OFFLINE_ALL_01…04](evidence/m1/PSE_TOGETHER_P27/): 38/39, 37/39, 38/39, 38/39. `p27_guard_contract` проходит в каждом. Падает вращающийся набор живых ENet-suites (`snapshot_contract`, `pse_fault_injection`, `sequence_live`): в одиночном запуске то 3/3 падает, то проходит; время `pse_fault_injection` 11,4 с при падении и 8,6 с при успехе, фон машины 30–50 % CPU (Spotify, MEGAsync, Telegram). A/B на тех же условиях: **принятый `b92/sequence_live.exe` падает 4 из 4**, `b94` — 3 из 4. Без `p27_guard_contract` один полный ctest дал 38/38, с ним следующий — 37/39, поэтому причиной считаю нагрузку, но чистого 39/39 нет и он не заявляется. Эти наборы не используют ни один файл среза.
