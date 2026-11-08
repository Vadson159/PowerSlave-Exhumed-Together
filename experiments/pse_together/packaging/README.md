# П25 / S9.3 — упаковка и диагностика

2026-11-05, исполнитель/самопроверяющий Codex, режим А.
**Пакет PARTIAL; диагностический ZIP PASS 46/46.** Это не PASS всего П25.
Нативные игровые DLL, loader, wire, хуки и профильная изоляция не менялись.
Новый C++ supervisor обслуживает только собственные standalone ENet EXE.

| Критерий S9.3 | Результат |
|---|---|
| Логи | Новая папка на каждый запуск; console.log, diagnostics.log, session.json; exit code передаётся вызывающему процессу |
| Краш-дампы | Реальная авария собственного native fixture → second-chance MiniDumpWriteDump → MDMP/ExceptionStream с правильным кодом. Только ENet; KEX не покрыт |
| Конфиг | friends.json реально задаёт IP/порт/token/name/id/ёмкость/захват дампов; schema/types/ranges/unknown keys проверяются до запуска. Только standalone ENet |
| RU/EN | [Русская инструкция](README_RU.md), [English instructions](README_EN.md): установка, подключение, ошибки, сбор, приватность, удаление и границы native-reference |
| Набор для друзей | Переносимый диагностический ZIP с тремя static-CRT x64 EXE; отдельный symbols.zip; MIT/BSD лицензии, SHA/size manifest и provenance frozen П1/П24 |
| Чистая установка игрового набора | **НЕ ПРОВЕРЕНО:** fresh-directory на этом ПК не является чистой Windows/машиной друга. Frozen broker ограничен двумя путями workspace, безопасный запуск KEX зависит от принятой REA-изоляции |

В ROADMAP §4.1 для режима приёмки П25 указана «сборка пакета и установка на
чистой машине». Поэтому полный PASS не выставлен. Для него нужен отдельный
проверенный игровой путь установки/профилей и чистая машина; расширение native
loader/isolation требует режима В. П25 не расширяет приёмку П24 или gameplay.
Логи, дампы и конфиг ENet не выдаются за реализацию этих функций внутри KEX.

## Воспроизведение

Команды из корня мода; выходные каталоги evidence должны быть новыми.
Нужны уже используемые в проекте Python, CMake, VS2022 x64/SDK10.0.26100.0.

```powershell
python -X utf8 -B experiments/pse_together/tools/run_offline.py --build-dir b85 --out docs/evidence/m1/P25_PACKAGING/OFFLINE_ALL
& 'C:/Program Files/CMake/bin/cmake.exe' -S experiments/pse_together/packaging -B b85p -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
& 'C:/Program Files/CMake/bin/cmake.exe' --build b85p --config Release
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir b85p -C Release --output-on-failure
& 'C:/Program Files/CMake/bin/cmake.exe' -S tools/enet-prototype -B b85t -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.26100.0' '-DCMAKE_CXX_FLAGS_RELEASE=/O2 /Ob2 /DNDEBUG /Zi' '-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG /INCREMENTAL:NO'
& 'C:/Program Files/CMake/bin/cmake.exe' --build b85t --config Release --target pse_host pse_client
python -X utf8 -B experiments/pse_together/packaging/build_package.py --out docs/evidence/m1/P25_PACKAGING/DIST08
python -X utf8 -B experiments/pse_together/packaging/validate_package.py --dist docs/evidence/m1/P25_PACKAGING/DIST08 --out docs/evidence/m1/P25_PACKAGING/VALIDATION07
python -X utf8 -B experiments/pse_together/packaging/check_integrity.py --out docs/evidence/m1/P25_PACKAGING/INTEGRITY01
```

Основные исходники: [supervisor.cpp](supervisor.cpp), [friends.ps1](friends.ps1),
[builder](build_package.py), [validator](validate_package.py), [integrity](check_integrity.py).
Validator проверяет настоящий ZIP, запускает Windows PowerShell5.1 из иной cwd,
без Python/VS/PowerShell в PATH, в папке с кириллицей/пробелами. Проверены bat-entry,
схема/негативные конфиги, отказ на повреждённом EXE, host/client auth по loopback,
два exit0 и receipts, реальная авария/дамп/exception context, коллекция с редактированием
известных путей/MAC, отсутствие token/config/dumps по умолчанию, opt-in дампа,
отключение захвата, обычный exit1 на занятом порту, exact EXE checker и
побайтовая воспроизводимость обоих архивов при одинаковых входных артефактах.
Заявление о воспроизводимости относится к упаковке, не к полной SHA-повторяемости MSVC.

Integrity переисполняет принятые audit83/supplement83 с заменой только ROOT/OUT,
не меняет predicates и не переписывает старые receipts. Проверены raw exact restores
П24 (4 host +6 client +controller), завершение/16 session-close receipts,
current original77/обе копии/actual5protected. Новых запусков KEX/REA не было.

## Ошибки и сохранённые попытки

Evidence: [P25_PACKAGING](../../../docs/evidence/m1/P25_PACKAGING).

- Первый build standalone целей из корня b85 дал MSB1009: EXCLUDE_FROM_ALL;
  `b85/transport` успешно собрал их, но без PDB в Release. Незавершённые DIST/DIST02
  сохранены. Отдельная b85t использует /Zi и /DEBUG, без правок transport sources.
- VALIDATION01 выявил отсутствие Get-FileHash при урезанном окружении Windows
  PowerShell5.1; SHA заменён на стандартный .NET stream hashing.
- VALIDATION02: ошибка аргумента `value` в самом validator; продукт не запускался.
- VALIDATION03: требуется явная загрузка System.IO.Compression в PowerShell5.1;
  добавлена. Уже тогда положительно проверены auth/exit0 и реальный dump.
- VALIDATION04: 43/43. Дополнены проверки batch entry и обычного ненулевого выхода.
- VALIDATION05: bat зависел от PATH для powershell.exe; исправлен на точный
  системный путь. VALIDATION06: 46/46. Финальная упаковка сохраняет исходные
  имена PDB payload для поиска символов отладчиком; VALIDATION07 проверяет её.
- ENet vendor warnings C4005/C5287 и прежний unused parameter C4100 сохранены
  в build_transport.log. Новый supervisor собирается без предупреждений.

Дампы/архивы не отправлялись. Скрипты не добавляют firewall rules, службы,
реестр, не устанавливают DLL в игру и не предоставляют gameplay permit.
