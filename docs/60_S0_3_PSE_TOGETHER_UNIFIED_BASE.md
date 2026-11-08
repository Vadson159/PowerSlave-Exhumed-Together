# DOC-60 / S0.3 — единая кодовая база `pse_together`

> 2026-11-03, Sonnet 5.5 (high). Задача S0.3 из [ROADMAP](ROADMAP.md). **Только offline:** игра, Frida/REA, DLL-загрузчик и второй процесс KEX не запускались; runtime-приёмки нет. EXE SHA-256 `b8f131037187ca8d0a282527fb6f55a2012241d5b9dac8bf2038baf35b71a76d` (обе тестовые копии сверены перед прогоном, не менялись). Статус: **PASS в заявленных offline-границах** — входная проверка П1, 2026-11-04 ([review](evidence/m1/PSE_TOGETHER_P1/S03_ENTRY_REVIEW.md), [машинная сверка](evidence/m1/PSE_TOGETHER_P1/S03_ENTRY_CHECKS.json)). Сохранённые исследования и прогоны при этой проверке не повторялись.

Входная проверка подтвердила 55/55 source hashes, 31/31 пары UNIFICATION_MAP, MinHook 16/16, журналы 23/23 наборов и сохранённые DLL-сравнения. Текст ниже описывает исходную работу S0.3; последующие изменения канонического `experiments/pse_together/` и новый runtime-кандидат П1 — [DOC-61](61_P1_NATIVE_PROXY_DLL.md).

## Итог

Лабораторные копии M1 сведены в один CMake-проект [`experiments/pse_together/`](../experiments/pse_together/README.md):

- **Роли и флаги** (`PSE_ROLE_HOST` / `PSE_ROLE_CLIENT`, `PSE_FEATURE_SNAPSHOT` / `_MAPREADY` / `_VISUAL_PROXY`) заменили разрозненные макросы `PSE_M12A` / `PSE_DOC55_SNAPSHOT` / `PSE_DOC56_MAPREADY` / `PSE_DOC58B`. Недопустимые сочетания — ошибка компиляции ([`config.hpp`](../experiments/pse_together/include/pse_together/config.hpp)).
- **Один исходник на роль:** `host.cpp` (был `bootstrap.cpp`), `client.cpp` (был `client.cpp` DOC-56 + `client58b.cpp` DOC-58B — это один файл плюс diff). Четыре DLL-варианта собираются из них флагами.
- **Один автомат прокси:** `proxy/visual_proxy.hpp` = DOC-58B.2 (надмножество DOC-58B), namespace `pse::proxy`. Контракты DOC-58B (32) и DOC-58B.1 (18) прогоняются **без изменения тел тестов** на объединённом заголовке — это регрессия слияния.
- **Одна команда** для всех offline-наборов: `python -X utf8 -B experiments/pse_together/tools/run_offline.py` (сборка + `ctest` + сверка итоговой строки каждого набора, а не одного exit 0).

## Результат (VERIFIED, [RUN_20261103_223452](evidence/m1/PSE_TOGETHER_S03/RUN_20261103_223452/RESULT.json))

Чистая сборка `b60/` (configure 0, build 0, диагностик в коде `pse_together` нет; `/W4 /WX /permissive-`), затем `ctest`: **23/23 набора**, 72,6 с тестов.

| Группа | Наборы (итоговая строка) |
|---|---|
| Транспорт/мост (`tools/enet-prototype`) | `pse_test_suite` 46/46, `pse_stress_test` 11/11, `pse_fault_injection` 16, `pse_bridge_acceptance` 11/11 |
| Сессия хоста DOC-44/46/47/49/50 | `session_generation_test`, `session_adapter_test`, `session_pressure_test`, `routing_contract` 330, `r1_live`, `admission_contract` 112, `r2_fixed` (аргумент `r2`), `timeout_contract`, `sequence_contract` 282, `sequence_live` 12 |
| Нативная сторона DOC-51/53/55/56 | `background_contract` 37, `snapshot_contract` 42, `map_ready_contract` 210, `native_client_contract` (checks 132–136, `failures=0`) |
| Автомат прокси DOC-58B/58B.1/58B.2 | `proxy58b_contract` 32, `lifecycle58b1_contract` 18, `lifecycle58b2_contract` 21 |
| JS (адаптер остаётся Frida-JS до S0.4) | `adapter58b_js` 11, `controller58a_js` 39 |

Счёты 15 исторических команд DOC-57 совпали ([REGRESSION_SUMMARIES](evidence/m1/NATIVE_VISUAL_PROXY_57/REGRESSION_SUMMARIES.json)). Два счёта зависят от тайминга при неизменном исходнике: `native_client_contract` (в трёх подряд запусках 132/136/134; в DOC-57 — 134) и «rejected attempts» в `session_pressure_test` (37–39); они закреплены регулярным выражением с `failures=0` ([expected_suites.json](../experiments/pse_together/tools/expected_suites.json)).

Состав: 31 файл кода из 13 лабораторий ([UNIFICATION_MAP](evidence/m1/PSE_TOGETHER_S03/UNIFICATION_MAP.json): что откуда, SHA-256 и объём правок), MinHook без изменений, [SOURCE_MANIFEST](evidence/m1/PSE_TOGETHER_S03/SOURCE_MANIFEST.json) (55 файлов). Правки исходников свелись к: переименованию макросов и namespace (`doc58b[2]` → `pse::proxy`, `p58` → `pse::seam`), относительным `#include`, заголовку `config.hpp` и `#error`-защитам в `host.cpp`/`client.cpp`; 10 копий (`host.cpp`, `client.cpp`, `seam.hpp`, `visual_proxy.hpp`, `network_input.*`, `build_profile.hpp`, `map_identity.hpp`, `map_ready_contract.cpp`, `proxy58b_contract.cpp`) после правки `sed` стали LF вместо CRLF — на машинный код не влияет (см. эквивалентность DLL).

## Эквивалентность DLL (VERIFIED, статически, [DLL_EQUIVALENCE](evidence/m1/PSE_TOGETHER_S03/DLL_EQUIVALENCE_20261103_223129/DLL_EQUIVALENCE.json))

Побайтово сравнить с замороженными DLL нельзя: сегодняшний компилятор пересобирает **неизменный** исходник host DLL в другие байты (`toolchain_drift`: host — `.text/.rdata/.pdata` отличаются; client58b — совпал). Поэтому сравнение — тем же тулчейном исходник лаборатории против исходника `pse_together`, с `/PDBALTPATH:%_PDB%` (иначе длина пути PDB сдвигает `.rdata` и все rip-relative смещения) и маской build-идентичности (timestamp, checksum, debug directory, `A0x<hash>` имён анонимных namespace) — [`dll_compare.py`](../experiments/pse_together/tools/dll_compare.py), [`verify_dll_equivalence.py`](../experiments/pse_together/tools/verify_dll_equivalence.py):

| Вариант | Лаборатория | Результат |
|---|---|---|
| `pse_dll_host_bootstrap` | `native_bootstrap` | все 7 секций равны, экспорты равны |
| `pse_dll_host` (HOST+SNAPSHOT+MAPREADY) | `native_map_ready` / `pse_m12a` | все 7 секций равны, экспорты равны |
| `pse_dll_client` (DOC-56) | `native_map_ready` / `pse_native_client` | все 7 секций равны, экспорты равны |
| `pse_dll_client_proxy` | `native_network_visual_proxy_58b` | **`.text`, `.pdata`, `.rdata`, `.data`, `.reloc` отличаются** (ожидаемо, ниже) |
| контроль: то же, но заголовок автомата DOC-58B | то же | `.text` и `.pdata` **равны**; различаются `.rdata`/`.data`/`.reloc` только именами RTTI после переименования namespace |

Вывод: слияние `client.cpp`/`client58b.cpp`, `seam.hpp` и переименования не меняют машинный код. Код `pse_dll_client_proxy` отличается от замороженного DOC-58B DLL **только** добавками DOC-58B.2 в автомате (`ENGINE_RETIRED`, `OrphanCause`, `onEngineTeardownWitness`, `nativeQueueHazard`). `client.cpp` и `seam.hpp` эти точки не вызывают (grep: 0), т.е. в DLL они недостижимы; их поведение проверено **только offline** (21/21), а не в игре.

## Что НЕ доказано и ограничения

- Это не runtime-приёмка. Критерий **сегмента S0** («сценарий DOC-58B run 02 воспроизводится на единой сборке без Frida-адаптера») зависит от S0.4 и не закрыт.
- Собранные в `b60/` DLL **не заменяют замороженные** (`2b66f075…`, `3249b735…` и др. в [CURRENT_STATE](CURRENT_STATE.md)); их хеши не воспроизводятся из исходников. `pse_dll_client_proxy` ≠ замороженный DOC-58B DLL (см. выше): для живого прогона нужен новый pre-native gate, и сначала — S0.4.
- Лаборатории `experiments/m1/*` остаются как есть (замороженные evidence/`workflow.py` ссылаются на них): исходники временно существуют в двух местах. Какой каталог считать каноническим, фиксирует S1 (предложение: `pse_together`).
- В единый код не вошли: JS-адаптер и контроллер (S0.4), «до-исправления» репродукции `r1_original`/`r2_original` (дефекты воспроизводят намеренно), не-наборы (`suite_timer_fixture`, `respawn_*`/`timeout_trace` DOC-53 follow-up, QA-клиенты и `cadence_host` DOC-49, `terminal_snapshot`, Python runner/review, Frida/REA-скрипты).
- ENet-вендор `tools/enet-prototype/third_party/enet` вне git (собственный `.git`, открытый вопрос S0.2): сборка из чистого клона без него невозможна. Из чистого **каталога** (удалён `b60/`) проверено.
- Loopback-наборы используют фиксированные порты 287xx и идут последовательно; параллельный `ctest -j` не проверялся.
- Проверка не независима: исполнитель = автор слияния; нужен проверяющий другой модели.

## Ошибки стенда (сохранены, [HARNESS_ERRORS](evidence/m1/PSE_TOGETHER_S03/HARNESS_ERRORS.md))

1. `r2_fixed` без аргумента — режим R1 (репродукция дефекта, выход 10): исправлено `ARGS r2` ([ctest_try1.log](evidence/m1/PSE_TOGETHER_S03/commands/ctest_try1.log)).
2. `ctest` обрезал вывод успешных тестов до 1 КБ → в junit нет итоговых строк: сверка не прошла (RUN_20261103_221933: 4/23) — это поймано именно потому, что exit 0 не принимается; добавлены `--test-output-size-*`.
3. Ожидаемые счёты: `session_pressure_test`, `native_client_contract`, `controller58a_js` (нет ключа `failed`) — шаблоны уточнены (RUN_20261103_222140: 20/23).
4. Экранирование `\d` в JSON и первый заход `dll_compare` без маски `A0x<hash>` — исправлены до итогового прогона.

## Целостность

Baseline 12 455 файлов (docs/experiments/tools), оригинальная установка игры (77 файлов), обе копии EXE, защищённые сохранения — [BASELINE.json](evidence/m1/PSE_TOGETHER_S03/BASELINE.json); итог — [INTEGRITY_FINAL.json](evidence/m1/PSE_TOGETHER_S03/INTEGRITY_FINAL.json). Новые файлы только в `experiments/pse_together/`, `docs/evidence/m1/PSE_TOGETHER_S03/`, `docs/60_…`, записи в PROGRESS / EVIDENCE_INDEX / ROADMAP / CURRENT_STATE, строка в AGENTS.md. Каталоги `b60`–`b67` — сборки вне git. Игровых процессов нет, REA-сессий не открывалось.

## Следующий шаг

S0.4 (Sol max): перенос управления прокси из Frida-JS в C++ внутри `pse_dll_client_proxy`. Параллельно возможны S0.5 и S1.1.
