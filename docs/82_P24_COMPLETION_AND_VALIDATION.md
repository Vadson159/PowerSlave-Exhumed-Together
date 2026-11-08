# DOC-82 — завершение П24: загрузчик и SHA EXE

2026-11-05. Исполнитель — Codex. Пакет **П24**, задачи **S9.1 и S9.2**, режим **В**. Реализация [DOC-69](69_P24_PROXY_DLL_LOADER.md) сохранена без изменений; результат передан независимому проверяющему. Целевой EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d`.

## Результат

Принятые игровые runs не повторялись. SDL2 shim, broker `pe-together.dll`, checker и frozen P1 payload не менялись. Проверены текущая сборка, исходные runtime evidence, точное восстановление хуков и неизменность оригинала/профилей. Закрыт прежний пробел S9.2: отдельный нативный диагностический процесс без игры и Frida наблюдал **настоящее окно отказа frozen shim**, его RU/EN текст, ожидаемый и фактический SHA, Win32 193 и последующий выход процесса с кодом 193.

| Проверка | Результат и evidence |
|---|---|
| Общая сборка и CTest, самостоятельные summary каждого набора | **32/32 PASS**, [RESULT](evidence/m1/P24_COMPLETION_82/OFFLINE_ALL/RESULT.json), [build](evidence/m1/P24_COMPLETION_82/OFFLINE_ALL/build.log), [CTest](evidence/m1/P24_COMPLETION_82/OFFLINE_ALL/ctest.log) |
| Контракт загрузчика на свежих артефактах b82 | **756/756**, [raw output](evidence/m1/P24_COMPLETION_82/OFFLINE_ALL/logs/p24_loader_contract.log): 738 exports/ordinal, forwarders, checker exact/changed/missing, broker foreign refusal |
| Воспроизводимость SDL2.dll / pe-together.dll / pe-check.exe | У каждого **7/7 PE-секций и exports совпали** после штатного исключения build/debug identity, [BUILD_EQUIVALENCE](evidence/m1/P24_COMPLETION_82/BUILD_EQUIVALENCE.json). Полные SHA свежей сборки отличаются |
| Настоящий GUI отказ frozen shim в foreign native child | **13/13 PASS**, [RESULT](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_02/RESULT.json), [текст окна и modules](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_02/dialog.txt), [UTF-16 raw](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_02/dialog_utf16.txt) |
| Frozen sources/artifacts, исходники загрузчика, EXE/copies, профили и отсутствие оставшихся процессов | **41/41 PASS**, [COMPLETION_CHECKS](evidence/m1/P24_COMPLETION_82/COMPLETION_CHECKS.json) |
| Оригинал и реальные protected roots | [INTEGRITY](evidence/m1/P24_COMPLETION_82/INTEGRITY.json): все **77** файлов оригинала, обе независимые копии и **4 файла Powerslave EX + Steam remotecache.vdf** совпали с prelaunch baseline |

Это разные уровни проверки. CTest и PE equivalence — **VERIFIED** offline/static. Окно отказа и exit193 — **RUNTIME OBSERVED вне KEX**. Самостоятельная загрузка П1 в KEX, primary APC/bootstrap, simulation tick, exact restores и exit0 обеих ролей — **RUNTIME OBSERVED** из сохранённых [P24_HOST_01](evidence/m1/PSE_TOGETHER_P24/P24_HOST_01/native.jsonl) и [P24_CLIENT_01](evidence/m1/PSE_TOGETHER_P24/P24_CLIENT_01/native.jsonl), независимо проверяемых в режиме В. Новых игровых наблюдений DOC-82 не заявляет.

## Закрытие GUI-пробела S9.2

Новый [диагностический C++ EXE](evidence/m1/P24_COMPLETION_82/tools/dialog_fixture.cpp) вызывает настоящий `SDL_Init(0)` неизменённого frozen shim. Собственный thread-local `WH_CBT` callback наблюдает `HCBT_ACTIVATE` окна `#32770`, проверяет его PID/TID и заголовок, считывает реальные дочерние controls и текущий список модулей. Он не заменяет `MessageBoxW`, `checkExe` или `ExitProcess`. После начала modal loop одноразовый timer закрывает только это собственное окно через `EndDialog(IDOK)`; затем реальный shim исполняет свой `ExitProcess(193)`.

Foreign EXE имеет SHA `6a0e43b7659c3be609ab7daf13885c9ecc26761eee874a4731eac3237a186e92`. В окне получены этот SHA, целевой expected SHA, RU/EN ошибка и код 193. Module inventory содержит shim, но не broker/payload/Frida. Broker и payload в fixture даже не размещались. Никакие файлы игры или сохранений этим тестом не открывались для записи. Это подтверждение window creation/control contents/error path, без утверждения о визуальной оценке скриншота.

Сохранены ошибки диагностического стенда:

1. [DIALOG_BUILD](evidence/m1/P24_COMPLETION_82/DIALOG_BUILD.log) — в новом fixture отсутствовал `psapi.h`; добавлен header, последующие [BUILD_02](evidence/m1/P24_COMPLETION_82/DIALOG_BUILD_02.log) и [BUILD_03](evidence/m1/P24_COMPLETION_82/DIALOG_BUILD_03.log) успешны. Продуктовый код не менялся.
2. [DIALOG_NATIVE_01](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_01/RESULT.json) — настоящее окно и его текст получены, но `WM_COMMAND` на activation не завершил dialog. Child завершён по timeout; это **FAIL**, не exit193. Исходники первой попытки сохранены рядом. В попытке 02 изменено условие: timer после activation вызывает `EndDialog` в modal loop; подтверждены штатный возврат окна и реальный exit193. Идентичный неудачный вызов не повторялся.
3. При первоначальной ручной конфигурации fixture PowerShell разбил некавыченный `-DCMAKE_SYSTEM_VERSION=10.0.26100.0`; CMake сам выбрал требуемый SDK 10.0.26100.0. Для воспроизведения ниже аргумент заключён в кавычки. Основной runner передавал аргументы непосредственно и этой ошибки не имел.

## Целостность и завершение

Старый общий audit в DOC-69 проверял отсутствующий путь `PowerSlave Exhumed`; фактический путь сохранений — `Powerslave EX`. Новая проверка использует оба `protected_before.json` из игровых runs, сопоставляет их с `protected_after.json` и самостоятельно вычисляет текущий snapshot каждого реального protected root. Совпали имена, размеры и SHA всех пяти файлов, добавлений нет. Старые receipts не переписаны. Полный snapshot оригинальной установки равен исходному, 77 файлов обеих копий побайтно эквивалентны исходным и не являются hardlink на оригинал; stage-файлы отсутствуют.

Проверка текущих процессов не нашла KEX и диагностические children. В DOC-69 сохранены exit0 обоих KEX, restoration receipts и [закрытие 16 run-owned REA sessions](evidence/m1/PSE_TOGETHER_P24/SESSIONS_CLOSED.json); в этой работе REA sessions не создавались. Exact restores подлежат отдельной сверке проверяющим по raw logs и исходному контракту.

## Воспроизведение

Пути evidence и build должны быть новыми: команды не предназначены для перезаписи завершённых receipts.

```powershell
python -X utf8 -B experiments/pse_together/tools/run_offline.py --build-dir b82 --out docs/evidence/m1/P24_COMPLETION_82/OFFLINE_ALL
& 'C:/Program Files/CMake/bin/cmake.exe' -S docs/evidence/m1/P24_COMPLETION_82/tools -B b82d -G 'Visual Studio 17 2022' -A x64 '-DCMAKE_SYSTEM_VERSION=10.0.26100.0'
& 'C:/Program Files/CMake/bin/cmake.exe' --build b82d --config Release
python -X utf8 -B docs/evidence/m1/P24_COMPLETION_82/tools/run_dialog.py
python -X utf8 -B docs/evidence/m1/P24_COMPLETION_82/tools/check_completion.py
```

Скрипты [run_dialog.py](evidence/m1/P24_COMPLETION_82/tools/run_dialog.py) и [check_completion.py](evidence/m1/P24_COMPLETION_82/tools/check_completion.py) привязаны к указанным деревьям и именам; новый повтор требует новых output names. Самостоятельную сверку сохранённых файлов можно выполнять без запуска игры.

## Граница П24

S9.1 — самостоятельный proxy loader без Frida DLL-loader, два принятых workspace-пути и frozen P1 DLL. В игровых evidence Frida оставалась средством прежней изоляции и наблюдения, как разрешено D5. **Полностью Frida-free процесс KEX и portable production-release остаются UNKNOWN.** Native profile isolation, конфиг подключения, роль/порт/UI и набор для друзей не добавлены. П25/S9.3 и финальные полевые тесты не входят в П24. S9.2 — точный SHA с понятным сообщением; сигнатурный поиск необязателен и не реализован.

**Итог:** независимый проверяющий принял П24/S9.1/S9.2 как **PASS (bounded)**, [DOC-83](83_P24_INDEPENDENT_ACCEPTANCE.md). По этому вердикту обновлены ROADMAP, PROGRESS, CURRENT_STATE и EVIDENCE_INDEX; прочие пакеты не закрывались.
