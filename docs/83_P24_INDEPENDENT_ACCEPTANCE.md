# П24 — независимая приёмка S9.1/S9.2

2026-11-05. Проверяющий — отдельный агент Codex, независимо от исполнителя
[DOC-69](69_P24_PROXY_DLL_LOADER.md) и доработки evidence П24. Режим **В**.

**Вердикт: PASS (bounded) пакета П24 и задач S9.1, S9.2.** Блокирующих
дефектов в проверенном загрузчике не найдено. Зависимость П25 от приёмки П24
снята. Это приёмка самостоятельной загрузки frozen П1 через SDL shim и exact
EXE gate; публичная переносимая сборка и полностью Frida-free процесс KEX
этим решением не аттестованы.

## 1. Основание и независимость

Прочитаны AGENTS.md, CURRENT_STATE, ROADMAP §1/§3/строка П24/S9.1–S9.2,
[DOC-41](41_M1_NATIVE_BOOTSTRAP.md), [DOC-01](01_BINARY_MANIFEST.md),
[lifetime handoff](M0_TO_M1_HANDOFF.md), DOC-69, текущие и замороженные
исходники shim/broker/common/checker/CMake, staging/restore и валидатор исполнителя.
Решение пользователя D5 разрешает новый proxy loader; Frida разрешена для
прежней изоляции и наблюдения. В этих границах S9.1 требует загрузки без
Frida loader, а не удаления принятой защиты профилей из диагностического run.

Проверяющий не запускал игру, не подключался к REA и не повторял принятые
gameplay runs. Runtime выводы получены повторным содержательным чтением
сохранённых raw logs и capture bundles. Собственные скрипты
[audit83.py](evidence/m1/P24_ACCEPTANCE_83/audit83.py) и
[supplement83.py](evidence/m1/P24_ACCEPTANCE_83/supplement83.py) не импортируют
валидаторы исполнения. Их результаты —
[85/85](evidence/m1/P24_ACCEPTANCE_83/AUDIT83.json) и
[19/19](evidence/m1/P24_ACCEPTANCE_83/SUPPLEMENT83.json), вместе **104/104**
сверок evidence/артефактов. Это 104 проверки согласованности, не 104 игровых опытов.

## 2. Содержательное ревью загрузчика

| Область | Независимый вывод |
|---|---|
| SDL ABI | VERIFIED: 738 исходных имён/ordinal, 737 PE forwarders и один локальный `SDL_Init(unsigned)->int`. 99 импортов EXE покрыты; произвольные SDL ABI не реконструируются. Реальный foreign-process resolve по всем именам/ordinal и несколько вызовов forwarders сохранены в OFFLINE_03; свежий loader suite — 756/756. |
| Loader lock | VERIFIED: оба DllMain только сохраняют HMODULE. I/O, LoadLibrary, SHA, APC и Prepare идут из SDL_Init/PSELoaderStart после DLL initialization. Новых KEX hooks/ABI в loader нет. |
| EXE gate | VERIFIED: CNG SHA-256 точного текущего EXE проверяется shim до загрузки broker/payload; broker проверяет его снова. У frozen payload сохранён собственный exact-path/SHA gate. Ошибка чтения/несовпадение закрывает запуск. Сигнатурного обхода нет. |
| Пути и payload | VERIFIED: broker допускает лишь две заданные workspace-копии, точную роль и соседний payload с SHA принятого П1. Log directory сверяется с canonical handle path внутри workspace; ADS/существующий файл отвергаются, payload создаёт журнал через CREATE_NEW. Final handle/module paths сверяются. |
| SHA/загрузка файла | VERIFIED: SDL и runtime читаются с FILE_SHARE_READ, без write/delete sharing; их handles удерживаются во время LoadLibrary/Prepare. Для EXE `checkExe` закрывает свой handle после вычисления SHA; утверждение об удержании всех hash handles до Prepare было бы слишком широким. В этом scope используется уже запущенный exact EXE и повторный gate payload. |
| Ошибка/повторный вход | VERIFIED: InitOnce сериализует initialization; broker возвращает TRUE из callback даже при сохранённом error result, поэтому повторный foreign refusal остаётся защёлкнутым. Shim при ошибке показывает текст и завершает процесс; на продолжение с незаполненным originalInit пути нет. |
| Thread/lifetime | VERIFIED и RUNTIME OBSERVED: Prepare на вызывающем SDL_Init потоке, затем same-process alertable APC; оба журнала подтверждают actual primary TID. Broker/payload закреплены до process exit; FreeLibrary/hot unload/worker teardown нет. Prepare не выдаёт gameplay permit. |

Код ограничен доверенными локальными файлами лаборатории. Это не security
boundary против процесса, который уже может изменять workspace, подменять DLL
или выполнять код внутри игры. PE forwarders используют штатный Windows loader;
проверка EXE не означает, что никакой SDL forwarder не мог быть разрешён ранее.
Эти ограничения не обходят exact gate перед native payload и не блокируют
предъявленные S9.1/S9.2.

## 3. Frozen artifacts и пересборка

Все snapshot hashes и восемь frozen DLL/EXE/PDB файлов из
[ARTIFACT_MANIFEST](evidence/m1/PSE_TOGETHER_P24/ARTIFACT_MANIFEST.json)
пересчитаны. Текущие executable loader sources, SDL2.def, loader CMake и
build_profile совпадают с frozen. Host/client payload побайтово равны принятым
P1_HOST_03/P1_CLIENT_03; непринятые варианты других пакетов не выбираются.

Свежая сборка исполнителя b82 проверена по
[BUILD_EQUIVALENCE](evidence/m1/P24_COMPLETION_82/BUILD_EQUIVALENCE.json).
Проверяющий повторно сравнил её собственным PE parser: **7/7 секций каждого**
SDL2.dll, pe-together.dll и pe-check.exe равны frozen после маски только
timestamps/checksum/debug-directory/CodeView. Анонимные имена и машинный код
не маскировались. Полные SHA rebuilt файлов отличаются из-за build identity;
за runtime эталон остаются frozen hashes, а не эти rebuilt SHA.

Общие [offline результаты](evidence/m1/P24_COMPLETION_82/OFFLINE_ALL/RESULT.json)
сверены с raw CTest: **32/32**, loader contract **756/756**. Компиляция и эти
offline проверки не используются как замена игровому наблюдению.

## 4. Реальная загрузка и безопасный выход

Независимо разобраны
[host native](evidence/m1/PSE_TOGETHER_P24/P24_HOST_01/native.jsonl),
[client native](evidence/m1/PSE_TOGETHER_P24/P24_CLIENT_01/native.jsonl),
[REA_STARTUP](evidence/m1/PSE_TOGETHER_P24/REA_STARTUP.json),
[REA_GAMEPLAY](evidence/m1/PSE_TOGETHER_P24/REA_GAMEPLAY.json),
[REA_EXIT](evidence/m1/PSE_TOGETHER_P24/REA_EXIT.json), launch/gate/stage/restore
receipts и фактические generated_isolation.js.

| Run | Независимо подтверждено |
|---|---|
| P24_HOST_01, PID5948/TID39724 | 2796 последовательных записей, один Prepare/APC/bootstrap, реальные state3/local update и 2687 native_primary_input с растущим tick. Четыре уникальных expected control restores exact16. Shutdown→quiescence→engine shutdown return→terminal, external handle0, foreign_calls0, failed=false, ticks_after_shutdown0, exit0. |
| P24_CLIENT_01, PID31852/TID35152 | 88 последовательных записей, один Prepare/bootstrap, продолжение native state3/local updates. Шесть уникальных proxy restores exact16 и отдельный client_control_restored exact16/status0. World exit→quiescence→engine shutdown return→terminal, player_writes0, foreign_calls0, failed=false, exit0. |

Наблюдатель фиксирует все четыре loader layers по exact соседним путям.
Оба журнала работают на соответствующем primary TID. В bundles только четыре
различных script-load receipts: два exact-hash isolation script и два
read-only module observer; game-session captures содержат только эти пары.
Frida LoadLibrary/M11Prepare/permit и gameplay JS в предъявленных свидетельствах
отсутствуют. Guard preflight предшествует Prepare, summaries без errors/overflow,
observed successful write opens внутри workspace. Gate PASS получен до
какой-либо gameplay выдачи; permit не выдавался вообще.

Host `guards:false` относится к неинициализированному P2 storage при пустом
handle: этот smoke не проверяет initialized P2 guards. Никаких новых
P2/proxy/network sessions или репликации gameplay пакет не доказывает.

## 5. Графический отказ S9.2

Прежний незакрытый GUI witness DOC-69 закрыт новым условием:
[native fixture](evidence/m1/P24_COMPLETION_82/tools/dialog_fixture.cpp)
в собственном foreign EXE вызывает реальный frozen SDL_Init. Thread-local
WH_CBT observer читает реальные #32770/title/child controls и список модулей;
one-shot timer после activation завершает только собственный HWND через
EndDialog. После возврата MessageBox shim сам выполняет ExitProcess(193).
Код fixture не подменяет MessageBox, SHA gate, SDL_Init или ExitProcess.

Проверены [raw UTF16](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_02/dialog_utf16.txt),
[читаемый raw текст](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_02/dialog.txt),
[результат 13/13](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_02/RESULT.json)
и [runner](evidence/m1/P24_COMPLETION_82/tools/run_dialog.py).
Frozen shim hash точен; actual SHA равен фактическому fixture EXE, expected
равен целевому EXE. Реальное окно содержит RU/EN отказ, Win32:193 и сообщение
«Мод не запущен». PID21736/TID9164 принадлежат fixture; timeout=false/exit193.
В inventory нет broker/payload/Frida, broker/payload рядом не размещались.
Это **RUNTIME OBSERVED вне KEX**, пригодное доказательство GUI отказа, без
нового игрового запуска или изменения продукта.

Первая native попытка с преждевременным PostMessage завершилась timeout и
сохранена как [DIALOG_NATIVE_01](evidence/m1/P24_COMPLETION_82/DIALOG_NATIVE_01/RESULT.json).
Её «owned dialog dismissed» означало успешную отправку сообщения, но не
доказанное закрытие; приёмка опирается на новую02 с EndDialog/exit193.
Ошибки сборки fixture также сохранены. Первый собственный audit83 дал85/86:
проверяющий ошибочно ожидал несколько host state3 tick_status после лимита
periodic logger; [ATTEMPT01](evidence/m1/P24_ACCEPTANCE_83/AUDIT83_ATTEMPT01.json)
сохранён. Проверка исправлена на фактические продолжающиеся native input ticks;
продукт и исторический runtime evidence не менялись. Финальный audit исключает
сверку текущего README с frozen README: его статус после приёмки законно
обновляется; hash замороженного README по-прежнему проверяется.

## 6. Целостность и оставшиеся границы

[Свежая integrity](evidence/m1/P24_COMPLETION_82/INTEGRITY.json) проверена
независимым повторным чтением: original77 и обе копии77 совпадают с baseline,
копии не hardlinks оригинала. SDL2 обеих копий восстановлена, четыре добавленных
loader файла отсутствуют. Четыре реальных файла пользовательского `Powerslave EX`
и Steam remotecache (всего5) равны protected_before/protected_after обоих runs и
текущим файлам. Старый generic audit называл отсутствующий `PowerSlave Exhumed`;
это не использовано вместо проверки фактического пути. Raw receipts закрытия
16 собственных REA sessions сохранены; detached/exit0 проверены отдельно от
их `target_state:unknown`. В fresh completion check игровых и diagnostic
процессов не осталось.

**Сохранённые границы PASS:** два exact workspace-пути, frozen П1, один EXE SHA,
host/client bootstrap и выбранный no-permit shutdown. Native isolation без
Frida, portable paths/payload/config, публичная упаковка, LAN/cross-PC,
подключение игроков и игровой кооператив не проверены. Signature search был
необязательным и не реализован. П25 может начинаться после этой приёмки;
полностью Frida-free KEX требует отдельного принятого profile-isolation gate.
