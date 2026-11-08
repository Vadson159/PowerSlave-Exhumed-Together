# pse_together — каноническая кодовая база (S0.3, П1/S0.4)

> П16/S4.5 — **PARTIAL**, [DOC-87](../../docs/87_P16_RESPAWN_AND_WEAPON_POLICY.md).
> `p16_respawn_contract`, `p16_respawn_enet`, `p16_weapon_contract` входят в
> общую команду (теперь38наборов). Новый отдельный `pse_dll_host_p16` содержит
> persistent weapon-only pickup seam поверх П7: предмет остаётся, ammo не
> выдаётся. DLL не принята в игре; native respawn/TPK/client rearm ещё
> отсутствуют. Принятые native DLL и wirev1.10–v1.13 не заменять.

> П23: **REVIEW / PARTIAL режима В**, [DOC-84](../../docs/84_P23_UI_LOBBY_CHAT_AUTOMAP.md), [UI backend](src/ui/README.md). По решению D13 — отдельный v1.13 core, старые протоколы сохранены. Fresh34/34 suites,510UI+76real ENet checks,8endpoints/одинПК; независимый backend PASS в проверенных границах. Native меню/HUD/frame binder/автокарта не реализованы, пакет не PASS. Новые наборы p23_ui_contract/p23_lobby_live входят в общий runner; короткая сборка b84. No new KEX/REA sessions/hooks; original77/actual5protected и retained exact restores проверены.

> П7 / S2.2–S2.3: отдельные `pse_dll_host_p7` и `pse_dll_client_p7`, **PASS (bounded) режима В**, независимая приёмка [DOC-81](../../docs/81_P7_INDEPENDENT_ACCEPTANCE.md), исполнение [DOC-80](../../docs/80_P7_SELF_STATE_NATIVE_GATES.md). Свой native Player получает health/ammo8/оружие/keys/inventory/death с хоста через отдельную v1.12; четыре типа local common damage подавлены. Независимые31/31checks; RUN03:842/842 exact native matches,8weapons,single death; offline32/32. Сборка `cmake --build b81 --config Release --target pse_dll_host_p7 pse_dll_client_p7`; реально загруженные frozen DLL — в `docs/evidence/m1/PSE_TOGETHER_P7/P7_{HOST,CLIENT}_03/native_artifact/`, пересборка7/7sections. Два процесса/TOMB/одна жизнь/shared QPC; cross-PC clock sync, respawn/rearm, полный combat input и общий мир не приняты. Key permit требует отдельного свежего gate В каждого run; старый receipt повторно применять нельзя. Legacy v1.10/v1.11 неизменны. Runtime запускать только через REA/DOC-51, как [RUNTIME_P1](tools/RUNTIME_P1.md), с P7 wrapper/gates.

> П3 / S1.1–S1.2: `pse_dll_client_p3` (`PSE_FEATURE_P3`: только CLIENT + VISUAL_PROXY, без P4/P8/P14), отдельная сборка `b71`. **PASS (bounded), режим В** ([DOC-71](../../docs/71_P3_RECONNECT_AND_MAP_CHANGE.md), приёмка [DOC-78 §7](../../docs/78_P3_ACCEPTANCE_CHECK.md); run 05 после исправлений F1–F5, F7). Реконнект и повторный Spawn одного прокси поверх ENGINE_RETIRED (инкарнации неизменённого автомата 58B.2), teardown-witness в DLL, anti-ABA по alloc seq, Remove только перед Drain того же тика, исправление N1 DOC-64. Новый `p3_rearm_contract` (53/0) — в общей команде теперь **29 offline suites**. Run 03: 13 Spawn, 10 переходов карт, 19/19 проверок журналов. Порядок прогона — [RUNTIME_P3](../../docs/evidence/m1/PSE_TOGETHER_P3/tools/RUNTIME_P3.md). Не сделаны смерть → рестарт (N3) и следующая карта (аттестован только TOMB).

> П24 / S9.1–S9.2: **PASS (bounded), режим В**, независимая приёмка [DOC-83](../../docs/83_P24_INDEPENDENT_ACCEPTANCE.md), завершение [DOC-82](../../docs/82_P24_COMPLETION_AND_VALIDATION.md), исходная реализация [DOC-69](../../docs/69_P24_PROXY_DLL_LOADER.md). [Loader README](src/loader/README.md): SDL2 shim → broker → frozen P1 без Frida DLL-loader, exact EXE SHA gate; target `pse_loader_p24` входит в `pse_all`. Свежие 32/32 offline, loader 756/756, три rebuilt артефакта 7/7 PE-секций; independent 85/85 + 19/19. Настоящее окно отказа frozen shim/exit193 подтверждено 13/13 вне KEX, без Frida. Saved game runs: primary bootstrap/native tick/exact restores/exit0, Frida только для прежней изоляции/наблюдения по D5. Полностью Frida-free KEX, portable payload и публичный релиз не аттестованы; gameplay permit загрузчик не выдаёт.

> П8: `pse_dll_host_p8` / `pse_dll_client_p8`, `PSE_FEATURE_P8`, отдельная сборка `b68`. **REVIEW / PARTIAL, режим В** ([DOC-68](../../docs/68_P8_PLAYER_SPRITES_AND_ANIMATIONS.md)). Новые персонажи экспедиции; 27/27 offline, 37/37 bounded run04 assertions. Сборка ресурсов — `tools/build_expedition_sprites.py`, все offline — `tools/run_p8_offline.py`, порядок новых runs — [RUNTIME_P8](tools/RUNTIME_P8.md). Слоты пока повторяют 2 архетипа; 8 различимых вариантов и реальные combat transitions не приняты.

> П4: `pse_dll_host_p4` / `pse_dll_client_p4`, флаг `PSE_FEATURE_P4`. **PASS (bounded), режим В**, [DOC-79](../../docs/79_P4_ACCEPTANCE_REVIEW.md), исполнение [DOC-72](../../docs/72_P4_REMEDIATION_AND_RUNTIME.md). Свежие30/30 offline, независимые91/91 raw checks, обе final DLL П4 и client П1 —7/7 PE-секций. Anchor/corridor/native lerp, jitter-buffer/long-pause recovery и живой фон приняты в области TOMB/один Actor1007/равные caches/dry water state; «любые порталы/карты» остаётся PARTIAL. П4 использует отдельные `pse_core_p4`/`pse_session_p4` и identity-only wire v1.11/D9; legacy v1.10 не изменяется. Сборка П4 — отдельная `b72`, проверяющего — `b79`.

П4 добавляет anchor с ожиданием до1800 eligible ticks, portal corridor/native LinkSector, буфер64/160мс с rolling offset/refill/long-pause recovery и opt-in background client. Native runs03/04 подтвердили переходы259→123→176 и lerp до renderer; финальная DLL04 —123→176. Смена floor/ceiling caches или water state отклоняется; все порталы/карты не аттестованы. TOMB и диагностические инварианты P1 сохраняются. Seam update помещает снапшот в буфер (Unchanged); actual apply выполняется в tickLeave.

Фоновый режим по умолчанию выключен; `PSE_P4_EnableBackground(1)` разрешает lease только после native Ready/baseline, original pause/UI gates сохраняются. Новый runtime требует свежих RUNS/profiles и нового gate (`pre_native_p4.py --build-dir b72 --offline <RESULT.json> --out <NEW.json>`), который копирует DLL/PDB/исходники до запуска. Порядок — принятый launch/bind/isolation/DOC51 через REA. Wrappers `runtime_p4_72*.py` относятся к завершённым02/03/04; имена не переиспользовать. Держать клиента foreground до Ready/baseline. Raw runs и source snapshots — DOC-72. Внутренний wire111 fork аттестован для native П4/identity contract; его standalone/world mocks не входят в П4.

Сведённые копии лабораторий `experiments/m1/*` (M1.1, DOC-44…58B.2) и нативный C++ адаптер П1. Лаборатории **не изменены** (замороженные DLL, evidence и `workflow.py` ссылаются на них); этот каталог — канонический рабочий код. S0.3 принята в offline-границах ([DOC-60](../../docs/60_S0_3_PSE_TOGETHER_UNIFIED_BASE.md)). П1 — REVIEW режима В ([DOC-61](../../docs/61_P1_NATIVE_PROXY_DLL.md)); зависимые пакеты ждут другой модели.

## Одна команда

```bash
python -X utf8 -B experiments/pse_together/tools/run_offline.py
```

Конфигурирует и собирает дерево `b60/` (короткий путь — MAX_PATH), запускает все34 offline-набора через `ctest` и сверяет итоговую строку каждого набора с `tools/expected_suites.json` (одного exit0 недостаточно). Evidence — `docs/evidence/m1/PSE_TOGETHER_S03/RUN_<utc>/`. Флаги: `--build-dir b84` для отдельной сборки, `--no-build`, `--out DIR`. Только offline: игра, Frida/REA и загрузчик не запускаются; это не runtime-приёмка.

Дополнительная проверка пассивного renderer census: `node experiments/pse_together/tools/check_observer_census.js` (32 checks, без игры). Порядок живого прогона — [RUNTIME_P1.md](tools/RUNTIME_P1.md).

Вручную: `cmake -S experiments/pse_together -B b60 -G "Visual Studio 17 2022" -A x64`, `cmake --build b60 --config Release --target pse_all`, `ctest -C Release` в `b60`.

## Роли и флаги

Определены в [`include/pse_together/config.hpp`](include/pse_together/config.hpp) (ошибки компиляции при несовместимых сочетаниях).

| Флаг | Смысл |
|---|---|
| `PSE_ROLE_HOST` | `src/native/host.cpp`: сеть хоста, ввод P2, снапшоты, фоновая симуляция. Без флага роли — голый DOC-41 bootstrap |
| `PSE_ROLE_CLIENT` | `src/native/client.cpp`: второй экземпляр игры |
| `PSE_FEATURE_SNAPSHOT` | DOC-55 снапшоты игроков |
| `PSE_FEATURE_MAPREADY` | DOC-56 нативная идентичность карты + MapReady (требует SNAPSHOT) |
| `PSE_FEATURE_VISUAL_PROXY` | DOC-58B/58B.2 автомат и нативный C++ адаптер прокси П1 (требует MAPREADY, только клиент) |

Прежние имена: `PSE_M12A` → `PSE_ROLE_HOST`, `PSE_DOC55_SNAPSHOT` → `PSE_FEATURE_SNAPSHOT`, `PSE_DOC56_MAPREADY` → `PSE_FEATURE_MAPREADY`, `PSE_DOC58B` → `PSE_FEATURE_VISUAL_PROXY`.

## DLL-цели (`b60/dll/<цель>/`)

| Цель | Роль + флаги | Выходное имя | Аналог в лабораториях |
|---|---|---|---|
| `pse_dll_host` | HOST + SNAPSHOT + MAPREADY | `pse_m11_bootstrap.dll` | host DLL DOC-56 |
| `pse_dll_host_bootstrap` | — | `pse_m11_bootstrap.dll` | DOC-41 bootstrap |
| `pse_dll_client` | CLIENT + SNAPSHOT + MAPREADY | `pse_native_client.dll` | client DOC-56 |
| `pse_dll_client_proxy` | CLIENT + все три | `pse_native_client58b.dll` | автомат 58B.2 и внутренний C++ адаптер П1 |

Загрузчик DOC-51 и имена DLL сохранены. `PSE58B_RegisterSeam` теперь отказывает (50): таблица callbacks находится в DLL. Добавлены атомарное разрешение Spawn и read-only DTO/filter exports. Frida используется стендом для загрузки/isolation и наблюдений; JS-контроллер прокси не загружается. `verify_dll_equivalence.py` относится к сохранённой консолидации S0.3; новый proxy target П1 закономерно отличается от лабораторной DLL.

## Раскладка

П14: отдельная экспериментальная `pse_dll_host_p14` (`PSE_FEATURE_P14`, HOST),
AI acquisition/chase fallback и owner-aware health/ammo, shared Ankh dedup без
вызова original Ankh OnTouch/Steam. Статус **REVIEW / PARTIAL, режим В**;
нативные gameplay gates не закрыты — [DOC-66](../../docs/66_P14_HOST_AI_AND_PICKUPS.md).
`p14_gameplay_contract` запускает тот же модуль с engine doubles (offline).
`tools/runtime_p14.py` связывает принятые launcher/isolation/loader с новыми
run IDs и замороженной Client DLL П1; запуск — по REA lane в `RUNTIME_P1.md`.

```
include/pse_together/config.hpp   роли и флаги
src/session/   network_input.*  сетевая сессия хоста (DOC-44/46/47/49/50/53/55/56), map_identity.hpp, snapshot_contract.hpp
src/native/    host.cpp, client.cpp, seam.hpp, build_profile.hpp (+ background_*, map_reader.hpp)
src/proxy/     visual_proxy.hpp  автомат прокси: 58B.2 как надмножество 58B; multi_slot_snapshot_guard.hpp
src/tools/     session_client_main.cpp  (прежний m12a_native_client)
tests/{session,native,proxy}/   контрактные наборы
third_party/minhook/            без изменений
tools/         run_offline.py, expected_suites.json, integrity.py, dll_compare.py, verify_dll_equivalence.py
```

Транспорт (`pse_core`: Wire v1.10, мост, ENet) остаётся в `tools/enet-prototype/` и подключается через `add_subdirectory` без правок; его наборы (`pse_test_suite`, `pse_stress_test`, `pse_fault_injection`, `pse_bridge_acceptance`) входят в общий запуск.

## Что сюда не вошло (и почему)

- Исторические JS-адаптер/контроллер остаются в лабораториях как референс; их наборы (`adapter58b_js` 11, `controller58a_js` 39) входят в регрессию, но игровой proxy target их не использует. П1 добавил `bounded_proxy_contract` (118 checks) в общий запуск.
- Историческая «до-исправления» репродукция (`r1_original`, `r2_original`) — дефекты воспроизводят намеренно, не регрессия.
- Не наборы: DOC-53 `suite_timer_fixture`, DOC-53 follow-up `respawn_*`/`timeout_trace`, DOC-49 QA-клиенты/`cadence_host`, `terminal_snapshot`, runner/workflow/review Python, Frida/REA-скрипты.
- `pse_test_suite` пишет `docs/TEST_RESULTS.md` относительно cwd: ctest запускает его в `b60/testrun/pse_test_suite/`, репозиторий не затрагивается.
